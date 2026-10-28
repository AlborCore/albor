#pragma once

#include <memory>
#include <string_view>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "window/platform_window.h"
#include "window/system_appearance.h"
#include "window/window_metrics.h"
#include "window/window_options.h"

namespace albor
{
    // the opaque native state of one windows window, defined only in the win32 translation unit (§3.5)
    struct Win32WindowState;

    // the win32 window that hosts the render surface and reports resize, close and scale changes (§5.3, AD-16)
    class Win32PlatformWindow final : public PlatformWindow
    {
    public:
        // build the native window for the options, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<Win32PlatformWindow> create(const WindowOptions& options);

        // take the native window down, which the close path also reaches
        ~Win32PlatformWindow() override;

        Win32PlatformWindow(const Win32PlatformWindow&) = delete;
        Win32PlatformWindow& operator=(const Win32PlatformWindow&) = delete;
        Win32PlatformWindow(Win32PlatformWindow&&) = delete;
        Win32PlatformWindow& operator=(Win32PlatformWindow&&) = delete;

        // override: attach the listener the native window raises events into
        void set_listener(PlatformWindowListener* listener) override;

        // override: put the native window on screen
        void show() override;

        // override: take the native window off screen
        void hide() override;

        // override: replace the native window title
        void set_title(std::string_view title) override;

        // override: resize the native window in logical pixels
        void set_size(Size size) override;

        // override: read the authoritative native window metrics
        [[nodiscard]] WindowMetrics metrics() const override;

        // override: hand out the window handle a renderer backend attaches to (§3.5)
        [[nodiscard]] NativeWindowHandle native_handle() const override;

        // override: hand out the hwnd an owned popup uses as its parent (§5.1)
        [[nodiscard]] NativeWindowHandle native_window_handle() const override;

        // override: read the appearance the system renders with, which a theme follows (§5.3, §8.4)
        [[nodiscard]] SystemAppearance system_appearance() const override;

        // override: ask the pacing source for another frame (§4.3, AD-16)
        void request_frame() override;

        // override: hand the input method's anchor over, which is the caret rect a candidate window is placed by (§8.3)
        void set_text_anchor(Rect anchor) override;

        // override: hand the document selection over, which is what an input method replaces by range (§8.3)
        void set_text_selection(TextInputRange selection) override;

        // override: report whether a frame was requested and not yet drawn (AD-18b)
        [[nodiscard]] bool frame_requested() const override;

        // override: report whether the native window is currently on screen
        [[nodiscard]] bool visible() const override;

        // override: close the native window without raising a close request
        void close() override;

    private:
        // build the native window over its opaque state
        explicit Win32PlatformWindow(std::shared_ptr<Win32WindowState> state);

        // the opaque win32 state, which the window procedure reaches through the window's own user data (§4.2)
        std::shared_ptr<Win32WindowState> m_state;
    };
} // namespace albor
