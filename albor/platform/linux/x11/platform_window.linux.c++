#include "albor/platform/linux/x11/platform_window.linux.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include "albor/core/env.h"
#include "albor/platform/linux/common/input_normalization.h"
#include "albor/platform/linux/x11/connection.linux.h"
#include "albor/platform/linux/x11/native_surface.linux.h"

namespace albor::x11
{
    using linux_platform::key_from_keysym;
    using linux_platform::modifiers_from_state;
    using linux_platform::text_from_key;

    // the atoms a window writes its title and close protocol through, which every window interns once
    struct Atoms
    {
        // the protocols property the close request is registered on
        xcb_atom_t protocols = XCB_ATOM_NONE;

        // the delete window protocol the window manager asks to close with
        xcb_atom_t delete_window = XCB_ATOM_NONE;

        // the utf8 title property a modern window manager reads
        xcb_atom_t net_wm_name = XCB_ATOM_NONE;

        // the utf8 string type the title property carries
        xcb_atom_t utf8_string = XCB_ATOM_NONE;

        // the active window message a raise asks the window manager for
        xcb_atom_t net_active_window = XCB_ATOM_NONE;

        // the parent property a transient surface names its owner with (§5.1)
        xcb_atom_t wm_transient_for = XCB_ATOM_NONE;

        // the window type property, which separates a popup from an ordinary window (§5.1)
        xcb_atom_t net_wm_window_type = XCB_ATOM_NONE;

        // the popup menu type, which a compositor draws without chrome or shadow (§5.1)
        xcb_atom_t net_wm_window_type_popup_menu = XCB_ATOM_NONE;
    };

    // one native window's state, which the event registry points at
    struct X11WindowState
    {
        // the shared connection the window and its renderer draw through
        std::shared_ptr<X11Connection> connection;

        // the atoms the window writes through
        Atoms atoms;

        // the window the compositor shows
        xcb_window_t window = 0;

        // the surface bundle a renderer reads, which a teardown clears first (§10.5)
        X11NativeSurface native;

        // the listener the native events reach
        PlatformWindowListener* listener = nullptr;

        // the metrics the window currently presents at
        WindowMetrics metrics;

        // the device scale the metrics carry, which the session resolves once (AD-27)
        float scale_factor = 1.0f;

        // the keymap the key events are normalized through
        xkb_context* keymap_context = nullptr;

        // the keymap the window resolves keysyms from
        xkb_keymap* keymap = nullptr;

        // the state that carries the held modifiers
        xkb_state* keyboard_state = nullptr;

        // whether the window is on screen
        bool visible = false;

        // whether a frame was requested and not yet drawn
        bool frame_requested = false;

        // whether the window is a transient popup, which the window manager never manages (§5.1)
        bool popup = false;

        // the owner a popup is anchored to, which the window manager uses for stacking (§5.1)
        xcb_window_t parent = 0;

        // the trigger rectangle a popup is placed against, in the owner's logical units (§5.1)
        Rect anchor{};

        // whether the popup holds the pointer grab it dismisses an outside press with (§5.1)
        bool grabbed = false;

        // whether the window exists as a native object
        bool created = false;
    };

    namespace
    {
        // the windows the event loop dispatches onto, keyed by the xcb window id
        std::unordered_map<xcb_window_t, std::weak_ptr<X11WindowState>>& window_registry() {
            // the map is held for the process, because one loop dispatches every window (§5.3)
            static std::unordered_map<xcb_window_t, std::weak_ptr<X11WindowState>> map;
            return map;
        }

        // the lock that serializes the registry, which the ui thread mutates
        std::mutex& registry_mutex() {
            // one mutex guards the map for the process (§10.5)
            static std::mutex mutex;
            return mutex;
        }

        // register one window so the loop can find it by its id
        void register_window(const std::shared_ptr<X11WindowState>& state) {
            // the map is locked while it changes, because a reader may run on another thread
            const std::lock_guard<std::mutex> lock(registry_mutex());
            window_registry()[state->window] = state;
        }

        // drop one window from the registry before it is destroyed
        void unregister_window(xcb_window_t window) {
            // the map is locked while it changes, because a reader may run on another thread
            const std::lock_guard<std::mutex> lock(registry_mutex());
            window_registry().erase(window);
        }

        // find the state an xcb event names, or null when the window is gone
        [[nodiscard]] std::shared_ptr<X11WindowState> find_window(xcb_window_t window) {
            // the map is locked while it is read, because a writer may run on another thread
            const std::lock_guard<std::mutex> lock(registry_mutex());
            const auto found = window_registry().find(window);
            return found == window_registry().end() ? nullptr : found->second.lock();
        }

