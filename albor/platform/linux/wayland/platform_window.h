#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "albor/core/geometry.h"
#include "albor/core/native_window_handle.h"
#include "albor/window/platform_window.h"
#include "albor/window/system_appearance.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

namespace albor
{
    // the decoration a wayland window ends up with, which the strategy selects per compositor (§5.1, §5.3)
    enum class WaylandDecorationMode
    {
        // the compositor draws no chrome and the client has no fallback
        None,

        // the compositor draws the native chrome through xdg-decoration
        ServerSide,

        // libdecor draws the client-side frame, which is what GNOME needs
        ClientSide,
    };

    // choose the decoration for one toplevel, preferring the compositor's native chrome over libdecor (§5.3, AD-16)
    [[nodiscard]] WaylandDecorationMode choose_decoration(bool server_side_available, bool client_side_available);

    // hand out the wheel batch slot the vertical axis writes and the frame reads (§8.3)
    [[nodiscard]] std::uint32_t wayland_vertical_axis_index();

    // report whether a pointer axis event carries the vertical scroll a list consumes (§8.3)
    [[nodiscard]] bool wayland_axis_is_vertical(std::uint32_t axis);

    // report the scale a compositor prefers from the one hundred twentieths the protocol carries (AD-27)
    [[nodiscard]] float fractional_scale_factor(std::uint32_t preferred_scale);

    // report whether a surface can honour a fractional scale, which needs both protocols (AD-27)
    [[nodiscard]] bool fractional_scale_available(bool manager_available, bool viewporter_available);

    // hand out the portal window identifier a window exports, which is empty where no exporter or role exists (§5.3)
    [[nodiscard]] std::string portal_parent_handle(NativeWindowHandle window);

    // report the surface local destination a fractional buffer maps onto, rounded halfway away from zero (AD-27)
    [[nodiscard]] Size viewport_destination(Size logical_size);

    // the opaque native state of one wayland window, defined only in the linux translation unit (§3.5)
    struct WaylandWindowState; // the xdg shell window that reports resize, close and scale changes (§5.3, AD-16, AD-22)
    class WaylandPlatformWindow final : public PlatformWindow
    {
    public:
        // build the native window for the options, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<WaylandPlatformWindow> create(const WindowOptions& options);

        // take the surface and its shell objects down
        ~WaylandPlatformWindow() override;

        WaylandPlatformWindow(const WaylandPlatformWindow&) = delete;
        WaylandPlatformWindow& operator=(const WaylandPlatformWindow&) = delete;
        WaylandPlatformWindow(WaylandPlatformWindow&&) = delete;
        WaylandPlatformWindow& operator=(WaylandPlatformWindow&&) = delete;

        // override: attach the listener the native window raises events into
        void set_listener(PlatformWindowListener* listener) override;

        // override: commit a buffer, which is what puts the window on screen (§5.3)
        void show() override;

        // override: bring the window forward, which xdg activation requests where the compositor supports it (§5.1)
        void activate() override;

        // override: take the window off screen, which an unmap is (§5.3)
        void hide() override;

        // override: replace the window title
        void set_title(std::string_view title) override;

        // override: resize the window in logical pixels
        void set_size(Size size) override;

        // override: read the authoritative native window metrics
        [[nodiscard]] WindowMetrics metrics() const override;

        // override: hand out the wayland surface a renderer backend attaches to (§3.5, AD-05)
        [[nodiscard]] NativeWindowHandle native_handle() const override;

        // override: hand out the parent window state a popup attaches to (§5.1)
        [[nodiscard]] NativeWindowHandle native_window_handle() const override;

        // report the decoration the window ended up with, which a test asserts on (§5.3)
        [[nodiscard]] WaylandDecorationMode decoration_mode() const;

        // report whether the window honours a fractional preferred scale, which a test asserts on (AD-27)
        [[nodiscard]] bool fractional_scale_active() const;

        // report whether the window carries the libdecor client-side frame, which the decoration test asserts (§5.3)
        [[nodiscard]] bool uses_client_side_frame() const;

        // override: read the colour scheme the portal reports, which the portable dark scheme stands in for (§5.3, §8.4)
        [[nodiscard]] SystemAppearance system_appearance() const override;

        // override: ask the surface for another frame (§4.3, AD-16)
        void request_frame() override;

        // override: hand the text anchor over, which the input method places its candidate by (§8.3)
        void set_text_anchor(Rect anchor) override;

        // override: hand the document selection over, which the input method reads (§8.3)
        void set_text_selection(TextInputRange selection) override;

        // override: report whether a frame was requested and not yet drawn (AD-18b)
        [[nodiscard]] bool frame_requested() const override;

        // override: report whether the native window is currently on screen
        [[nodiscard]] bool visible() const override;

        // override: close the native window without raising a close request
        void close() override;

    private:
        // build the native window over its opaque state
        explicit WaylandPlatformWindow(std::shared_ptr<WaylandWindowState> state);

        // the opaque xdg state, which the surface listeners keep alive across a veto (§4.2)
        std::shared_ptr<WaylandWindowState> m_state;
    };
} // namespace albor
