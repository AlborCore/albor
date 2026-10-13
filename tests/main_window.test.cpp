#include <memory>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "application/application.h"
#include "application/timer.h"
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
                m_handle = animation_engine()->schedule_repeating([](float) { return true; }, albor::AnimationDuration{16});
        }

        // the handle the schedule handed out, which zero still means (§7.3)
        albor::AnimationHandle m_handle = 0;
    };

    // the builder that hands the application the animated leaf, which the window owns on its behalf (§8.1)
    albor::RootBuilder animated_form() {
        return [](albor::BindingEngine&, albor::TextEngine&) { return std::make_unique<AnimatedLeaf>(); };
    }
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

TEST(MainWindow, AMainWindowIsDrivenByEveryPumpedTick) {
    // arrange the factories and one main window (§6.1)
    InstalledScope scope;
    albor::Application& application = albor::Application::instance();
    albor::WindowOptions options;
    options.title = "Main Window";
    const albor::WindowHandle handle = application.create_window(options, empty_form());
    ASSERT_TRUE(handle.valid());
    // act pump two ticks, which drive the window twice (§6.1)
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
    // act pump the remaining window through another tick (§6.1)
    const std::uint64_t built_before = application.frame_count(second_handle);
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
