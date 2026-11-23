#include "albor/platform/linux/wayland/platform_window.h"

#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "albor/platform/linux/common/input_normalization.h"
#include "albor/platform/linux/wayland/native_surface.linux.h"
#include "albor/platform/linux/wayland/portal.h"
#include "albor/platform/linux/wayland/wayland_connection.linux.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

namespace albor
{
    // the wayland names the window adapter shares, which the calls below name unqualified (§5.3)
    using linux_platform::button_from_evdev;
    using linux_platform::key_from_keysym;
    using linux_platform::modifiers_from_state;
    using linux_platform::PendingWheel;
    using linux_platform::text_from_key;
    using wayland::PortalClient;
    using wayland::PortalSubscription;
    using wayland::WaylandConnection;
    using wayland::WaylandNativeSurface;

    WaylandDecorationMode choose_decoration(bool server_side_available, bool client_side_available) {
        // the compositor's native chrome is the first choice, because it follows the desktop's own theme (§5.3)
        if (server_side_available)
            return WaylandDecorationMode::ServerSide;
        // libdecor draws the frame where the compositor has no xdg-decoration, which is what GNOME needs (§5.3)
        if (client_side_available)
            return WaylandDecorationMode::ClientSide;
        // a host with neither draws the content without chrome rather than refusing the window (§5.3)
        return WaylandDecorationMode::None;
    }

    float fractional_scale_factor(std::uint32_t preferred_scale) {
        // a compositor that named no scale leaves the identity, which the integer path also starts from (AD-27)
        if (preferred_scale == 0)
            return 1.0f;
        // the protocol carries the numerator over one hundred twentieths, which is one logical pixel (AD-27)
        return static_cast<float>(preferred_scale) / 120.0f;
    }

    bool fractional_scale_available(bool manager_available, bool viewporter_available) {
        // both protocols are needed, because a preferred scale without a viewport cannot be mapped (AD-27)
        return manager_available && viewporter_available;
    }

    Size viewport_destination(Size logical_size) {
        // the destination is rounded halfway away from zero, which is the rounding the toplevel rule names (AD-27)
        const long width = std::max(1L, std::lround(logical_size.width));
        // the height follows the same rounding, which keeps the aspect the compositor configured (AD-27)
        const long height = std::max(1L, std::lround(logical_size.height));
        // the destination is the logical extent the physical buffer is scaled onto (AD-27)
        return Size{static_cast<float>(width), static_cast<float>(height)};
    }

    // the opaque xdg state, defined here so that no wayland type reaches a public header (§3.5, AD-05)
    struct WaylandWindowState;

    // one activation request, which stays alive until the compositor answers its token (§5.1)
    struct ActivationRequest
    {
        // the state the request belongs to, which the done handler clears (§5.1)
        WaylandWindowState* state = nullptr;

        // the activation protocol the request is redeemed through (§5.1)
        xdg_activation_v1* activation = nullptr;

        // the surface the activation raises (§5.1)
        wl_surface* surface = nullptr;

        // the token the compositor answers, which the done handler destroys (§5.1)
        xdg_activation_token_v1* token = nullptr;
    };

    struct WaylandWindowState :
        // the shared base is what the listeners keep the state alive through (§4.2)
        std::enable_shared_from_this<WaylandWindowState>
    {
        // the framework listener the native window raises events into (§5.3)
        PlatformWindowListener* listener = nullptr;

        // the shared connection the surface and the shell come from, which the loop keeps alive (§5.3)
        std::shared_ptr<WaylandConnection> connection;

        // the portal client the appearance follows, which a host without a portal leaves unavailable (§8.4)
        std::shared_ptr<PortalClient> portal;

        // the registration the appearance changes arrive through, which the window releases with its state (§8.4)
        PortalSubscription appearance;

        // the exported surface reference the portal imports as this window, which is null without xdg-foreign (§5.3)
        zxdg_exported_v2* exported = nullptr;

        // the handle the compositor assigned the export, which the portal window identifier carries (§5.3)
        std::string exported_handle;

        // the activation protocol the window is raised through, which is null where the compositor cannot raise it (§5.1)
        xdg_activation_v1* activation = nullptr;

        // the wayland objects a renderer surface attaches through, which the native handle carries (§3.5, AD-05)
        WaylandNativeSurface native;

        // the surface the compositor composes, which no buffer may be attached to before the first configure (§5.3)
        wl_surface* surface = nullptr;

        // the fractional preferred scale object, which is null where the compositor prefers an integer scale (AD-27)
        wp_fractional_scale_v1* fractional_scale = nullptr;

        // the viewport that maps the physical buffer onto the logical extent, which is null without the protocol (AD-27)
        wp_viewport* viewport = nullptr;

        // the shell surface that carries the xdg role state (§5.3)
        xdg_surface* shell_surface = nullptr;

        // the toplevel every role rides, which carries the title and the close request (§5.1)
        xdg_toplevel* toplevel = nullptr;

        // the popup role a popup rides, which replaces the toplevel and carries its own configure (§5.1)
        xdg_popup* popup = nullptr;

        // the positioner that places the popup relative to its parent (§5.1)
        xdg_positioner* positioner = nullptr;

        // the parent state a popup attaches to, which the framework's native parent names and does not own here (§5.1)
        WaylandWindowState* parent = nullptr;

        // the anchor rect a popup is placed against, which a dismissal rebuilds the positioner with (§5.1)
        Rect popup_anchor;

        // the decoration the compositor draws, which is null where no manager exists or a popup runs (§5.1, §5.3)
        zxdg_toplevel_decoration_v1* decoration = nullptr;

#ifdef ALBOR_WAYLAND_LIBDECOR
        // the libdecor frame that decorates an ordinary window, which is null for a popup or the fallback (§5.3)
        libdecor_frame* frame = nullptr;

        // whether the libdecor frame was mapped, which a later show must not repeat (§5.3)
        bool frame_mapped = false;
#endif

        // the text input the input method talks to, which is null where no manager exists (§8.3)
        zwp_text_input_v3* text_input = nullptr;

        // the pointer the pointer events arrive on, which is null where the seat has none (§8.3)
        wl_pointer* pointer = nullptr;

        // the keyboard the key events arrive on, which is null where the seat has none (§8.3)
        wl_keyboard* keyboard = nullptr;

        // the xkb context the keymap is built in, which one per window is (§8.3)
        xkb_context* xkb = nullptr;

        // the keymap the compositor shared, which the keymap event carries (§8.3)
        xkb_keymap* keymap = nullptr;

        // the keymap state the modifiers are read from, which tracks what the modifiers event last applied (§8.3)
        xkb_state* keymap_state = nullptr;

        // the index the keymap resolved the shift modifier to, which the state answers with (§8.3)
        xkb_mod_index_t mod_shift = XKB_MOD_INVALID;

        // the index the keymap resolved the control modifier to (§8.3)
        xkb_mod_index_t mod_control = XKB_MOD_INVALID;

        // the index the keymap resolved the alt modifier to, which the portable set names option (§8.3)
        xkb_mod_index_t mod_alt = XKB_MOD_INVALID;

        // the index the keymap resolved the logo modifier to, which the portable set names command (§8.3)
        xkb_mod_index_t mod_logo = XKB_MOD_INVALID;

        // the version the seat was bound at, which caps the pointer and the keyboard version (§8.3)
        std::uint32_t seat_version = 0;

        // the logical size the window asked for, which the compositor may adjust (§5.1)
        Size requested_size;

        // the scale the compositor prefers, which separates logical pixels from physical ones (AD-27)
        float scale_factor = 1.0f;

        // the metrics the last configure reported, which the framework reads as authoritative (§9.1)
        WindowMetrics metrics;

        // the logical position the pointer last named, which a wheel and a button carry (§8.3)
        Point pointer_position;

        // the wheel batches of the two axes, which a frame turns into one event each (§8.3)
        PendingWheel wheel[2];

        // whether the pointer is on this surface, which a button and a motion gate on (§8.3)
        bool pointer_on_surface = false;

        // whether the keyboard is on this surface, which a key gates on (§8.3)
        bool keyboard_on_surface = false;

        // whether the input method session is aimed at this surface, which its events gate on (§8.3)
        bool ime_on_surface = false;

        // the serial of the input event that would open a popup, which a grab is answered with (§5.1)
        std::uint32_t last_input_serial = 0;

        // the activation request the compositor has not answered, which a teardown releases (§5.1)
        ActivationRequest* pending_activation = nullptr;

        // the size the toplevel configure proposed, which the surface configure applies (§5.3)
        int proposed_width = 0;
        int proposed_height = 0;

        // whether the preedit and the commit carry facts the done event has not applied yet (§8.3)
        bool preedit_pending = false;
        bool commit_pending = false;

        // the preedit text the done event will apply, which a composition holds (§8.3)
        std::string preedit_text;

        // the commit text the done event will apply, which a resolved composition carries (§8.3)
        std::string commit_text;

        // whether a composition is open, which the done event closes (§8.3)
        bool composing = false;

        // the selection the framework pushed, which the input method reads back (§8.3)
        TextInputRange selection;

        // the anchor rect the framework pushed, which the candidate window is placed by (§8.3)
        Rect anchor;

        // whether an anchor was pushed at all (§8.3)
        bool anchor_valid = false;

        // whether a frame was requested and not yet drawn, which is per window pacing (AD-18b)
        bool frame_requested = false;

        // whether the window is currently on screen, which show and hide record (§5.3)
        bool visible = false;

        // whether the window is a popup, which the decoration path consults (§5.1)
        bool popup_role = false;

        // the decoration the window uses, which the strategy and the compositor's mode answer decide (§5.3)
        WaylandDecorationMode decoration_mode = WaylandDecorationMode::None;

        // whether the native proxies were created, which a dismissed popup keeps after its surface goes (§5.1)
        bool created = false;

        // the shm pool the current buffer lives in, which the release event frees (§6.2)
        wl_shm_pool* pool = nullptr;

        // the buffer the compositor currently reads, which the release event frees (§6.2)
        wl_buffer* buffer = nullptr;

        // the mapping the compositor reads pixels from, which the release frees (§6.2)
        void* pool_data = nullptr;

        // the descriptor the mapping came from, which the release closes (§6.2)
        int pool_fd = -1;

        // the byte extent the mapping spans, which the release unmaps by (§6.2)
        int pool_size = 0;

        // one retired buffer, whose mapping the compositor may still read until it releases (§6.2)
        struct RetiredBuffer
        {
            // the buffer proxy the release event names
            wl_buffer* buffer = nullptr;

            // the pool the buffer was carved from
            wl_shm_pool* pool = nullptr;

            // the mapping the compositor read pixels from
            void* data = nullptr;

            // the descriptor the mapping came from
            int fd = -1;

            // the byte extent the mapping spans
            int size = 0;
        };

        // the retired buffers, which the release events drain (§6.2)
        std::vector<RetiredBuffer> retired;
    };

    namespace
    {
        // report whether a renderer presents through the surface, which then owns every buffer it commits (§6.3)
        bool renderer_presenting(const WaylandWindowState* state) {
            // a window without shared state has no renderer presenting into it (§10.5)
            return state->native.state != nullptr && state->native.state->renderer_presenting;
        }

        // the axis the vertical scroll rides, which a vertical wheel reports (§8.3)
        constexpr std::uint32_t AXIS_VERTICAL = 1;

        // the keymap format the xkb library reads, which the compositor shares (§8.3)
        constexpr std::uint32_t KEYMAP_FORMAT_XKB_V1 = 1;

        // the key state a pressed key arrives with, which a release differs from (§8.3)
        constexpr std::uint32_t KEY_STATE_PRESSED = 1;

        // the logical point a surface local position names, which the surface space already is (§8.3, AD-27)
        Point point_of(double x, double y) {
            // the point keeps the surface space, because pointer events arrive in logical units (AD-27)
            return Point{static_cast<float>(x), static_cast<float>(y)};
        }

        // the framework receives one normalized event, which the listener carries (§8.3)
        void raise_event(WaylandWindowState* state, const Event& event) {
            // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
            if (state->listener == nullptr)
                return;
            state->listener->on_platform_event(event);
        }

        // report whether one canonical modifier is held, which the keymap state answers (§8.3)
        bool mod_active(WaylandWindowState* state, xkb_mod_index_t index) {
            // an index the keymap carries none of is inactive, which is what the invalid index means (§8.3)
            if (index == XKB_MOD_INVALID)
                return false;
            // the state answers for the effective set, which is what the modifiers event last applied (§8.3)
            return xkb_state_mod_index_is_active(state->keymap_state, index, XKB_STATE_MODS_EFFECTIVE) == 1;
        }

        // the portable modifiers the keymap state currently resolves to (§8.3)
        Modifiers modifiers_of(WaylandWindowState* state) {
            // a keyboard without a keymap carries no modifiers, which is what an unreadable state reports (§8.3)
            if (state->keymap_state == nullptr)
                return Modifiers{};
            // each canonical modifier is queried by the index the keymap resolved it to (§8.3)
            return modifiers_from_state(mod_active(state, state->mod_shift), mod_active(state, state->mod_control), mod_active(state, state->mod_alt), mod_active(state, state->mod_logo));
        }

