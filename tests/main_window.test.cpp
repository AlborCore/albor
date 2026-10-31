#include <memory>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "application/application.h"
#include "application/window_stage.h"
#include "core/dev_hook.h"
#include "layout/column_layout.h"
#include "platform/headless/headless_event_loop.h"
#include "render/render-software/software_renderer_backend.h"
#include "text/text_engine.h"
#include "window/platform_window_factory.h"

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
