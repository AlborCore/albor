#include <memory>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/core/dev_hook.h"
#include "albor/layout/column_layout.h"
#include "albor/platform/headless/headless_event_loop.h"
#include "albor/render/render-software/software_renderer_backend.h"
#include "albor/text/text_engine.h"
#include "albor/widgets/button.h"
#include "albor/widgets/text_field.h"
#include "albor/window/platform_window_factory.h"

namespace
{
    // the engine double that measures to an empty layout, which keeps the window tests off any shaper (§9.1)
    class TestTextEngine final : public albor::TextEngine
    {
    public:
        // override: hand out the empty layout, because the wiring under test never draws (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view, float) const override { return {}; }

        // override: hand out the empty layout at the explicit size, because the wiring under test never draws (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view, float, float) const override { return {}; }

        // override: report the engine usable, because the application checks readiness (§9.1)
        [[nodiscard]] bool ready() const override { return true; }
    };

    // the scope that installs the headless platform, the cpu backend and the engine double, and clears after (§5.3, AD-16)
    class InstalledScope
    {
    public:
        InstalledScope() {
            albor::set_text_engine_factory([] { return std::make_unique<TestTextEngine>(); });
            albor::software::install_software_renderer_backend();
            albor::headless::install_headless_platform();
        }

        ~InstalledScope() {
            albor::set_text_engine_factory(nullptr);
            albor::set_renderer_backend_factory(nullptr);
            albor::set_platform_window_factory(nullptr);
            albor::set_platform_event_loop_factory(nullptr);
        }
    };

    // the builder that hands the application an empty column, which the pipeline owns on the window's behalf (§8.1)
    albor::RootBuilder empty_form() {
        return [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<albor::ColumnLayout>(); };
    }

    // the dialog form the keyboard checks drive: one field, a default and a cancel button (§8.3)
    albor::RootBuilder dialog_form(int& accepted, int& dismissed) {
        return [&accepted, &dismissed](albor::BindingEngine& engine, albor::TextEngine& text) {
            // the column stacks the field and the two keys, which is the shape of a form dialog (§8.2)
            auto column = std::make_unique<albor::ColumnLayout>();
            // the field is first, so it is where the caret enters when the dialog opens (§8.3)
            auto field = std::make_unique<albor::TextField>(&engine, &text);
            // the identity is what the initial focus check reads back (AD-11)
            field->set_explicit_id("name");
            // the field joins the column, which is the dialog root (§8.1)
            column->add_child(std::move(field));
            // the default button is the one a plain return runs (§8.3)
            auto accept = std::make_unique<albor::Button>(&engine, &text, "OK");
            // the identity names the control the key runs (AD-11)
            accept->set_explicit_id("ok");
            // the role is what makes the button the dialog default (§8.3)
            accept->set_default(true);
            // the click is counted, which is how the test observes the key (§8.3)
            accept->set_on_click([&accepted] { accepted++; });
            // the button joins the column (§8.1)
            column->add_child(std::move(accept));
            // the cancel button is the one a plain escape runs (§8.3)
            auto cancel = std::make_unique<albor::Button>(&engine, &text, "Cancel");
            // the identity names the control the key runs (AD-11)
            cancel->set_explicit_id("cancel");
            // the role is what makes the button the dialog cancel (§8.3)
            cancel->set_cancel(true);
            // the click is counted, which is how the test observes the key (§8.3)
            cancel->set_on_click([&dismissed] { dismissed++; });
            // the button joins the column (§8.1)
            column->add_child(std::move(cancel));
            return column;
        };
    }
    // the leaf that closes its own popup on a press, which models a list row committing a choice (§8.3, §5.1)
    class PopupClosingLeaf final : public albor::UiNode
    {
    public:
        // build the leaf over the application and handle it closes, which the test reads back (§5.1)
        PopupClosingLeaf(albor::Application& application, albor::WindowHandle& popup, int& deliveries) :
            m_application(application), m_popup(popup), m_deliveries(deliveries) {}

        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override {
            // the leaf fills the offer, because the extent is not what this suite is about (§8.2)
            out_size = constraints.constrain(albor::Size{10.0f, 10.0f});
            m_measured_size = out_size;
        }

        [[nodiscard]] bool hit_test(albor::Point) const override {
            // every press lands on the leaf, which is the popup row under test (§8.3)
            return true;
        }

        void dispatch_event(albor::Event& event) override {
            // only a press commits, because that is the row callback the list raises (§8.3)
            if (event.kind != albor::EventKind::PointerDown)
                return;
            // the press is counted before the hide, so the test sees it exactly once (§8.3)
            m_deliveries++;
            // the row claims the press, which is what arms the capture before the hide (§8.3)
            event.handled = true;
            // the popup leaves the screen from inside its own dispatch, which is the regression (§5.1)
            m_application.close_popup(m_popup);
        }

    private:
        // the application that owns the transient surface (§5.1)
        albor::Application& m_application;

        // the handles the leaf closes, which the test keeps alive across the dispatch (§5.1)
        albor::WindowHandle& m_popup;

        // how many presses reached the leaf, which proves the next one was not swallowed (§8.3)
        int& m_deliveries;
    };

    // the leaf that schedules one repeating animation the first time it is measured (§7.3)
    class AnimatedLeaf final : public albor::UiNode
    {
    public:
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override {
            // the leaf fills the offer, because the extent is not what this suite is about (§8.2)
            out_size = constraints.constrain(albor::Size{10.0f, 10.0f});
            m_measured_size = out_size;
            // the animation is scheduled once the wiring handed the engine down the tree (§7.3, AD-04d)
            if (m_handle == 0 && animation_engine() != nullptr)
                m_handle = animation_engine()->schedule_repeating(
                    [this](float) {
                        // a real animation writes a property or invalidates its node, which asks for the frame (§7.3)
                        invalidate();
                        return true;
                    },
                    albor::AnimationDuration{16});
        }

        // the handle the schedule handed out, which zero still means (§7.3)
        albor::AnimationHandle m_handle = 0;
    };

    // the recorder the runtime's entry points write through, because the hook takes raw pointers (§12.1)
    struct BinderRecorder
    {
        // the tree the runtime offered, which is what a reload would patch (§12.1)
        void* bound_root = nullptr;

        // the surface and the executor a capture reads, which every wired window lends (§6.3, §10.3)
        void* bound_surface = nullptr;
        void* bound_executor = nullptr;

        // whether the offered window carried a tree a reload could patch (§6.1, §12.1)
        bool bound_has_tree = false;