        // the token the compositor granted, which is what redeems the activation request (§5.1)
        void on_activation_token_done(void* data, xdg_activation_token_v1* token, const char* token_string) {
            // the request is the data the token was created with, which the done event releases (§5.1)
            ActivationRequest* request = static_cast<ActivationRequest*>(data);
            // a granted token is redeemed against the surface it names (§5.1)
            if (token_string != nullptr && request->activation != nullptr)
                xdg_activation_v1_activate(request->activation, token_string, request->surface);
            // the state no longer waits for the token, which a later activate starts fresh (§5.1)
            if (request->state != nullptr && request->state->pending_activation == request)
                request->state->pending_activation = nullptr;
            // the token is destroyed, because the request no longer needs it (§5.1)
            xdg_activation_token_v1_destroy(token);
            delete request;
        }

        // the listener the activation token answers through, which carries the done event (§5.1)
        constexpr xdg_activation_token_v1_listener ACTIVATION_TOKEN_LISTENER = {&on_activation_token_done};

        // build the shm buffer a mapped window presents, which a scale change rebuilds (AD-27)
        bool attach_initial_buffer(WaylandWindowState* state);

        // report whether a window maps its buffer through the fractional scale and the viewport (AD-27)
        bool uses_fractional_scale(const WaylandWindowState* state) {
            // both objects are needed, because a preference without a viewport cannot be mapped (AD-27)
            return state->fractional_scale != nullptr && state->viewport != nullptr;
        }

        // report the integer buffer scale a window presents at, which a fractional window keeps at one (AD-27)
        std::int32_t buffer_scale_of(const WaylandWindowState* state) {
            // a fractional window maps through the viewport, so the protocol keeps the buffer scale at one (AD-27)
            if (uses_fractional_scale(state))
                return 1;
            // the integer path rounds the scale to the factor the compositor reads (AD-27)
            return static_cast<std::int32_t>(std::max(1L, std::lround(state->scale_factor)));
        }

        // map the viewport onto a logical extent, which a fractional window reads its buffer through (AD-27)
        void apply_viewport_destination(WaylandWindowState* state, Size logical_size) {
            // a window without a viewport maps its buffer directly, which the integer path does (AD-27)
            if (state->viewport == nullptr)
                return;
            // the destination is the logical extent the physical buffer is scaled onto (AD-27)
            const Size destination = viewport_destination(logical_size);
            // the compositor is told the logical extent the buffer maps onto (AD-27)
            wp_viewport_set_destination(state->viewport, static_cast<std::int32_t>(destination.width), static_cast<std::int32_t>(destination.height));
        }

        // apply a new scale, which a preferred scale and an output change drive (AD-27)
        void apply_scale(WaylandWindowState* state, float scale_factor) {
            // a compositor that reports no scale yet keeps the identity, which a layout divides by (AD-27)
            const float effective_scale = scale_factor > 0.0f ? scale_factor : 1.0f;
            // a scale that did not change applies nothing, which is what the comparison guards (AD-27)
            if (state->scale_factor == effective_scale)
                return;
            // the scale moves first, because a metrics listener reads it while handling the change (AD-27)
            state->scale_factor = effective_scale;
            // the shared state is held locally, because a listener may tear the window down later (§4.2, §10.5)
            const std::shared_ptr<wayland::WaylandSurfaceState> surface_state = state->native.state;
            // a surface and its state live together, which a teardown clears (§10.5)
            if (surface_state != nullptr && state->surface != nullptr) {
                // the surface is locked, so the scale change lands atomically against a present (AD-27, §10.5)
                const std::lock_guard<std::mutex> surface_lock(surface_state->mutex);
                // the compositor is told the scale, which the buffer it reads is interpreted at (AD-27)
                wl_surface_set_buffer_scale(state->surface, buffer_scale_of(state));
            }
            // the metrics are rebuilt at the new scale, which is what a scale change changes (AD-27)
            const WindowMetrics metrics = WindowMetrics::from_logical(state->metrics.logical_size, effective_scale);
            // the scale and the buffer scale already moved, so an unchanged pixel size raises nothing (§9.1, AD-27)
            if (metrics == state->metrics)
                return;
            // the framework receives the metrics the platform now reports (§9.1)
            state->metrics = metrics;
            if (state->listener != nullptr)
                state->listener->on_platform_resize(metrics);
            // a listener may have destroyed the window, which leaves no surface to rebuild (§4.2)
            if (surface_state == nullptr || state->surface == nullptr || !state->visible)
                return;
            // a renderer owned surface presents the new scale with its own next buffer, which owns the commit (§6.3)
            if (renderer_presenting(state))
                return;
            // the surface is locked, so the rebuild commits atomically against a render thread present (§10.5)
            const std::lock_guard<std::mutex> surface_lock(surface_state->mutex);
            if (attach_initial_buffer(state))
                wl_surface_commit(state->surface);
        }

        // the metrics one configure reports, which a size of zero leaves at the requested one (§5.3)
        void apply_configure(WaylandWindowState* state, int width, int height) {
            // a configure that names no size keeps the size the client asked for, which the request carries (§5.3)
            const Size logical = width > 0 && height > 0 ? Size{static_cast<float>(width), static_cast<float>(height)} : state->requested_size;
            // the metrics are rebuilt from the logical size and the scale, which is what the framework reads (§9.1, AD-27)
            const WindowMetrics metrics = WindowMetrics::from_logical(logical, state->scale_factor);
            // an unchanged configure raises nothing, which is what the comparison guards (§9.1)
            if (metrics == state->metrics)
                return;
            // the metrics move first, because a listener reads them while handling the resize (§5.3, D9.1)
            state->metrics = metrics;
            // the viewport follows the logical extent, which the next frame maps its buffer onto (AD-27)
            apply_viewport_destination(state, metrics.logical_size);
            // the framework receives the metrics the platform now reports (§9.1)
            if (state->listener != nullptr)
                state->listener->on_platform_resize(metrics);
        }

        // the preferred scale the compositor announced, which the fractional protocol owns (AD-27)
        void on_fractional_preferred_scale(void* data, wp_fractional_scale_v1*, std::uint32_t preferred_scale) {
            // the state is kept alive across the listener, because the scale may tear the window down (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // the fraction the compositor prefers is applied, which the protocol carries over one hundred twenty (AD-27)
            apply_scale(state.get(), fractional_scale_factor(preferred_scale));
        }

        // the listener set of the fractional scale, which carries the preferred scale (AD-27)
        constexpr wp_fractional_scale_v1_listener FRACTIONAL_SCALE_LISTENER = {&on_fractional_preferred_scale};

        // destroy the per surface scale objects, which a surface teardown requires first (AD-27)
        void destroy_scale_objects(WaylandWindowState* state) {
            // the fractional scale is destroyed first, because it extends the surface (AD-27)
            if (state->fractional_scale != nullptr) {
                wp_fractional_scale_v1_destroy(state->fractional_scale);
                state->fractional_scale = nullptr;
            }
            // the viewport is destroyed with the surface it maps, which no commit may outlive (AD-27)
            if (state->viewport != nullptr) {
                wp_viewport_destroy(state->viewport);
                state->viewport = nullptr;
            }
        }

        // create the per surface scale objects, which the fractional protocols need before a commit (AD-27)
        void create_scale_objects(WaylandWindowState* state) {
            // a compositor without both protocols keeps the integer scale, which no object is created for (AD-27)
            if (state->surface == nullptr || !fractional_scale_available(state->connection->fractional_scales() != nullptr, state->connection->viewporter() != nullptr))
                return;
            // the viewport maps the physical buffer onto the logical extent, which a fraction needs (AD-27)
            state->viewport = wp_viewporter_get_viewport(state->connection->viewporter(), state->surface);
            // the fractional scale object carries the preferred scale the compositor announces (AD-27)
            state->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(state->connection->fractional_scales(), state->surface);
            // the pair is only usable together, so a refused object releases the other (AD-27)
            if (state->viewport == nullptr || state->fractional_scale == nullptr) {
                destroy_scale_objects(state);
                return;
            }
            // the preference arrives through the listener, which the compositor announces once created (AD-27)
            wp_fractional_scale_v1_add_listener(state->fractional_scale, &FRACTIONAL_SCALE_LISTENER, state);
        }

        // free one buffer the compositor released, which carries its own mapping (§6.2)
        void free_buffer(WaylandWindowState* state, wl_buffer* released) {
            // the current buffer is freed in place, which its fields carry (§6.2)
            if (state->buffer == released) {
                // the buffer proxy is destroyed first, because the pool it came from must outlive it (§6.2)
                wl_buffer_destroy(released);
                // the pool is destroyed, which the compositor no longer reads (§6.2)
                wl_shm_pool_destroy(state->pool);
                // the mapping is unmapped and its descriptor closed, which the pool came from (§6.2)
                munmap(state->pool_data, static_cast<std::size_t>(state->pool_size));
                close(state->pool_fd);
                // the fields clear, which a later attach rebuilds (§6.2)
                state->pool = nullptr;
                state->buffer = nullptr;
                state->pool_data = nullptr;
                state->pool_fd = -1;
                state->pool_size = 0;
                return;
            }
            // a retired buffer is found in the list, which the release event drains (§6.2)
            for (std::size_t index = 0; index < state->retired.size(); ++index) {
                if (state->retired[index].buffer == released) {
                    // the record is copied out, which the erase below consumes (§6.2)
                    const WaylandWindowState::RetiredBuffer record = state->retired[index];
                    // the buffer proxy is destroyed first, because the pool it came from must outlive it (§6.2)
                    wl_buffer_destroy(released);
                    // the pool is destroyed, which the compositor no longer reads (§6.2)
                    wl_shm_pool_destroy(record.pool);
                    // the mapping is unmapped and its descriptor closed (§6.2)
                    munmap(record.data, static_cast<std::size_t>(record.size));
                    close(record.fd);
                    // the record is erased, which the list no longer carries (§6.2)
                    state->retired.erase(state->retired.begin() + static_cast<std::ptrdiff_t>(index));
                    return;
                }
            }
        }

        // retire the current buffer, which the compositor may still read until it releases (§6.2)
        void retire_current(WaylandWindowState* state) {
            // a surface with no current buffer retires nothing, which is what an unmapped window is (§6.2)
            if (state->buffer == nullptr)
                return;
            // the record moves to the retired set, which the release event drains (§6.2)
            WaylandWindowState::RetiredBuffer record;
            record.buffer = state->buffer;
            record.pool = state->pool;
            record.data = state->pool_data;
            record.fd = state->pool_fd;
            record.size = state->pool_size;
            state->retired.push_back(record);
            // the fields clear, which the new buffer fills (§6.2)
            state->pool = nullptr;
            state->buffer = nullptr;
            state->pool_data = nullptr;
            state->pool_fd = -1;
            state->pool_size = 0;
        }

        // release the activation request the compositor has not answered, which a teardown and a role rebuild share (§5.1)
        void release_pending_activation(WaylandWindowState* state) {
            // a window with no pending request has nothing to release (§5.1)
            if (state->pending_activation == nullptr)
                return;
            // the token is destroyed, because no done event may reach a dead surface (§5.1)
            xdg_activation_token_v1_destroy(state->pending_activation->token);
            delete state->pending_activation;
            state->pending_activation = nullptr;
        }

