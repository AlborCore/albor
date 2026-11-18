// the win32 input half of §8.3: the button-less move and the departure, which a hover is built from

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>

#include <gtest/gtest.h>

#include "albor/platform/windows/platform_window.h"
#include "albor/window/window_options.h"

using namespace albor;

namespace
{
    // the listener double the suite raises the adapter's reports into, which records them (§5.3)
    class RecordingListener final : public PlatformWindowListener
    {
    public:
        void on_platform_resize(const WindowMetrics&) override {}

        bool on_platform_close_request() override { return true; }

        void on_platform_closed() override {}

        void on_platform_frame() override {}

        void on_platform_event(const Event& event) override {
            // the last event is kept for the ordinary assertions, while the count proves the adapter raised one (§8.3)
            last_event = event;
            events++;
        }

        void on_platform_focus_lost() override {}

        void on_platform_pointer_exited() override { pointer_exits++; }

        void on_platform_appearance_changed(const SystemAppearance&) override {}

        // the last normalized event the window raised, which the motion test reads (§8.3)
        Event last_event{};

        // how many normalized events the window raised, which a move raises exactly once (§8.3)
        int events = 0;

        // how many pointer exits the window raised, which leaving the client area drives (§8.3)
        int pointer_exits = 0;
    };

    // build the window one case drives, attaching the listener before any message reaches it (§5.3)
    std::unique_ptr<Win32PlatformWindow> input_window(RecordingListener& listener) {
        WindowOptions options;
        options.title = "window-input";
        options.size = Size{320.0f, 240.0f};
        std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
        if (platform != nullptr)
            platform->set_listener(&listener);
        return platform;
    }
} // namespace

TEST(Win32WindowInput, PlainMotionReachesTheApp) {
    // arrange a window whose client area receives the pointer, which is the surface a hover names (§5.3, §8.3)
    RecordingListener listener;
    const std::unique_ptr<Win32PlatformWindow> platform = input_window(listener);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    // act move the pointer without a button held, which is the message a hover is built from (§8.3)
    (void)SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    // assert the adapter raised the move the framework routes (§8.3)
    ASSERT_EQ(listener.events, 1);
    EXPECT_EQ(listener.last_event.kind, EventKind::PointerMove);
    // assert the position is logical, so the client pixel is divided by the scale the window reports (§5.3)
    const float scale = platform->metrics().scale_factor;
    EXPECT_FLOAT_EQ(listener.last_event.position.x, 10.0f / scale);
    EXPECT_FLOAT_EQ(listener.last_event.position.y, 10.0f / scale);
}

TEST(Win32WindowInput, TheLeaveMessageReportsThePointerExit) {
    // arrange a window whose move armed the tracking the system answers with a leave (§8.3)
    RecordingListener listener;
    const std::unique_ptr<Win32PlatformWindow> platform = input_window(listener);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    (void)SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    ASSERT_EQ(listener.events, 1);
    // act leave the client area, which the tracked window reports as its own message (§8.3)
    (void)SendMessageW(window, WM_MOUSELEAVE, 0, 0);
    // assert the departure reached the framework, which is what ends the hover of every node left behind (§8.3)
    EXPECT_EQ(listener.pointer_exits, 1);
    // assert the exit is not a normalized event of its own, because the controller synthesizes a node's leave (§8.3)
    EXPECT_EQ(listener.events, 1);
}