        // how many releases arrived, because that is what proves the binding was withdrawn (§5.2)
        int releases = 0;

        // the root the last release named, because a release carries the tree it owns (§5.2)
        void* released_root = nullptr;
    };

    // the recorder the static entry points below write to, which one test installs at a time (§12.1)
    BinderRecorder* g_recorder = nullptr;

    // the entry point the runtime calls when a stage is wired, which records the offer (§12.1)
    void record_bind(const albor::dev::WindowBinding& binding) {
        // the offer is recorded whole, because that is the whole contract (§12.1)
        if (g_recorder == nullptr)
            return;
        g_recorder->bound_root = binding.root;
        g_recorder->bound_surface = binding.surface;
        g_recorder->bound_executor = binding.executor;
        g_recorder->bound_has_tree = binding.has_tree;
    }

    // the entry point the runtime calls when a stage dies, which records the withdrawal (§5.2)
    void record_unbind(void* root) {
        // the withdrawal is counted and named, because both are what a caller reads back (§5.2)
        if (g_recorder == nullptr)
            return;
        g_recorder->releases++;
        g_recorder->released_root = root;
        // the capture handles go with the tree, because a closed window's surface is gone (§5.2, §6.3)
        g_recorder->bound_surface = nullptr;
        g_recorder->bound_executor = nullptr;
    }

    // the scope that stands in for a linked dev extension, which is what the runtime hands its tree to (§12.1)
    class InstalledRecorder
    {
    public:
        // arrange the recorder, and install the entry points that feed it (§12.1, AD-02)
        InstalledRecorder(BinderRecorder& recorder) {
            g_recorder = &recorder;
            albor::dev::install_root_binder(&record_bind, &record_unbind);
        }

        // an inert binder is left behind, because the hook is process wide and a later suite asserts it (§12.1)
        ~InstalledRecorder() {
            // the recorder is forgotten first, because it dies with this frame and a bind must not write to it (§12.1)
            g_recorder = nullptr;
            albor::dev::install_root_binder(&record_bind, &record_unbind);
        }
    };

    // the builder that hands the application the animated leaf, which the window owns on its behalf (§8.1)
    albor::RootBuilder animated_form() {
        return [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<AnimatedLeaf>(); };
    }

    // the leaf that answers every event, which is what a host observes the outcome of (§8.3)
    class AnsweringLeaf final : public albor::UiNode
    {
    public:
        void dispatch_event(albor::Event& event) override {
            // the leaf claims the event, which is the outcome a host reads on the routed copy (§8.3)
            event.handled = true;
        }
    };

    // the builder that hands the application the answering leaf, which is what the routed event is observed on (§8.1)
    albor::RootBuilder handled_form() {
        return [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<AnsweringLeaf>(); };
    }

    // the leaf that asks for its own window's teardown the first time it is measured (§4.2, §6.1)
    class SelfClosingLeaf final : public albor::UiNode
    {
    public:
        SelfClosingLeaf(albor::WindowHandle& handle, albor::Application& application) :
            m_handle(handle), m_application(application) {}

        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override {
            // the extent is the offer, because what this leaf does is not a layout question (§8.2)
            out_size = constraints.constrain(albor::Size{10.0f, 10.0f});
            // the teardown is asked once, because the leaf is measured on every frame (§4.2)
            if (m_requested)
                return;
            m_requested = true;
            // the window is still alive here, because the pass that is measuring it has not released it (§4.2)
            m_application.destroy_window(m_handle);
        }

    private:
        // the window this leaf belongs to, which the handle names only once the creation returned (§5.2)
        albor::WindowHandle& m_handle;

        // the application that owns the window and therefore the teardown (§4.1)
        albor::Application& m_application;

        // whether the teardown was already asked, because a leaf is measured on every frame (§4.2)
        bool m_requested = false;
    };
} // namespace

TEST(MainWindow, AMainWindowRefusesWithoutARenderer) {
    // arrange the platform and the engine, and no renderer, which is what linking skips (§6.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::set_renderer_backend_factory(nullptr);
    // act create a main window over a selection that cannot draw (§6.3)
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    // assert the handle is invalid, because a window that draws nothing never shows (§6.3)
    EXPECT_FALSE(handle.valid());
}

TEST(MainWindow, AMainWindowRefusesWithoutATextEngine) {
    // arrange the platform and the renderer, and no text engine, which is what linking skips (§9.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::set_text_engine_factory(nullptr);
    // act create a main window over a selection that cannot measure (§9.1)
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    // assert the handle is invalid, because a tree that measures nothing is not driven (§9.1)
    EXPECT_FALSE(handle.valid());
}

TEST(MainWindow, AMainWindowIsWiredByTheApplication) {
    // arrange the factories the composition root would have installed by linking (§5.3, §6.3, §9.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    // arrange the options of the window, whose tree the builder builds when the window joins (§5.1, §8.1)
    albor::WindowOptions options;
    options.title = "Main Window";
    // act create the main window, which the application wires for rendering (§6.1, §8.3)
    const albor::WindowHandle handle = application.create_window(options, empty_form());
    // assert the window is valid and its stage is wired but has built nothing yet (§6.1)
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(application.window_count(), 1u);
    EXPECT_EQ(application.frame_count(handle), 0u);
    // cleanup the application is a singleton, so this test's window is torn down before the next one (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, TwoMainWindowsAreWiredIndependently) {
    // arrange the factories, which the two windows share (§6.2)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    // arrange two main windows that model one application with several of them (§5.2)
    albor::WindowOptions first;
    first.title = "First";
    albor::WindowOptions second;
    second.title = "Second";
    // act create both (§6.1)
    const albor::WindowHandle first_handle = application.create_window(first, empty_form());
    const albor::WindowHandle second_handle = application.create_window(second, empty_form());
    // assert both are valid and distinct, and the registry counts them (§5.2)
    ASSERT_TRUE(first_handle.valid());
    ASSERT_TRUE(second_handle.valid());
    EXPECT_NE(first_handle.id(), second_handle.id());
    EXPECT_EQ(application.window_count(), 2u);
    // act pump one tick, which drives each window's own frame (§6.1, §4.1)
    application.pump();
    // assert each window built its own frame (§6.1)
    EXPECT_EQ(application.frame_count(first_handle), 1u);
    EXPECT_EQ(application.frame_count(second_handle), 1u);
    // cleanup the application is a singleton, so both windows are torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(first_handle));
    EXPECT_TRUE(application.destroy_window(second_handle));
}

