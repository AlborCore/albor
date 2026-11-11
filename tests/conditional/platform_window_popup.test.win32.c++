// the win32 half of the popup role: the borderless, topmost, non activating surface a dropdown consumes (§5.1, AD-08)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "albor/platform/windows/platform_window.h"
#include "albor/window/window_options.h"

using namespace albor;

namespace
{
    // the listener double the suite raises native events into, which records what the window reported (§5.3)
    class RecordingListener final : public PlatformWindowListener
    {
    public:
        void on_platform_resize(const WindowMetrics& metrics) override {
            resize = metrics;
            resizes++;
        }

        bool on_platform_close_request() override { return true; }

        void on_platform_closed() override { closed++; }

        void on_platform_frame() override { frames++; }

        void on_platform_event(const Event& event) override {
            // the last event is kept for the ordinary assertions, while a test may react to the press (§5.3)
            last_event = event;
            if (on_event)
                on_event(event);
        }

        // the reaction a test installs, which opens a popup during the owner press (§5.1)
        std::function<void(const Event&)> on_event;

        void on_platform_focus_lost() override { focus_lost++; }

        void on_platform_focus_gained() override { focus_gained++; }

        void on_platform_appearance_changed(const SystemAppearance&) override { appearance_changed++; }

        // the metrics the window last reported, which the resize message pushes (§9.1)
        WindowMetrics resize{};

        // how many resize reports the window raised, which a move must not add to (§5.3)
        int resizes = 0;

        // how many closed reports the window raised, which a dismissal raises exactly once (§5.2)
        int closed = 0;

        // how many frame requests the window raised, which a popup does not pace through (§4.3)
        int frames = 0;

        // the last normalized event the window raised, which an inside click delivers (§8.3)
        Event last_event{};

        // how many focus losses the window raised, which an activation change drives (§8.3)
        int focus_lost = 0;

