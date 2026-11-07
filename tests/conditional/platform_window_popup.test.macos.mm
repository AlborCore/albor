// the macos half of the popup role: the borderless, floating, non activating panel a dropdown consumes (§5.1, AD-08)

#import <AppKit/AppKit.h>

#include <memory>

#include <gtest/gtest.h>

#include "albor/platform/macos/platform_window.h"
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

        void on_platform_event(const Event& event) override { last_event = event; }

        void on_platform_appearance_changed(const SystemAppearance&) override { appearance_changed++; }

        // the metrics the window last reported, which the resize observer pushes (§9.1)
        WindowMetrics resize{};

        // how many resize reports the window raised, which a move must not add to (§5.3)
        int resizes = 0;

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

TEST(MacosPopupWindow, ThePopupRoleBuildsABorderlessNonactivatingFloatingPanel) {
    // arrange a popup option set, which is the role under test (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-shape";
    options.size = albor::Size{200.0f, 300.0f};
    // act
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    NSPanel* panel = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-shape"])
            panel = (NSPanel*)window;
    }
    ASSERT_NE(panel, nil);
    // assert the popup carries no title bar, which is what the borderless style mask of §5.1 means
    EXPECT_FALSE((panel.styleMask & NSWindowStyleMaskTitled) != 0);
    // assert the panel may become key without activating the application, which is the non activating mask of §5.1
    EXPECT_TRUE((panel.styleMask & NSWindowStyleMaskNonactivatingPanel) != 0);
    // assert the popup floats above the ordinary windows, which keeps a dropdown above its trigger (§5.1)
    EXPECT_EQ(panel.level, NSFloatingWindowLevel);
    // assert the panel takes key only on demand, which a list does once a user types (§5.1)
    EXPECT_TRUE(panel.becomesKeyOnlyIfNeeded);
    // assert the popup is transparent, which lets a rounded dropdown corner show through (§5.1)
    EXPECT_FALSE(panel.opaque);
}

TEST(MacosPopupWindow, ShowingAPopupOrdersFrontWithoutTakingKey) {
    // arrange a created popup, which the show path orders front (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-show";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    // act
    platform->show();
    // assert the framework records the popup as on screen, which the show path sets (§5.1)
    EXPECT_TRUE(platform->visible());
    NSPanel* panel = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-show"])
            panel = (NSPanel*)window;
    }
    ASSERT_NE(panel, nil);
    // assert the popup never took key, which is the no activate item of §5.1
    EXPECT_FALSE(panel.isKeyWindow);
}

TEST(MacosPopupWindow, ANormalWindowKeepsTheTitledMask) {
    // arrange a normal option set, which is the regression shape of the non popup path (§5.3)
    WindowOptions options;
    options.title = "normal-shape";
    options.size = albor::Size{200.0f, 300.0f};
    // act
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    NSWindow* found = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"normal-shape"])
            found = window;
    }
    ASSERT_NE(found, nil);
    // assert appkit leaves the window release to arc, which every role carries (§5.3)
    EXPECT_FALSE(found.releasedWhenClosed);
    // assert a normal window is not a panel, which keeps the popup surface out of the ordinary path (§5.1)
    EXPECT_FALSE([found isKindOfClass:[NSPanel class]]);
    // assert a normal window keeps the titled mask, which the non popup branch builds (§5.3)
    EXPECT_TRUE((found.styleMask & NSWindowStyleMaskTitled) != 0);
    // act
    platform->show();
    // assert the ordinary show path still marks the window visible, which the activating branch sets (§5.3)
    EXPECT_TRUE(platform->visible());
}