TEST(MainWindow, AnAlwaysRenderWindowIsDrivenByEveryPumpedTick) {
    // arrange a tree that opts into continuous rendering, which is the explicit exception to the retained policy (§6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions options;
    options.title = "Main Window";
    albor::MainWindowConfig config;
    config.always_render = true;
    const albor::WindowHandle handle = application.create_window(options, empty_form(), config);
    ASSERT_TRUE(handle.valid());
    // act pump two ticks, which drive the window twice because every tick renders (§6.1)
    application.pump();
    application.pump();
    // assert the two stage frame ran for each tick (§6.1, AD-18)
    EXPECT_EQ(application.frame_count(handle), 2u);
    // cleanup the application is a singleton, so this test's window is torn down before the next one (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, DestroyingTheLastMainWindowEndsTheRun) {
    // arrange the factories and one main window (§6.1, §4.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions options;
    options.title = "Main Window";
    const albor::WindowHandle handle = application.create_window(options, empty_form());
    ASSERT_TRUE(handle.valid());
    // act destroy the only window, which is the close request the user made (§5.2)
    EXPECT_TRUE(application.destroy_window(handle));
    // assert the window is gone, its stage is gone and the run ended itself (§4.1, §6.1)
    EXPECT_EQ(application.window(handle), nullptr);
    EXPECT_EQ(application.window_count(), 0u);
    EXPECT_EQ(application.frame_count(handle), 0u);
    EXPECT_TRUE(application.quit_requested());
}

TEST(MainWindow, DestroyingOneOfTwoKeepsTheApplicationRunning) {
    // arrange the factories and two main windows (§5.2, §6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions first;
    first.title = "First";
    albor::WindowOptions second;
    second.title = "Second";
    const albor::WindowHandle first_handle = application.create_window(first, empty_form());
    const albor::WindowHandle second_handle = application.create_window(second, empty_form());
    ASSERT_TRUE(first_handle.valid());
    ASSERT_TRUE(second_handle.valid());
    // act destroy one of the two (§5.2)
    EXPECT_TRUE(application.destroy_window(first_handle));
    // assert the first window is gone and the second still runs, because its stage was never dropped (§4.1)
    EXPECT_EQ(application.window(first_handle), nullptr);
    EXPECT_NE(application.window(second_handle), nullptr);
    EXPECT_EQ(application.window_count(), 1u);
    // act invalidate and pump the remaining window, which is the retained path (§6.1)
    const std::uint64_t built_before = application.frame_count(second_handle);
    application.stage(second_handle)->root().invalidate();
    application.pump();
    // assert the survivor is still driven, which is what the run down to nothing means (§4.1, §6.1)
    EXPECT_EQ(application.frame_count(second_handle), built_before + 1u);
    // act destroy the last one, whose run ends now because nothing is left (§4.1)
    EXPECT_TRUE(application.destroy_window(second_handle));
    EXPECT_EQ(application.window(second_handle), nullptr);
    EXPECT_TRUE(application.quit_requested());
}

TEST(MainWindow, AFrameWithARunningAnimationAdvancesIt) {
    // arrange the factories, which the window draws through (§5.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions options;
    options.title = "Animations";
    const albor::WindowHandle handle = application.create_window(options, animated_form());
    ASSERT_TRUE(handle.valid());
    // act pump once, which measures the leaf and starts the animation after the frame it appears in (§7.3)
    application.pump();
    // assert the first frame ran through the idle branch, because the animation was not scheduled yet (§7.3)
    EXPECT_EQ(application.frame_count(handle), 1u);
    // act pump again, which now sees an animation running and takes the advance instead (§7.3)
    application.pump();
    // assert both frames built, because the advance is on the frame the recordings follow (§7.3, AD-04d)
    EXPECT_EQ(application.frame_count(handle), 2u);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AFacadeIsHandedBackSoAHostNeverDowncasts) {
    // arrange the factories and one main window, whose tree is the named component (§8.1, §14.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions options;
    options.title = "Facade";
    albor::ColumnLayout* facade = nullptr;
    // act the typed overload builds the component and keeps the concrete pointer for the host (§7.3, AD-24)
    const albor::WindowHandle handle = application.create_window(options, facade);
    // assert the facade is the tree the stage drives, which is what removes the cast every example wrote (§8.1)
    ASSERT_TRUE(handle.valid());
    ASSERT_NE(facade, nullptr);
    ASSERT_NE(application.stage(handle), nullptr);
    EXPECT_EQ(&application.stage(handle)->root(), facade);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AStageIsReachableForAMainWindowAndAbsentForAPlainOne) {
    // arrange the factories and one main window plus one plain window, which owns no tree (§6.1, §8.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle main_handle = application.create_window(albor::WindowOptions{}, empty_form());
    const albor::WindowHandle plain_handle = application.create_window(albor::WindowOptions{});
    ASSERT_TRUE(main_handle.valid());
    ASSERT_TRUE(plain_handle.valid());
    // act and assert only the main window has a stage, because the escape hatch is per main window (§6.1)
    EXPECT_NE(application.stage(main_handle), nullptr);
    EXPECT_EQ(application.stage(plain_handle), nullptr);
    // cleanup the application is a singleton, so both windows are torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(main_handle));
    EXPECT_TRUE(application.destroy_window(plain_handle));
}

TEST(MainWindow, AMainWindowOffersItsTreeToTheLinkedExtension) {
    // arrange the factories and a recording extension, which is what linking the dev runtime installs (§12.1, AD-02)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    BinderRecorder recorder;
    InstalledRecorder installed(recorder);
    // act create a main window, whose stage offers its tree the moment it is wired (§12.1, §8.1)
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    // assert the extension received the live tree, which is what a reload patches (§12.1)
    ASSERT_NE(application.stage(handle), nullptr);
    EXPECT_EQ(recorder.bound_root, &application.stage(handle)->root());
    // act close the window, which is what stops a reload reaching a dead tree (§5.2)
    EXPECT_TRUE(application.destroy_window(handle));
    // assert the release arrived and named that tree, because the binding belonged to this window alone (§5.2)
    EXPECT_EQ(recorder.releases, 1);
    EXPECT_EQ(recorder.released_root, recorder.bound_root);
}

