#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "core/native_window_handle.h"
#include "ui/ui_event.h"
#include "window/platform_window.h"
#include "window/window_handle.h"
#include "window/window_metrics.h"
#include "window/window_options.h"

namespace albor
{

    // the framework half of a window, which owns the metrics, the options and the platform window (§5.1)
    class Window final : public PlatformWindowListener
    {
    public:
        // adopt a native window, which is null when the platform refused the options
        Window(WindowHandle handle, WindowOptions options, std::unique_ptr<PlatformWindow> platform);

        // take the native window down before the listener it points at disappears
        ~Window() override;

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        // report whether the window owns a native window
        [[nodiscard]] bool alive() const;

        // hand out the handle that names this window
        [[nodiscard]] WindowHandle handle() const;

        // hand out the creation time options
        [[nodiscard]] const WindowOptions& options() const;

        // read the authoritative metrics the platform reports
        [[nodiscard]] WindowMetrics metrics() const;

        // put the window on screen
        void show();

        // take the window off screen
        void hide();

        // replace the window title
        void set_title(std::string_view title);

        // resize the window in logical pixels
        void set_size(Size size);

        // ask the platform to present another frame
        void request_frame();

        // hand out the native object a renderer backend attaches to (§3.5)
        [[nodiscard]] NativeWindowHandle native_handle() const;

        // install the callback a native close request consults before it may proceed (§5.2)
        void set_close_handler(std::function<bool()> handler);

        // install the callback a native close reports to, which is the Closed notification of §5.2
        void set_closed_handler(std::function<void()> handler);

        // install the callback a resize notification reaches
        void set_resize_handler(std::function<void(const WindowMetrics&)> handler);

        // install the callback a platform frame tick reaches
        void set_frame_handler(std::function<void()> handler);

        // install the callback a normalized input event reaches, which routes the native input of §8.3
        void set_input_handler(std::function<void(const Event&)> handler);

        // close the window outright, which is what a granted close request and application shutdown do (§5.2)
        void close();

        // report whether a close is still pending after the application vetoed it
        [[nodiscard]] bool close_pending() const;

        // report whether the window is currently on screen
        [[nodiscard]] bool visible() const;

        // override: report a native resize into the framework (§5.3)
        void on_platform_resize(const WindowMetrics& metrics) override;

        // override: consult the application before a native close proceeds (§5.2)
        bool on_platform_close_request() override;

        // override: report a native window close, which is the Closed notification of §5.2
        void on_platform_closed() override;

        // override: deliver a platform pacing tick to the frame handler (§4.3)
        void on_platform_frame() override;

        // override: deliver a normalized input event to the input handler (§5.3, §8.3)
        void on_platform_event(const Event& event) override;

    private:
        // the handle that names this window
        WindowHandle m_handle;

        // the creation time options, kept because a role is immutable (§5.1)
        WindowOptions m_options;

        // the native window, which is null when the platform refused the options
        std::unique_ptr<PlatformWindow> m_platform;

        // the callback a close request consults, which vetoes when it returns false
        std::function<bool()> m_close_handler;

        // the callback a native window close reports to, which is null when nobody listens
        std::function<void()> m_closed_handler;

        // the callback a resize notification reaches
        std::function<void(const WindowMetrics&)> m_resize_handler;

        // the callback a platform frame tick reaches
        std::function<void()> m_frame_handler;

        // the callback a normalized input event reaches
        std::function<void(const Event&)> m_input_handler;

        // the last metrics the platform reported, which a null native window never updates
        WindowMetrics m_metrics;

        // whether a close was requested and vetoed, which the next frame may retry
        bool m_close_pending = false;

        // whether the framework itself closed the window, which makes the native close notification a repeat (§5.2)
        bool m_close_initiated = false;

        // whether the window is on screen
        bool m_visible = false;
    };

} // namespace albor