TEST(MacosPopupWindow, ThePopupIsAChildOfItsOwnerAndSitsBelowTheAnchor) {
    // arrange an owner window and a popup anchored at a known content point (§5.1)
    WindowOptions owner_options;
    owner_options.title = "popup-anchor-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-anchor-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    const albor::Rect anchor = albor::Rect::from_origin_size(albor::Point{20.0f, 30.0f}, albor::Size{100.0f, 24.0f});
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-anchor";
    popup_options.size = albor::Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = anchor;
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    // act
    platform->show();
    // assert the panel is a child of its owner, which is what makes it follow the owner (§5.1)
    NSPanel* panel = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-anchor"])
            panel = (NSPanel*)window;
    }
    ASSERT_NE(panel, nil);
    EXPECT_EQ(panel.parentWindow, owner_window);
    // assert the frame hangs below the anchor in screen coordinates (§5.1)
    const NSRect content_bounds = owner_window.contentView.bounds;
    const NSRect anchor_content = NSMakeRect(anchor.x, content_bounds.size.height - anchor.y - anchor.height, anchor.width, anchor.height);
    const NSRect anchor_window = [owner_window.contentView convertRect:anchor_content toView:nil];
    const NSRect anchor_screen = [owner_window convertRectToScreen:anchor_window];
    EXPECT_NEAR(panel.frame.origin.x, anchor_screen.origin.x, 0.5);
    EXPECT_NEAR(panel.frame.origin.y, anchor_screen.origin.y - 80.0, 0.5);
    // act hide and show the retained panel, which must reattach it to the same owner (§5.1)
    platform->hide();
    EXPECT_EQ(panel.parentWindow, nil);
    platform->show();
    // assert the reopened panel is a child again and keeps the anchor placement (§5.1)
    EXPECT_EQ(panel.parentWindow, owner_window);
    EXPECT_NEAR(panel.frame.origin.x, anchor_screen.origin.x, 0.5);
    EXPECT_NEAR(panel.frame.origin.y, anchor_screen.origin.y - 80.0, 0.5);
}

TEST(MacosPopupWindow, AClickOnTheAnchorDoesNotDismissThePopup) {
    // arrange a shown popup over a trigger rectangle in its owner (§5.1)
    WindowOptions owner_options;
    owner_options.title = "popup-anchor-click-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    RecordingListener owner_listener;
    owner->set_listener(&owner_listener);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-anchor-click-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    const albor::Rect anchor = albor::Rect::from_origin_size(albor::Point{20.0f, 30.0f}, albor::Size{100.0f, 24.0f});
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-anchor-click";
    popup_options.size = albor::Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = anchor;
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act a press lands on the trigger, which belongs to the toggle rather than the dismissal (§5.1)
    const NSRect content_bounds = owner_window.contentView.bounds;
    const NSPoint content_point = NSMakePoint(anchor.x + 5.0, content_bounds.size.height - anchor.y - anchor.height + 5.0);
    const NSPoint window_point = [owner_window.contentView convertPoint:content_point toView:nil];
    NSEvent* click = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:window_point modifierFlags:0 timestamp:0.0 windowNumber:owner_window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
    [[NSApplication sharedApplication] sendEvent:click];
    // assert the click reached the owner, which is the trigger that toggles the list (§5.1)
    EXPECT_EQ(owner_listener.last_event.kind, EventKind::PointerDown);
    // assert the popup stays open, because the trigger toggles it itself (§5.1)
    EXPECT_EQ(listener.closed, 0);
    EXPECT_TRUE(platform->visible());
}

TEST(MacosPopupWindow, ADialogAttachesToItsParent) {
    // arrange an owner window and a dialog that names it as parent (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-parent-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-parent-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-parent";
    dialog_options.size = albor::Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<MacosPlatformWindow> dialog = MacosPlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    // act show and hide the dialog, which follows the native child relation (§5.1)
    dialog->show();
    NSWindow* dialog_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-parent"])
            dialog_window = window;
    }
    ASSERT_NE(dialog_window, nil);
    // assert the dialog is a child of its parent, which is the appkit owner relation (§5.1)
    EXPECT_EQ(dialog_window.parentWindow, owner_window);
    dialog->hide();
    // assert a hidden dialog leaves the child list, which keeps the owner clean (§5.1)
    EXPECT_EQ(dialog_window.parentWindow, nil);
}