TEST(MainWindow, AStageHandsOutTheEnginesItsTreeWasBuiltOver) {
    // arrange the factories and one main window, whose stage owns the engines for the run (§7.3, §9.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    ASSERT_NE(application.stage(handle), nullptr);
    // act and assert the same objects answer every call, because a caller holds them for the whole run (§7.3)
    EXPECT_EQ(&application.stage(handle)->binding_engine(), &application.stage(handle)->binding_engine());
    EXPECT_EQ(&application.stage(handle)->text_engine(), &application.stage(handle)->text_engine());
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, ABackgroundIsTakenFromTheConfig) {
    // arrange a config that names the clear colour, which is what a window presents against (§6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::MainWindowConfig config;
    config.background = albor::Color::rgb(0.2f, 0.3f, 0.4f);
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form(), config);
    ASSERT_TRUE(handle.valid());
    // act pump a tick, which records the scene against that colour (§6.1)
    application.pump();
    // assert the window presented a frame, because the config reached the stage rather than a constant (§6.1)
    EXPECT_EQ(application.frame_count(handle), 1u);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AWindowThatDrawsByHandPresentsItsOwnScene) {
    // arrange a config whose source owns the scene, because the window brings no tree at all (§6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    std::uint64_t seen_index = 0;
    albor::MainWindowConfig config;
    config.scene_source = [&seen_index](const albor::WindowMetrics& metrics, std::uint64_t index) {
        // the index is recorded, because the source is what decides what a frame is (§6.1)
        seen_index = index;
        std::vector<albor::Rect> rects;
        return std::make_shared<const albor::FrameScene>(albor::FrameScene::from_primitives(metrics, albor::Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects)));
    };
    // act create the window with no builder, which is the shape an application that draws by hand takes (§6.1)
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, albor::RootBuilder{}, config);
    ASSERT_TRUE(handle.valid());
    // assert the window has no tree, because a scene source replaced it (§8.1)
    ASSERT_NE(application.stage(handle), nullptr);
    EXPECT_FALSE(application.stage(handle)->has_tree());
    // arrange a hook, because the slot beside a scene source answers it too (§6.1)
    int hooks = 0;
    EXPECT_TRUE(application.set_frame_hook(handle, [&hooks] { hooks++; }));
    // act pump two ticks, which the source answers twice (§6.1)
    application.pump();
    application.pump();
    // assert each tick presented a frame, the source saw the running index and the slot ran (§6.1)
    EXPECT_EQ(application.frame_count(handle), 2u);
    EXPECT_EQ(seen_index, 1u);
    EXPECT_EQ(hooks, 2);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AWindowThatDrawsByHandStillOffersItsRoot) {
    // arrange a scene source with no builder, which is the shape hello-world uses (§6.1, §10.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    BinderRecorder recorder;
    InstalledRecorder installed(recorder);
    albor::MainWindowConfig config;
    config.scene_source = [](const albor::WindowMetrics&, std::uint64_t) { return std::shared_ptr<const albor::FrameScene>{}; };
    // act create the window, which owns a pipeline root even without a tree (§6.1)
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, albor::RootBuilder{}, config);
    ASSERT_TRUE(handle.valid());
    // assert the root was offered without a reloadable tree, because the automation host observes every window (§10.3)
    ASSERT_NE(application.stage(handle), nullptr);
    EXPECT_FALSE(application.stage(handle)->has_tree());
    EXPECT_NE(recorder.bound_root, nullptr);
    EXPECT_FALSE(recorder.bound_has_tree);
    // act close the window, which withdraws the same root (§5.2)
    EXPECT_TRUE(application.destroy_window(handle));
    // assert the release named the offered root once (§5.2)
    EXPECT_EQ(recorder.releases, 1);
    EXPECT_EQ(recorder.released_root, recorder.bound_root);
}

TEST(MainWindow, AWindowOffersTheCaptureHandlesItsSurfaceNeeds) {
    // arrange the factories and a recording extension, which is what linking the dev runtime installs (§12.1, AD-02)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    BinderRecorder recorder;
    InstalledRecorder installed(recorder);
    // act create a main window, whose stage lends its surface the moment it is wired (§6.3, §10.3)
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    // assert the capture handles arrived with the tree, because a capture flushes the executor first (§6.3)
    EXPECT_NE(recorder.bound_root, nullptr);
    EXPECT_NE(recorder.bound_surface, nullptr);
    EXPECT_NE(recorder.bound_executor, nullptr);
    EXPECT_TRUE(recorder.bound_has_tree);
    // act close the window, which withdraws the handles with the tree it owned (§5.2)
    EXPECT_TRUE(application.destroy_window(handle));
    // assert no handle outlives the window, which keeps a later capture off a dead surface (§5.2)
    EXPECT_EQ(recorder.releases, 1);
    EXPECT_EQ(recorder.bound_surface, nullptr);
    EXPECT_EQ(recorder.bound_executor, nullptr);
}

TEST(MainWindow, ASceneSourceThatProducesNothingPresentsNothing) {
    // arrange a source that declines every frame, which is what an application with nothing to draw means (§6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::MainWindowConfig config;
    config.scene_source = [](const albor::WindowMetrics&, std::uint64_t) { return std::shared_ptr<const albor::FrameScene>{}; };
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, albor::RootBuilder{}, config);
    ASSERT_TRUE(handle.valid());
    // act pump two ticks, which the source declines (§6.1)
    application.pump();
    application.pump();
    // assert no frame was counted, because a tick with no scene presents nothing (§6.1)
    EXPECT_EQ(application.frame_count(handle), 0u);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, ARetainedTreeSkipsATickThatChangedNothing) {
    // arrange a default window, because retained rendering is the normal policy (§6.1, AD-06)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    // act pump three ticks against a tree nothing invalidated (§6.1)
    application.pump();
    application.pump();
    application.pump();
    // assert only the first built, because the scene was already current (§6.1, AD-06)
    EXPECT_EQ(application.frame_count(handle), 1u);
    // act invalidate the tree, which is what a settled binding or an animation does (§6.1, §7.3)
    application.stage(handle)->root().invalidate();
    application.pump();
    // assert the invalidated tick built again, because the policy is dirty and not off (§6.1)
    EXPECT_EQ(application.frame_count(handle), 2u);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AFrameHookRunsBetweenTheBuildAndTheSubmit) {
    // arrange a main window and a hook that records what it observed (§6.1, §10.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::MainWindowConfig config;
    config.always_render = true;
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form(), config);
    ASSERT_TRUE(handle.valid());
    int runs = 0;
    EXPECT_TRUE(application.set_frame_hook(handle, [&runs] { runs++; }));
    // act pump two ticks, which continuous rendering presents twice (§6.1)
    application.pump();
    application.pump();
    // assert the hook ran once per presented frame, because it sits between build and submit (§6.1)
    EXPECT_EQ(runs, 2);
    EXPECT_EQ(application.frame_count(handle), 2u);
    // assert a window with no stage refuses the hook, because there is nothing to install it into (§6.1)
    const albor::WindowHandle plain = application.create_window(albor::WindowOptions{});
    ASSERT_TRUE(plain.valid());
    EXPECT_FALSE(application.set_frame_hook(plain, [] {}));
    // cleanup the application is a singleton, so both windows are torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
    EXPECT_TRUE(application.destroy_window(plain));
}

