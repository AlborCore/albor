#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "inspector/scene_inspector.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/screenshot.h"

using namespace albor;

namespace
{
    // the metrics every suite presents at, which is what a surface is measured against (§6.1)
    WindowMetrics metrics() { return WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f); }

    // a scene holding two rectangles, which is what a frame usually carries (§6.1)
    FrameScene scene_with_two_rects() {
        // the first rectangle sits near the top left (§6.2)
        std::vector<Rect> rects;
        // the rectangles are listed, because the primitive factory builds one command each (§6.1)
        rects.push_back(Rect{.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f});
        // the second sits lower and further right (§6.2)
        rects.push_back(Rect{.x = 100.0f, .y = 50.0f, .width = 60.0f, .height = 30.0f});
        // the frame is built over them, which is the ordinary hand drawn shape (§6.1)
        return FrameScene::from_primitives(metrics(), Color::rgb(0.0f, 0.0f, 0.0f), std::move(rects));
    }
} // namespace

TEST(SceneInspectorChecks, AFrameIsCountedByKind) {
    // arrange
    const FrameScene scene = scene_with_two_rects();
    // act
    const inspector::SceneReport report = inspector::inspect(scene);
    // assert the total matches what was recorded (§6.1)
    EXPECT_EQ(report.commands, 2U);
    // assert the rectangle count is what the frame holds (§12.2)
    EXPECT_EQ(report.per_kind.at(static_cast<std::size_t>(DrawCommandKind::Rect)), 2U);
}

TEST(SceneInspectorChecks, TheCoveredAreaSpansEveryCommand) {
    // arrange
    const FrameScene scene = scene_with_two_rects();
    // act
    const inspector::SceneReport report = inspector::inspect(scene);
    // assert the extent runs from the first rectangle's origin to the second's far edge (§6.2)
    EXPECT_FLOAT_EQ(report.covered.x, 10.0f);
    EXPECT_FLOAT_EQ(report.covered.y, 10.0f);
    EXPECT_FLOAT_EQ(report.covered.width, 150.0f);
    EXPECT_FLOAT_EQ(report.covered.height, 70.0f);
}

TEST(SceneInspectorChecks, AFrameThatDrawsNothingIsReported) {
    // arrange a frame with no command, which is legal but usually a mistake (§6.1)
    const FrameScene scene(metrics(), Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    const inspector::SceneReport report = inspector::inspect(scene);
    // assert the finding names the rule, so a developer can search for it (§12.2)
    ASSERT_EQ(report.findings.size(), 1U);
    EXPECT_EQ(report.findings.at(0).code, "I0001");
}

TEST(SceneInspectorChecks, ACommandOffTheSurfaceIsReported) {
    // arrange a frame whose rectangle lies beyond the surface (§6.2)
    std::vector<Rect> off_surface;
    // the rectangle is placed past the right edge, which can never land (§6.2)
    off_surface.push_back(Rect{.x = 900.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f});
    // the frame is built over it (§6.1)
    const FrameScene scene = FrameScene::from_primitives(metrics(), Color::rgb(0.0f, 0.0f, 0.0f), std::move(off_surface));
    // act
    const inspector::SceneReport report = inspector::inspect(scene);
    // assert the off screen command is named, because it is usually an unnoticed mistake (§12.2)
    ASSERT_EQ(report.findings.size(), 1U);
    EXPECT_EQ(report.findings.at(0).code, "I0003");
}

TEST(SceneInspectorChecks, ACommandOnTheSurfaceIsNotReported) {
    // arrange
    const FrameScene scene = scene_with_two_rects();
    // act
    const inspector::SceneReport report = inspector::inspect(scene);
    // assert a frame with nothing wrong reports nothing, because a debugger that always complains is noise (§12.2)
    EXPECT_TRUE(report.findings.empty());
}

TEST(BoundsOverlayChecks, TheOverlayDrawsFourEdgesPerCommand) {
    // arrange
    const FrameScene scene = scene_with_two_rects();
    // act
    const std::shared_ptr<const FrameScene> overlay = inspector::bounds_overlay(scene, Color::rgb(1.0f, 0.0f, 1.0f));
    // assert two commands contribute four edges each (§12.2)
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->command_count(), 8U);
}

TEST(BoundsOverlayChecks, TheOverlayPutsInkOnTheSurface) {
    // arrange
    const FrameScene scene = scene_with_two_rects();
    // act the overlay renders through the ordinary software path, because it is an ordinary scene (§6.1)
    const std::unique_ptr<software::SoftwareRenderSurface> surface = software::SoftwareRenderSurface::create(metrics());
    software::SoftwareRendererBackend backend;
    const std::shared_ptr<const FrameScene> overlay = inspector::bounds_overlay(scene, Color::rgb(1.0f, 0.0f, 1.0f));
    ASSERT_NE(overlay, nullptr);
    backend.execute_frame(*surface, *overlay);
    // assert a pixel is lit, because an overlay nobody can see is not a visualizer (§12.2)
    const Screenshot drawn = capture_surface(*surface);
    ASSERT_FALSE(drawn.empty());
    bool any_lit = false;
    for (std::uint8_t channel : drawn.pixels) {
        if (channel != 0) {
            any_lit = true;
            break;
        }
    }
    EXPECT_TRUE(any_lit);
}