        // take the native proxies down, which a close and a teardown share (§5.2)
        void destroy_native(WaylandWindowState* state) {
            // a window that was never created has nothing to destroy (§5.2)
            if (!state->created)
                return;
            // the export is released before the role, which the portal may still reference the window through (§5.3)
            if (state->exported != nullptr) {
                zxdg_exported_v2_destroy(state->exported);
                state->exported = nullptr;
                state->exported_handle.clear();
            }
            // the text input is destroyed, which the input method releases its focus under (§8.3)
            if (state->text_input != nullptr) {
                zwp_text_input_v3_destroy(state->text_input);
                state->text_input = nullptr;
            }
            // an unanswered activation token is released, which the done event no longer reaches (§5.1)
            release_pending_activation(state);
#ifdef ALBOR_WAYLAND_LIBDECOR
            // a libdecor frame owns its xdg surface and toplevel, which the unref destroys with it (§5.3)
            if (state->frame != nullptr) {
                libdecor_frame_unref(state->frame);
                state->frame = nullptr;
                state->frame_mapped = false;
                state->shell_surface = nullptr;
                state->toplevel = nullptr;
                state->decoration = nullptr;
            }
#endif
            // the decoration is destroyed, which the compositor drops its chrome under (§5.3)
            if (state->decoration != nullptr) {
                zxdg_toplevel_decoration_v1_destroy(state->decoration);
                state->decoration = nullptr;
            }
            // the popup is destroyed first, because its role references the parent surface (§5.1)
            if (state->popup != nullptr) {
                xdg_popup_destroy(state->popup);
                state->popup = nullptr;
            }
            // the positioner is destroyed with the popup it placed (§5.1)
            if (state->positioner != nullptr) {
                xdg_positioner_destroy(state->positioner);
                state->positioner = nullptr;
            }
            // the parent reference clears, because the popup no longer references it (§5.1)
            state->parent = nullptr;
            // the toplevel is destroyed, which takes the role state down (§5.3)
            if (state->toplevel != nullptr) {
                xdg_toplevel_destroy(state->toplevel);
                state->toplevel = nullptr;
            }
            // the shell surface is destroyed, which the surface role follows (§5.3)
            if (state->shell_surface != nullptr) {
                xdg_surface_destroy(state->shell_surface);
                state->shell_surface = nullptr;
            }
            // the pointer is released, which a seat version five or later carries (§8.3)
            if (state->pointer != nullptr) {
                // the release request exists from pointer version five on, which the seat version caps (§8.3)
                if (state->seat_version >= 5)
                    wl_pointer_release(state->pointer);
                else
                    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(state->pointer));
                state->pointer = nullptr;
            }
            // the keyboard is released, which a seat version five or later carries (§8.3)
            if (state->keyboard != nullptr) {
                if (state->seat_version >= 5)
                    wl_keyboard_release(state->keyboard);
                else
                    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(state->keyboard));
                state->keyboard = nullptr;
            }
            // the current buffer is retired, which the release event frees (§6.2)
            retire_current(state);
            // every retired buffer is freed, because the teardown outlives no compositor use (§6.2)
            for (const WaylandWindowState::RetiredBuffer& record : state->retired) {
                // the buffer proxy is destroyed first, because the pool it came from must outlive it (§6.2)
                wl_buffer_destroy(record.buffer);
                // the pool is destroyed, the mapping unmapped and the descriptor closed (§6.2)
                wl_shm_pool_destroy(record.pool);
                munmap(record.data, static_cast<std::size_t>(record.size));
                close(record.fd);
            }
            state->retired.clear();
            // the shared state is cleared under its lock, so a render thread stops before the proxy dies (§10.5)
            if (state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                state->native.state->display = nullptr;
                state->native.state->surface = nullptr;
                state->native.state->shm = nullptr;
                state->native.state->mapped = false;
                // the map cycle is over with the surface, which no present may reopen (§5.3)
                state->native.state->configured = false;
                // the last holder destroys the surface, so an in flight present never names a dead proxy (§10.5)
                state->native.state->pending_surface = state->surface;
            }
            // the surface is destroyed by the last holder, which drops the adapter's own reference to it (§10.5)
            state->native.state = nullptr;
            if (state->surface != nullptr) {
                // the per surface scale objects are destroyed first, because they extend the surface (AD-27)
                destroy_scale_objects(state);
                state->surface = nullptr;
            }
            // the native bundle clears, which a later handle read reports as invalid (§3.5)
            state->native = WaylandNativeSurface{};
            // the native window is gone, which a second teardown reports (§5.2)
            state->created = false;
        }

        // take the window down and report the close, which a granted request shares (§5.2)
        void finish_close(WaylandWindowState* state) {
            // the proxies are destroyed, which is what takes a wayland window down (§5.2)
            destroy_native(state);
            // a closed window has nothing left to draw (§4.3)
            state->frame_requested = false;
            state->visible = false;
            // the framework is told the window closed, which is the Closed notification of §5.2
            if (state->listener != nullptr)
                state->listener->on_platform_closed();
        }

        // the compositor asked for the close, which the application may veto (§5.2)
        void request_close(WaylandWindowState* state) {
            // a listener that is gone means the framework window is already destroyed, so the close is granted (§4.2)
            const bool granted = state->listener == nullptr || state->listener->on_platform_close_request();
            // a vetoed close proceeds no further, which the pending record keeps (§5.2)
            if (!granted)
                return;
            // the window may already be gone, because the veto itself may have destroyed it (§4.2)
            if (state->surface == nullptr)
                return;
            // the close is finished, which reports the Closed notification (§5.2)
            finish_close(state);
        }

        // take the surface off screen and stop the render thread, which a hide and a dismissal share (§5.3)
        void hide_native(WaylandWindowState* state) {
            // a surface the window no longer has was already off screen (§5.3)
            if (state->surface == nullptr)
                return;
            // the render thread is stopped before the unmap, so a frame cannot re-map a hidden window (§10.5)
            if (state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                state->native.state->mapped = false;
                // the unmap spends the map cycle, which a show starts again with a bufferless commit (§5.3)
                state->native.state->configured = false;
            }
#ifdef ALBOR_WAYLAND_LIBDECOR
            // a decorated frame is hidden with its content, which the libdecor frame surface carries (§5.3)
            if (state->frame != nullptr)
                libdecor_frame_set_visibility(state->frame, false);
#endif
            // the buffer is detached, which is what takes the surface off screen (§5.3)
            wl_surface_attach(state->surface, nullptr, 0, 0);
            // the commit applies the unmap, which is the whole of hiding (§5.3)
            wl_surface_commit(state->surface);
            // the window is off screen, which no frame request may re-map (§4.3)
            state->visible = false;
            state->frame_requested = false;
        }

#ifdef ALBOR_WAYLAND_LIBDECOR
        // destroy the raw xdg toplevel role, which a client-side decoration answer replaces with libdecor (§5.3)
        void destroy_toplevel_role(WaylandWindowState* state) {
            // the decoration is released first, because it references the toplevel it was created for (§5.3)
            if (state->decoration != nullptr) {
                zxdg_toplevel_decoration_v1_destroy(state->decoration);
                state->decoration = nullptr;
            }
            // the toplevel is destroyed, which takes the role state down (§5.3)
            if (state->toplevel != nullptr) {
                xdg_toplevel_destroy(state->toplevel);
                state->toplevel = nullptr;
            }
            // the shell surface follows, which leaves the wl_surface reusable (§5.3)
            if (state->shell_surface != nullptr) {
                xdg_surface_destroy(state->shell_surface);
                state->shell_surface = nullptr;
            }
            // the configure belonged to the spent role, which the replacement surface wins again (§5.3)
            if (state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                state->native.state->configured = false;
            }
        }
#endif

        // release the popup role a dismissal spent, which a reopen rebuilds on a fresh surface (§5.1)
        void destroy_popup_role(WaylandWindowState* state) {
            // the popup object is spent after its done event, which the protocol asks the client to destroy (§5.1)
            if (state->popup != nullptr) {
                xdg_popup_destroy(state->popup);
                state->popup = nullptr;
            }
            // the positioner is destroyed with the popup it placed (§5.1)
            if (state->positioner != nullptr) {
                xdg_positioner_destroy(state->positioner);
                state->positioner = nullptr;
            }
            // the activation request names the surface, so it is released first (§5.1)
            release_pending_activation(state);
            // the shell surface carries the spent role, which a reopen replaces (§5.1)
            if (state->shell_surface != nullptr) {
                xdg_surface_destroy(state->shell_surface);
                state->shell_surface = nullptr;
            }
            // the surface is released with the role, because a new xdg role needs a fresh surface (§5.1)
            if (state->surface != nullptr) {
                // the per surface scale objects extend the surface, so they go first (AD-27)
                destroy_scale_objects(state);
                // the surface is released under the state lock, which a last present cannot cross (§10.5)
                if (state->native.state != nullptr) {
                    const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                    // a renderer releases the surface itself, because its wsi surface still names it (§6.3)
                    if (state->native.state->renderer_presenting)
                        state->native.state->retired.push_back(state->surface);
                    else
                        wl_surface_destroy(state->surface);
                    // the shared state names no surface while the role is spent (§10.5)
                    state->native.state->surface = nullptr;
                    // the configure of the retired surface admits no buffer on the fresh one (§5.3)
                    state->native.state->configured = false;
                }
                else {
                    // a window without shared state presents through nothing, which destroys the surface here (§10.5)
                    wl_surface_destroy(state->surface);
                }
                state->surface = nullptr;
            }
        }

        // the dismissal a popup shares between the compositor's done event and its hide (§5.1)
        void dismiss_popup(WaylandWindowState* state) {
            // a popup whose role is already gone is not dismissed twice, so its done event fires one close (§5.1, §5.2)
            if (state->surface == nullptr)
                return;
            // the popup is taken off screen, which is the whole of a dismissal for a transient surface (§5.1)
            hide_native(state);
            // the spent role is destroyed, which a reopen rebuilds rather than reusing (§5.1)
            destroy_popup_role(state);
            // the framework is told the popup closed, so its owner can re-arm and tear it down (§5.2)
            if (state->listener != nullptr)
                state->listener->on_platform_closed();
        }

        // the buffer the compositor released, which its mapping is freed by (§6.2)
        void on_buffer_release(void* data, wl_buffer* released) {
            // the listener was installed with the state, which is the only receiver (§6.2)
            free_buffer(static_cast<WaylandWindowState*>(data), released);
        }

        // build the shm buffer the map commit attaches, which is sized at the request (§6.2, AD-27)
        bool attach_initial_buffer(WaylandWindowState* state) {
            // a surface the compositor has none of attaches nothing (§5.3)
            if (state->surface == nullptr || state->connection->shm() == nullptr)
                return false;
            // the scale converts the logical request into the physical buffer (AD-27)
            const int width = static_cast<int>(state->requested_size.width * state->scale_factor);
            const int height = static_cast<int>(state->requested_size.height * state->scale_factor);
            // a request that carries no area attaches nothing, which keeps the window unmapped (§6.2)
            if (width <= 0 || height <= 0)
                return false;
            // the pool is one anonymous file the compositor reads pixels from (§6.2)
            const int fd = memfd_create("albor-surface", MFD_CLOEXEC);
            // a pool that could not be created leaves the window unmapped, which the false reports (§6.2)
            if (fd < 0)
                return false;
            // the stride is the byte extent of one row of opaque pixels (§6.2)
            const int stride = width * 4;
            // the pool size is the byte extent of the whole buffer (§6.2)
            const int size = stride * height;
            // the file is sized to the buffer, which the compositor maps its side of (§6.2)
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                // the descriptor is closed, because the pool never came up (§6.2)
                close(fd);
                return false;
            }
            // the mapping is shared, because the compositor reads its side directly (§6.2)
            void* data = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            // a mapping that failed leaves the descriptor closed, which the caller retries on (§6.2)
            if (data == MAP_FAILED) {
                close(fd);
                return false;
            }
            // the pixels start fully transparent, which is what the first frame replaces (§6.2)
            std::memset(data, 0, static_cast<std::size_t>(size));
            // the pool is created on the compositor, which reads the descriptor's bytes (§6.2)
            wl_shm_pool* pool = wl_shm_create_pool(state->connection->shm(), fd, size);
            // a pool the compositor refused leaves the mapping freed, which the false reports (§6.2)
            if (pool == nullptr) {
                munmap(data, static_cast<std::size_t>(size));
                close(fd);
                return false;
            }
            // the buffer is carved from the pool, which the compositor scales and composes (§6.2, AD-27)
            wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
            // a buffer that could not be created leaves the pool destroyed (§6.2)
            if (buffer == nullptr) {
                wl_shm_pool_destroy(pool);
                munmap(data, static_cast<std::size_t>(size));
                close(fd);
                return false;
            }
            // the buffer listener is what the release arrives through (§6.2)
            static constexpr wl_buffer_listener BUFFER_LISTENER = {&on_buffer_release};
            wl_buffer_add_listener(buffer, &BUFFER_LISTENER, state);
            // the current buffer is retired, which the compositor releases after the next swap (§6.2)
            retire_current(state);
            // the new buffer becomes the current one, which the release event frees (§6.2)
            state->pool = pool;
            state->buffer = buffer;
            state->pool_data = data;
            state->pool_fd = fd;
            state->pool_size = size;
            // the viewport maps the physical buffer onto the requested logical extent (AD-27)
            apply_viewport_destination(state, state->requested_size);
            // the scale is applied before the commit, which the buffer is read at (AD-27)
            wl_surface_set_buffer_scale(state->surface, buffer_scale_of(state));
            // the buffer is attached, which the commit below maps (§5.3)
            wl_surface_attach(state->surface, buffer, 0, 0);
            // the whole surface is damaged, which is what a first frame presents (§6.2)
            wl_surface_damage(state->surface, 0, 0, INT32_MAX, INT32_MAX);
            return true;
        }

        // hand the map cycle to the compositor, which no buffer may precede (§5.3, §10.5)
        bool begin_map(WaylandWindowState* state) {
            // a window torn down across an earlier dispatch has nothing left to map (§4.2)
            if (state->native.state == nullptr || state->surface == nullptr)
                return false;
            // the shared state is kept alive across the dispatch, which a listener may tear down (§4.2)
            const std::shared_ptr<wayland::WaylandSurfaceState> surface_state = state->native.state;
            // the cycle state is read under the lock, which the render thread presents under (§10.5)
            bool configured = false;
            {
                const std::lock_guard<std::mutex> surface_lock(surface_state->mutex);
                configured = surface_state->configured;
                // a frame that races the commit is stopped, because the compositor owes this cycle an answer (§10.5)
                surface_state->mapped = false;
                // an unconfigured surface sends the bufferless initial commit again, which an unmap spends (§5.3)
                if (!configured)
                    wl_surface_commit(state->surface);
            }
            // an already configured surface owes no commit, which the first show after create relies on (§5.3)
            if (configured)
                return true;
            // the answer is dispatched with no lock held, so a resize never waits on the render thread (§10.5)
            if (wl_display_roundtrip(state->connection->display()) < 0)
                return false;
            // a listener may have closed the window across the dispatch, which leaves nothing to map (§4.2)
            if (state->native.state == nullptr || state->surface == nullptr)
                return false;
            // a compositor that owes a configure sent it, while one keeping its previous cycle sent none (§5.3)
            const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
            state->native.state->configured = true;
            return true;
        }

        // attach the initial buffer and mark the surface mapped, which a show and its reopen share (§5.3, §10.5)
        bool attach_and_map(WaylandWindowState* state) {
            // a listener may have closed the window across the dispatch, which leaves nothing to map (§4.2)
            if (state->native.state == nullptr || state->surface == nullptr)
                return false;
            // the surface is locked, so the map commits atomically against a render thread present (§10.5)
            const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
            // a map cycle the compositor has not accepted attaches no buffer, which the protocol forbids (§5.3)
            if (!state->native.state->configured) {
                // the surface stays off screen, which a later show starts again (§5.3)
                state->native.state->mapped = false;
                return false;
            }
            // a renderer owned surface maps on its own next buffer, because a placeholder breaks explicit sync (§6.3)
            if (renderer_presenting(state)) {
                // the viewport and the buffer scale are surface state the renderer's next commit carries (AD-27)
                apply_viewport_destination(state, state->metrics.valid() ? state->metrics.logical_size : state->requested_size);
                wl_surface_set_buffer_scale(state->surface, buffer_scale_of(state));
                // the render thread may present now, which is the whole of showing that surface (§10.5)
                state->native.state->mapped = true;
                return true;
            }
            // a buffer that could not be built leaves the window unmapped (§6.2)
            if (!attach_initial_buffer(state))
                return false;
            // the commit puts the buffer on screen, which is the whole of showing (§5.3)
            wl_surface_commit(state->surface);
            // the render thread may present now that the surface is mapped (§10.5)
            state->native.state->mapped = true;
            return true;
        }

        // the output the surface entered, which carries the fallback scale on older compositors (AD-27)
        void on_surface_enter(void* data, wl_surface*, wl_output*) {
            // the state is kept alive across the listener, because the scale may tear the window down (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
#ifdef ALBOR_WAYLAND_SURFACE_V6
            // a surface version six carries the preferred scale, so the output is not the authority (AD-27)
            if (state->connection->compositor_version() >= 6)
                return;
#endif
            // a fractional window reads the protocol preference, so the output is not the authority (AD-27)
            if (uses_fractional_scale(state.get()))
                return;
            // the scale of the output the window sits on is applied, which the fallback authority is (AD-27)
            apply_scale(state.get(), static_cast<float>(state->connection->output_scale()));
        }

        // the output the surface left, which carries no fact the adapter tracks (AD-27)
        void on_surface_leave(void*, wl_surface*, wl_output*) {
            // the scale of the next enter event applies, which is what the fallback waits for (AD-27)
        }