TEST(MacosPopupWindow, ActivatingADialogKeepsItVisibleAndAttached) {
    // arrange an owner and a dialog attached to it (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-activate-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-activate-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-activate";
    dialog_options.size = albor::Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<MacosPlatformWindow> dialog = MacosPlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    dialog->show();
    NSWindow* dialog_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-activate"])
            dialog_window = window;
    }
    ASSERT_NE(dialog_window, nil);
    // act the framework brings an already visible dialog back, which is what closing another one needs (§5.1)
    dialog->activate();
    // assert the dialog is still visible and a child of its owner, which is the native ownership relation (§5.1)
    EXPECT_TRUE(dialog_window.isVisible);
    EXPECT_EQ(dialog_window.parentWindow, owner_window);
    dialog->hide();
}

TEST(MacosPopupWindow, MovingTheOwnerKeepsThePopupAttached) {
    // arrange a shown popup attached to an owner, which appkit moves as one surface (§5.1)
    WindowOptions owner_options;
    owner_options.title = "popup-follow-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-follow-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "popup-follow";
    popup_options.size = albor::Size{180.0f, 80.0f};
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = albor::Rect::from_origin_size(albor::Point{20.0f, 30.0f}, albor::Size{100.0f, 24.0f});
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(popup_options);
    ASSERT_NE(platform, nullptr);
    platform->show();
    NSPanel* panel = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-follow"])
            panel = (NSPanel*)window;
    }
    ASSERT_NE(panel, nil);
    const NSPoint owner_before = owner_window.frame.origin;
    const NSPoint popup_before = panel.frame.origin;
    // act move the owner, which carries the transient child with it (§5.1)
    [owner_window setFrameOrigin:NSMakePoint(owner_before.x + 40.0, owner_before.y + 30.0)];
    const NSPoint owner_after = owner_window.frame.origin;
    // assert the popup followed by the same offset, because the anchor did not change (§5.1)
    EXPECT_NEAR(panel.frame.origin.x, popup_before.x + owner_after.x - owner_before.x, 0.5);
    EXPECT_NEAR(panel.frame.origin.y, popup_before.y + owner_after.y - owner_before.y, 0.5);
}

TEST(MacosPopupWindow, AnOutsideClickDismissesThePopupAndReportsClosed) {
    // arrange a shown popup with a listener, which the dismissal reports the close to (§5.2)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-dismiss";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // arrange a second window outside the popup, which the press targets so the monitor reads it as outside (§5.1)
    NSWindow* outside = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 50.0, 50.0) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    NSEvent* click = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:NSMakePoint(1.0, 1.0) modifierFlags:0 timestamp:0.0 windowNumber:outside.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
    // act
    [[NSApplication sharedApplication] sendEvent:click];
    // assert the framework was told the popup closed, which is the dismissal report of §5.2
    EXPECT_EQ(listener.closed, 1);
    // assert the popup is off screen, which the dismissal sets before it reports (§5.1)
    EXPECT_FALSE(platform->visible());
}

TEST(MacosPopupWindow, AClickInsideThePopupDoesNotDismissIt) {
    // arrange a shown popup with a listener, which must stay open on an inside click (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-inside";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    NSPanel* panel = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-inside"])
            panel = (NSPanel*)window;
    }
    ASSERT_NE(panel, nil);
    NSEvent* click = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:NSMakePoint(1.0, 1.0) modifierFlags:0 timestamp:0.0 windowNumber:panel.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
    // act
    [[NSApplication sharedApplication] sendEvent:click];
    // assert the popup raised no close, because the click reached the popup itself (§5.1)
    EXPECT_EQ(listener.closed, 0);
    // assert the popup stays on screen, which the inside click leaves it (§5.1)
    EXPECT_TRUE(platform->visible());
}

TEST(MacosPopupWindow, AClickWhileThePopupIsHiddenReportsNothing) {
    // arrange a created popup that was never shown, which the hidden guard protects (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-hidden";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    // arrange a press outside the hidden popup, which the monitor would otherwise read as a dismissal (§5.1)
    NSWindow* outside = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 50.0, 50.0) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    NSEvent* click = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:NSMakePoint(1.0, 1.0) modifierFlags:0 timestamp:0.0 windowNumber:outside.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
    // act
    [[NSApplication sharedApplication] sendEvent:click];
    // assert a hidden popup is never dismissed, so its hook does not fire (§5.1)
    EXPECT_EQ(listener.closed, 0);
    // assert the popup stays off screen, which an outside press cannot change (§5.1)
    EXPECT_FALSE(platform->visible());
}

