#pragma once

#include <memory>
#include <string>

#include "window/platform_window.h"

namespace albor::headless
{
    // the window that has no native window behind it, which is what a test and a screenshot run against (§5.3, AD-16)
    class HeadlessPlatformWindow final : public PlatformWindow
    {
    public:
        // build a headless window from the options, returning null when they are refused (§5.1)
        [[nodiscard]] static std::unique_ptr<HeadlessPlatformWindow> create(const WindowOptions& options);

        ~HeadlessPlatformWindow() override;

        HeadlessPlatformWindow(const HeadlessPlatformWindow&) = delete;
        HeadlessPlatformWindow& operator=(const HeadlessPlatformWindow&) = delete;
        HeadlessPlatformWindow(HeadlessPlatformWindow&&) = delete;
        HeadlessPlatformWindow& operator=(HeadlessPlatformWindow&&) = delete;

        // override: attach the listener this window raises events into (§5.3)
        void set_listener(PlatformWindowListener* listener) override;

        // override: put the window on screen, which here only records that it is (§5.3)
        void show() override;

        // override: take the window off screen, which here only records that it is not (§5.3)
        void hide() override;

        // override: replace the title, which a headless window keeps because reports read it (§5.3)
        void set_title(std::string_view title) override;

        // override: replace the logical size, which raises a resize like a real window does (§5.3, D9.1)
        void set_size(Size size) override;

        // override: read the synthetic metrics this window presents at (§5.3)
        [[nodiscard]] WindowMetrics metrics() const override;

        // override: there is no native object, so the handle is empty rather than fabricated (§3.5, AD-05)
        [[nodiscard]] NativeWindowHandle native_handle() const override;

        // override: record that a frame was asked for, which a test then pumps (§4.3, AD-18b)
        void request_frame() override;

        // override: report whether a frame is asked for and not yet drawn (§4.3, AD-18b)
        [[nodiscard]] bool frame_requested() const override;

        // override: report whether the window is on screen (§5.3)
        [[nodiscard]] bool visible() const override;

        // override: close the window, which raises a close request the application may veto (§5.2)
        void close() override;

        // override: read the synthetic appearance this window reports, which is what a test drives (§5.3, §8.4)
        [[nodiscard]] SystemAppearance system_appearance() const override;

        // raise a synthetic resize, which is how a test drives the ui thread onto new metrics (§5.3, D9.1)
        void raise_resize(const WindowMetrics& metrics);

        // raise a synthetic appearance change, which is how a test drives a theme flip without a system (§8.4)
        void raise_appearance_change(const SystemAppearance& appearance);

        // raise a synthetic normalized event, which is how a test drives input without a device (§8.3)
        void raise_event(const Event& event);

        // raise a close request, which is how a test drives the modality path (§5.2)
        void raise_close_request();

        // draw one frame and clear the request, which is what a headless pump does instead of a display link (§4.3)
        void pump_frame();

        // hand out the title, which a headless test asserts on because nothing is drawn (§5.3)
        [[nodiscard]] const std::string& title() const;

        // report whether a resize is waiting to be drawn, which is what a test synchronizes on (§5.3)
        [[nodiscard]] bool resize_pending() const;

    private:
        // build the window over the initial metrics the options carry (§5.3)
        explicit HeadlessPlatformWindow(const WindowOptions& options);

        // the listener this window raises events into, which the window borrows (§5.3)
        PlatformWindowListener* m_listener = nullptr;

        // the metrics this window presents at, which are synthetic and owned here (§5.3)
        WindowMetrics m_metrics;

        // the title this window keeps, because a headless run has nothing to draw it on (§5.3)
        std::string m_title;

        // whether a frame was asked for and not yet drawn, which is the per window pacing flag (§4.3, AD-18b)
        bool m_frame_requested = false;

        // whether the window is on screen, which show and hide record (§5.3)
        bool m_visible = false;

        // whether a resize has been raised but not yet drawn, which is what a test waits on (§5.3)
        bool m_resize_pending = false;

        // the appearance this window reports, which is synthetic and owned here (§5.3, §8.4)
        SystemAppearance m_appearance;
    };
} // namespace albor::headless
