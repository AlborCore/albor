// the macos input half of §8.3: the button-less move, the tracking area and the departure, which no click carries

#import <AppKit/AppKit.h>

#include <memory>

#include <gtest/gtest.h>

#include "albor/platform/macos/platform_window.h"
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

        // how many pointer exits the window raised, which leaving the content drives (§8.3)
        int pointer_exits = 0;
    };

    // the shown window a test drives, which is a real appkit surface because the tracking area needs one (§8.3)
    struct InputWindow
    {
        // the adapter under test, which the ownership of the native surface follows (§5.3)
        std::unique_ptr<MacosPlatformWindow> platform;

        // the listener the adapter reports into, which outlives the assertions (§5.3)
        RecordingListener listener;

        // the native window the adapter built, which the content view is read from (§5.3)
        NSWindow* window = nil;

        // the content view the pointer moves over, which is the surface a node's hover names (§8.3)
        NSView* content = nil;
    };

    // build and show the window one case drives, attaching the listener before anything is presented (§5.3)
    InputWindow shown_input_window() {
        InputWindow built;
        WindowOptions options;
        options.title = "window-input";
        options.size = albor::Size{320.0f, 240.0f};
        built.platform = MacosPlatformWindow::create(options);
        if (built.platform == nullptr)
            return built;
        built.platform->set_listener(&built.listener);
        built.platform->show();
        built.window = (__bridge NSWindow*)const_cast<void*>(built.platform->native_window_handle().object());
        if (built.window != nil)
            built.content = built.window.contentView;
        // the layout pass appkit would run is asked for, so the suite needs no display cycle to observe the area (§8.3)
        if (built.content != nil)
            [built.content updateTrackingAreas];
        return built;
    }
} // namespace

TEST(MacosWindowInput, ButtonLessMotionReachesTheApp) {
    // arrange a shown window whose content view receives the pointer (§5.3, §8.3)
    InputWindow window = shown_input_window();
    ASSERT_NE(window.platform, nullptr);
    ASSERT_NE(window.content, nil);
    // act move the pointer without a button held, which is the motion a hover is built from (§8.3)
    NSEvent* move = [NSEvent mouseEventWithType:NSEventTypeMouseMoved location:NSMakePoint(10.0, 10.0) modifierFlags:0 timestamp:0.0 windowNumber:window.window.windowNumber context:nil eventNumber:1 clickCount:0 pressure:0.0];
    ASSERT_NE(move, nil);
    [window.content mouseMoved:move];
    // assert the adapter raised the move, because no click path ever carried it before (§8.3)
    ASSERT_EQ(window.listener.events, 1);
    EXPECT_EQ(window.listener.last_event.kind, EventKind::PointerMove);
    // assert the position is logical and flipped to the tree origin, which every pointer event takes (§8.3)
    EXPECT_FLOAT_EQ(window.listener.last_event.position.x, 10.0f);
    EXPECT_FLOAT_EQ(window.listener.last_event.position.y, static_cast<float>(window.content.bounds.size.height) - 10.0f);
}

TEST(MacosWindowInput, TheTrackingAreaCoversMovesEntersAndExits) {
    // arrange a shown window whose content view is the area the pointer is tracked in (§8.3)
    InputWindow window = shown_input_window();
    ASSERT_NE(window.platform, nullptr);
    ASSERT_NE(window.content, nil);
    // act read the areas appkit holds for the content view (§8.3)
    NSTrackingArea* tracking = nil;
    for (NSTrackingArea* area in window.content.trackingAreas)
        if ((area.options & NSTrackingMouseMoved) != 0)
            tracking = area;
    // assert the view tracks moves, because appkit delivers no button-less motion without that option (§8.3)
    ASSERT_NE(tracking, nil);
    // assert the same area reports the entry and the exit, which seed and end the hover (§8.3)
    EXPECT_TRUE((tracking.options & NSTrackingMouseEnteredAndExited) != 0);
    // assert the tracked rectangle follows the view, so a resize needs no manual update (§5.3)
    EXPECT_TRUE((tracking.options & NSTrackingInVisibleRect) != 0);
    // assert the area answers while the application is active, which popups need because they never take key (§8.3)
    EXPECT_TRUE((tracking.options & NSTrackingActiveInActiveApp) != 0);
}

TEST(MacosWindowInput, APointerEntrySeedsTheHover) {
    // arrange a shown window whose content view the pointer enters, which happens before any move (§8.3)
    InputWindow window = shown_input_window();
    ASSERT_NE(window.platform, nullptr);
    ASSERT_NE(window.content, nil);
    // act enter the content view, which a window shown under the pointer reports first (§8.3)
    NSEvent* entry = [NSEvent enterExitEventWithType:NSEventTypeMouseEntered location:NSMakePoint(10.0, 10.0) modifierFlags:0 timestamp:0.0 windowNumber:window.window.windowNumber context:nil eventNumber:1 trackingNumber:1 userData:nil];
    ASSERT_NE(entry, nil);
    [window.content mouseEntered:entry];
    // assert the entry reached the framework as a move, so a hover exists before the pointer travels (§8.3)
    ASSERT_EQ(window.listener.events, 1);
    EXPECT_EQ(window.listener.last_event.kind, EventKind::PointerMove);
}

TEST(MacosWindowInput, APointerExitReachesTheFramework) {
    // arrange a shown window the pointer is over, which holds the hover it must end (§8.3)
    InputWindow window = shown_input_window();
    ASSERT_NE(window.platform, nullptr);
    ASSERT_NE(window.content, nil);
    // act leave the content view with the tracking event appkit delivers there, which no move reports (§8.3)
    NSEvent* exit_event = [NSEvent enterExitEventWithType:NSEventTypeMouseExited location:NSMakePoint(10.0, 10.0) modifierFlags:0 timestamp:0.0 windowNumber:window.window.windowNumber context:nil eventNumber:2 trackingNumber:1 userData:nil];
    ASSERT_NE(exit_event, nil);
    [window.content mouseExited:exit_event];
    // assert the adapter reported the departure, which is what ends the hover of every node left behind (§8.3)
    EXPECT_EQ(window.listener.pointer_exits, 1);
    // assert the exit is not a normalized event of its own, because the controller synthesizes a node's leave (§8.3)
    EXPECT_EQ(window.listener.events, 0);
}