TEST(MacosPopupWindow, ASecondOutsideClickDoesNotReportClosedAgain) {
    // arrange a shown popup that a first outside press dismisses (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-once";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    NSWindow* outside = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 50.0, 50.0) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    NSEvent* first = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:NSMakePoint(1.0, 1.0) modifierFlags:0 timestamp:0.0 windowNumber:outside.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
    [[NSApplication sharedApplication] sendEvent:first];
    ASSERT_EQ(listener.closed, 1);
    // arrange a second outside press, which arrives while the popup is already hidden (§5.1)
    NSEvent* second = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:NSMakePoint(1.0, 1.0) modifierFlags:0 timestamp:0.0 windowNumber:outside.windowNumber context:nil eventNumber:2 clickCount:1 pressure:1.0];
    // act
    [[NSApplication sharedApplication] sendEvent:second];
    // assert the dismissal raises its hook exactly once, which the hidden guard holds (§5.1, §5.2)
    EXPECT_EQ(listener.closed, 1);
}

TEST(MacosPopupWindow, ADeactivationDismissesThePopupAndReportsClosed) {
    // arrange a shown popup, which a deactivation dismisses (§8.4)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-deactivate";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act the application resigns active, which appkit announces to the observer (§8.4)
    [[NSNotificationCenter defaultCenter] postNotificationName:NSApplicationDidResignActiveNotification object:[NSApplication sharedApplication]];
    // assert the framework was told the popup closed, which the deactivation reports through the shared path (§5.2)
    EXPECT_EQ(listener.closed, 1);
    // assert the framework records the popup as gone, which the dismissal sets (§5.1)
    EXPECT_FALSE(platform->visible());
}

