#pragma once

#include <memory>
#include <string_view>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "window/platform_window.h"
#include "window/window_metrics.h"
#include "window/window_options.h"

namespace albor
{
    // the opaque native state of one macOS window, defined only in the objective c++ translation unit (§3.5)
    struct MacosWindowState;

    // the appkit window that attaches a CAMetalLayer and reports resize, close and scale changes (§5.3, AD-16)
    class MacosPlatformWindow final : public PlatformWindow
    {
    public:
        // build the native window for the options, reporting a refusal by returning null
        [[nodiscard]] static std::unique_ptr<MacosPlatformWindow> create(const WindowOptions& options);

        // take the native window and its display link down
        ~MacosPlatformWindow() override;

        MacosPlatformWindow(const MacosPlatformWindow&) = delete;
        MacosPlatformWindow& operator=(const MacosPlatformWindow&) = delete;
        MacosPlatformWindow(MacosPlatformWindow&&) = delete;
        MacosPlatformWindow& operator=(MacosPlatformWindow&&) = delete;

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

        // override: hand out the metal layer a renderer backend attaches to (§3.5)
        [[nodiscard]] NativeWindowHandle native_handle() const override;

        // override: ask the display link for another frame (§4.3, AD-16)
        void request_frame() override;

        // override: hand the text anchor over, which the content view holds for the input system (§8.3)
        void set_text_anchor(Rect anchor) override;

        // override: report whether a frame was requested and not yet drawn (AD-18b)
        [[nodiscard]] bool frame_requested() const override;

        // override: report whether the native window is currently on screen
        [[nodiscard]] bool visible() const override;

        // override: close the native window without raising a close request
        void close() override;

    private:
        // build the native window over its opaque state
        explicit MacosPlatformWindow(std::shared_ptr<MacosWindowState> state);

        // the opaque appkit state, which the notification blocks share so that they stay valid (§4.2)
        std::shared_ptr<MacosWindowState> m_state;
    };
} // namespace albor
