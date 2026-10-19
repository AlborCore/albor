// the win32 half of the popup role: the borderless, topmost, non activating surface a dropdown consumes (§5.1, AD-08)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>

#include <gtest/gtest.h>

#include "platform/windows/platform_window.h"
#include "window/window_options.h"

using namespace albor;

namespace
{
    // the listener double the suite raises native events into, which records what the window reported (§5.3)
    class RecordingListener final : public PlatformWindowListener
    {
    public:
        void on_platform_resize(const WindowMetrics& metrics) override { resize = metrics; }

        bool on_platform_close_request() override { return true; }

        void on_platform_closed() override { closed++; }

        void on_platform_frame() override { frames++; }

        void on_platform_event(const Event& event) override { last_event = event; }

        void on_platform_appearance_changed(const SystemAppearance&) override { appearance_changed++; }

        // the metrics the window last reported, which the resize message pushes (§9.1)
        WindowMetrics resize{};

        // how many closed reports the window raised, which a dismissal raises exactly once (§5.2)
        int closed = 0;

        // how many frame requests the window raised, which a popup does not pace through (§4.3)
        int frames = 0;

        // the last normalized event the window raised, which an inside click delivers (§8.3)
        Event last_event{};

        // how many appearance changes the window raised, which a theme flip drives (§8.4)
        int appearance_changed = 0;
    };
} // namespace

TEST(Win32PopupWindow, ThePopupRoleBuildsABorderlessNonactivatingTopmostWindow) {
    // arrange a popup option set, which is the role under test (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-shape";
    options.size = albor::Size{200.0f, 300.0f};
    // act
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    ASSERT_NE(popup, nullptr);
    // assert the popup carries no title bar, which is what the borderless style of §5.1 means
    EXPECT_EQ(static_cast<DWORD>(GetWindowLongW(popup, GWL_STYLE)) & WS_CAPTION, 0u);
    // assert the popup floats above the ordinary windows, which keeps a dropdown above its trigger (§5.1)
    EXPECT_TRUE((static_cast<DWORD>(GetWindowLongW(popup, GWL_EXSTYLE)) & WS_EX_TOPMOST) != 0);
    // assert the popup never activates on its own click, which is the non activating surface of §5.1
    EXPECT_TRUE((static_cast<DWORD>(GetWindowLongW(popup, GWL_EXSTYLE)) & WS_EX_NOACTIVATE) != 0);
}

TEST(Win32PopupWindow, ShowingAPopupShowsWithoutTakingForeground) {
    // arrange a created popup, which the show path displays without activation (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-show";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    // act
    platform->show();
    // assert the framework records the popup as on screen, which the show path sets (§5.1)
    EXPECT_TRUE(platform->visible());
    // assert the popup never took the foreground, which is the no activate item of §5.1
    EXPECT_NE(GetForegroundWindow(), popup);
}

TEST(Win32PopupWindow, ANormalWindowKeepsTheTitledStyle) {
    // arrange a normal option set, which is the regression shape of the non popup path (§5.3)
    WindowOptions options;
    options.title = "normal-shape";
    options.size = albor::Size{200.0f, 300.0f};
    // act
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    ASSERT_NE(window, nullptr);
    // assert a normal window keeps the titled style, which the non popup branch builds (§5.3)
    EXPECT_TRUE((static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE)) & WS_CAPTION) != 0);
    // assert a normal window is not topmost, which keeps the popup surface out of the ordinary path (§5.1)
    EXPECT_EQ(static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE)) & WS_EX_TOPMOST, 0u);
    // act
    platform->show();
    // assert the ordinary show path still marks the window visible, which the activating branch sets (§5.3)
    EXPECT_TRUE(platform->visible());
}

TEST(Win32PopupWindow, AnOutsideClickDismissesThePopupAndReportsClosed) {
    // arrange a shown popup with a listener, which the dismissal reports the close to (§5.2)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-dismiss";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    // act a press outside the frame arrives through the capture, which names client coordinates of it (§5.1)
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-40, -20));
    // assert the framework was told the popup closed, which is the dismissal report of §5.2
    EXPECT_EQ(listener.closed, 1);
    // assert the popup is off screen, which the dismissal sets before it reports (§5.1)
    EXPECT_FALSE(platform->visible());
}

TEST(Win32PopupWindow, AClickInsideThePopupDoesNotDismissIt) {
    // arrange a shown popup with a listener, which must stay open on an inside click (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-inside";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    // act
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    // assert the popup raised no close, because the click reached the popup itself (§5.1)
    EXPECT_EQ(listener.closed, 0);
    // assert the popup stays on screen, which the inside click leaves it (§5.1)
    EXPECT_TRUE(platform->visible());
}

TEST(Win32PopupWindow, AClickWhileThePopupIsHiddenReportsNothing) {
    // arrange a created popup that was never shown, which the hidden guard protects (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-hidden";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    // arrange a press outside the hidden popup, which the monitor would otherwise read as a dismissal (§5.1)
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-40, -20));
    // act assert a hidden popup is never dismissed, so its hook does not fire (§5.1)
    EXPECT_EQ(listener.closed, 0);
    // assert the popup stays off screen, which an outside press cannot change (§5.1)
    EXPECT_FALSE(platform->visible());
}

TEST(Win32PopupWindow, ASecondOutsideClickDoesNotReportClosedAgain) {
    // arrange a shown popup that a first outside press dismisses (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-once";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-40, -20));
    ASSERT_EQ(listener.closed, 1);
    // arrange a second outside press, which arrives while the popup is already hidden (§5.1)
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-40, -20));
    // act assert the dismissal raises its hook exactly once, which the hidden guard holds (§5.1, §5.2)
    EXPECT_EQ(listener.closed, 1);
}

TEST(Win32PopupWindow, ADeactivationDismissesThePopupAndReportsClosed) {
    // arrange a shown popup, which a deactivation dismisses (§8.4)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-deactivate";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // arrange the ordinary window the application activation sits under, which is who the message names (§8.4)
    WindowOptions host_options;
    host_options.title = "popup-deactivate-host";
    host_options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> host = Win32PlatformWindow::create(host_options);
    ASSERT_NE(host, nullptr);
    // the host carries the framework the activation is announced into, which a production window always has (§5.3)
    host->set_listener(&listener);
    host->show();
    HWND host_window = static_cast<HWND>(const_cast<void*>(host->native_handle().object()));
    // act the application loses activation, which win32 announces to the window that held it, not the popup (§8.4)
    (void)SendMessageW(host_window, WM_ACTIVATEAPP, FALSE, 0);
    // assert the framework was told the popup closed, which the deactivation reports through the shared path (§5.2)
    EXPECT_EQ(listener.closed, 1);
    // assert the framework records the popup as gone, which the dismissal sets (§5.1)
    EXPECT_FALSE(platform->visible());
}

TEST(Win32PopupWindow, ANormalWindowIsNotClosedByDeactivation) {
    // arrange a shown normal window, which does not hide on deactivation (§5.3)
    WindowOptions options;
    options.title = "normal-deactivate";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    // act
    (void)SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
    // assert a non popup raises no close, because only a popup dismisses on deactivation (§5.1)
    EXPECT_EQ(listener.closed, 0);
    // assert the ordinary window stays on screen, which a deactivation leaves it (§5.3)
    EXPECT_TRUE(platform->visible());
}