TEST(MacosPopupWindow, ANormalWindowIsNotClosedByDeactivation) {
    // arrange a shown normal window, which does not hide on deactivation (§5.3)
    WindowOptions options;
    options.title = "normal-deactivate";
    options.size = albor::Size{200.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    platform->show();
    // act
    [[NSNotificationCenter defaultCenter] postNotificationName:NSApplicationDidResignActiveNotification object:[NSApplication sharedApplication]];
    // assert a non popup raises no close, because only a popup dismisses on deactivation (§5.1)
    EXPECT_EQ(listener.closed, 0);
    // assert the ordinary window stays on screen, which a deactivation leaves it (§5.3)
    EXPECT_TRUE(platform->visible());
}

TEST(MacosWindowMetrics, CreatingAWindowReportsTheContentSizeItWasGiven) {
    // arrange a normal window with a known content size (§5.3, AD-27)
    WindowOptions options;
    options.title = "metrics-create";
    options.size = albor::Size{640.0f, 480.0f};
    // act
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    const WindowMetrics metrics = platform->metrics();
    EXPECT_FLOAT_EQ(metrics.logical_size.width, 640.0f);
    EXPECT_FLOAT_EQ(metrics.logical_size.height, 480.0f);
}

TEST(MacosWindowMetrics, SetSizeKeepsTheContentSizeAndTheFrameAroundIt) {
    // arrange a window created at one size and resized to another (§5.3)
    WindowOptions options;
    options.title = "metrics-set-size";
    options.size = albor::Size{400.0f, 300.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    // act
    platform->set_size(albor::Size{640.0f, 480.0f});
    // assert the content area is the requested size, which is what metrics reports (§5.3, AD-27)
    const WindowMetrics metrics = platform->metrics();
    EXPECT_FLOAT_EQ(metrics.logical_size.width, 640.0f);
    EXPECT_FLOAT_EQ(metrics.logical_size.height, 480.0f);
    // assert the frame carries the same content plus the native title bar (§5.3)
    NSWindow* window = (__bridge NSWindow*)const_cast<void*>(platform->native_window_handle().object());
    ASSERT_NE(window, nil);
    const NSRect expected = [window frameRectForContentRect:NSMakeRect(0.0, 0.0, 640.0, 480.0)];
    EXPECT_FLOAT_EQ(window.frame.size.width, expected.size.width);
    EXPECT_FLOAT_EQ(window.frame.size.height, expected.size.height);
}

TEST(MacosWindowMetrics, MovingAWindowDoesNotChangeItsSize) {
    // arrange a shown window whose size is settled (§5.3)
    WindowOptions options;
    options.title = "metrics-move";
    options.size = albor::Size{500.0f, 320.0f};
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    platform->show();
    const WindowMetrics before = platform->metrics();
    NSWindow* window = (__bridge NSWindow*)const_cast<void*>(platform->native_window_handle().object());
    ASSERT_NE(window, nil);
    RecordingListener listener;
    platform->set_listener(&listener);
    const NSPoint origin = window.frame.origin;
    // act
    [window setFrameOrigin:NSMakePoint(window.frame.origin.x + 40.0, window.frame.origin.y + 30.0)];
    // assert
    EXPECT_FLOAT_EQ(window.frame.origin.x, origin.x + 40.0);
    EXPECT_EQ(listener.resizes, 0);
    const WindowMetrics after = platform->metrics();
    EXPECT_FLOAT_EQ(after.logical_size.width, before.logical_size.width);
    EXPECT_FLOAT_EQ(after.logical_size.height, before.logical_size.height);
}

TEST(MacosWindowMetrics, TheWindowServerFrameMatchesTheAppKitFrame) {
    // arrange a shown window whose server record is looked up by its own window number (§5.3)
    WindowOptions options;
    options.title = "metrics-server-frame";
    options.size = albor::Size{1200.0f, 800.0f};
    options.visible = false;
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    platform->show();
    NSWindow* window = (__bridge NSWindow*)const_cast<void*>(platform->native_window_handle().object());
    ASSERT_NE(window, nil);
    const NSInteger number = window.windowNumber;
    // act
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (list == nullptr)
        GTEST_SKIP() << "no on screen window server on this host";
    CGRect server = CGRectNull;
    for (NSDictionary* entry in (__bridge NSArray*)list) {
        if ([entry[(id)kCGWindowNumber] integerValue] == number) {
            NSDictionary* bounds = entry[(id)kCGWindowBounds];
            server = CGRectMake([bounds[@"X"] doubleValue], [bounds[@"Y"] doubleValue], [bounds[@"Width"] doubleValue], [bounds[@"Height"] doubleValue]);
        }
    }
    CFRelease(list);
    if (CGRectIsNull(server))
        GTEST_SKIP() << "the window is not on screen";
    // assert the frame is the content plus the title bar in both sources (§5.3, AD-27)
    EXPECT_FLOAT_EQ(server.size.width, window.frame.size.width);
    EXPECT_FLOAT_EQ(server.size.height, window.frame.size.height);
    EXPECT_FLOAT_EQ(platform->metrics().logical_size.height, 800.0f);
}

TEST(MacosPopupWindow, ThePopupLayerIsNonopaque) {
    // arrange a popup option set, which the transparent layer serves (§5.1)
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.title = "popup-layer";
    options.size = albor::Size{200.0f, 300.0f};
    // act
    std::unique_ptr<MacosPlatformWindow> platform = MacosPlatformWindow::create(options);
    // assert
    ASSERT_NE(platform, nullptr);
    NSView* content = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"popup-layer"])
            content = window.contentView;
    }
    ASSERT_NE(content, nil);
    // assert the metal layer is non opaque, so the compositor may blend the popup over what lies beneath (§5.1)
    EXPECT_FALSE(content.layer.opaque);
}

