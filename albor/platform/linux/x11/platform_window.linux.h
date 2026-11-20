#pragma once

#include <memory>
#include <string_view>

#include "albor/core/geometry.h"
#include "albor/core/native_window_handle.h"
#include "albor/window/platform_window.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

namespace albor::x11
{
    // the opaque native state of one xcb window, defined only in the linux translation unit (§3.5)
    struct X11WindowState;

    // the xcb backed native window, which reports resize, close and focus changes
    class X11PlatformWindow final : public PlatformWindow
    {
    public:
        // build the native window for the options, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<X11PlatformWindow> create(const WindowOptions& options);

        // take the window and its connection reference down
        ~X11PlatformWindow() override;

        X11PlatformWindow(const X11PlatformWindow&) = delete;
        X11PlatformWindow& operator=(const X11PlatformWindow&) = delete;
        X11PlatformWindow(X11PlatformWindow&&) = delete;
        X11PlatformWindow& operator=(X11PlatformWindow&&) = delete;

        // override: attach the listener the native window raises events into
        void set_listener(PlatformWindowListener* listener) override;

        // override: map the window, which puts it on screen
        void show() override;

        // override: raise the window through the window manager
        void activate() override;

        // override: unmap the window, which takes it off screen
        void hide() override;

        // override: replace the window title
        void set_title(std::string_view title) override;

        // override: resize the window in logical pixels
        void set_size(Size size) override;

        // override: read the authoritative native window metrics
        [[nodiscard]] WindowMetrics metrics() const override;

        // override: hand out the xcb surface a renderer backend attaches to (§3.5, AD-05)
        [[nodiscard]] NativeWindowHandle native_handle() const override;

        // override: hand out the window itself, which an owned popup names as its parent (§5.1)
        [[nodiscard]] NativeWindowHandle native_window_handle() const override;

        // override: ask the window for another frame (§4.3, AD-16)
        void request_frame() override;

        // override: report whether a frame was requested and not yet drawn (AD-18b)
        [[nodiscard]] bool frame_requested() const override;

        // override: report whether the native window is currently on screen
        [[nodiscard]] bool visible() const override;

        // override: close the native window without raising a close request
        void close() override;

    private:
        // build the native window over its opaque state
        explicit X11PlatformWindow(std::shared_ptr<X11WindowState> state);

        // the opaque xcb state, which the event registry keeps alive
        std::shared_ptr<X11WindowState> m_state;
    };

} // namespace albor::x11