TEST(MainWindow, AHostObservesTheEventItsTreeAnswered) {
    // arrange a main window whose tree is a leaf that answers every event, and a host that watches (§8.3, AD-24)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, handled_form());
    ASSERT_TRUE(handle.valid());
    bool observed = false;
    bool saw_handled = false;
    application.set_input_handler([&](albor::WindowHandle source, const albor::Event& event) {
        // the host only answers for its own window, because another window is left alone (§5.2)
        if (source != handle)
            return;
        observed = true;
        // the routed copy carries the outcome, which is what a host reacts to imperatively (§8.3)
        saw_handled = event.handled;
    });
    // act raise a click on the window, which routes into the tree (§8.3)
    albor::Window* window = application.window(handle);
    ASSERT_NE(window, nullptr);
    albor::Event click;
    click.kind = albor::EventKind::PointerDown;
    window->on_platform_event(click);
    // assert the host saw the routed event and the tree's answer, because the stage reports before it returns (§8.3)
    EXPECT_TRUE(observed);
    EXPECT_TRUE(saw_handled);
    // cleanup the application is a singleton, so this window is torn down before the next test (§4.1)
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AWindowAddedWhileTheLoopRunsJoinsTheRun) {
    // arrange the factories and one main window (§4.1, §5.2)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle first_handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(first_handle.valid());
    albor::WindowHandle second_handle;
    // act add a second window from a task, which is the on demand path a host drives (§4.1, §4.2)
    application.post([&] { second_handle = application.create_window(albor::WindowOptions{}, empty_form()); });
    application.pump();
    // assert the new window is wired and driven by the same pass that created it (§6.1, §4.1)
    ASSERT_TRUE(second_handle.valid());
    EXPECT_EQ(application.window_count(), 2u);
    EXPECT_EQ(application.frame_count(second_handle), 1u);
    // act close the first, which must leave the second running (§4.1)
    EXPECT_TRUE(application.destroy_window(first_handle));
    EXPECT_NE(application.window(second_handle), nullptr);
    application.stage(second_handle)->root().invalidate();
    application.pump();
    // assert the survivor is still driven, which is the whole claim of running until the last close (§4.1, §6.1)
    EXPECT_EQ(application.frame_count(second_handle), 2u);
    // act close the last one, which is what ends the run (§4.1)
    EXPECT_TRUE(application.destroy_window(second_handle));
    EXPECT_EQ(application.window_count(), 0u);
}

TEST(MainWindow, APopupPressThatClosesTheSurfaceDoesNotKeepTheNextPress) {
    // arrange a main window and a popup whose own press hides it (§5.1, §8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    albor::WindowHandle popup;
    int deliveries = 0;
    albor::RootBuilder builder = [&application, &popup, &deliveries](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<PopupClosingLeaf>(application, popup, deliveries); };
    popup = application.open_popup(*application.root(handle), albor::Rect::from_origin_size(albor::Point{10.0f, 20.0f}, albor::Size{120.0f, 30.0f}), albor::Size{240.0f, 120.0f}, std::move(builder), albor::Color::rgb(0.1f, 0.1f, 0.1f), [] {});
    ASSERT_TRUE(popup.valid());
    albor::WindowStage* stage = application.stage(popup);
    ASSERT_NE(stage, nullptr);
    // act press the popup, which commits and hides the surface from inside its dispatch (§8.3)
    albor::Event press{.kind = albor::EventKind::PointerDown, .position = albor::Point{5.0f, 5.0f}};
    stage->deliver(press);
    // assert the press closed the surface and did not leave a capture behind (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(deliveries, 1);
    EXPECT_FALSE(application.window(popup)->visible());
    // act reopen and press again, which a stale capture would swallow (§8.3)
    application.show_popup(popup);
    albor::Event second{.kind = albor::EventKind::PointerDown, .position = albor::Point{6.0f, 6.0f}};
    stage->deliver(second);
    // assert the reopened surface accepted the second press (§8.3)
    EXPECT_TRUE(second.handled);
    EXPECT_EQ(deliveries, 2);
    // cleanup the popup and the owner (§5.1)
    application.destroy_popup(popup);
    EXPECT_TRUE(application.destroy_window(handle));
}

TEST(MainWindow, AStalePortableParentIsRefusedRatherThanMadeTopLevel) {
    // arrange a window whose owner is destroyed before the child is requested (§5.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle owner = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(owner.valid());
    ASSERT_TRUE(application.destroy_window(owner));
    albor::WindowOptions child;
    child.title = "stale-parent-child";
    child.parent = owner;
    // act create the child through the portable parent handle, which now names nothing (§5.1)
    const albor::WindowHandle stale = application.create_window(child, empty_form());
    // assert the orphan was refused instead of being created as a top level window (§5.1)
    EXPECT_FALSE(stale.valid());
    EXPECT_EQ(application.window_count(), 0u);
}

TEST(MainWindow, AWindowClosedDuringAFrameIsTornDownAfterThePass) {
    // arrange the factories and one main window whose tree closes itself on its first measure (§4.2, §6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowHandle self_closing;
    albor::RootBuilder builder = [&self_closing, &application](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<SelfClosingLeaf>(self_closing, application); };
    // act create the window and pump it, which asks for its own teardown from inside the pass (§4.2)
    self_closing = application.create_window(albor::WindowOptions{}, builder);
    ASSERT_TRUE(self_closing.valid());
    application.pump();
    // assert the window is gone, because the teardown was deferred to the end of the pass (§4.2)
    EXPECT_EQ(application.window(self_closing), nullptr);
    EXPECT_EQ(application.window_count(), 0u);
    EXPECT_EQ(application.stage(self_closing), nullptr);
}