TEST(MacosPopupWindow, ClosingAParentedDialogDetachesItFromTheOwner) {
    // arrange an owner window and a dialog it owns, both shown (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-close-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-close-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-close";
    dialog_options.size = albor::Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<MacosPlatformWindow> dialog = MacosPlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    dialog->show();
    NSWindow* dialog_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-close"])
            dialog_window = window;
    }
    ASSERT_NE(dialog_window, nil);
    ASSERT_EQ(dialog_window.parentWindow, owner_window);
    // assert appkit leaves the window release to arc, which is the over release of #150 (§5.3)
    EXPECT_FALSE(dialog_window.releasedWhenClosed);
    // a weak reference observes whether appkit released the window, which is the over release of #150 (§5.3)
    __weak NSWindow* weak_dialog_window = dialog_window;
    dialog_window = nil;
    // act the framework closes the dialog, which is the close path the application takes (§5.2)
    dialog->close();
    // assert the closed window is ordered out and its child relation is gone, which the display cycle reads (§5.1)
    EXPECT_FALSE(weak_dialog_window.isVisible);
    EXPECT_EQ(weak_dialog_window.parentWindow, nil);
    EXPECT_EQ(owner_window.childWindows.count, 0U);
    // act the closed surface is destroyed, which is the path that crashed (#150)
    dialog.reset();
    // assert the owner survived the teardown untouched (§5.1)
    EXPECT_TRUE(owner_window.isVisible);
    owner->close();
}

TEST(MacosPopupWindow, DestroyingAParentedDialogLeavesNoChildInTheOwner) {
    // arrange an owner window and a shown dialog it owns (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-destroy-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-destroy-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-destroy";
    dialog_options.size = albor::Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<MacosPlatformWindow> dialog = MacosPlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    dialog->show();
    // act the dialog is destroyed without an explicit close, which is the destructor path of #150 (§5.1)
    dialog.reset();
    // the run loop is pumped over several display cycles, which is where the over release surfaced (#150)
    for (int pass = 0; pass < 4; pass++) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
    // assert the owner is still alive and lists no child, so no stale release remains (§5.1)
    EXPECT_TRUE(owner_window.isVisible);
    EXPECT_EQ(owner_window.childWindows.count, 0U);
    owner->close();
}

TEST(MacosPopupWindow, DestroyingTheOwnerBeforeItsDialogIsQuiet) {
    // arrange an owner window and a shown dialog it owns (§5.1)
    WindowOptions owner_options;
    owner_options.title = "dialog-owner-first-owner";
    owner_options.size = albor::Size{320.0f, 240.0f};
    std::unique_ptr<MacosPlatformWindow> owner = MacosPlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    owner->show();
    NSWindow* owner_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-owner-first-owner"])
            owner_window = window;
    }
    ASSERT_NE(owner_window, nil);
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.title = "dialog-owner-first";
    dialog_options.size = albor::Size{180.0f, 120.0f};
    dialog_options.native_parent = owner->native_window_handle();
    std::unique_ptr<MacosPlatformWindow> dialog = MacosPlatformWindow::create(dialog_options);
    ASSERT_NE(dialog, nullptr);
    dialog->show();
    NSWindow* dialog_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"dialog-owner-first"])
            dialog_window = window;
    }
    ASSERT_NE(dialog_window, nil);
    ASSERT_EQ(dialog_window.parentWindow, owner_window);
    // the weak references drop the strong locals, so a deallocation is observable rather than masked (§5.1)
    __weak NSWindow* weak_owner = owner_window;
    __weak NSWindow* weak_dialog = dialog_window;
    owner_window = nil;
    dialog_window = nil;
    // act the owner is destroyed while its dialog is still shown, which appkit answers by closing the child (§5.1)
    owner.reset();
    // the run loop is pumped, because a surviving child would surface during the display cycle (#150)
    for (int pass = 0; pass < 4; pass++) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
    // assert the display cycle survived the owner teardown, which is the crash path of #150 (§5.1)
    EXPECT_FALSE(weak_owner.isVisible);
    EXPECT_FALSE(weak_dialog.isVisible);
    // act the dialog is destroyed afterwards, which is the late teardown of #150 (§5.1)
    dialog.reset();
    for (int pass = 0; pass < 4; pass++) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
    // assert the late teardown completed without a stale release, which is the regression of #150 (§5.1)
    EXPECT_FALSE(weak_dialog.isVisible);
}