        // intern one atom, which reports none when the display refuses
        [[nodiscard]] xcb_atom_t intern_atom(xcb_connection_t* connection, const char* name) {
            // the request is sent first, because the reply is what carries the atom
            const xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, static_cast<std::uint16_t>(std::strlen(name)), name);
            xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection, cookie, nullptr);
            // a refused intern leaves no atom, which the caller treats as unsupported
            if (reply == nullptr)
                return XCB_ATOM_NONE;
            const xcb_atom_t atom = reply->atom;
            std::free(reply);
            return atom;
        }

        // write the utf8 title the window manager and its switcher read
        void write_title(X11WindowState* state, std::string_view title) {
            xcb_connection_t* connection = state->connection->connection();
            // the modern property carries utf8, which every window manager reads
            if (state->atoms.net_wm_name != XCB_ATOM_NONE)
                xcb_change_property(connection, XCB_PROP_MODE_REPLACE, state->window, state->atoms.net_wm_name, state->atoms.utf8_string, 8, static_cast<std::uint32_t>(title.size()), title.data());
            // the legacy property carries the same bytes, which a client without the modern property reads
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE, state->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, static_cast<std::uint32_t>(title.size()), title.data());
        }

        // send the window manager the message that raises a window
        void request_activation(X11WindowState* state) {
            // a display without the active window atom cannot raise through the window manager
            if (state->atoms.net_active_window == XCB_ATOM_NONE)
                return;
            xcb_client_message_event_t message{};
            message.response_type = XCB_CLIENT_MESSAGE;
            message.format = 32;
            message.window = state->window;
            message.type = state->atoms.net_active_window;
            // source indication one names an application request, which a window manager may honour
            message.data.data32[0] = 1;
            message.data.data32[1] = XCB_CURRENT_TIME;
            xcb_send_event(state->connection->connection(), 0, state->connection->screen()->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, reinterpret_cast<const char*>(&message));
            xcb_flush(state->connection->connection());
        }

        // read the Xft dpi the desktop publishes on the root window, which a hidpi session sets (AD-27)
        [[nodiscard]] float xft_dpi(xcb_connection_t* connection, const xcb_screen_t* screen) {
            // a display without a root window publishes no resource string, which leaves no dpi (AD-27)
            if (connection == nullptr || screen == nullptr)
                return 0.0f;
            // the resource manager string carries the Xft.dpi line every x11 toolkit reads (AD-27)
            const xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, screen->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 4096);
            xcb_get_property_reply_t* reply = xcb_get_property_reply(connection, cookie, nullptr);
            if (reply == nullptr)
                return 0.0f;
            const int length = xcb_get_property_value_length(reply);
            float dpi = 0.0f;
            // a reply that carries the setting is searched once, because the string is a handful of lines (AD-27)
            if (length > 0) {
                const std::string_view resources(static_cast<const char*>(xcb_get_property_value(reply)), static_cast<std::size_t>(length));
                const std::size_t found = resources.find("Xft.dpi:");
                if (found != std::string_view::npos)
                    dpi = std::strtof(std::string(resources.substr(found + 8u)).c_str(), nullptr);
            }
            std::free(reply);
            return dpi;
        }

        // resolve the device scale from the session, which every metric the window publishes multiplies by (AD-27)
        [[nodiscard]] float resolve_scale(xcb_connection_t* connection, const xcb_screen_t* screen) {
            // an explicit scale wins, which a session sets when it overrides the dpi (AD-27)
            const std::optional<std::string> gdk_scale = albor::core::get_environment_variable("GDK_SCALE");
            if (gdk_scale.has_value()) {
                const float parsed = std::strtof(gdk_scale->c_str(), nullptr);
                if (parsed >= 1.0f && parsed <= 4.0f)
                    return parsed;
            }
            // the resource setting is the dpi every toolkit reads, which a hidpi desktop sets (AD-27)
            float dpi = xft_dpi(connection, screen);
            // a display that publishes an implausible density falls back to its own physical size (AD-27)
            if (dpi < 48.0f || dpi > 480.0f) {
                dpi = screen != nullptr && screen->width_in_millimeters > 0 ? static_cast<float>(screen->width_in_pixels) * 25.4f / static_cast<float>(screen->width_in_millimeters) : 0.0f;
            }
            // a display that names no density keeps the logical extent, which is what it draws at (AD-27)
            if (dpi < 48.0f || dpi > 480.0f)
                return 1.0f;
            // the scale is quantized to quarter steps and clamped to the range the framework draws (AD-27)
            return std::clamp(std::round(dpi / 96.0f * 4.0f) / 4.0f, 1.0f, 4.0f);
        }

        // map one logical point onto the device pixels the x server reports, which every input event divides by (AD-27)
        [[nodiscard]] Point logical_point(const X11WindowState* state, std::int16_t x, std::int16_t y) {
            // a window without a resolved scale keeps the pixels it was given (AD-27)
            const float scale = state->scale_factor > 0.0f ? state->scale_factor : 1.0f;
            return Point{static_cast<float>(x) / scale, static_cast<float>(y) / scale};
        }

        // map one logical extent onto the pixel size an x window is created and resized at (AD-27)
        [[nodiscard]] std::uint16_t pixel_extent(float logical, float scale) {
            // a zero or negative request still names one pixel, which the server accepts (AD-27)
            const float pixels = std::round(logical * (scale > 0.0f ? scale : 1.0f));
            return static_cast<std::uint16_t>(std::min(65535.0f, std::max(1.0f, pixels)));
        }

        // update the metrics a configure event reports, which the framework reads
        void update_metrics(X11WindowState* state, std::uint16_t width, std::uint16_t height) {
            // a configure with no area is the window manager's transient state, which the last extent survives
            if (width == 0 || height == 0)
                return;
            // the server reports device pixels, which the logical extent divides the resolved scale out of (AD-27)
            const float scale = state->scale_factor > 0.0f ? state->scale_factor : 1.0f;
            state->metrics = WindowMetrics::from_logical(Size{static_cast<float>(width) / scale, static_cast<float>(height) / scale}, scale);
            if (state->listener != nullptr)
                state->listener->on_platform_resize(state->metrics);
        }

        // place a popup against the trigger rectangle of its owner, which the anchor names in logical units (§5.1)
        void position_popup(X11WindowState* state) {
            // a popup without an owner stands where its anchor names against the screen (AD-27)
            if (state->parent == 0 || state->connection == nullptr || state->connection->screen() == nullptr)
                return;
            // the owner origin is what turns the anchor into a root position (§5.1)
            const xcb_translate_coordinates_cookie_t cookie = xcb_translate_coordinates(state->connection->connection(), state->parent, state->connection->screen()->root, 0, 0);
            xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(state->connection->connection(), cookie, nullptr);
            // an owner the server cannot translate leaves the popup where it is (§5.1)
            if (reply == nullptr)
                return;
            const float scale = state->scale_factor > 0.0f ? state->scale_factor : 1.0f;
            const int screen_width = static_cast<int>(state->connection->screen()->width_in_pixels);
            const int screen_height = static_cast<int>(state->connection->screen()->height_in_pixels);
            const int width = static_cast<int>(state->metrics.pixel_size.width);
            const int height = static_cast<int>(state->metrics.pixel_size.height);
            // the popup hangs under the trigger's bottom edge, which leaves the current selection visible (§5.1)
            int x = reply->dst_x + static_cast<int>(std::round(state->anchor.x * scale));
            int y = reply->dst_y + static_cast<int>(std::round((state->anchor.y + state->anchor.height) * scale));
            // a popup that would run past the screen bottom flips above the trigger, like the wayland positioner (§5.1)
            if (y + height > screen_height)
                y = reply->dst_y + static_cast<int>(std::round(state->anchor.y * scale)) - height;
            // a popup that would run past the screen edge slides back onto it, which keeps it reachable (§5.1)
            if (x + width > screen_width)
                x = screen_width - width;
            x = std::max(0, x);
            y = std::max(0, y);
            std::free(reply);
            const std::uint32_t values[] = {static_cast<std::uint32_t>(static_cast<std::uint16_t>(x)), static_cast<std::uint32_t>(static_cast<std::uint16_t>(y))};
            xcb_configure_window(state->connection->connection(), state->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
        }

        // take the pointer grab a popup dismisses an outside press with, which xdg_popup.grab gives on wayland (§5.1)
        void grab_pointer(X11WindowState* state) {
            // a window without a connection holds no grab (§5.1)
            if (state->connection == nullptr || !state->popup)
                return;
            const std::uint32_t mask = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
            // the pointer stays free to leave the popup, which the outside press needs to be reported (§5.1)
            const xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(state->connection->connection(), 0, state->window, mask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
            xcb_grab_pointer_reply_t* reply = xcb_grab_pointer_reply(state->connection->connection(), cookie, nullptr);
            // a grab the server refused leaves the outside press to the owner, which still closes it (§5.1)
            state->grabbed = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
            std::free(reply);
            xcb_flush(state->connection->connection());
        }

        // release the pointer grab, which a hide and a dismissal both do (§5.1)
        void ungrab_pointer(X11WindowState* state) {
            // a window that holds no grab releases nothing (§5.1)
            if (state->connection == nullptr || !state->grabbed)
                return;
            xcb_ungrab_pointer(state->connection->connection(), XCB_CURRENT_TIME);
            xcb_flush(state->connection->connection());
            state->grabbed = false;
        }

        // report whether the pointer is outside the popup, which a grab press hides because the server clamps it (§5.1)
        [[nodiscard]] bool outside_popup(X11WindowState* state, std::int16_t x, std::int16_t y) {
            const auto width = static_cast<std::int16_t>(state->metrics.pixel_size.width);
            const auto height = static_cast<std::int16_t>(state->metrics.pixel_size.height);
            // a point beyond the window is outside it, which the server reports without clamping (§5.1)
            if (x < 0 || y < 0 || x > width || y > height)
                return true;
            // a point away from the edge is inside, because the server only clamps a press that left the window (§5.1)
            if (x < width - 1 && y < height - 1)
                return false;
            // the popup is a child of the root, so its geometry names the origin the pointer is compared against (§5.1)
            const xcb_get_geometry_cookie_t geometry_cookie = xcb_get_geometry(state->connection->connection(), state->window);
            xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(state->connection->connection(), geometry_cookie, nullptr);
            // a window the server cannot describe keeps the popup open, because no dismissal was proven (§5.1)
            if (geometry == nullptr)
                return false;
            const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(state->connection->connection())).data;
            // the root query reports the true pointer position, which a query against the grab window clamps (§5.1)
            const xcb_query_pointer_cookie_t pointer_cookie = xcb_query_pointer(state->connection->connection(), screen->root);
            xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(state->connection->connection(), pointer_cookie, nullptr);
            // a pointer the server cannot report keeps the popup open, because no dismissal was proven (§5.1)
            if (pointer == nullptr) {
                std::free(geometry);
                return false;
            }
            const bool outside = pointer->root_x < geometry->x || pointer->root_y < geometry->y || pointer->root_x >= geometry->x + geometry->width || pointer->root_y >= geometry->y + geometry->height;
            std::free(pointer);
            std::free(geometry);
            return outside;
        }

        // dismiss a popup the outside press closed, which the framework hears through the closed notification (§5.1)
        void dismiss_popup(X11WindowState* state) {
            // the grab goes first, so the press that dismissed the popup is the last one it sees (§5.1)
            ungrab_pointer(state);
            // the native window leaves the screen, which keeps its tree for the next opening (§5.1)
            if (state->connection != nullptr && state->window != 0)
                xcb_unmap_window(state->connection->connection(), state->window);
            // the window is off screen from here, which a later show maps again (§5.3)
            state->visible = false;
            // the framework is told the surface left, so the control returns to its closed state (§5.1)
            if (state->listener != nullptr)
                state->listener->on_platform_closed();
        }

        // destroy the native window and clear the surface state a renderer reads
        void destroy_window(X11WindowState* state) {
            // a window that was never created has nothing to destroy
            if (!state->created || state->window == 0)
                return;
            // the surface state is cleared under its lock, so a render thread stops before the window dies (§10.5)
            if (state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                state->native.state->window = 0;
                state->native.state->mapped = false;
                // the last holder destroys the window, so an in flight present never names a dead id (§10.5)
                state->native.state->pending_window = state->window;
            }
            // the registry stops dispatching the window before it is destroyed
            unregister_window(state->window);
            // a state no renderer holds destroys the window now, which drops the adapter's own reference to it (§10.5)
            state->native.state = nullptr;
            // the window the adapter names is gone from here, which the teardown reports (§5.3)
            xcb_flush(state->connection->connection());
            state->window = 0;
            state->created = false;
            state->visible = false;
            state->frame_requested = false;
            // the native bundle clears, which a later handle read reports as invalid (§3.5)
            state->native = X11NativeSurface{};
        }

        // map the held modifiers onto the portable set
        [[nodiscard]] Modifiers current_modifiers(const X11WindowState* state) {
            // a window without a keymap reports no modifier, which is what an empty state means
            if (state->keyboard_state == nullptr || state->keymap == nullptr)
                return {};
            const xkb_mod_mask_t active = xkb_state_serialize_mods(state->keyboard_state, XKB_STATE_MODS_EFFECTIVE);
            const xkb_mod_index_t shift = xkb_keymap_mod_get_index(state->keymap, XKB_MOD_NAME_SHIFT);
            const xkb_mod_index_t control = xkb_keymap_mod_get_index(state->keymap, XKB_MOD_NAME_CTRL);
            const xkb_mod_index_t alt = xkb_keymap_mod_get_index(state->keymap, XKB_MOD_NAME_ALT);
            const xkb_mod_index_t logo = xkb_keymap_mod_get_index(state->keymap, XKB_MOD_NAME_LOGO);
            // a modifier the keymap does not name is never held
            const bool shift_held = shift != XKB_MOD_INVALID && (active & (1u << shift)) != 0;
            const bool control_held = control != XKB_MOD_INVALID && (active & (1u << control)) != 0;
            const bool alt_held = alt != XKB_MOD_INVALID && (active & (1u << alt)) != 0;
            const bool logo_held = logo != XKB_MOD_INVALID && (active & (1u << logo)) != 0;
            return modifiers_from_state(shift_held, control_held, alt_held, logo_held);
        }

        // dispatch one key event, which carries the resolved key and its modifiers
        void dispatch_key(X11WindowState* state, const xcb_key_press_event_t& event, bool pressed) {
            // a key event without a listener reaches nothing
            if (state->listener == nullptr || state->keyboard_state == nullptr)
                return;
            const xkb_keycode_t keycode = static_cast<xkb_keycode_t>(event.detail);
            // the keysym is read before the state updates, so a release still names the key it released
            const xkb_keysym_t symbol = xkb_state_key_get_one_sym(state->keyboard_state, keycode);
            // the character is read with the same state, because a shifted key types its shifted glyph (§8.3)
            char typed[32] = {};
            const int typed_length = xkb_state_key_get_utf8(state->keyboard_state, keycode, typed, sizeof(typed));
            // the held modifiers are read before the key changes the state
            const Modifiers modifiers = current_modifiers(state);
            // the state advances with the key, which the next event reads
            xkb_state_update_key(state->keyboard_state, keycode, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
            Event message{};
            message.kind = pressed ? EventKind::KeyDown : EventKind::KeyUp;
            message.key = key_from_keysym(static_cast<std::uint32_t>(symbol));
            message.modifiers = modifiers;
            state->listener->on_platform_event(message);
            // a printable press commits its character as text, which is what a text field inserts (§8.3)
            if (!pressed)
                return;
            // the text the press commits is empty when it names no printable character (§8.3)
            const std::string text = text_from_key(modifiers, typed, typed_length);
            // a key that types no text leaves the named key as the whole event (§8.3)
            if (text.empty())
                return;
            Event committed{};
            committed.kind = EventKind::TextInput;
            committed.text = text;
            state->listener->on_platform_event(committed);
        }

        // dispatch one pointer button, which a wheel is not
        void dispatch_button(X11WindowState* state, const xcb_button_press_event_t& event, bool pressed) {
            // a pointer event without a listener reaches nothing
            if (state->listener == nullptr)
                return;
            // a press outside the grabbed popup dismisses it, which is the outside press a menu consumes (§5.1)
            if (state->popup && pressed && outside_popup(state, event.event_x, event.event_y)) {
                dismiss_popup(state);
                return;
            }
            // the wheel buttons report a scroll rather than a press, which the portable set carries as a wheel
            if (event.detail >= 4 && event.detail <= 7) {
                // the press is the scroll, while its release carries no portable event at all
                if (pressed && (event.detail == 4 || event.detail == 5)) {
                    Event message{};
                    message.kind = EventKind::Wheel;
                    message.position = logical_point(state, event.event_x, event.event_y);
                    message.scroll = event.detail == 4 ? 1.0f : -1.0f;
                    message.precision = ScrollPrecision::Line;
                    state->listener->on_platform_event(message);
                }
                return;
            }
            Event message{};
            message.kind = pressed ? EventKind::PointerDown : EventKind::PointerUp;
            message.position = logical_point(state, event.event_x, event.event_y);
            // x11 numbers its core buttons one to three, which the portable set names directly
            message.button = event.detail == 1 ? PointerButton::Left : event.detail == 2 ? PointerButton::Middle : event.detail == 3 ? PointerButton::Right : PointerButton::Other;
            state->listener->on_platform_event(message);
        }
    } // namespace

    X11PlatformWindow::X11PlatformWindow(std::shared_ptr<X11WindowState> state) :
        m_state(std::move(state)) {}

    std::unique_ptr<X11PlatformWindow> X11PlatformWindow::create(const WindowOptions& options) {
        // a window with no creatable options is refused before a connection is made
        if (!options.valid())
            return nullptr;
        // the shared connection is reused by every window on the display
        std::shared_ptr<X11Connection> connection = X11Connection::connect(std::string_view());
        if (connection == nullptr || connection->connection() == nullptr || connection->screen() == nullptr)
            return nullptr;
        std::shared_ptr<X11WindowState> state = std::make_shared<X11WindowState>();
        state->connection = connection;
        // the xkb keymap resolves the keysym and modifiers the key events carry
        state->keymap_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (state->keymap_context != nullptr) {
            state->keymap = xkb_keymap_new_from_names(state->keymap_context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (state->keymap != nullptr)
                state->keyboard_state = xkb_state_new(state->keymap);
        }
        // the session resolves the device scale once, which every extent the window names multiplies by (AD-27)
        state->scale_factor = resolve_scale(connection->connection(), connection->screen());
        // the window is created at the device extent the logical request maps onto, which is what x11 sizes in (AD-27)
        const std::uint16_t width = pixel_extent(options.size.width, state->scale_factor);
        const std::uint16_t height = pixel_extent(options.size.height, state->scale_factor);
        // a popup is a transient surface the window manager never manages, so it carries its own placement (§5.1)
        state->popup = options.role == WindowRole::Popup;
        // the owner a popup anchors to crosses as the window it names, which the framework resolved (§5.1)
        if (state->popup && options.native_parent.valid())
            state->parent = static_cast<xcb_window_t>(reinterpret_cast<std::uintptr_t>(const_cast<void*>(options.native_parent.object())));
        // the trigger rectangle the popup hangs under is kept, which a taskbar move re-applies (§5.1)
        state->anchor = options.anchor;
        state->window = xcb_generate_id(connection->connection());
        const std::uint32_t event_mask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE;
        // a popup is override redirect, which keeps every window manager from decorating or placing it (§5.1)
        const std::uint32_t override_redirect = state->popup ? static_cast<std::uint32_t>(XCB_CW_OVERRIDE_REDIRECT) : 0u;
        const std::uint32_t attribute_mask = static_cast<std::uint32_t>(XCB_CW_BACK_PIXEL) | static_cast<std::uint32_t>(XCB_CW_EVENT_MASK) | override_redirect;
        // the attribute list follows the mask bit order, which places the override flag between the other two (§5.1)
        const std::uint32_t popup_values[] = {connection->screen()->black_pixel, 1, event_mask};
        const std::uint32_t window_values[] = {connection->screen()->black_pixel, event_mask};
        const std::uint32_t* values = state->popup ? popup_values : window_values;
        xcb_create_window(connection->connection(), XCB_COPY_FROM_PARENT, state->window, connection->screen()->root, 0, 0, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, connection->screen()->root_visual, attribute_mask, values);
        // the atoms the title and the close protocol ride are interned once per window
        state->atoms.protocols = intern_atom(connection->connection(), "WM_PROTOCOLS");
        state->atoms.delete_window = intern_atom(connection->connection(), "WM_DELETE_WINDOW");
        state->atoms.net_wm_name = intern_atom(connection->connection(), "_NET_WM_NAME");
        state->atoms.utf8_string = intern_atom(connection->connection(), "UTF8_STRING");
        state->atoms.net_active_window = intern_atom(connection->connection(), "_NET_ACTIVE_WINDOW");
        state->atoms.wm_transient_for = intern_atom(connection->connection(), "WM_TRANSIENT_FOR");
        state->atoms.net_wm_window_type = intern_atom(connection->connection(), "_NET_WM_WINDOW_TYPE");
        state->atoms.net_wm_window_type_popup_menu = intern_atom(connection->connection(), "_NET_WM_WINDOW_TYPE_POPUP_MENU");
        // a popup names its owner and its menu type, which a compositor draws without chrome or shadow (§5.1)
        if (state->popup) {
            // the transient relationship is what keeps the popup above the window it belongs to (§5.1)
            if (state->parent != 0 && state->atoms.wm_transient_for != XCB_ATOM_NONE)
                xcb_change_property(connection->connection(), XCB_PROP_MODE_REPLACE, state->window, state->atoms.wm_transient_for, XCB_ATOM_WINDOW, 32, 1, &state->parent);
            // the menu type is what tells the compositor the surface is a popup rather than a window (§5.1)
            if (state->atoms.net_wm_window_type != XCB_ATOM_NONE && state->atoms.net_wm_window_type_popup_menu != XCB_ATOM_NONE)
                xcb_change_property(connection->connection(), XCB_PROP_MODE_REPLACE, state->window, state->atoms.net_wm_window_type, XCB_ATOM_ATOM, 32, 1, &state->atoms.net_wm_window_type_popup_menu);
        }
        // the close request is registered, so the window manager asks instead of killing the client
        if (!state->popup && state->atoms.protocols != XCB_ATOM_NONE && state->atoms.delete_window != XCB_ATOM_NONE)
            xcb_change_property(connection->connection(), XCB_PROP_MODE_REPLACE, state->window, state->atoms.protocols, XCB_ATOM_ATOM, 32, 1, &state->atoms.delete_window);
        // the title is written before the window is mapped, which the window manager reads
        if (!state->popup)
            write_title(state.get(), options.title);
        // the surface state carries the connection and window a renderer presents through
        state->native.kind = linux_wsi::Kind::X11;
        // the state destroys the window when its last holder releases it, which keeps an in flight present valid (§10.5)
        state->native.state = std::shared_ptr<X11SurfaceState>(new X11SurfaceState(), destroy_surface_state);
        state->native.state->owner = connection;
        state->native.state->connection = connection->connection();
        state->native.state->window = state->window;
        state->native.state->mapped = false;
        // the metrics start at the logical extent the options name, which a configure updates (AD-27)
        state->metrics = WindowMetrics::from_logical(options.size, state->scale_factor);
        // the window exists from here, which the registry and a teardown key on
        state->created = true;
        register_window(state);
        // the requests are sent, which the window manager answers
        xcb_flush(connection->connection());
        return std::unique_ptr<X11PlatformWindow>(new X11PlatformWindow(std::move(state)));
    }

    X11PlatformWindow::~X11PlatformWindow() {
        // the listener is detached, because the destructor is past every event it would raise
        if (m_state != nullptr) {
            m_state->listener = nullptr;
            // a popup that still holds the pointer gives it back before its window goes away (§5.1)
            ungrab_pointer(m_state.get());
        }
        // the native window is taken down, which clears the surface a renderer reads
        if (m_state != nullptr) {
            destroy_window(m_state.get());
            if (m_state->keyboard_state != nullptr)
                xkb_state_unref(m_state->keyboard_state);
            if (m_state->keymap != nullptr)
                xkb_keymap_unref(m_state->keymap);
            if (m_state->keymap_context != nullptr)
                xkb_context_unref(m_state->keymap_context);
        }
    }

    void X11PlatformWindow::set_listener(PlatformWindowListener* listener) {
        // the listener is what every native event is delivered to
        m_state->listener = listener;
    }

    void X11PlatformWindow::show() {
        // a window that no longer exists cannot be shown
        if (!m_state->created)
            return;
        // an already visible window is not mapped twice, which a repeated show has no effect on
        if (m_state->visible)
            return;
        // a popup is placed against its trigger again, because the owner may have moved since the last opening (§5.1)
        if (m_state->popup)
            position_popup(m_state.get());
        xcb_map_window(m_state->connection->connection(), m_state->window);
        xcb_flush(m_state->connection->connection());
        m_state->visible = true;
        if (m_state->native.state != nullptr) {
            const std::lock_guard<std::mutex> surface_lock(m_state->native.state->mutex);
            m_state->native.state->mapped = true;
        }
        // the grab is what dismisses the popup on a press outside it, which a managed window never needs (§5.1)
        if (m_state->popup)
            grab_pointer(m_state.get());
    }

    void X11PlatformWindow::activate() {
        // a window that no longer exists cannot be raised
        if (!m_state->created)
            return;
        // the window manager raises the window, which an unmanaged host ignores
        request_activation(m_state.get());
        xcb_set_input_focus(m_state->connection->connection(), XCB_INPUT_FOCUS_PARENT, m_state->window, XCB_CURRENT_TIME);
        xcb_flush(m_state->connection->connection());
    }

    void X11PlatformWindow::hide() {
        // an already hidden window is not unmapped twice, which a repeated hide has no effect on
        if (!m_state->visible)
            return;
        // the grab is released first, so no press reaches a popup that is leaving (§5.1)
        ungrab_pointer(m_state.get());
        xcb_unmap_window(m_state->connection->connection(), m_state->window);
        xcb_flush(m_state->connection->connection());
        m_state->visible = false;
        m_state->frame_requested = false;
        if (m_state->native.state != nullptr) {
            const std::lock_guard<std::mutex> surface_lock(m_state->native.state->mutex);
            m_state->native.state->mapped = false;
        }
    }

    void X11PlatformWindow::set_title(std::string_view title) {
        // a window that no longer exists has no title bar to write into
        if (!m_state->created)
            return;
        write_title(m_state.get(), title);
        xcb_flush(m_state->connection->connection());
    }

    void X11PlatformWindow::set_size(Size size) {
        // a window that no longer exists cannot be resized
        if (!m_state->created)
            return;
        // the request is logical, which the server sizes in device pixels (AD-27)
        const std::uint16_t width = pixel_extent(size.width, m_state->scale_factor);
        const std::uint16_t height = pixel_extent(size.height, m_state->scale_factor);
        const std::uint32_t values[] = {width, height};
        // the metrics follow the request at once, which a configure event later confirms (AD-27)
        update_metrics(m_state.get(), width, height);
        xcb_configure_window(m_state->connection->connection(), m_state->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        xcb_flush(m_state->connection->connection());
    }

    WindowMetrics X11PlatformWindow::metrics() const {
        // the metrics are the ones the last configure reported, which the platform is the authority on (§9.1)
        return m_state->metrics;
    }

    NativeWindowHandle X11PlatformWindow::native_handle() const {
        // a window that was destroyed hands out the invalid handle, which a backend refuses (§3.5)
        if (!m_state->created)
            return NativeWindowHandle{};
        // the bundle crosses into the framework as an opaque pointer, which no xcb type may name (§3.5, AD-05)
        return NativeWindowHandle::from_object(&m_state->native);
    }

    NativeWindowHandle X11PlatformWindow::native_window_handle() const {
        // a window that was destroyed names no owner, which an orphaned popup refuses (§5.1)
        if (!m_state->created || m_state->window == 0)
            return NativeWindowHandle{};
        // the window id crosses as an opaque handle, which no xcb type may name (§3.5, AD-05)
        return NativeWindowHandle::from_object(reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_state->window)));
    }

    void X11PlatformWindow::request_frame() {
        // the flag records that a frame is wanted, which hide and close clear (§4.3)
        m_state->frame_requested = true;
    }

    bool X11PlatformWindow::frame_requested() const {
        // the flag stays set until the window hides or closes, which matches the other adapters (AD-18b)
        return m_state->frame_requested;
    }

    bool X11PlatformWindow::visible() const {
        // a created and mapped window is the one on screen
        return m_state->created && m_state->visible;
    }

    void X11PlatformWindow::close() {
        // a window that was never created has nothing to close
        if (!m_state->created)
            return;
        // a popup releases its grab before the window goes away (§5.1)
        ungrab_pointer(m_state.get());
        // the listener is detached first, because closing raises events the framework is already past (§4.2)
        m_state->listener = nullptr;
        // the native window is taken down without asking the framework, because the framework initiated this (§5.2)
        destroy_window(m_state.get());
    }

    void dispatch_x11_event(xcb_generic_event_t* event) {
        // an empty event is the loop's end marker, which nothing dispatches
        if (event == nullptr)
            return;
        // the high bit carries a synthetic flag, which the event type masks away
        const std::uint8_t type = event->response_type & 0x7Fu;
        // the target window is read from the event, which every struct shares at the same offset
        xcb_window_t window = 0;
        switch (type) {
        case XCB_EXPOSE:
            window = reinterpret_cast<xcb_expose_event_t*>(event)->window;
            break;
        case XCB_CONFIGURE_NOTIFY:
            window = reinterpret_cast<xcb_configure_notify_event_t*>(event)->window;
            break;
        case XCB_CLIENT_MESSAGE:
            window = reinterpret_cast<xcb_client_message_event_t*>(event)->window;
            break;
        case XCB_DESTROY_NOTIFY:
            window = reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window;
            break;
        case XCB_FOCUS_IN:
            window = reinterpret_cast<xcb_focus_in_event_t*>(event)->event;
            break;
        case XCB_FOCUS_OUT:
            window = reinterpret_cast<xcb_focus_out_event_t*>(event)->event;
            break;
        case XCB_LEAVE_NOTIFY:
            window = reinterpret_cast<xcb_leave_notify_event_t*>(event)->event;
            break;
        case XCB_MOTION_NOTIFY:
            window = reinterpret_cast<xcb_motion_notify_event_t*>(event)->event;
            break;
        case XCB_BUTTON_PRESS:
            window = reinterpret_cast<xcb_button_press_event_t*>(event)->event;
            break;
        case XCB_BUTTON_RELEASE:
            window = reinterpret_cast<xcb_button_release_event_t*>(event)->event;
            break;
        case XCB_KEY_PRESS:
            window = reinterpret_cast<xcb_key_press_event_t*>(event)->event;
            break;
        case XCB_KEY_RELEASE:
            window = reinterpret_cast<xcb_key_release_event_t*>(event)->event;
            break;
        default:
            return;
        }
        const std::shared_ptr<X11WindowState> state = find_window(window);
        // an event for a window the registry no longer holds reaches nothing
        if (state == nullptr)
            return;
        switch (type) {
        case XCB_EXPOSE:
            if (state->listener != nullptr)
                state->listener->on_platform_frame();
            break;
        case XCB_CONFIGURE_NOTIFY: {
            const auto* configure = reinterpret_cast<xcb_configure_notify_event_t*>(event);
            update_metrics(state.get(), configure->width, configure->height);
            break;
        }
        case XCB_CLIENT_MESSAGE: {
            const auto* message = reinterpret_cast<xcb_client_message_event_t*>(event);
            // only the delete protocol is a close request, which the framework may veto (§5.2)
            if (message->type != state->atoms.protocols || message->data.data32[0] != state->atoms.delete_window)
                break;
            // the listener decides, which a veto keeps the window on screen for (§5.2)
            PlatformWindowListener* listener = state->listener;
            if (listener != nullptr && !listener->on_platform_close_request())
                break;
            // a granted request closes the window, which raises no second request (§5.2)
            state->listener = nullptr;
            destroy_window(state.get());
            // the framework is told the window closed, which is what drops its stage and ends the run (§5.2)
            if (listener != nullptr)
                listener->on_platform_closed();
            break;
        }
        case XCB_DESTROY_NOTIFY: {
            // the window manager destroyed the window, which ends the window without a veto (§5.2)
            PlatformWindowListener* listener = state->listener;
            state->listener = nullptr;
            destroy_window(state.get());
            // the framework is told the window closed, which is the same notification a granted request raises (§5.2)
            if (listener != nullptr)
                listener->on_platform_closed();
            break;
        }
        case XCB_FOCUS_IN:
            if (state->listener != nullptr)
                state->listener->on_platform_focus_gained();
            break;
        case XCB_FOCUS_OUT:
            if (state->listener != nullptr)
                state->listener->on_platform_focus_lost();
            break;
        case XCB_LEAVE_NOTIFY:
            if (state->listener != nullptr)
                state->listener->on_platform_pointer_exited();
            break;
        case XCB_MOTION_NOTIFY: {
            const auto* motion = reinterpret_cast<xcb_motion_notify_event_t*>(event);
            if (state->listener != nullptr) {
                Event message{};
                message.kind = EventKind::PointerMove;
                message.position = logical_point(state.get(), motion->event_x, motion->event_y);
                state->listener->on_platform_event(message);
            }
            break;
        }
        case XCB_BUTTON_PRESS:
            dispatch_button(state.get(), *reinterpret_cast<xcb_button_press_event_t*>(event), true);
            break;
        case XCB_BUTTON_RELEASE:
            dispatch_button(state.get(), *reinterpret_cast<xcb_button_release_event_t*>(event), false);
            break;
        case XCB_KEY_PRESS:
            dispatch_key(state.get(), *reinterpret_cast<xcb_key_press_event_t*>(event), true);
            break;
        case XCB_KEY_RELEASE:
            dispatch_key(state.get(), *reinterpret_cast<xcb_key_release_event_t*>(event), false);
            break;
        default:
            break;
        }
    }

} // namespace albor::x11