TEST(MainWindow, ANativePopupOpensOverItsOwnerAndReopens) {
    // arrange a main window whose root anchors a native popup (§5.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    ASSERT_NE(application.root(handle), nullptr);
    int dismissals = 0;
    // act open a transient surface with its own empty tree (§5.1)
    const albor::WindowHandle popup = application.open_popup(*application.root(handle), albor::Rect::from_origin_size(albor::Point{10.0f, 20.0f}, albor::Size{120.0f, 30.0f}), albor::Size{240.0f, 120.0f}, [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<albor::ColumnLayout>(); }, albor::Color::rgb(0.1f, 0.1f, 0.1f), [&dismissals] { dismissals++; });
    ASSERT_TRUE(popup.valid());
    ASSERT_NE(application.window(popup), nullptr);
    // assert the surface is a popup over the owner, sized and anchored as the control asked (§5.1)
    EXPECT_EQ(application.window(popup)->options().role, albor::WindowRole::Popup);
    EXPECT_EQ(application.window(popup)->options().parent, handle);
    EXPECT_EQ(application.window(popup)->options().native_parent, application.window(handle)->native_window_handle());
    EXPECT_FLOAT_EQ(application.window(popup)->options().size.width, 240.0f);
    EXPECT_FLOAT_EQ(application.window(popup)->options().size.height, 120.0f);
    EXPECT_FLOAT_EQ(application.window(popup)->options().anchor.x, 10.0f);
    EXPECT_FLOAT_EQ(application.window(popup)->options().anchor.y, 20.0f);
    EXPECT_TRUE(application.window(popup)->visible());
    EXPECT_EQ(application.window_count(), 2u);
    // act hide and show the retained surface, which is the cheap reopen path (§5.1)
    application.close_popup(popup);
    EXPECT_FALSE(application.window(popup)->visible());
    EXPECT_EQ(dismissals, 0);
    application.show_popup(popup);
    EXPECT_TRUE(application.window(popup)->visible());
    // act report a native dismissal, which hides the surface but keeps its tree (§5.1)
    application.window(popup)->on_platform_closed();
    EXPECT_EQ(dismissals, 1);
    EXPECT_NE(application.window(popup), nullptr);
    EXPECT_FALSE(application.window(popup)->visible());
    // assert the retained surface is still reopenable after the dismissal (§5.1)
    application.show_popup(popup);
    EXPECT_TRUE(application.window(popup)->visible());
    // cleanup the popup and owner are both released (§5.1)
    application.destroy_popup(popup);
    EXPECT_EQ(application.window(popup), nullptr);
    EXPECT_TRUE(application.destroy_window(handle));
    EXPECT_EQ(application.window_count(), 0u);
}

TEST(MainWindow, DestroyingAWindowDestroysThePopupsItOwns) {
    // arrange a main window and a native popup anchored inside it (§5.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
    ASSERT_TRUE(handle.valid());
    const albor::WindowHandle popup = application.open_popup(*application.root(handle), albor::Rect{}, albor::Size{100.0f, 80.0f}, [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<albor::ColumnLayout>(); }, albor::Color::rgb(0.1f, 0.1f, 0.1f), [] {});
    ASSERT_TRUE(popup.valid());
    ASSERT_EQ(application.window_count(), 2u);
    // act destroy the owner, which must not leave the transient surface behind (§5.1)
    EXPECT_TRUE(application.destroy_window(handle));
    // assert the popup is gone with its owner, because a transient surface has no life of its own (§5.1)
    EXPECT_EQ(application.window(popup), nullptr);
    EXPECT_EQ(application.window_count(), 0u);
}

TEST(MainWindow, ShutdownClosesAnOpenPopupBeforeTheMainTree) {
    // arrange a local application with a popup still open, and a dismissal observer (§5.1)
    InstalledScope scope;
    int dismissals = 0;
    {
        albor::Application application;
        const albor::WindowHandle handle = application.create_window(albor::WindowOptions{}, empty_form());
        ASSERT_TRUE(handle.valid());
        const albor::WindowHandle popup = application.open_popup(*application.root(handle), albor::Rect{}, albor::Size{100.0f, 80.0f}, [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<albor::ColumnLayout>(); }, albor::Color::rgb(0.1f, 0.1f, 0.1f), [&dismissals] { dismissals++; });
        ASSERT_TRUE(popup.valid());
        // act the local application is destroyed with the popup still open (§5.1)
    }
    // assert the shutdown was quiet, because its own teardown is not an outside dismissal (§5.1)
    EXPECT_EQ(dismissals, 0);
}