#ifdef ALBOR_WAYLAND_SURFACE_V6
        // the preferred scale the compositor announced, which is the scale authority from surface version six on (AD-27)
        void on_surface_preferred_scale(void* data, wl_surface*, std::int32_t factor) {
            // the state is kept alive across the listener, because the scale may tear the window down (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // the fractional protocol overrides the integer preference, which the compositor announces there (AD-27)
            if (uses_fractional_scale(state.get()))
                return;
            // the scale the compositor prefers is applied, which the authority from surface version six on is (AD-27)
            apply_scale(state.get(), static_cast<float>(std::max(1, static_cast<int>(factor))));
        }

        // the preferred transform the compositor announced, which the adapter keeps identity (AD-27)
        void on_surface_preferred_transform(void*, wl_surface*, std::uint32_t) {
            // a rotated transform is out of scope, which the identity keeps (AD-27)
        }
#endif

        // the configure the shell surface sent, which is acknowledged and read as metrics (§5.3)
        void on_shell_surface_configure(void* data, xdg_surface* shell_surface, std::uint32_t serial) {
            // the state is kept alive across the listener, because the resize may tear the window down (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // the configure is acknowledged, which the next commit must be preceded by (§5.3)
            xdg_surface_ack_configure(shell_surface, serial);
            // the map cycle is ready for a buffer, which the render thread reads under the state lock (§10.5)
            if (state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                state->native.state->configured = true;
            }
            // the metrics are applied, which a proposal of zero leaves at the requested one (§5.3)
            apply_configure(state.get(), state->proposed_width, state->proposed_height);
            // the proposal is consumed, because the next sequence starts from zero (§5.3)
            state->proposed_width = 0;
            state->proposed_height = 0;
        }

        // the toplevel state the compositor proposed, which the surface configure applies (§5.3)
        void on_toplevel_configure(void* data, xdg_toplevel*, std::int32_t width, std::int32_t height, wl_array*) {
            // the listener was installed with the state, which is the only receiver (§5.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // the proposed size is remembered, which the surface configure applies (§5.3)
            state->proposed_width = static_cast<int>(width);
            state->proposed_height = static_cast<int>(height);
        }

        // the close the compositor asked for, which the application may veto (§5.2)
        void on_toplevel_close(void* data, xdg_toplevel*) {
            // the state is kept alive across the veto, because a granted close may destroy the window (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // the close is asked of the framework, which may veto it (§5.2)
            request_close(state.get());
        }

        // the bounds the compositor proposed, which no framework path reads (§5.3)
        void on_toplevel_configure_bounds(void*, xdg_toplevel*, std::int32_t, std::int32_t) {
            // the bounds are a proposal, which the framework's own layout owns (§5.3)
        }

        // the capabilities the compositor announced, which no framework path reads (§5.3)
        void on_toplevel_wm_capabilities(void*, xdg_toplevel*, wl_array*) {
            // the capabilities name compositor chrome, which the framework draws none of (§5.3)
        }

        // the mode the compositor chose, which says whether it draws the chrome or the client must (§5.3)
        void on_decoration_configure(void* data, zxdg_toplevel_decoration_v1*, std::uint32_t mode) {
            // the listener was installed with the state, which is the only receiver (§5.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a client-side answer is honoured only where the private frame can draw it (§5.3)
            bool client_side_available = false;
#ifdef ALBOR_WAYLAND_LIBDECOR
            client_side_available = state->connection->decoration_context() != nullptr;
#endif
            // the server mode is the compositor's chrome, while the client mode asks the client to draw a frame (§5.3)
            if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)
                state->decoration_mode = WaylandDecorationMode::ServerSide;
            else
                state->decoration_mode = client_side_available ? WaylandDecorationMode::ClientSide : WaylandDecorationMode::None;
        }

        // record the handle the compositor assigned the export, which the portal imports the window by (§5.3)
        void on_exported_handle(void* data, zxdg_exported_v2*, const char* handle) {
            // the export was created with the state, which is the only receiver (§5.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a compositor that named no handle leaves the window unexported (§5.3)
            if (handle == nullptr)
                return;
            // the handle is what the portal window identifier carries (§5.3)
            state->exported_handle = handle;
        }

        // the size the popup configure proposed, which the shell surface configure applies (§5.1)
        void on_popup_configure(void* data, xdg_popup*, std::int32_t, std::int32_t, std::int32_t width, std::int32_t height) {
            // the listener was installed with the state, which is the only receiver (§5.1)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a configure that names a size is remembered, which the surface configure applies (§5.1)
            if (width > 0 && height > 0) {
                state->proposed_width = static_cast<int>(width);
                state->proposed_height = static_cast<int>(height);
            }
        }

        // the compositor dismissed the popup, which the framework closes (§5.1)
        void on_popup_done(void* data, xdg_popup*) {
            // the state is kept alive across the dismissal, because the hide reaches the framework (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // the popup is hidden rather than closed, so its control can reopen it (§5.1)
            dismiss_popup(state.get());
        }

        // the popup was repositioned, which its configure carries the size of (§5.1)
        void on_popup_repositioned(void*, xdg_popup*, std::uint32_t) {
            // the new position is applied by the compositor, which the next configure reports (§5.1)
        }

#ifdef ALBOR_WAYLAND_LIBDECOR
        // the content size libdecor proposed, which the frame commit applies (§5.3)
        void on_frame_configure(libdecor_frame* frame, libdecor_configuration* configuration, void* data) {
            // the listener was installed with the state, which is the only receiver (§5.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // the content size is what the application buffers, which a missing size leaves at the request (§5.3)
            int width = 0;
            int height = 0;
            if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height) || width <= 0 || height <= 0) {
                width = static_cast<int>(state->requested_size.width);
                height = static_cast<int>(state->requested_size.height);
            }
            // the requested size follows the content the decorations left room for, which the buffer matches (§5.3)
            state->requested_size = Size{static_cast<float>(width), static_cast<float>(height)};
            // the state is committed first, which is what lets the frame finish the configure (§5.3)
            libdecor_state* decoration_state = libdecor_state_new(width, height);
            libdecor_frame_commit(frame, decoration_state, configuration);
            libdecor_state_free(decoration_state);
            // the metrics follow the content size, which the framework reads (§9.1)
            apply_configure(state, width, height);
        }

        // the close libdecor forwarded, which the application may veto (§5.2)
        void on_frame_close(libdecor_frame*, void* data) {
            // the state is kept alive across the veto, because a granted close may destroy the window (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // the close is asked of the framework, which may veto it (§5.2)
            request_close(state.get());
        }

        // the commit libdecor asked the content surface for, which its decorations need (§5.3)
        void on_frame_commit(libdecor_frame*, void* data) {
            // the listener was installed with the state, which is the only receiver (§5.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a surface that is already gone carries no commit (§4.2)
            if (state->surface == nullptr)
                return;
            // the content is committed, which is what the frame's decorations follow (§5.3)
            wl_surface_commit(state->surface);
        }

        // the popup dismissal libdecor forwarded, which the framework popup host owns (§5.1)
        void on_frame_dismiss_popup(libdecor_frame*, const char*, void*) {
            // the framework closes its own popups, which this hint does not reach across (§5.1)
        }

        // the interface every decorated frame is built with, which carries the four events the adapter reads (§5.3)
        constexpr libdecor_frame_interface FRAME_INTERFACE = {&on_frame_configure, &on_frame_close, &on_frame_commit, &on_frame_dismiss_popup};

        // create the libdecor frame that owns the xdg role, which the fallback and a client-side answer share (§5.3)
        bool create_frame(WaylandWindowState* state, const WindowOptions& options) {
            // the frame decorates the surface of an ordinary, dialog or tool window (§5.3)
            state->frame = libdecor_decorate(state->connection->decoration_context(), state->surface, const_cast<libdecor_frame_interface*>(&FRAME_INTERFACE), state);
            // a frame libdecor refused leaves the caller to tear the window down (§5.3)
            if (state->frame == nullptr)
                return false;
            // the frame owns the xdg surface and toplevel, which the popup parent and the metrics read (§5.3)
            state->shell_surface = libdecor_frame_get_xdg_surface(state->frame);
            state->toplevel = libdecor_frame_get_xdg_toplevel(state->frame);
            // the title and the app id are frame state, which the decoration draws (§5.3)
            libdecor_frame_set_title(state->frame, options.title.c_str());
            libdecor_frame_set_app_id(state->frame, "albor");
            // a dialog or tool names its parent frame, which the compositor uses for stacking and focus (§5.1)
            if (options.native_parent.valid()) {
                // the parent state is cast back from the opaque handle, which only the adapter reads (§3.5, AD-05)
                WaylandWindowState* parent = static_cast<WaylandWindowState*>(const_cast<void*>(options.native_parent.object()));
                // a parent that rides a frame names it, which is what the relation reads (§5.1)
                if (parent != nullptr && parent->frame != nullptr)
                    libdecor_frame_set_parent(state->frame, parent->frame);
            }
            return true;
        }
