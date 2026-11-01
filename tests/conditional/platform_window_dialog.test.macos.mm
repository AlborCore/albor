// the end to end parented dialog teardown on appkit, which is the regression of #150 (§5.1, §5.2)

#import <AppKit/AppKit.h>

#include <memory>
#include <string_view>

#include <gtest/gtest.h>

#include "application/application.h"
#include "layout/column_layout.h"
#include "platform/macos/event_loop.h"
#include "render/render-software/software_renderer_backend.h"
#include "text/text_engine.h"
#include "widgets/button.h"
#include "window/platform_window_factory.h"
#include "window/window.h"

using namespace albor;

namespace
{
    // the engine double that measures to an empty layout, which keeps the teardown test off any shaper (§9.1)
    class TestTextEngine final : public TextEngine
    {
    public:
        // override: hand out the empty layout, because the test never inspects shaped text (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view, float) const override { return {}; }

        // override: hand out the empty layout at the explicit size, because the test never inspects shaped text (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view, float, float) const override { return {}; }

        // override: report the engine usable, because the application checks readiness (§9.1)
        [[nodiscard]] bool ready() const override { return true; }
    };

    // the scope that installs the appkit platform, the cpu backend and the engine double, and clears after (§5.3, AD-16)
    class InstalledScope
    {
    public:
        InstalledScope() {
            set_text_engine_factory([] { return std::make_unique<TestTextEngine>(); });
            software::install_software_renderer_backend();
            macos::install_macos_platform();
        }

        ~InstalledScope() {
            set_text_engine_factory(nullptr);
            set_renderer_backend_factory(nullptr);
            set_platform_window_factory(nullptr);
            set_platform_event_loop_factory(nullptr);
            set_platform_file_dialog_factory(nullptr);
        }

        InstalledScope(const InstalledScope&) = delete;
        InstalledScope& operator=(const InstalledScope&) = delete;
    };
} // namespace

TEST(MacosDialogWindow, ClosingADialogFromItsButtonHandlerSurvivesTheDisplayCycle) {
    // arrange the appkit platform, the software renderer and the empty text engine (§5.3, AD-16)
    InstalledScope scope;
    // the application runs over the appkit loop, which the test pumps by hand (§4.1)
    Application application(std::make_unique<macos::MacosEventLoop>());
    // the parent window carries a tree, so the stage and the modal gate are the real ones (§5.1)
    RootBuilder parent_form = [](BindingEngine&, TextEngine&) { return std::make_unique<ColumnLayout>(); };
    WindowOptions parent_options;
    parent_options.title = "app-dialog-parent";
    parent_options.size = albor::Size{320.0f, 240.0f};
    const WindowHandle parent = application.create_window(parent_options, parent_form);
    ASSERT_TRUE(parent.valid());
    // the dialog names the parent and starts off screen, which is the reproduction order of #150 (§5.1)
    WindowHandle dialog_handle;
    RootBuilder dialog_form = [&application, &dialog_handle](BindingEngine& engine, TextEngine& text) {
        // the column holds the default key, whose click closes the dialog (§8.3)
        auto column = std::make_unique<ColumnLayout>();
        // the default key is the one a plain return runs (§8.3)
        auto accept = std::make_unique<Button>(&engine, &text, "OK");
        // the identity names the control (AD-11)
        accept->set_explicit_id("ok");
        // the role makes return run it (§8.3)
        accept->set_default(true);
        // the handler closes the dialog through the one veto path (§5.2)
        accept->set_on_click([&application, &dialog_handle] { (void)application.request_close(dialog_handle); });
        // the key joins the column (§8.1)
        column->add_child(std::move(accept));
        return column;
    };
    WindowOptions dialog_options;
    dialog_options.role = WindowRole::Dialog;
    dialog_options.modality = Modality::WindowModal;
    dialog_options.title = "app-dialog-child";
    dialog_options.size = albor::Size{200.0f, 140.0f};
    dialog_options.parent = parent;
    dialog_options.visible = false;
    dialog_handle = application.create_window(dialog_options, dialog_form);
    ASSERT_TRUE(dialog_handle.valid());
    // the dialog is shown after creation, which is the reproduction step of #150 (§5.1)
    application.window(dialog_handle)->show();
    // the first frame arranges the tree and places the focus on the default key (§8.3)
    application.pump();
    // the native windows are found by title, because appkit is the ownership authority (§5.1)
    NSWindow* parent_window = nil;
    NSWindow* dialog_window = nil;
    for (NSWindow* window in [NSApplication sharedApplication].windows) {
        if ([window.title isEqualToString:@"app-dialog-parent"])
            parent_window = window;
        if ([window.title isEqualToString:@"app-dialog-child"])
            dialog_window = window;
    }
    ASSERT_NE(parent_window, nil);
    ASSERT_NE(dialog_window, nil);
    // assert the dialog is a child of its parent, which is the relation the crash corrupts (§5.1)
    ASSERT_EQ(dialog_window.parentWindow, parent_window);
    // assert appkit leaves the release to arc, which is the contract #150 broke (§5.3)
    EXPECT_FALSE(dialog_window.releasedWhenClosed);
    EXPECT_FALSE(parent_window.releasedWhenClosed);
    // the weak reference drops the strong local, so a deallocation is observable rather than masked (§5.1)
    __weak NSWindow* weak_dialog_window = dialog_window;
    dialog_window = nil;
    // act return runs the default key, whose handler asks to close the dialog (§8.3)
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    application.window(dialog_handle)->on_platform_event(event);
    // assert the teardown was posted, because the button is still on the dispatch stack (§4.2)
    EXPECT_NE(application.window(dialog_handle), nullptr);
    EXPECT_NE(weak_dialog_window, nil);
    // the pump applies the posted teardown and draws the parent, which is the display cycle path of #150 (§4.2)
    application.pump();
    EXPECT_EQ(application.window(dialog_handle), nullptr);
    // the run loop is pumped over several display cycles, which exercises the deferred deallocation (#150)
    for (int pass = 0; pass < 4; pass++) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
    // assert the closed dialog is ordered out without an over release, which is what crashed the display cycle (#150)
    EXPECT_FALSE(weak_dialog_window.isVisible);
    // assert the parent survived and lists no child, so the child relation did not outlive the dialog (§5.1)
    EXPECT_NE(application.window(parent), nullptr);
    EXPECT_EQ(parent_window.childWindows.count, 0U);
    // cleanup closes the parent, which is the last native surface the test owns (§5.1)
    EXPECT_TRUE(application.destroy_window(parent));
}