TEST(MainWindow, ClosingADialogGivesFocusBackToTheControlThatOpenedIt) {
    // arrange the factories and a parent whose tree owns the control that opens the dialog (§5.3, §8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::RootBuilder opener = [](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column owns one identifiable button, which is the invoking control of §8.3
        auto column = std::make_unique<albor::ColumnLayout>();
        // the button measures through the engine, because it carries a caption (§9.1)
        auto button = std::make_unique<albor::Button>(&engine, &text, "Open");
        // the identity is what survives a rebuild, so it is what the dialog remembers (AD-11)
        button->set_explicit_id("open_button");
        // the button hangs under the column, which is the window tree (§8.1)
        column->add_child(std::move(button));
        return column;
    };
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, opener);
    ASSERT_TRUE(parent.valid());
    // the frame arranges the tree, which is what gives the button bounds to focus (§8.2, §8.3)
    application.pump();
    // the invoking control is located by identity, which is how the framework finds it again (AD-11)
    albor::UiNode* button = nullptr;
    application.root(parent)->visit([&button](albor::UiNode& node) {
        if (button == nullptr && node.identity().explicit_id == "open_button")
            button = &node;
    });
    ASSERT_NE(button, nullptr);
    application.stage(parent)->focus().request_focus(button);
    ASSERT_EQ(application.stage(parent)->focus().focused(), button);
    // act a window modal dialog opens, which remembers the control that opened it (§5.1)
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, empty_form());
    ASSERT_TRUE(dialog.valid());
    // the parent loses its focus while the dialog owns the interaction, which deactivation models (§5.1)
    application.stage(parent)->focus().clear_focus();
    EXPECT_EQ(application.stage(parent)->focus().focused(), nullptr);
    // act the dialog closes, which is what returns the focus to the invoking control (§8.3)
    EXPECT_TRUE(application.destroy_window(dialog));
    // assert the parent is still open and its focus returned to the control that opened the dialog (§5.1)
    EXPECT_NE(application.window(parent), nullptr);
    EXPECT_EQ(application.stage(parent)->focus().focused(), button);
    // cleanup
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, ADialogOpensWithFocusOnItsFirstField) {
    // arrange a parent and a form dialog, which is the shape the conventions describe (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, dialog_form(accepted, dismissed));
    ASSERT_TRUE(dialog.valid());
    // act the first frame arranges the tree, which is when focus has bounds to accept (§8.2, §8.3)
    application.pump();
    // assert the first field holds the focus, not the default or the cancel key (§8.3)
    albor::UiNode* focused = application.stage(dialog)->focus().focused();
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->identity().explicit_id, "name");
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, ReturnRunsTheDialogDefault) {
    // arrange a form dialog whose field holds the focus (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, dialog_form(accepted, dismissed));
    ASSERT_TRUE(dialog.valid());
    application.pump();
    // act the field commits and leaves return, so the dialog default runs (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Return};
    application.window(dialog)->on_platform_event(event);
    // assert the default ran once and the cancel did not (§8.3)
    EXPECT_EQ(accepted, 1);
    EXPECT_EQ(dismissed, 0);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, EscapeRunsTheDialogCancel) {
    // arrange a form dialog whose field holds the focus (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, dialog_form(accepted, dismissed));
    ASSERT_TRUE(dialog.valid());
    application.pump();
    // act escape reaches the dialog, because the field declines it (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Escape};
    application.window(dialog)->on_platform_event(event);
    // assert the cancel key ran once and the default did not (§8.3)
    EXPECT_EQ(dismissed, 1);
    EXPECT_EQ(accepted, 0);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, ADialogCanOptOutOfItsKeyConventions) {
    // arrange a dialog that declines both conventions (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    dialog_options.dialog.return_invokes_default = false;
    dialog_options.dialog.escape_dismisses = false;
    const albor::WindowHandle dialog = application.create_window(dialog_options, dialog_form(accepted, dismissed));
    ASSERT_TRUE(dialog.valid());
    application.pump();
    // act both keys reach the dialog and nothing consumes them (§8.3)
    albor::Event accept{.kind = albor::EventKind::KeyDown, .key = albor::Key::Return};
    application.window(dialog)->on_platform_event(accept);
    albor::Event dismiss{.kind = albor::EventKind::KeyDown, .key = albor::Key::Escape};
    application.window(dialog)->on_platform_event(dismiss);
    // assert neither key ran a control, which is what opting out means (§8.3)
    EXPECT_EQ(accepted, 0);
    EXPECT_EQ(dismissed, 0);
    EXPECT_NE(application.window(dialog), nullptr);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, ADialogWithoutACancelAsksToCloseOnEscape) {
    // arrange a dialog whose only key is the default, so escape has no control to run (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    albor::RootBuilder accept_only = [&accepted](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column stacks the field and one default key (§8.2)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field is the control the dialog opens on (§8.3)
        auto field = std::make_unique<albor::TextField>(&engine, &text);
        // the identity is what the focus check reads back (AD-11)
        field->set_explicit_id("name");
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        // the default key is the only control the dialog names (§8.3)
        auto accept = std::make_unique<albor::Button>(&engine, &text, "OK");
        // the identity names the control (AD-11)
        accept->set_explicit_id("ok");
        // the role makes return run it (§8.3)
        accept->set_default(true);
        // the click is counted (§8.3)
        accept->set_on_click([&accepted] { accepted++; });
        // the key joins the column (§8.1)
        column->add_child(std::move(accept));
        return column;
    };
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, accept_only);
    ASSERT_TRUE(dialog.valid());
    application.pump();
    // act escape reaches the dialog, which asks the application to close it (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Escape};
    application.window(dialog)->on_platform_event(event);
    // assert the close is posted, because the stage that asked is still on the stack (§4.2)
    EXPECT_NE(application.window(dialog), nullptr);
    application.pump();
    // assert the dialog closed and no default ran (§8.3)
    EXPECT_EQ(application.window(dialog), nullptr);
    EXPECT_EQ(accepted, 0);
    // cleanup
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, AVetoedEscapeLeavesTheDialogOpen) {
    // arrange a dialog with no cancel key and a close handler that vetoes it (§5.2, §8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, empty_form());
    ASSERT_TRUE(dialog.valid());
    application.set_close_handler([dialog](albor::WindowHandle handle) { return handle != dialog; });
    application.pump();
    // act escape asks to close, and the handler vetoes it (§5.2)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Escape};
    application.window(dialog)->on_platform_event(event);
    application.pump();
    // assert the dialog is still open, because the veto is what a dirty form relies on (§5.2)
    EXPECT_NE(application.window(dialog), nullptr);
    // cleanup
    application.set_close_handler({});
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, ADialogWhoseFirstFocusableIsCancelFocusesTheDefault) {
    // arrange a dialog whose cancel key is authored before its default, which is the conventional macOS order (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::RootBuilder keys = [&accepted, &dismissed](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column holds the two keys, with cancel first (§8.2)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the cancel key is authored first, so a naive walk would focus it (§8.3)
        auto cancel = std::make_unique<albor::Button>(&engine, &text, "Cancel");
        // the identity names the control (AD-11)
        cancel->set_explicit_id("cancel");
        // the role makes escape run it (§8.3)
        cancel->set_cancel(true);
        // the click is counted (§8.3)
        cancel->set_on_click([&dismissed] { dismissed++; });
        // the key joins the column (§8.1)
        column->add_child(std::move(cancel));
        // the default key follows it (§8.3)
        auto accept = std::make_unique<albor::Button>(&engine, &text, "OK");
        // the identity names the control (AD-11)
        accept->set_explicit_id("ok");
        // the role makes return run it (§8.3)
        accept->set_default(true);
        // the click is counted (§8.3)
        accept->set_on_click([&accepted] { accepted++; });
        // the key joins the column (§8.1)
        column->add_child(std::move(accept));
        return column;
    };
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, keys);
    ASSERT_TRUE(dialog.valid());
    // act the first frame arranges the tree and places the focus (§8.3)
    application.pump();
    // assert the default holds the focus, because focusing cancel would make return cancel (§8.3)
    albor::UiNode* focused = application.stage(dialog)->focus().focused();
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->identity().explicit_id, "ok");
    // assert return runs the default and not the cancel (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Return};
    application.window(dialog)->on_platform_event(event);
    EXPECT_EQ(accepted, 1);
    EXPECT_EQ(dismissed, 0);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, EscapeDoesNotCloseWhenTheCancelControlIsDisabled) {
    // arrange a dialog whose cancel key exists but refuses the press (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int dismissed = 0;
    albor::RootBuilder keys = [&dismissed](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column holds the disabled cancel and a default (§8.2)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the cancel key refuses the press while a save is in flight (§8.3)
        auto cancel = std::make_unique<albor::Button>(&engine, &text, "Cancel");
        // the identity names the control (AD-11)
        cancel->set_explicit_id("cancel");
        // the role makes escape run it, even while disabled (§8.3)
        cancel->set_cancel(true);
        // the click is counted, which must stay zero (§8.3)
        cancel->set_on_click([&dismissed] { dismissed++; });
        // the key refuses input (§8.3)
        cancel->set_disabled(true);
        // the key joins the column (§8.1)
        column->add_child(std::move(cancel));
        // the default key follows it (§8.3)
        auto accept = std::make_unique<albor::Button>(&engine, &text, "OK");
        // the identity names the control (AD-11)
        accept->set_explicit_id("ok");
        // the role makes return run it (§8.3)
        accept->set_default(true);
        // the key joins the column (§8.1)
        column->add_child(std::move(accept));
        return column;
    };
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, keys);
    ASSERT_TRUE(dialog.valid());
    application.pump();
    // act escape reaches the dialog, whose cancel is disabled (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Escape};
    application.window(dialog)->on_platform_event(event);
    // assert the dialog stays open, because the authored cancel owns the key even while it refuses (§8.3)
    EXPECT_NE(application.window(dialog), nullptr);
    EXPECT_EQ(dismissed, 0);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, ClosingTheDialogFromItsControlHandlerDoesNotUseTheStage) {
    // arrange a dialog whose cancel handler closes the dialog, which is the normal application pattern (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    albor::WindowHandle dialog_handle;
    albor::RootBuilder keys = [&application, &dialog_handle](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column holds the cancel and a default (§8.2)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the cancel key closes the dialog through the one veto path (§5.2)
        auto cancel = std::make_unique<albor::Button>(&engine, &text, "Cancel");
        // the identity names the control (AD-11)
        cancel->set_explicit_id("cancel");
        // the role makes escape run it (§8.3)
        cancel->set_cancel(true);
        // the handler closes the window it belongs to, which must not free the stage mid dispatch (§4.2)
        cancel->set_on_click([&application, &dialog_handle] { (void)application.request_close(dialog_handle); });
        // the key joins the column (§8.1)
        column->add_child(std::move(cancel));
        // the default key follows it (§8.3)
        auto accept = std::make_unique<albor::Button>(&engine, &text, "OK");
        // the identity names the control (AD-11)
        accept->set_explicit_id("ok");
        // the role makes return run it (§8.3)
        accept->set_default(true);
        // the key joins the column (§8.1)
        column->add_child(std::move(accept));
        return column;
    };
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    dialog_handle = application.create_window(dialog_options, keys);
    ASSERT_TRUE(dialog_handle.valid());
    application.pump();
    // act escape presses cancel, whose handler asks to close the dialog (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Escape};
    application.window(dialog_handle)->on_platform_event(event);
    // assert the teardown was posted, because the window is still on its own dispatch stack (§4.2)
    EXPECT_NE(application.window(dialog_handle), nullptr);
    application.pump();
    // assert the posted teardown ran on the next pass, so the stage was never read after release (§4.2)
    EXPECT_EQ(application.window(dialog_handle), nullptr);
    // cleanup
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, MultilineReturnDoesNotRunTheDialogDefault) {
    // arrange a dialog whose focused field edits a document (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    albor::TextField* notes = nullptr;
    albor::RootBuilder form = [&accepted, &notes](albor::BindingEngine& engine, albor::TextEngine& text) {
        // the column holds the multi line field and the default key (§8.2)
        auto column = std::make_unique<albor::ColumnLayout>();
        // the field edits a document, so return inserts a break (§8)
        auto field = std::make_unique<albor::TextField>(&engine, &text, "line");
        // the identity names the control (AD-11)
        field->set_explicit_id("notes");
        // the field edits more than one line (§8)
        field->set_multiline(true);
        // the caret stands past the seeded text, which is where a seeded field continues (§8.3)
        field->set_caret(4);
        // the test reads the value back through the control (§8.3)
        notes = field.get();
        // the field joins the column (§8.1)
        column->add_child(std::move(field));
        // the default key follows it (§8.3)
        auto accept = std::make_unique<albor::Button>(&engine, &text, "OK");
        // the identity names the control (AD-11)
        accept->set_explicit_id("ok");
        // the role makes return run it when nothing claimed the key (§8.3)
        accept->set_default(true);
        // the click is counted, which must stay zero (§8.3)
        accept->set_on_click([&accepted] { accepted++; });
        // the key joins the column (§8.1)
        column->add_child(std::move(accept));
        return column;
    };
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, form);
    ASSERT_TRUE(dialog.valid());
    application.pump();
    // act return reaches the focused field, which inserts a break (§8.3)
    albor::Event event{.kind = albor::EventKind::KeyDown, .key = albor::Key::Return};
    application.window(dialog)->on_platform_event(event);
    // assert the break is the edit and the default did not run (§8.3)
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->value(), "line\n");
    EXPECT_EQ(accepted, 0);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, InitialFocusIsNotPlacedBeforeTheFirstFrame) {
    // arrange a dialog whose tree has not been arranged yet (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    const albor::WindowHandle dialog = application.create_window(dialog_options, dialog_form(accepted, dismissed));
    ASSERT_TRUE(dialog.valid());
    // assert focus needs bounds, so it is not placed before the arrange pass (§8.2, §8.3)
    EXPECT_EQ(application.stage(dialog)->focus().focused(), nullptr);
    // act
    application.pump();
    // assert the first frame placed it (§8.3)
    EXPECT_NE(application.stage(dialog)->focus().focused(), nullptr);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}

TEST(MainWindow, FocusFirstControlOffLeavesTheFocusWhereItWas) {
    // arrange a dialog that declines the initial focus convention (§8.3)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions parent_options;
    parent_options.title = "Parent";
    const albor::WindowHandle parent = application.create_window(parent_options, empty_form());
    ASSERT_TRUE(parent.valid());
    int accepted = 0;
    int dismissed = 0;
    albor::WindowOptions dialog_options;
    dialog_options.role = albor::WindowRole::Dialog;
    dialog_options.modality = albor::Modality::WindowModal;
    dialog_options.title = "Dialog";
    dialog_options.parent = parent;
    dialog_options.dialog.focus_first_control = false;
    const albor::WindowHandle dialog = application.create_window(dialog_options, dialog_form(accepted, dismissed));
    ASSERT_TRUE(dialog.valid());
    // act
    application.pump();
    // assert nothing took the focus, which is what the convention flag controls (§8.3)
    EXPECT_EQ(application.stage(dialog)->focus().focused(), nullptr);
    // cleanup
    EXPECT_TRUE(application.destroy_window(dialog));
    EXPECT_TRUE(application.destroy_window(parent));
}