#endif

        // the surface the pointer entered, which names where the button and the wheel land (§8.3)
        void on_pointer_enter(void* data, wl_pointer*, std::uint32_t, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a focus the compositor gave another surface does not aim this window (§8.3)
            if (surface != state->surface) {
                // the pointer is off this surface, which the button and the wheel gate on (§8.3)
                state->pointer_on_surface = false;
                return;
            }
            // the pointer is on this surface, which the button and the wheel gate on (§8.3)
            state->pointer_on_surface = true;
            // the position is remembered, which a button event carries none of (§8.3)
            state->pointer_position = point_of(wl_fixed_to_double(x), wl_fixed_to_double(y));
        }

        // the surface the pointer left, which ends the batch a wheel rides (§8.3)
        void on_pointer_leave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a focus the compositor took from another surface does not touch this window (§8.3)
            if (surface != state->surface)
                return;
            // the pointer is off this surface, which no button or wheel may follow (§8.3)
            state->pointer_on_surface = false;
            // the batches drop, because a wheel event behind the surface would carry a stale position (§8.3)
            state->wheel[0].clear();
            state->wheel[1].clear();
            // the pointer is over no node of this window, so every hover it held ends (§8.3)
            if (state->listener != nullptr)
                state->listener->on_platform_pointer_exited();
        }

        // the move the pointer made, which is delivered in logical units (§8.3)
        void on_pointer_motion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a motion that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface)
                return;
            // the position is remembered, which a later button and wheel carry (§8.3)
            state->pointer_position = point_of(wl_fixed_to_double(x), wl_fixed_to_double(y));
            // the move is normalized, which the tree routes (§8.3)
            Event message;
            message.kind = EventKind::PointerMove;
            message.position = state->pointer_position;
            raise_event(state.get(), message);
        }

        // the press or the release the pointer made, which the left button carries (§8.3)
        void on_pointer_button(void* data, wl_pointer*, std::uint32_t serial, std::uint32_t, std::uint32_t button, std::uint32_t button_state) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a button that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface)
                return;
            // the serial is remembered, which a popup grab is answered with (§5.1)
            state->last_input_serial = serial;
            // the connection shares the serial with the clipboard and the activation request (§3.5, §5.1)
            state->connection->note_input_serial(serial);
            // the press or the release is normalized, which the tree routes (§8.3)
            Event message;
            message.kind = button_state == KEY_STATE_PRESSED ? EventKind::PointerDown : EventKind::PointerUp;
            // the button maps onto the portable set, which the context menu and the drag read (§8.3)
            message.button = button_from_evdev(button);
            // the position is the one the pointer last named, because a button event carries none of its own (§8.3)
            message.position = state->pointer_position;
            raise_event(state.get(), message);
        }

        // the continuous delta an axis event carried, which the frame may batch into one wheel (§8.3)
        void on_pointer_axis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // an axis that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface)
                return;
            // only the vertical axis is delivered, which is what the sibling adapters carry (§8.3)
            if (axis != AXIS_VERTICAL)
                return;
            // the delta joins the batch, which the frame emits once (§8.3)
            state->wheel[AXIS_VERTICAL].note_axis(-static_cast<float>(wl_fixed_to_double(value)));
        }

        // the frame an axis group ends with, which emits one wheel event (§8.3)
        void on_pointer_frame(void* data, wl_pointer*) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a frame that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface) {
                // the batches drop, because the pointer is elsewhere (§8.3)
                state->wheel[0].clear();
                state->wheel[1].clear();
                return;
            }
            // the vertical batch is answered with one wheel event, which the frame grouped (§8.3)
            if (state->wheel[AXIS_VERTICAL].pending()) {
                // the wheel event carries the batch's scroll and its unit (§8.3)
                Event message;
                message.kind = EventKind::Wheel;
                message.position = state->pointer_position;
                message.scroll = state->wheel[AXIS_VERTICAL].scroll();
                message.precision = state->wheel[AXIS_VERTICAL].precision();
                raise_event(state.get(), message);
            }
            // both batches clear, because the next frame groups on its own (§8.3)
            state->wheel[0].clear();
            state->wheel[1].clear();
        }

        // the wheel source the compositor named, which no framework path reads (§8.3)
        void on_pointer_axis_source(void*, wl_pointer*, std::uint32_t) {
            // the source names the hardware, which the unit already carries (§8.3)
        }

        // the axis the wheel stopped on, which ends a momentum gesture the batch already covered (§8.3)
        void on_pointer_axis_stop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {
            // a stopped momentum carries no delta, which nothing is emitted for (§8.3)
        }

        // the steps a notched wheel reported, which the batch carries until the frame (§8.3)
        void on_pointer_axis_discrete(void* data, wl_pointer*, std::uint32_t axis, std::int32_t steps) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // an axis that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface)
                return;
            // only the vertical axis is delivered, which is what the sibling adapters carry (§8.3)
            if (axis != AXIS_VERTICAL)
                return;
            // the steps join the batch, which the frame emits once (§8.3)
            state->wheel[AXIS_VERTICAL].note_discrete(-steps);
        }

#ifdef ALBOR_WAYLAND_POINTER_WARP
        // the warp the compositor named, which moves the pointer inside the surface without a device (§8.3)
        void on_pointer_warp(void* data, wl_pointer*, wl_fixed_t x, wl_fixed_t y) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a warp that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface)
                return;
            // the position is remembered, which a later button and wheel carry (§8.3)
            state->pointer_position = point_of(wl_fixed_to_double(x), wl_fixed_to_double(y));
            // the move is normalized, which the tree routes (§8.3)
            Event message;
            message.kind = EventKind::PointerMove;
            message.position = state->pointer_position;
            raise_event(state.get(), message);
        }
#endif

#ifdef ALBOR_WAYLAND_AXIS_VALUE120
        // the high resolution count a wheel reported, which the batch carries until the frame (§8.3)
        void on_pointer_axis_value120(void* data, wl_pointer*, std::uint32_t axis, std::int32_t value120) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // an axis that arrived on another surface is not this window's (§8.3)
            if (!state->pointer_on_surface)
                return;
            // only the vertical axis is delivered, which is what the sibling adapters carry (§8.3)
            if (axis != AXIS_VERTICAL)
                return;
            // the count joins the batch, which the frame emits once (§8.3)
            state->wheel[AXIS_VERTICAL].note_value120(-value120);
        }
#endif

#ifdef ALBOR_WAYLAND_SURFACE_V6
        // the relative direction an axis reported, which the flavour already carries (§8.3)
        void on_pointer_axis_relative_direction(void*, wl_pointer*, std::uint32_t, std::uint32_t) {
            // the direction is a hint for touchpads, which the pass through keeps raw (§8.3)
        }