        // how many focus gains the window raised, which a key status return drives (§8.3)
        int focus_gained = 0;

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

TEST(Win32PopupWindow, ThePopupIsOwnedByItsAnchorWindowAndSitsBelowTheAnchor) {
    // arrange an owner window placed at a known screen point, which makes the anchor predictable (§5.1)
    WindowOptions owner_options;
    owner_options.title = "popup-anchor-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    owner->show();
    SetWindowPos(owner_window, nullptr, 100, 100, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE);
    const Rect anchor = Rect::from_origin_size(Point{20.0f, 30.0f}, Size{100.0f, 24.0f});
    // act create and show a popup anchored in that owner (§5.1)
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-anchor";
    popup_options.size = Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = anchor;
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(popup, nullptr);
    platform->show();
    // assert win32 owns the popup through the trigger window, which keeps it above its owner (§5.1)
    EXPECT_EQ(GetWindow(popup, GW_OWNER), owner_window);
    // assert the popup hangs below the anchor, scaled by the owner dpi (§5.1, AD-27)
    const float scale = static_cast<float>(GetDpiForWindow(owner_window)) / 96.0f;
    POINT anchor_screen{static_cast<LONG>(std::lround(static_cast<double>(anchor.x) * scale)), static_cast<LONG>(std::lround(static_cast<double>(anchor.y) * scale))};
    ASSERT_NE(ClientToScreen(owner_window, &anchor_screen), 0);
    RECT frame{};
    ASSERT_NE(GetWindowRect(popup, &frame), 0);
    EXPECT_EQ(frame.left, anchor_screen.x);
    EXPECT_EQ(frame.top, anchor_screen.y + static_cast<LONG>(std::lround(static_cast<double>(anchor.height) * scale)));
    EXPECT_EQ(frame.right - frame.left, static_cast<LONG>(std::lround(static_cast<double>(popup_options.size.width) * scale)));
    EXPECT_EQ(frame.bottom - frame.top, static_cast<LONG>(std::lround(static_cast<double>(popup_options.size.height) * scale)));
}

TEST(Win32PopupWindow, AClickOnTheAnchorIsForwardedWithoutDismissingThePopup) {
    // arrange an owner and a shown popup, both under listeners so the forwarding is observable (§5.1, §8.3)
    WindowOptions owner_options;
    owner_options.title = "popup-anchor-click-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    RecordingListener owner_listener;
    owner->set_listener(&owner_listener);
    owner->show();
    SetWindowPos(owner_window, nullptr, 100, 100, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE);
    const Rect anchor = Rect::from_origin_size(Point{20.0f, 30.0f}, Size{100.0f, 24.0f});
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-anchor-click";
    popup_options.size = Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = anchor;
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(popup, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act a press lands on the trigger, which the captured popup forwards to its owner (§5.1)
    const float scale = static_cast<float>(GetDpiForWindow(owner_window)) / 96.0f;
    POINT trigger{static_cast<LONG>(std::lround(static_cast<double>(anchor.x + anchor.width * 0.5f) * scale)), static_cast<LONG>(std::lround(static_cast<double>(anchor.y + anchor.height * 0.5f) * scale))};
    ASSERT_NE(ClientToScreen(owner_window, &trigger), 0);
    POINT in_popup = trigger;
    ASSERT_NE(ScreenToClient(popup, &in_popup), 0);
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(in_popup.x, in_popup.y));
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // assert the click reached the owner as a pointer press, which is the toggle path (§5.1, §8.3)
    EXPECT_EQ(owner_listener.last_event.kind, EventKind::PointerDown);
    // assert the popup itself was not dismissed, because the trigger owns the toggle (§5.1)
    EXPECT_EQ(listener.closed, 0);
    EXPECT_TRUE(platform->visible());
}

TEST(Win32PopupWindow, ADialogIsOwnedByItsParent) {
    // arrange an owner window and a dialog that names it as parent (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-parent-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    owner->show();
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-parent";
    dialog_options.size = Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    // act create and show the dialog, which win32 owns through the creation owner (§5.1)
    std::unique_ptr<Win32PlatformWindow> dialog = Win32PlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    HWND dialog_window = static_cast<HWND>(const_cast<void*>(dialog->native_window_handle().object()));
    ASSERT_NE(dialog_window, nullptr);
    dialog->show();
    // assert the dialog reports the owner, which keeps it above and minimized with it (§5.1)
    EXPECT_EQ(GetWindow(dialog_window, GW_OWNER), owner_window);
}

TEST(Win32PopupWindow, ActivatingADialogKeepsItFocused) {
    // arrange an owner and a dialog owned by it (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-activate-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    owner->show();
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-activate";
    dialog_options.size = Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<Win32PlatformWindow> dialog = Win32PlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    HWND dialog_window = static_cast<HWND>(const_cast<void*>(dialog->native_window_handle().object()));
    ASSERT_NE(dialog_window, nullptr);
    dialog->show();
    // act the framework brings an already visible dialog back, which is what closing another one needs (§5.1)
    dialog->activate();
    // assert the dialog holds the thread focus and stays owned by its parent (§5.1)
    EXPECT_EQ(GetFocus(), dialog_window);
    EXPECT_EQ(GetWindow(dialog_window, GW_OWNER), owner_window);
}

TEST(Win32PopupWindow, MovingTheOwnerMovesThePopupWithIt) {
    // arrange an owner at a known screen point and a popup anchored inside it (§5.1)
    WindowOptions owner_options;
    owner_options.title = "popup-follow-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    owner->show();
    SetWindowPos(owner_window, nullptr, 100, 100, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE);
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-follow";
    popup_options.size = Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = Rect::from_origin_size(Point{20.0f, 30.0f}, Size{100.0f, 24.0f});
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(popup, nullptr);
    platform->show();
    RECT before{};
    ASSERT_NE(GetWindowRect(popup, &before), 0);
    // act move the owner, which win32 does not move an owned window for on its own (§5.1)
    SetWindowPos(owner_window, nullptr, 300, 200, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT after{};
    ASSERT_NE(GetWindowRect(popup, &after), 0);
    // assert the popup followed by the owner delta, because the anchor did not change (§5.1)
    EXPECT_EQ(after.left - before.left, 200);
    EXPECT_EQ(after.top - before.top, 100);
}

TEST(Win32PopupWindow, AShowInsideTheOwnerPressLeavesTheReleaseAndThenArmsDismissal) {
    // arrange an owner whose press opens a popup, which is the real combo box sequence (§5.1)
    WindowOptions owner_options;
    owner_options.title = "popup-open-press-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    RecordingListener owner_listener;
    owner->set_listener(&owner_listener);
    owner->show();
    SetWindowPos(owner_window, nullptr, 100, 100, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE);
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-open-press";
    popup_options.size = Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = Rect::from_origin_size(Point{20.0f, 30.0f}, Size{100.0f, 24.0f});
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(popup, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    owner_listener.on_event = [&platform](const Event& event) {
        // the trigger opens its list on the press, which is the control path (§8.3)
        if (event.kind == EventKind::PointerDown)
            platform->show();
    };
    // act press the owner trigger, which opens the popup while the owner still holds the capture (§5.1)
    const float scale = static_cast<float>(GetDpiForWindow(owner_window)) / 96.0f;
    const int trigger_x = static_cast<int>(std::lround(static_cast<double>(20.0f + 50.0f) * scale));
    const int trigger_y = static_cast<int>(std::lround(static_cast<double>(30.0f + 12.0f) * scale));
    (void)SendMessageW(owner_window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(trigger_x, trigger_y));
    // assert the popup left the capture with the owner, so the matching release still reaches it (§5.1)
    EXPECT_TRUE(platform->visible());
    EXPECT_EQ(GetCapture(), owner_window);
    // act release the press on the owner, which is where win32 routes it through the owner capture (§5.1)
    (void)SendMessageW(owner_window, WM_LBUTTONUP, 0, MAKELPARAM(trigger_x, trigger_y));
    // assert the owner saw its release, which clears the framework capture and completes the press (§8.3)
    EXPECT_EQ(owner_listener.last_event.kind, EventKind::PointerUp);
    // assert the popup now holds the capture, which is what routes the next outside click to it (§5.1)
    EXPECT_EQ(GetCapture(), popup);
    // act click outside the popup, which the captured popup must dismiss (§5.1)
    (void)SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-10, -10));
    // assert the popup closed and released the capture, which ends the press sequence (§5.1)
    EXPECT_EQ(listener.closed, 1);
    EXPECT_FALSE(platform->visible());
    EXPECT_NE(GetCapture(), popup);
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

TEST(Win32WindowMetrics, CreatingAWindowReportsTheClientSizeItWasGiven) {
    // arrange a normal window with a known client size (§5.3, AD-27)
    WindowOptions options;
    options.title = "metrics-create";
    options.size = albor::Size{640.0f, 480.0f};
    // act
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    const WindowMetrics metrics = platform->metrics();
    EXPECT_FLOAT_EQ(metrics.logical_size.width, 640.0f);
    EXPECT_FLOAT_EQ(metrics.logical_size.height, 480.0f);
}

TEST(Win32WindowMetrics, SetSizeKeepsTheClientSizeAndTheFrameAroundIt) {
    // arrange a window created at one size and resized to another (§5.3)
    WindowOptions options;
    options.title = "metrics-set-size";
    options.size = albor::Size{400.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    // act
    platform->set_size(albor::Size{640.0f, 480.0f});
    // assert the client area is the requested size, which is what metrics reports (§5.3, AD-27)
    const WindowMetrics metrics = platform->metrics();
    EXPECT_FLOAT_EQ(metrics.logical_size.width, 640.0f);
    EXPECT_FLOAT_EQ(metrics.logical_size.height, 480.0f);
    // assert the window frame is the client area plus the native chrome (§5.3)
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RECT frame{};
    ASSERT_NE(GetWindowRect(window, &frame), 0);
    RECT client{};
    ASSERT_NE(GetClientRect(window, &client), 0);
    EXPECT_GT(frame.right - frame.left, client.right - client.left);
    EXPECT_GT(frame.bottom - frame.top, client.bottom - client.top);
}

TEST(Win32WindowMetrics, MovingAWindowDoesNotChangeItsSize) {
    // arrange a shown window whose size is settled (§5.3)
    WindowOptions options;
    options.title = "metrics-move";
    options.size = albor::Size{500.0f, 320.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    platform->show();
    const WindowMetrics before = platform->metrics();
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    RECT origin{};
    ASSERT_NE(GetWindowRect(window, &origin), 0);
    // act
    SetWindowPos(window, nullptr, 40, 30, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // assert
    RECT moved{};
    ASSERT_NE(GetWindowRect(window, &moved), 0);
    EXPECT_NE(moved.left, origin.left);
    EXPECT_EQ(listener.resizes, 0);
    const WindowMetrics after = platform->metrics();
    EXPECT_FLOAT_EQ(after.logical_size.width, before.logical_size.width);
    EXPECT_FLOAT_EQ(after.logical_size.height, before.logical_size.height);
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

TEST(Win32PopupWindow, ClosingADialogLeavesTheOwnerIntact) {
    // arrange an owner window and a dialog that names it as parent, both shown (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-close-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    RecordingListener owner_listener;
    owner->set_listener(&owner_listener);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    owner->show();
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-close";
    dialog_options.size = Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<Win32PlatformWindow> dialog = Win32PlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    RecordingListener dialog_listener;
    dialog->set_listener(&dialog_listener);
    HWND dialog_window = static_cast<HWND>(const_cast<void*>(dialog->native_window_handle().object()));
    ASSERT_NE(dialog_window, nullptr);
    dialog->show();
    ASSERT_EQ(GetWindow(dialog_window, GW_OWNER), owner_window);
    // act the framework closes the dialog, which destroys its native window (§5.2)
    dialog->close();
    // assert the dialog is gone while the owner survived, which is the win32 half of #150 (§5.1)
    EXPECT_FALSE(IsWindow(dialog_window));
    EXPECT_TRUE(IsWindow(owner_window));
    EXPECT_FALSE(dialog->native_window_handle().valid());
    // assert the framework close reported nothing back, because the framework already knew (§5.2)
    EXPECT_EQ(dialog_listener.closed, 0);
    EXPECT_EQ(owner_listener.closed, 0);
    // act the closed surface is destroyed and the owner closes afterwards (§5.1)
    dialog.reset();
    owner->close();
    // assert both native windows are gone without a duplicate closed report (§5.1)
    EXPECT_FALSE(IsWindow(owner_window));
    EXPECT_EQ(owner_listener.closed, 0);
}

TEST(Win32PopupWindow, DestroyingTheOwnerBeforeItsDialogIsQuiet) {
    // arrange an owner window and a dialog that names it as parent, both shown (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-owner-first-owner";
    owner_options.size = Size{320.0f, 240.0f};
    std::unique_ptr<Win32PlatformWindow> owner = Win32PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    RecordingListener owner_listener;
    owner->set_listener(&owner_listener);
    HWND owner_window = static_cast<HWND>(const_cast<void*>(owner->native_window_handle().object()));
    ASSERT_NE(owner_window, nullptr);
    owner->show();
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-owner-first";
    dialog_options.size = Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<Win32PlatformWindow> dialog = Win32PlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    RecordingListener dialog_listener;
    dialog->set_listener(&dialog_listener);
    HWND dialog_window = static_cast<HWND>(const_cast<void*>(dialog->native_window_handle().object()));
    ASSERT_NE(dialog_window, nullptr);
    dialog->show();
    // act the owner is destroyed while its dialog is shown, which win32 answers by destroying it (§5.1)
    owner.reset();
    // assert the system took the owned dialog down with its owner, which the destroy message reports (§5.2)
    EXPECT_FALSE(IsWindow(owner_window));
    EXPECT_FALSE(IsWindow(dialog_window));
    EXPECT_EQ(dialog_listener.closed, 1);
    // assert the destroyed window already dropped its handle, so a late teardown cannot touch a recycled one (§4.2)
    EXPECT_FALSE(dialog->native_window_handle().valid());
    // act the dialog is destroyed afterwards, which is the late teardown of #150 (§5.1)
    dialog->close();
    // assert the late close touched no native window and reported nothing twice (§5.2)
    EXPECT_EQ(dialog_listener.closed, 1);
    dialog.reset();
}

TEST(Win32PopupWindow, ARightPressArrivesAsASecondaryPointerDown) {
    // arrange a normal window with a listener, which is what receives the press (§5.3, §8.3)
    WindowOptions options;
    options.title = "right-press";
    options.size = Size{200.0f, 200.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act
    (void)SendMessageW(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(10, 10));
    // assert the secondary press reached the framework with its button and logical position (§8.3)
    EXPECT_EQ(listener.last_event.kind, EventKind::PointerDown);
    EXPECT_EQ(listener.last_event.button, PointerButton::Right);
    const float scale = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
    EXPECT_FLOAT_EQ(listener.last_event.position.x, 10.0f / scale);
    EXPECT_FLOAT_EQ(listener.last_event.position.y, 10.0f / scale);
}

TEST(Win32PopupWindow, AMiddlePressArrivesAsAMiddlePointerDown) {
    // arrange a normal window that receives the third button, which a full mouse carries (§8.3)
    WindowOptions options;
    options.title = "middle-press";
    options.size = Size{200.0f, 200.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act
    (void)SendMessageW(window, WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(10, 10));
    // assert the third button reached the framework as the middle one, which a control may claim (§8.3)
    EXPECT_EQ(listener.last_event.kind, EventKind::PointerDown);
    EXPECT_EQ(listener.last_event.button, PointerButton::Middle);
}

TEST(Win32PopupWindow, ALostActivationReportsTheFocusLoss) {
    // arrange a shown normal window with a listener, which owns a drag until activation leaves (§8.3)
    WindowOptions options;
    options.title = "focus-lost";
    options.size = Size{200.0f, 200.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act the window loses activation, which is what a focus change sends (§5.1)
    (void)SendMessageW(window, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
    // assert the framework was told the drag can no longer finish (§8.3)
    EXPECT_EQ(listener.focus_lost, 1);
}

TEST(Win32PopupWindow, AFocusedWindowReportsTheFocusGain) {
    // arrange a shown normal window with a listener, which restores the cue when focus returns (§8.3)
    WindowOptions options;
    options.title = "focus-gained";
    options.size = Size{200.0f, 200.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // the show may already have focused the window, so the report is measured from that baseline (§5.1)
    const int before = listener.focus_gained;
    // act the window gains keyboard focus, which is what a focus return sends (§5.1)
    (void)SendMessageW(window, WM_SETFOCUS, 0, 0);
    // assert the framework was told the cue can return (§8.3)
    EXPECT_EQ(listener.focus_gained, before + 1);
}

TEST(Win32PopupWindow, ARightPressOutsideThePopupDismissesIt) {
    // arrange a shown popup with a listener, which the dismissal reports the close to (§5.2)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-right-dismiss";
    options.size = Size{200.0f, 300.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND popup = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(popup, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act the press lands outside the popup, which is the dismissal monitor the role installs (§5.1)
    (void)SendMessageW(popup, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(-10, -10));
    // assert the secondary press is the same outside dismissal a primary one is (§5.1, §8.3)
    EXPECT_EQ(listener.closed, 1);
    EXPECT_FALSE(platform->visible());
}

TEST(Win32PopupWindow, AnExtraButtonArrivesAsAnOtherPointerDown) {
    // arrange a normal window that receives a button beyond the three named ones (§8.3)
    WindowOptions options;
    options.title = "extra-button";
    options.size = Size{200.0f, 200.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act the first extra button presses at a known client point
    (void)SendMessageW(window, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(10, 10));
    // assert the extra button reached the framework as another button, which the application may bind (§8.3)
    EXPECT_EQ(listener.last_event.kind, EventKind::PointerDown);
    EXPECT_EQ(listener.last_event.button, PointerButton::Other);
    // cleanup the window hides, so a later suite starts with no stray surface (§5.3)
    platform->hide();
}

TEST(Win32PopupWindow, TheMenuKeyArrivesAsTheMenuKey) {
    // arrange a normal window that receives the menu key a full keyboard carries (§8.3)
    WindowOptions options;
    options.title = "menu-key";
    options.size = Size{200.0f, 200.0f};
    std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_window_handle().object()));
    ASSERT_NE(window, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act
    (void)SendMessageW(window, WM_KEYDOWN, VK_APPS, 0);
    // assert the menu key is portable, which is the keyboard route to the context menu (§8.3)
    EXPECT_EQ(listener.last_event.kind, EventKind::KeyDown);
    EXPECT_EQ(listener.last_event.key, Key::Menu);
    // cleanup the window hides, so a later suite starts with no stray surface (§5.3)
    platform->hide();
}
