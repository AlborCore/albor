// the macos half of the popup role: the borderless, floating, non activating panel a dropdown consumes (§5.1, AD-08)

#import <AppKit/AppKit.h>

#include <memory>

#include <gtest/gtest.h>

#include "platform/macos/platform_window.h"
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

        // the metrics the window last reported, which the resize observer pushes (§9.1)
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
    // assert a normal window is not a panel, which keeps the popup surface out of the ordinary path (§5.1)
    EXPECT_FALSE([found isKindOfClass:[NSPanel class]]);
    // assert a normal window keeps the titled mask, which the non popup branch builds (§5.3)
    EXPECT_TRUE((found.styleMask & NSWindowStyleMaskTitled) != 0);
    // act
    platform->show();
    // assert the ordinary show path still marks the window visible, which the activating branch sets (§5.3)
    EXPECT_TRUE(platform->visible());
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