#endif

        // the keymap the compositor shared, which the keys are translated through (§8.3)
        void on_keyboard_keymap(void* data, wl_keyboard*, std::uint32_t format, std::int32_t fd, std::uint32_t size) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a keymap the compositor shared in another format carries no keys, which the old one keeps (§8.3)
            if (fd < 0 || size == 0 || format != KEYMAP_FORMAT_XKB_V1) {
                // the descriptor is closed on every path, because a leaked one exhausts the table (§8.3)
                if (fd >= 0)
                    close(fd);
                return;
            }
            // the keymap bytes are mapped read only, which the compositor wrote (§8.3)
            void* bytes = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            // the descriptor is closed once the mapping stands, because the keymap lives in it (§8.3)
            close(fd);
            // a mapping that failed keeps the old keymap, which the guard reports (§8.3)
            if (bytes == MAP_FAILED)
                return;
            // the keymap is built from the bytes, which the state is derived from (§8.3)
            xkb_keymap* built = xkb_keymap_new_from_buffer(state->xkb, static_cast<const char*>(bytes), size, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            // the mapping is released, because the keymap copied the bytes it needed (§8.3)
            munmap(bytes, size);
            // a keymap that failed to compile keeps the old one, which the guard reports (§8.3)
            if (built == nullptr)
                return;
            // the previous state is dropped, which the new keymap replaces (§8.3)
            if (state->keymap_state != nullptr)
                xkb_state_unref(state->keymap_state);
            // the previous keymap is dropped, which the new one replaces (§8.3)
            if (state->keymap != nullptr)
                xkb_keymap_unref(state->keymap);
            // the keymap and its state take over, which the modifiers and the keys are read through (§8.3)
            state->keymap = built;
            state->keymap_state = xkb_state_new(state->keymap);
            // the canonical modifiers are resolved once, which the state answers with (§8.3)
            state->mod_shift = xkb_keymap_mod_get_index(state->keymap, "Shift");
            state->mod_control = xkb_keymap_mod_get_index(state->keymap, "Control");
            // the alt and the logo modifiers are the portable option and command (§8.3)
            state->mod_alt = xkb_keymap_mod_get_index(state->keymap, "Mod1");
            state->mod_logo = xkb_keymap_mod_get_index(state->keymap, "Mod4");
        }

        // the focus the keyboard gave the surface, which opens the input method session (§8.3)
        void on_keyboard_enter(void* data, wl_keyboard*, std::uint32_t serial, wl_surface* surface, wl_array*) {
            // the state is kept alive across the listener, because the focus may tear the window down (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a focus the compositor gave another surface does not aim this window (§8.3)
            if (surface != state->surface)
                return;
            // the keyboard is on this surface, which the keys gate on (§8.3)
            state->keyboard_on_surface = true;
            // the framework is told the window gained key status, which restores its focus cue (§8.3)
            if (state->listener != nullptr)
                state->listener->on_platform_focus_gained();
            // the serial is remembered, which a popup grab is answered with (§5.1)
            state->last_input_serial = serial;
            // the connection shares the serial with the clipboard and the activation request (§3.5, §5.1)
            state->connection->note_input_serial(serial);
            // a text input the compositor has none of opens nothing, which is the portable default (§8.3)
            if (state->text_input == nullptr)
                return;
            // the input method session is opened, which the preedit and the commit flow through (§8.3)
            zwp_text_input_v3_enable(state->text_input);
            // the state change is committed, which the compositor applies atomically (§8.3)
            zwp_text_input_v3_commit(state->text_input);
        }

        // the focus the keyboard took from the surface, which closes the input method session (§8.3)
        void on_keyboard_leave(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a focus the compositor took from another surface does not touch this window (§8.3)
            if (surface != state->surface)
                return;
            // the keyboard is off this surface, which no key may follow (§8.3)
            state->keyboard_on_surface = false;
            // the framework is told the window lost key status, which ends a drag no release can finish (§8.3)
            if (state->listener != nullptr)
                state->listener->on_platform_focus_lost();
            // a text input the compositor has none of closes nothing (§8.3)
            if (state->text_input != nullptr) {
                // the input method session is closed, which the compositor cancels its preedit under (§8.3)
                zwp_text_input_v3_disable(state->text_input);
                zwp_text_input_v3_commit(state->text_input);
            }
            // a composition that was open is renounced, which the field clears its mark under (§8.3)
            if (state->composing) {
                // the mark is gone, which is what the empty composing event of §8.3 reports
                state->composing = false;
                Event message;
                message.kind = EventKind::Composing;
                raise_event(state.get(), message);
            }
        }

        // the key the keyboard delivered, which the keymap translates (§8.3)
        void on_keyboard_key(void* data, wl_keyboard*, std::uint32_t serial, std::uint32_t, std::uint32_t key, std::uint32_t keyboard_state) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a key that arrived on another surface is not this window's (§8.3)
            if (!state->keyboard_on_surface)
                return;
            // the serial is remembered, which a popup grab is answered with (§5.1)
            state->last_input_serial = serial;
            // the connection shares the serial with the clipboard and the activation request (§3.5, §5.1)
            state->connection->note_input_serial(serial);
            // the press or the release is normalized, which the tree routes (§8.3)
            Event message;
            message.kind = keyboard_state == KEY_STATE_PRESSED ? EventKind::KeyDown : EventKind::KeyUp;
            // the keycode is offset by eight, which is where the evdev range starts (§8.3)
            const xkb_keycode_t keycode = static_cast<xkb_keycode_t>(key) + 8;
            // a keyboard without a keymap names no key, which is what an unreadable state reports (§8.3)
            message.key = Key::None;
            // the keysyms the keycode translates to are read from the keymap state (§8.3)
            if (state->keymap_state != nullptr) {
                const xkb_keysym_t* syms = nullptr;
                const int count = xkb_state_key_get_syms(state->keymap_state, keycode, &syms);
                // the first symbol is the key the user named, which the portable table maps (§8.3)
                if (count > 0 && syms != nullptr)
                    message.key = key_from_keysym(static_cast<std::uint32_t>(syms[0]));
            }
            // the modifiers are read from the state, which the modifiers event last applied (§8.3)
            message.modifiers = modifiers_of(state.get());
            raise_event(state.get(), message);
            // a printable press commits its character as text, which is what a text field inserts (§8.3)
            if (message.kind != EventKind::KeyDown || state->keymap_state == nullptr || state->composing)
                return;
            // the utf8 the key produces under the held modifiers is what the user typed (§8.3)
            char typed[32] = {};
            const int typed_length = xkb_state_key_get_utf8(state->keymap_state, keycode, typed, sizeof(typed));
            // the text the press commits is empty when it names no printable character (§8.3)
            const std::string text = text_from_key(message.modifiers, typed, typed_length);
            // a key that types no text leaves the named key as the whole event (§8.3)
            if (text.empty())
                return;
            Event committed;
            committed.kind = EventKind::TextInput;
            committed.text = text;
            raise_event(state.get(), committed);
        }

        // the modifiers the keyboard applied, which the keymap state tracks (§8.3)
        void on_keyboard_modifiers(void* data, wl_keyboard*, std::uint32_t, std::uint32_t depressed, std::uint32_t latched, std::uint32_t locked, std::uint32_t group) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a keyboard without a keymap records nothing, which is what an unreadable state reports (§8.3)
            if (state->keymap_state == nullptr)
                return;
            // the masks are applied to the state, which the next key event reads (§8.3)
            xkb_state_update_mask(state->keymap_state, depressed, latched, locked, 0, 0, group);
        }

        // the repeat rate the compositor named, which the framework's own timers own (§8.3)
        void on_keyboard_repeat_info(void*, wl_keyboard*, std::int32_t, std::int32_t) {
            // the repeat is a framework timer concern, which the platform carries none of (§8.3)
        }

        // the surface the input method focused, which its session rides (§8.3)
        void on_text_input_enter(void* data, zwp_text_input_v3*, wl_surface* surface) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // the session aims at this surface exactly when the compositor named it (§8.3)
            state->ime_on_surface = surface == state->surface;
        }

        // the surface the input method left, which its session ends on (§8.3)
        void on_text_input_leave(void* data, zwp_text_input_v3*, wl_surface* surface) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a focus on another surface does not touch this window (§8.3)
            if (surface != state->surface)
                return;
            // the session aims nowhere, which the events gate on (§8.3)
            state->ime_on_surface = false;
        }

        // the preedit the done event will apply, which a composition holds (§8.3)
        void on_text_input_preedit(void* data, zwp_text_input_v3*, const char* text, std::int32_t, std::int32_t) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a session that is not aimed at this surface carries nothing (§8.3)
            if (!state->ime_on_surface)
                return;
            // the preedit is buffered, which the done event applies (§8.3)
            state->preedit_pending = true;
            // a null text is the empty preedit, which the unmark path clears the mark with (§8.3)
            state->preedit_text = text != nullptr ? std::string(text) : std::string();
        }

        // the commit the done event will apply, which a resolved composition carries (§8.3)
        void on_text_input_commit(void* data, zwp_text_input_v3*, const char* text) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a session that is not aimed at this surface carries nothing (§8.3)
            if (!state->ime_on_surface)
                return;
            // the commit is buffered, which the done event applies (§8.3)
            state->commit_pending = true;
            // a null text is the empty commit, which inserts nothing (§8.3)
            state->commit_text = text != nullptr ? std::string(text) : std::string();
        }

        // the surrounding deletion the done event will apply, which the byte map of §8.3 stays out of reach for (§8.3)
        void on_text_input_delete(void* data, zwp_text_input_v3*, std::uint32_t, std::uint32_t) {
            // the listener was installed with the state, which is the only receiver (§8.3)
            WaylandWindowState* state = static_cast<WaylandWindowState*>(data);
            // a session that is not aimed at this surface carries nothing (§8.3)
            if (!state->ime_on_surface)
                return;
            // the deletion is dropped, because the document ranges of §8.3 are unaddressable here (§8.3)
        }

        // the changes are applied, which is what the done event commits (§8.3)
        void on_text_input_done(void* data, zwp_text_input_v3*, std::uint32_t) {
            // the state is kept alive across the listener, because the event may reach the tree (§4.2)
            const std::shared_ptr<WaylandWindowState> state = static_cast<WaylandWindowState*>(data)->shared_from_this();
            // a session that is not aimed at this surface carries nothing (§8.3)
            if (!state->ime_on_surface)
                return;
            // a commit supersedes the mark, which is the order the protocol applies (§8.3)
            if (state->commit_pending) {
                // the composition is closed, because the commit resolved it (§8.3)
                state->composing = false;
                Event message;
                message.kind = EventKind::TextInput;
                message.text = state->commit_text;
                raise_event(state.get(), message);
            }
            // a preedit without a commit is the mark, which the field paints (§8.3)
            else if (state->preedit_pending) {
                // the composition is open exactly while the mark carries text (§8.3)
                state->composing = !state->preedit_text.empty();
                Event message;
                message.kind = EventKind::Composing;
                message.text = state->preedit_text;
                raise_event(state.get(), message);
            }
            // the pendings clear, which the next batch rebuilds (§8.3)
            state->preedit_pending = false;
            state->commit_pending = false;
            state->preedit_text.clear();
            state->commit_text.clear();
        }

        // the listener set of the surface, which carries the scale facts (AD-27)
        constexpr wl_surface_listener SURFACE_LISTENER = {
            &on_surface_enter,
            &on_surface_leave,
#ifdef ALBOR_WAYLAND_SURFACE_V6
            &on_surface_preferred_scale,
            &on_surface_preferred_transform,
#endif
        };

        // the listener set of the shell surface, which carries the configure (§5.3)
        constexpr xdg_surface_listener SHELL_SURFACE_LISTENER = {&on_shell_surface_configure};

        // the listener set of the toplevel, which carries the close request (§5.2)
        constexpr xdg_toplevel_listener TOPLEVEL_LISTENER = {&on_toplevel_configure, &on_toplevel_close, &on_toplevel_configure_bounds, &on_toplevel_wm_capabilities};

        // the listener set of the popup, which carries its configure and dismissal (§5.1)
        constexpr xdg_popup_listener POPUP_LISTENER = {&on_popup_configure, &on_popup_done, &on_popup_repositioned};

        // the listener set of the decoration, which carries the mode the compositor chose (§5.3)
        constexpr zxdg_toplevel_decoration_v1_listener DECORATION_LISTENER = {&on_decoration_configure};

        // the listener set of the export, which carries the handle the portal imports the window by (§5.3)
        constexpr zxdg_exported_v2_listener EXPORTED_LISTENER = {&on_exported_handle};

        // the listener set of the pointer, which carries the normalized input of §8.3
        constexpr wl_pointer_listener POINTER_LISTENER = {
            &on_pointer_enter,
            &on_pointer_leave,
            &on_pointer_motion,
            &on_pointer_button,
            &on_pointer_axis,
            &on_pointer_frame,
            &on_pointer_axis_source,
            &on_pointer_axis_stop,
            &on_pointer_axis_discrete,
#ifdef ALBOR_WAYLAND_AXIS_VALUE120
            &on_pointer_axis_value120,
#endif
#ifdef ALBOR_WAYLAND_SURFACE_V6
            &on_pointer_axis_relative_direction,
#endif
#ifdef ALBOR_WAYLAND_POINTER_WARP
            &on_pointer_warp,
#endif
        };

        // the listener set of the keyboard, which carries the normalized input of §8.3
        constexpr wl_keyboard_listener KEYBOARD_LISTENER = {&on_keyboard_keymap, &on_keyboard_enter, &on_keyboard_leave, &on_keyboard_key, &on_keyboard_modifiers, &on_keyboard_repeat_info};

        // the listener set of the text input, which carries the composition of §8.3
        constexpr zwp_text_input_v3_listener TEXT_INPUT_LISTENER = {&on_text_input_enter, &on_text_input_leave, &on_text_input_preedit, &on_text_input_commit, &on_text_input_delete, &on_text_input_done};

        // export the window to the portal, which needs the toplevel role and the compositor's exporter (§5.3)
        void export_toplevel(WaylandWindowState* state) {
            // a popup has no toplevel role to export, which the protocol refuses (§5.3)
            if (state->popup != nullptr)
                return;
            // a compositor without the exporter cannot name the window to the portal (§5.3)
            if (state->connection == nullptr || state->connection->exporter() == nullptr)
                return;
            // a surface that was taken down has nothing to export (§5.3)
            if (state->surface == nullptr)
                return;
            // the exported reference is what the portal imports the window by (§5.3)
            state->exported = zxdg_exporter_v2_export_toplevel(state->connection->exporter(), state->surface);
            // an export the compositor refused leaves no parent for a panel (§5.3)
            if (state->exported == nullptr)
                return;
            // the handle event names the window, which arrives on the next dispatch (§5.3)
            zxdg_exported_v2_add_listener(state->exported, &EXPORTED_LISTENER, state);
        }

        // build the text input the window's input method session rides, which the manager may not exist for (§8.3)
        zwp_text_input_v3* create_text_input(WaylandWindowState* state) {
            // a compositor with no text input manager offers none, which the null reports (§8.3)
            if (state->connection->text_inputs() == nullptr)
                return nullptr;
            // the text input is created on the seat, which the focus events name surfaces on (§8.3)
            return zwp_text_input_manager_v3_get_text_input(state->connection->text_inputs(), state->connection->seat());
        }
    } // namespace

    namespace
    {
        // the shell surface a window's role owns, which libdecor creates after its first dispatch (§5.3)
        xdg_surface* role_shell_surface(WaylandWindowState* state) {
#ifdef ALBOR_WAYLAND_LIBDECOR
            // a frame owns the role, which the getter answers once libdecor finished its init (§5.3)
            if (state->frame != nullptr) {
                if (xdg_surface* surface = libdecor_frame_get_xdg_surface(state->frame))
                    return surface;
            }
#endif
            return state->shell_surface;
        }

        // the toplevel a window's role owns, which libdecor creates after its first dispatch (§5.3)
        xdg_toplevel* role_toplevel(WaylandWindowState* state) {
#ifdef ALBOR_WAYLAND_LIBDECOR
            // a frame owns the role, which the getter answers once libdecor finished its init (§5.3)
            if (state->frame != nullptr) {
                if (xdg_toplevel* toplevel = libdecor_frame_get_xdg_toplevel(state->frame))
                    return toplevel;
            }
#endif
            return state->toplevel;
        }

        // create the popup role the shell surface hosts, which a dismissal rebuilds rather than reuses (§5.1)
        bool create_popup_role(WaylandWindowState* state) {
            // the parent must still be alive, which the framework's teardown order guarantees (§5.1)
            if (state->parent == nullptr || role_shell_surface(state->parent) == nullptr || state->shell_surface == nullptr)
                return false;
            // the positioner is what places the popup relative to the parent surface (§5.1)
            state->positioner = xdg_wm_base_create_positioner(state->connection->shell());
            // a positioner the compositor refused leaves the caller nothing to attach (§5.1)
            if (state->positioner == nullptr)
                return false;
            // the requested size is the popup size, which the positioner carries (§5.1)
            const int popup_width = std::max(1, static_cast<int>(state->requested_size.width));
            const int popup_height = std::max(1, static_cast<int>(state->requested_size.height));
            // the anchor rect places the popup against the parent, which an empty anchor replaces with a point (§5.1)
            const Rect anchor = state->popup_anchor.width > 0.0f && state->popup_anchor.height > 0.0f ? state->popup_anchor : Rect{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};
            // the size and the anchor rect are the two facts the positioner needs to place the surface (§5.1)
            xdg_positioner_set_size(state->positioner, popup_width, popup_height);
            xdg_positioner_set_anchor_rect(state->positioner, static_cast<std::int32_t>(anchor.x), static_cast<std::int32_t>(anchor.y), std::max(1, static_cast<int>(anchor.width)), std::max(1, static_cast<int>(anchor.height)));
            // the popup hangs below the anchor and slides or flips where the screen edge would cut it (§5.1)
            xdg_positioner_set_anchor(state->positioner, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
            xdg_positioner_set_gravity(state->positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
            xdg_positioner_set_constraint_adjustment(state->positioner, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
            // the popup role attaches to the parent shell surface the positioner names (§5.1)
            state->popup = xdg_surface_get_popup(state->shell_surface, role_shell_surface(state->parent), state->positioner);
            // a popup the compositor refused leaves the positioner destroyed here, which the caller does not own (§5.1)
            if (state->popup == nullptr) {
                xdg_positioner_destroy(state->positioner);
                state->positioner = nullptr;
                return false;
            }
            // the popup listener carries its configure and dismissal, installed with the state (§5.1)
            xdg_popup_add_listener(state->popup, &POPUP_LISTENER, state);
            // the grab is what dismisses the popup on an outside click, which the parent's last serial keys (§5.1)
            if (state->connection->seat() != nullptr && state->parent->last_input_serial != 0)
                xdg_popup_grab(state->popup, state->connection->seat(), state->parent->last_input_serial);
            return true;
        }

        // rebuild the popup role a dismissal released, which reuses the surface the renderer presents through (§5.1)
        bool rebuild_popup_role(WaylandWindowState* state) {
            // the parent must still be alive, which the framework's teardown order guarantees (§5.1)
            if (state->parent == nullptr || role_shell_surface(state->parent) == nullptr || state->connection->compositor() == nullptr || state->connection->shell() == nullptr)
                return false;
            // the surface is created on the compositor, because a surface that carried a role cannot carry another (§5.1)
            state->surface = wl_compositor_create_surface(state->connection->compositor());
            // a surface the compositor refused leaves the popup dismissed (§5.1)
            if (state->surface == nullptr)
                return false;
            // the surface listener carries the scale facts, installed with the state (AD-27)
            wl_surface_add_listener(state->surface, &SURFACE_LISTENER, state);
            // the fractional scale and the viewport extend the fresh surface, which the reopen needs (AD-27)
            create_scale_objects(state);
            // the shared state is repointed, which the renderer presents through (§10.5)
            if (state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                state->native.state->surface = state->surface;
            }
            // the shell surface carries a fresh xdg role, created before any buffer exists (§5.3)
            state->shell_surface = xdg_wm_base_get_xdg_surface(state->connection->shell(), state->surface);
            // a shell surface the compositor refused leaves the popup dismissed (§5.1)
            if (state->shell_surface == nullptr)
                return false;
            // the shell surface listener carries the configure, installed with the state (§5.1)
            xdg_surface_add_listener(state->shell_surface, &SHELL_SURFACE_LISTENER, state);
            // the popup role is rebuilt for the surface, which the positioner places again (§5.1)
            if (!create_popup_role(state)) {
                xdg_surface_destroy(state->shell_surface);
                state->shell_surface = nullptr;
                return false;
            }
            // the initial commit is bufferless, which the protocol requires before the first configure (§5.3)
            wl_surface_commit(state->surface);
            // the initial configure is answered here, which the roundtrip delivers (§5.3)
            if (wl_display_roundtrip(state->connection->display()) < 0) {
                destroy_popup_role(state);
                return false;
            }
            return true;
        }
    } // namespace

    std::unique_ptr<WaylandPlatformWindow> WaylandPlatformWindow::create(const WindowOptions& options) {
        // an option set the framework refuses is never handed to the compositor (§5.1)
        if (!options.valid())
            return nullptr;
        // the shared connection supplies the surface and the shell, and an unreachable compositor refuses it (§5.3)
        const std::shared_ptr<WaylandConnection> connection = WaylandConnection::connect(std::string_view());
        if (connection == nullptr)
            return nullptr;
        // a compositor without a shell, a compositor or a shm pool offers no wayland window (§5.3, §6.2)
        if (connection->shell() == nullptr || connection->compositor() == nullptr || connection->shm() == nullptr)
            return nullptr;
        // the state is shared, because the proxies' listeners outlive this call (§4.2)
        std::shared_ptr<WaylandWindowState> state = std::make_shared<WaylandWindowState>();
        state->connection = connection;
        state->activation = connection->activation();
        state->requested_size = options.size;
        state->popup_role = options.role == WindowRole::Popup;
        // the desktop's scheme is what the window mirrors, which a host without a portal leaves at the default (§8.4)
        state->portal = PortalClient::shared();
        // the window follows the scheme, which a change reports to its listener on the ui thread (§8.4)
        state->appearance = state->portal->subscribe_appearance([weak = std::weak_ptr<WaylandWindowState>(state)](ColorScheme scheme) {
            // a window that was destroyed before the change has nothing to report (§8.4)
            const std::shared_ptr<WaylandWindowState> alive = weak.lock();
            // an unattached listener receives nothing, which a closed window leaves (§4.2)
            if (alive == nullptr || alive->listener == nullptr)
                return;
            // the framework receives the appearance the desktop now renders with (§8.4)
            alive->listener->on_platform_appearance_changed(SystemAppearance{.scheme = scheme});
        });
        // the xkb context is what the keymaps are built in, which one per window is (§8.3)
        state->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        // the surface is created on the compositor, which no buffer may touch before the first configure (§5.3)
        state->surface = wl_compositor_create_surface(connection->compositor());
        // a surface the compositor refused leaves no window, which the null reports (§5.3)
        if (state->surface == nullptr) {
            // the xkb context is released, which nothing else owns it
            if (state->xkb != nullptr)
                xkb_context_unref(state->xkb);
            return nullptr;
        }
        // the surface listener carries the scale facts, installed with the state (AD-27)
        wl_surface_add_listener(state->surface, &SURFACE_LISTENER, state.get());
        // the fractional scale and the viewport extend the surface, which the preferred scale arrives through (AD-27)
        create_scale_objects(state.get());
        // the shared state is built with the surface, because the first roundtrip already reports the scale (§10.5, AD-27)
        state->native.kind = linux_wsi::Kind::Wayland;
        state->native.state = std::shared_ptr<wayland::WaylandSurfaceState>(new wayland::WaylandSurfaceState(), wayland::destroy_surface_state);
        state->native.state->owner = connection;
        state->native.state->display = connection->display();
        state->native.state->surface = state->surface;
        state->native.state->shm = connection->shm();
        state->native.state->mapped = false;
        // the native window exists from here, which a teardown of every proxy keys on (§5.1)
        state->created = true;
        // native chrome is preferred, and libdecor is the fallback where the compositor has no xdg-decoration (§5.3)
        bool client_side_available = false;
#ifdef ALBOR_WAYLAND_LIBDECOR
        client_side_available = connection->decoration_context() != nullptr;
#endif
        state->decoration_mode = choose_decoration(connection->decorations() != nullptr, client_side_available);
        const bool use_libdecor = !state->popup_role && state->decoration_mode == WaylandDecorationMode::ClientSide;
        // the raw shell path carries a popup and the xdg-decoration fallback (§5.1, §5.3)
        if (!use_libdecor) {
            // the shell surface carries the xdg role, created before any buffer exists (§5.3)
            state->shell_surface = xdg_wm_base_get_xdg_surface(connection->shell(), state->surface);
            // a shell surface the compositor refused leaves the surface destroyed, which the null reports (§5.3)
            if (state->shell_surface == nullptr) {
                // the proxies are taken down, which the failure path owns (§5.2)
                destroy_native(state.get());
                // the xkb context is released, which nothing else owns it
                if (state->xkb != nullptr)
                    xkb_context_unref(state->xkb);
                return nullptr;
            }
            // the shell surface listener carries the configure, installed with the state (§5.3)
            xdg_surface_add_listener(state->shell_surface, &SHELL_SURFACE_LISTENER, state.get());
            // the role is a popup where the window is transient, and a toplevel for every other role (§5.1)
            if (state->popup_role) {
                // a popup needs the parent state the framework resolved, which no transient surface floats without (§5.1)
                if (!options.native_parent.valid()) {
                    destroy_native(state.get());
                    if (state->xkb != nullptr)
                        xkb_context_unref(state->xkb);
                    return nullptr;
                }
                // the parent state is cast back from the opaque handle, which only the adapter reads (§3.5, AD-05)
                WaylandWindowState* parent = static_cast<WaylandWindowState*>(const_cast<void*>(options.native_parent.object()));
                // a parent without a shell surface cannot host a popup, which the refusal reports (§5.1)
                if (parent == nullptr || role_shell_surface(parent) == nullptr) {
                    destroy_native(state.get());
                    if (state->xkb != nullptr)
                        xkb_context_unref(state->xkb);
                    return nullptr;
                }
                state->parent = parent;
                state->popup_anchor = options.anchor;
                // the popup role is created on the shell surface, which a dismissal later rebuilds (§5.1)
                if (!create_popup_role(state.get())) {
                    destroy_native(state.get());
                    if (state->xkb != nullptr)
                        xkb_context_unref(state->xkb);
                    return nullptr;
                }
            }
            else {
                // the toplevel is the role of every ordinary, dialog and tool window (§5.1)
                state->toplevel = xdg_surface_get_toplevel(state->shell_surface);
                // a toplevel the compositor refused leaves the proxies destroyed, which the null reports (§5.3)
                if (state->toplevel == nullptr) {
                    destroy_native(state.get());
                    if (state->xkb != nullptr)
                        xkb_context_unref(state->xkb);
                    return nullptr;
                }
                // the toplevel listener carries the close request, installed with the state (§5.2)
                xdg_toplevel_add_listener(state->toplevel, &TOPLEVEL_LISTENER, state.get());
                // the title is xdg state the initial commit applies (§5.3)
                xdg_toplevel_set_title(state->toplevel, options.title.c_str());
                // the app id names the application to the compositor, which the window list reads (§5.3)
                xdg_toplevel_set_app_id(state->toplevel, "albor");
                // a dialog or tool window names its parent, which the compositor uses for stacking and focus (§5.1)
                if (options.native_parent.valid()) {
                    // the parent state is cast back from the opaque handle, which only the adapter reads (§3.5, AD-05)
                    WaylandWindowState* parent = static_cast<WaylandWindowState*>(const_cast<void*>(options.native_parent.object()));
                    // a parent with a toplevel is what the parent relation names (§5.1)
                    if (xdg_toplevel* parent_toplevel = parent != nullptr ? role_toplevel(parent) : nullptr)
                        xdg_toplevel_set_parent(state->toplevel, parent_toplevel);
                }
                // the decorations are negotiated where the manager exists, and a popup rides none (§5.1, §5.3)
                if (connection->decorations() != nullptr) {
                    // the decoration object is created for the toplevel, which carries the mode
                    state->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(connection->decorations(), state->toplevel);
                    // a decoration the compositor refused leaves its own default, which the guard keeps (§5.3)
                    if (state->decoration != nullptr) {
                        // the mode event reports what the compositor will draw, which the strategy records (§5.3)
                        zxdg_toplevel_decoration_v1_add_listener(state->decoration, &DECORATION_LISTENER, state.get());
                        // the mode is requested server side, which is the chrome the compositor draws (§5.3)
                        zxdg_toplevel_decoration_v1_set_mode(state->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
                    }
                }
            }
        }
#ifdef ALBOR_WAYLAND_LIBDECOR
        else {
            // the frame decorates the surface of an ordinary, dialog or tool window (§5.3)
            if (!create_frame(state.get(), options)) {
                destroy_native(state.get());
                if (state->xkb != nullptr)
                    xkb_context_unref(state->xkb);
                return nullptr;
            }
        }
#endif
        // the initial commit is bufferless, which the protocol requires before the first configure (§5.3)
        if (!use_libdecor) {
            wl_surface_commit(state->surface);
            // the initial configure is answered here, which the roundtrip delivers (§5.3)
            if (wl_display_roundtrip(connection->display()) < 0) {
                destroy_native(state.get());
                if (state->xkb != nullptr)
                    xkb_context_unref(state->xkb);
                return nullptr;
            }
#ifdef ALBOR_WAYLAND_LIBDECOR
            // a compositor that advertises xdg-decoration but answers client-side asks the client to draw the frame (§5.3)
            if (state->decoration_mode == WaylandDecorationMode::ClientSide && client_side_available) {
                // the raw role is spent, and a fresh surface gives libdecor the role it owns (§5.3)
                destroy_toplevel_role(state.get());
                if (state->surface != nullptr) {
                    // the per surface scale objects follow the surface they extend (AD-27)
                    destroy_scale_objects(state.get());
                    wl_surface_destroy(state->surface);
                    state->surface = nullptr;
                }
                state->surface = wl_compositor_create_surface(connection->compositor());
                // a surface the compositor refused leaves no window, which the null reports (§5.3)
                if (state->surface == nullptr) {
                    destroy_native(state.get());
                    if (state->xkb != nullptr)
                        xkb_context_unref(state->xkb);
                    return nullptr;
                }
                // the surface listener carries the scale facts, installed with the state (AD-27)
                wl_surface_add_listener(state->surface, &SURFACE_LISTENER, state.get());
                // the fractional scale and the viewport extend the replacement surface, which libdecor draws (AD-27)
                create_scale_objects(state.get());
                // the native handle follows the replacement surface, which a renderer attaches through (§3.5, AD-05)
                if (state->native.state != nullptr) {
                    const std::lock_guard<std::mutex> surface_lock(state->native.state->mutex);
                    state->native.state->surface = state->surface;
                }
                // the frame draws the chrome the compositor declined to draw (§5.3)
                if (!create_frame(state.get(), options)) {
                    destroy_native(state.get());
                    if (state->xkb != nullptr)
                        xkb_context_unref(state->xkb);
                    return nullptr;
                }
            }
#endif
        }
        // a compositor that proposed no size leaves the requested one, which the metrics carry now (§5.3)
        if (!state->metrics.valid())
            state->metrics = WindowMetrics::from_logical(state->requested_size, state->scale_factor);
        // the pointer is taken where the seat announced one, which the capabilities name (§8.3)
        if (connection->seat() != nullptr && (connection->seat_capabilities() & WL_SEAT_CAPABILITY_POINTER) != 0) {
            // the pointer version is the seat's, which caps the release request and the events (§8.3)
            state->seat_version = connection->seat_version();
            state->pointer = wl_seat_get_pointer(connection->seat());
            // a pointer the seat refused carries none, which the guard keeps (§8.3)
            if (state->pointer != nullptr)
                wl_pointer_add_listener(state->pointer, &POINTER_LISTENER, state.get());
        }
        // the keyboard is taken where the seat announced one (§8.3)
        if (connection->seat() != nullptr && (connection->seat_capabilities() & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
            state->seat_version = connection->seat_version();
            state->keyboard = wl_seat_get_keyboard(connection->seat());
            if (state->keyboard != nullptr)
                wl_keyboard_add_listener(state->keyboard, &KEYBOARD_LISTENER, state.get());
        }
        // the text input is taken where the compositor has an input method manager (§8.3)
        state->text_input = create_text_input(state.get());
        if (state->text_input != nullptr)
            zwp_text_input_v3_add_listener(state->text_input, &TEXT_INPUT_LISTENER, state.get());
        // the window is exported to the portal, so a panel can name it as its parent (§5.3)
        export_toplevel(state.get());
        // the window is returned as the hal, so no wayland type escapes the module (§3.5, AD-05)
        return std::unique_ptr<WaylandPlatformWindow>(new WaylandPlatformWindow(state));
    }

    WaylandPlatformWindow::~WaylandPlatformWindow() {
        // the listener is detached first, because a teardown event would otherwise reach a half dead object (§4.2)
        m_state->listener = nullptr;
        // the native proxies are destroyed, which is what takes the surface down (§5.2)
        destroy_native(m_state.get());
        // the xkb objects are released, which the keymap and the state carry (§8.3)
        if (m_state->keymap_state != nullptr) {
            xkb_state_unref(m_state->keymap_state);
            m_state->keymap_state = nullptr;
        }
        if (m_state->keymap != nullptr) {
            xkb_keymap_unref(m_state->keymap);
            m_state->keymap = nullptr;
        }
        if (m_state->xkb != nullptr) {
            xkb_context_unref(m_state->xkb);
            m_state->xkb = nullptr;
        }
    }

    void WaylandPlatformWindow::set_listener(PlatformWindowListener* listener) {
        // the listeners reach the state, which every event already shares (§5.3)
        m_state->listener = listener;
    }

    void WaylandPlatformWindow::show() {
        // a dismissed popup gets a fresh role, which the spent one cannot be shown through again (§5.1)
        if (m_state->popup_role && m_state->shell_surface == nullptr) {
            if (!rebuild_popup_role(m_state.get()))
                return;
        }
        // a window with no surface cannot be shown (§5.3)
        if (m_state->surface == nullptr)
            return;
        // an already visible window is not shown twice, which a repeated show has no effect on (§5.3)
        if (m_state->visible)
            return;
#ifdef ALBOR_WAYLAND_LIBDECOR
        // a decorated frame is shown again after a hide, which the visibility carries (§5.3)
        if (m_state->frame != nullptr) {
            // only a mapped frame carries a visibility to flip, which the cairo plugin requires (§5.3)
            if (m_state->frame_mapped)
                libdecor_frame_set_visibility(m_state->frame, true);
            // a frame that was never mapped is mapped once, which starts its configure sequence (§5.3)
            if (!m_state->frame_mapped) {
                libdecor_frame_map(m_state->frame);
                m_state->frame_mapped = true;
            }
            // the frame's bufferless commit is answered here, which the roundtrip delivers before a buffer (§5.3)
            wl_display_roundtrip(m_state->connection->display());
            // libdecor owns the frame's configure sequence, so the map is ready once it answered or kept its cycle (§5.3)
            if (m_state->native.state != nullptr) {
                const std::lock_guard<std::mutex> surface_lock(m_state->native.state->mutex);
                m_state->native.state->configured = true;
            }
        }
#endif
        // a listener may have closed the window across the frame dispatch, which leaves nothing to map (§4.2)
        if (m_state->native.state == nullptr || m_state->surface == nullptr)
            return;
        // the map cycle is handed to the compositor, which no buffer may precede (§5.3)
        if (!begin_map(m_state.get()))
            return;
        // a refused buffer leaves the window off screen, which the visible flag reports (§5.3)
        if (!attach_and_map(m_state.get()))
            return;
        m_state->visible = true;
        // the events the commit produced are dispatched, so the metrics are current when show returns (§5.3)
        wl_display_roundtrip(m_state->connection->display());
        // a listener may have closed the window across the metrics dispatch, which leaves nothing to reopen (§4.2)
        if (m_state->native.state == nullptr || m_state->surface == nullptr)
            return;
        // a dismissal that arrived during the roundtrip spent the popup role, which the reopen rebuilds once (§5.1)
        if (m_state->popup_role && m_state->shell_surface == nullptr) {
            // the role is rebuilt and presented once, which keeps show from claiming a visible popup it cannot show (§5.1)
            if (!rebuild_popup_role(m_state.get()) || !begin_map(m_state.get()) || !attach_and_map(m_state.get()))
                return;
            m_state->visible = true;
            // the rebuilt configure is dispatched, which the metrics follow (§5.1)
            wl_display_roundtrip(m_state->connection->display());
        }
        // a compositor that stayed silent has the surface at the size it was asked for, which the metrics report (§5.3)
        if (!m_state->metrics.valid())
            apply_configure(m_state.get(), 0, 0);
    }

    void WaylandPlatformWindow::activate() {
        // a compositor without the activation protocol cannot be asked to raise the window (§5.1)
        if (m_state->activation == nullptr || m_state->surface == nullptr)
            return;
        // an earlier request the compositor has not answered is released, which a new activation supersedes (§5.1)
        release_pending_activation(m_state.get());
        // the token is requested first, because the activate request names the token the compositor grants (§5.1)
        xdg_activation_token_v1* token = xdg_activation_v1_get_activation_token(m_state->activation);
        // a token the compositor refused leaves the window where it is (§5.1)
        if (token == nullptr)
            return;
        // the request carries the token until the done event redeems it (§5.1)
        ActivationRequest* request = new ActivationRequest{.state = m_state.get(), .activation = m_state->activation, .surface = m_state->surface, .token = token};
        m_state->pending_activation = request;
        // the done event answers the token, which the listener redeems (§5.1)
        xdg_activation_token_v1_add_listener(token, &ACTIVATION_TOKEN_LISTENER, request);
        // the surface is named as the reason for the activation, which the protocol carries (§5.1)
        xdg_activation_token_v1_set_surface(token, m_state->surface);
        // a known input serial gives the compositor the user gesture it may require (§5.1)
        if (m_state->last_input_serial != 0 && m_state->connection->seat() != nullptr)
            xdg_activation_token_v1_set_serial(token, m_state->last_input_serial, m_state->connection->seat());
        // the token is committed, which asks the compositor for the grant (§5.1)
        xdg_activation_token_v1_commit(token);
    }

    void WaylandPlatformWindow::hide() {
        // an already hidden window is not hidden twice, which a repeated hide has no effect on (§5.3)
        if (!m_state->visible)
            return;
        // the surface is unmapped and the render thread stopped, which the hide and a dismissal share (§5.3)
        hide_native(m_state.get());
        // a popup leaves the screen by releasing its role, because the compositor spends an unmapped popup (§5.1)
        if (m_state->popup_role)
            destroy_popup_role(m_state.get());
    }

    void WaylandPlatformWindow::set_title(std::string_view title) {
#ifdef ALBOR_WAYLAND_LIBDECOR
        // a decorated frame carries its own title, which the chrome draws (§5.3)
        if (m_state->frame != nullptr) {
            // the title is handed over as a nul terminated string, which libdecor takes (§5.3)
            const std::string text(title);
            libdecor_frame_set_title(m_state->frame, text.c_str());
            return;
        }
#endif
        // a window with no toplevel has no title bar to write into (§5.3)
        if (m_state->toplevel == nullptr)
            return;
        // the title is handed over as a nul terminated string, which the protocol takes (§5.3)
        const std::string text(title);
        xdg_toplevel_set_title(m_state->toplevel, text.c_str());
        // the title applies on the next commit, which a mapped window gets from its renderer (§5.3)
        if (m_state->visible && m_state->surface != nullptr)
            wl_surface_commit(m_state->surface);
    }

    void WaylandPlatformWindow::set_size(Size size) {
        // a window with no surface cannot be resized (§5.3)
        if (m_state->surface == nullptr)
            return;
        // a size with no area cannot be presented, so the previous size is kept (§6.2)
        if (size.width <= 0.0f || size.height <= 0.0f)
            return;
        // the request is recorded, which the map and the next buffer build use (§5.1)
        m_state->requested_size = size;
        // the metrics before the request are kept, which tells a compositor configure from a silent one (§5.3)
        const WindowMetrics previous_metrics = m_state->metrics;
        // an unmapped window carries its size from the buffer it commits at show, so nothing native happens yet (§5.3)
        if (!m_state->visible)
            return;
#ifdef ALBOR_WAYLAND_LIBDECOR
        // a decorated frame commits the content size it was asked for, which the next configure applies (§5.3)
        if (m_state->frame != nullptr) {
            // the size is clamped to the integer content extent libdecor carries (§5.3)
            const int width = std::max(1, static_cast<int>(size.width));
            const int height = std::max(1, static_cast<int>(size.height));
            // the state is committed first, which is what lets the frame finish the resize (§5.3)
            libdecor_state* decoration_state = libdecor_state_new(width, height);
            libdecor_frame_commit(m_state->frame, decoration_state, nullptr);
            libdecor_state_free(decoration_state);
        }
#endif
        // a renderer owned surface resizes through its own next buffer, which owns the commit and its sync (§6.3)
        if (renderer_presenting(m_state.get())) {
            // the requested size is the metrics the renderer builds its next buffer for, which it presents (§5.3)
            apply_configure(m_state.get(), 0, 0);
            return;
        }
        // a mapped window asks for the new size through a buffer, the only request the protocol offers (§5.3)
        bool attached = false;
        {
            // the surface is locked, so the resize commits atomically against a render thread present (§10.5)
            const std::lock_guard<std::mutex> surface_lock(m_state->native.state->mutex);
            attached = attach_initial_buffer(m_state.get());
            // a built buffer is committed, which the compositor reads the size from (§5.3)
            if (attached)
                wl_surface_commit(m_state->surface);
        }
        // a buffer that could not be built leaves the metrics at the previous size (§6.2)
        if (!attached)
            return;
        // the configure the commit produced is dispatched, which reports the new metrics (§5.3)
        wl_display_roundtrip(m_state->connection->display());
        // a compositor that stayed silent has the surface at the requested size, which the metrics must report (§5.3)
        if (m_state->metrics == previous_metrics)
            apply_configure(m_state.get(), static_cast<int>(size.width), static_cast<int>(size.height));
    }

    WindowMetrics WaylandPlatformWindow::metrics() const {
        // the metrics are the ones the configure last reported, which the platform is the authority on (§9.1)
        return m_state->metrics;
    }

    NativeWindowHandle WaylandPlatformWindow::native_handle() const {
        // a surface that was destroyed hands out the invalid handle, which a backend refuses (§3.5)
        if (m_state->surface == nullptr)
            return NativeWindowHandle{};
        // the bundle crosses into the framework as an opaque pointer, which no wayland type may name (§3.5, AD-05)
        return NativeWindowHandle::from_object(&m_state->native);
    }

    NativeWindowHandle WaylandPlatformWindow::native_window_handle() const {
        // the parent state crosses into the framework as an opaque pointer, which a popup casts back (§5.1)
        return NativeWindowHandle::from_object(m_state.get());
    }

    SystemAppearance WaylandPlatformWindow::system_appearance() const {
        // the appearance is a system value, so it is what the portal last reported rather than a cached paint (§5.3, §8.4)
        SystemAppearance appearance;
        // a host without a portal keeps the portable dark scheme, which the default names (§5.3)
        if (m_state->portal != nullptr)
            appearance.scheme = m_state->portal->color_scheme();
        return appearance;
    }

    void WaylandPlatformWindow::request_frame() {
        // the pacing source is owned by the event loop, so a window only records that it wants a frame (§4.3)
        m_state->frame_requested = true;
    }

    void WaylandPlatformWindow::set_text_anchor(Rect anchor) {
        // a window with no surface has no input method to anchor (§8.3)
        if (m_state->surface == nullptr)
            return;
        // the anchor is stored, which is what a later session reads under (§8.3)
        m_state->anchor = anchor;
        m_state->anchor_valid = true;
        // a text input the compositor has none of anchors nothing, which is the portable default (§8.3)
        if (m_state->text_input == nullptr)
            return;
        // the rectangle is applied in surface local units, which the logical anchor already is (§8.3)
        zwp_text_input_v3_set_cursor_rectangle(m_state->text_input, static_cast<std::int32_t>(anchor.x), static_cast<std::int32_t>(anchor.y), static_cast<std::int32_t>(anchor.width), static_cast<std::int32_t>(anchor.height));
        // the state change is committed, which the compositor applies atomically (§8.3)
        zwp_text_input_v3_commit(m_state->text_input);
    }

    void WaylandPlatformWindow::set_text_selection(TextInputRange selection) {
        // a window with no surface has no input method to address a document through (§8.3)
        if (m_state->surface == nullptr)
            return;
        // the selection is stored for a later surrounding-text request, which this adapter does not send yet (§8.3)
        m_state->selection = selection;
    }

    bool WaylandPlatformWindow::frame_requested() const {
        // the flag is what the next pacing tick consults to decide whether to draw (§4.3)
        return m_state->frame_requested;
    }

    bool WaylandPlatformWindow::visible() const {
        // the flag is the framework's record of what it asked the compositor to do (§5.1)
        return m_state->visible;
    }

    void WaylandPlatformWindow::close() {
        // a window that was never created has nothing to close (§5.2)
        if (m_state->surface == nullptr)
            return;
        // the listener is detached first, because closing raises events the framework is already past (§4.2)
        m_state->listener = nullptr;
        // the native window is taken down without asking the framework, because the framework initiated this (§5.2)
        destroy_native(m_state.get());
        // a closed window has nothing left to draw (§4.3)
        m_state->frame_requested = false;
        m_state->visible = false;
    }

    WaylandDecorationMode WaylandPlatformWindow::decoration_mode() const {
        // the strategy and the compositor's mode answer decide the chrome, which a test reads (§5.3)
        return m_state->decoration_mode;
    }

    bool WaylandPlatformWindow::fractional_scale_active() const {
        // both objects are needed, which the fractional protocols create together (AD-27)
        return uses_fractional_scale(m_state.get());
    }

    bool WaylandPlatformWindow::uses_client_side_frame() const {
#ifdef ALBOR_WAYLAND_LIBDECOR
        // the frame is the client-side chrome, which only the fallback creates (§5.3)
        return m_state->frame != nullptr;
#else
        // a build without the private frame draws no client-side chrome (§5.3)
        return false;
#endif
    }

    WaylandPlatformWindow::WaylandPlatformWindow(std::shared_ptr<WaylandWindowState> state) :
        m_state(std::move(state)) {}

    std::string portal_parent_handle(NativeWindowHandle window) {
        // a handle that names no window gives the portal no parent, which the empty string reports (§5.3)
        if (!window.valid())
            return {};
        // the state is cast back from the opaque handle, which only the adapter reads (§3.5, AD-05)
        auto* state = static_cast<WaylandWindowState*>(const_cast<void*>(window.object()));
        // a state without an export names no window the portal can import (§5.3)
        if (state == nullptr || state->exported == nullptr)
            return {};
        // the handle arrives asynchronously, so a panel opened at once waits one roundtrip for it (§5.3)
        if (state->exported_handle.empty() && state->connection != nullptr)
            wl_display_roundtrip(state->connection->display());
        // an export the compositor never named leaves no parent for the panel (§5.3)
        if (state->exported_handle.empty())
            return {};
        // the portal names a wayland parent by its scheme and the exported handle (§5.3)
        return "wayland:" + state->exported_handle;
    }
} // namespace albor
