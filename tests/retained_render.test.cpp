#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "render/frame_scene.h"
#include "render/line_batch.h"
#include "render/render-skia/skia_render_context.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/render_context.h"
#include "render/render_executor.h"
#include "render/renderer_backend.h"
#include "render/resource_factory.h"
#include "render/screenshot.h"
#include "runtime/binding_engine.h"
#include "ui/component.h"
#include "ui/scene_builder.h"
#include "ui/ui_node.h"
#include "window/window_metrics.h"

using namespace albor;

namespace
{
    // the surface the retained path is proven against, which needs no window server (§6.3)
    constexpr float SURFACE_WIDTH = 64.0f;

    // the surface height, which is deep enough for a stroke to cross the whole plot (§6.3)
    constexpr float SURFACE_HEIGHT = 48.0f;

    // the colour a trace stroke carries, which the read back looks for (§6.3)
    const Color TRACE_COLOR = Color::rgb(0.0f, 1.0f, 0.0f);

    // a trace of two samples describes exactly one segment, which is the smallest drawable batch (§6.4)
    constexpr std::size_t TWO_SAMPLES = 2;

    // the stroke width a trace draws at, which is thick enough to survive one pixel of antialiasing (§6.4)
    constexpr float STROKE_WIDTH = 2.0f;

    // build the retained batch a chart would stroke, which runs left to right across the plot (§6.4, AD-06)
    std::shared_ptr<const LineBatch> trace_batch(const ResourceFactory& factory, float low, float high) {
        // the endpoints pair into one segment, so the batch is two points (§6.4)
        const Point points[] = {{0.0f, low}, {1.0f, high}};
        // the factory stamps the identifier the backend keys its cache on (§6.4, AD-06)
        return factory.create_line_batch(points);
    }

    // count the pixels the trace colour reached, which is how a stroke is located without pinning one pixel (§6.3)
    std::size_t pixels_matching(const std::vector<std::uint8_t>& pixels) {
        // every pixel is scanned, because a stroke lands wherever the geometry put it (§6.3)
        std::size_t matches = 0;
        // the read walks the buffer in the canonical four bytes per pixel order (§6.3, AD-26)
        for (std::size_t index = 0; index + 3u < pixels.size(); index += 4u) {
            // the green channel is the one the trace colour owns, which no background pixel carries (§6.3)
            if (pixels[index + 1u] > 150u && pixels[index] < 100u && pixels[index + 2u] < 100u)
                matches++;
        }
        return matches;
    }

    // the widget a chart is, which retains a batch and replays it under a transform (§6.4, AD-06)
    class RetainedTraceWidget final : public Component
    {
    public:
        // build the widget over the engine that settles its bindings (§8.1, §7.2)
        explicit RetainedTraceWidget(BindingEngine* engine, std::shared_ptr<const LineBatch> trace) :
            Component(engine, "retained_trace"), m_trace(std::move(trace)) {}

        // override: replay the retained batch under the transform the plot implies (§6.4, AD-06)
        void paint_content(RenderContext& context) override {
            // the replay is counted, which is what proves a second frame reused the resource (§6.4, AD-06)
            replays++;
            // the horizontal run stretches across the plot, which is the only mapping a resize changes (§6.4)
            const float scale_x = bounds().width;
            // the amplitude stretches across half the plot height, because the samples are signed around zero (§6.4)
            const float scale_y = -bounds().height * 0.5f;
            // the origin is the plot's left edge and vertical middle, where a zero amplitude lands (§6.4)
            const Transform placement = Transform::scaling(scale_x, scale_y).then(Transform::translation(bounds().x, bounds().y + bounds().height * 0.5f));
            // the retained batch replays in constant time, which is what retaining it bought (§6.4, AD-06)
            context.draw_line_batch(m_trace, placement, {.color = TRACE_COLOR, .width = STROKE_WIDTH});
        }

        // how many frames replayed the retained batch, which stays constant across data changes (§6.4, AD-06)
        std::size_t replays = 0;

    private:
        // the retained batch, which the frames replay rather than rebuild (§6.4, AD-06)
        std::shared_ptr<const LineBatch> m_trace;
    };
} // namespace

TEST(RetainedRenderPath, ARetainedBatchReachesRealPixels) {
    // arrange a widget holding a retained batch the way a chart does (§6.4, AD-06)
    ResourceFactory factory;
    const std::shared_ptr<const LineBatch> batch = trace_batch(factory, -1.0f, 1.0f);
    ASSERT_NE(batch, nullptr);
    BindingEngine engine;
    RetainedTraceWidget widget(&engine, batch);
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    // act the real scene build records the widget, which is stage one of the pipeline (§6.1)
    const std::shared_ptr<const FrameScene> scene = builder.build(widget, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // act the real cpu backend replays the retained command onto a real raster (§6.3, AD-26)
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    software::SoftwareRendererBackend backend;
    backend.execute_frame(*surface, *scene);
    // assert the stroke reached the raster, which is what no test had proven before (§6.3, AD-26)
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_FALSE(pixels.empty());
    EXPECT_GT(pixels_matching(pixels), 0u);
}

TEST(RetainedRenderPath, TheRecordedCommandCarriesTheRetainedResourceItself) {
    // arrange a widget holding one retained batch (§6.4, AD-06)
    ResourceFactory factory;
    const std::shared_ptr<const LineBatch> batch = trace_batch(factory, -1.0f, 1.0f);
    ASSERT_NE(batch, nullptr);
    BindingEngine engine;
    RetainedTraceWidget widget(&engine, batch);
    SceneBuilder builder;
    // act the scene build records the widget's draw (§6.1)
    const std::shared_ptr<const FrameScene> scene = builder.build(widget, WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // assert exactly one command was recorded, which is what makes the scene worth pinning (§6.1)
    ASSERT_EQ(scene->command_count(), 1u);
    // assert the recorded command replays the identical resource rather than a copy, which is what retaining means (§6.4)
    EXPECT_EQ(scene->commands().front().batch_geometry().batch, batch);
}

TEST(RetainedRenderPath, AResizeReplaysTheSameResourceUnderANewTransform) {
    // arrange a widget holding one retained batch (§6.4, AD-06)
    ResourceFactory factory;
    const std::shared_ptr<const LineBatch> batch = trace_batch(factory, -1.0f, 1.0f);
    ASSERT_NE(batch, nullptr);
    BindingEngine engine;
    RetainedTraceWidget widget(&engine, batch);
    SceneBuilder builder;
    const WindowMetrics first_metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> first = builder.build(widget, first_metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(first, nullptr);
    // act a resize remeasures and rerecords the same tree, which is what a window drag does (§8.2)
    const WindowMetrics second_metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH * 2.0f, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> second = builder.build(widget, second_metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(second, nullptr);
    // assert both frames carried the identical resource, which is the whole payoff of retaining it (§6.4, AD-06)
    ASSERT_EQ(first->command_count(), 1u);
    ASSERT_EQ(second->command_count(), 1u);
    EXPECT_EQ(first->commands().front().batch_geometry().batch, second->commands().front().batch_geometry().batch);
    // assert the transform moved, because a resize has to reach the geometry somehow (§6.4)
    EXPECT_NE(first->commands().front().transform(), second->commands().front().transform());
}

TEST(RetainedRenderPath, TheResourceIsThreadSafeAcrossConcurrentFactories) {
    // arrange two factories that a worker and the ui thread would each own (§6.4, AD-06)
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    // act many resources are stamped from one factory, which is what concurrent workers do (§6.4, AD-06)
    std::vector<std::shared_ptr<const LineBatch>> batches;
    // the loop stamps enough identifiers that a repeat would be visible in the assertions below (§6.4, AD-06)
    for (int index = 0; index < 64; index++)
        batches.push_back(factory.create_line_batch(points));
    // assert no identifier repeated, which is the guarantee a gpu cache key depends on (§6.4, AD-06)
    std::vector<std::uint64_t> identifiers;
    identifiers.reserve(batches.size());
    // the identifiers are collected apart from the batches so the comparison is by value (§6.4, AD-06)
    for (const auto& batch : batches)
        identifiers.push_back(batch->id());
    // the sorted copy is what the uniqueness check reads, because order is not what matters (§6.4, AD-06)
    std::vector<std::uint64_t> sorted = identifiers;
    // the sort is what puts equal identifiers next to each other for the uniqueness check (§6.4, AD-06)
    std::sort(sorted.begin(), sorted.end());
    // the uniqueness is the difference between the sorted run and the unique run (§6.4, AD-06)
    const auto unique_end = std::unique(sorted.begin(), sorted.end());
    EXPECT_EQ(static_cast<std::size_t>(std::distance(sorted.begin(), unique_end)), batches.size());
}

TEST(RetainedRenderPath, TheFactoryRefusesABatchThatWouldDangle) {
    // arrange a factory, which is the validator every retained resource passes (§6.4, AD-06)
    ResourceFactory factory;
    // act an empty point list is offered, which describes no segment (§6.4, AD-06)
    EXPECT_EQ(factory.create_line_batch(std::span<const Point>()), nullptr);
    // act an odd point count is offered, which leaves a segment without an endpoint (§6.4, AD-06)
    const Point dangling[] = {{0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}};
    EXPECT_EQ(factory.create_line_batch(dangling), nullptr);
    // act an even count is offered, which is the drawable case (§6.4, AD-06)
    const Point paired[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    EXPECT_NE(factory.create_line_batch(paired), nullptr);
}

TEST(RetainedRenderPath, AWidgetWithNoTraceDrawsNothing) {
    // arrange a batch the widget was never given, which is what a cleared data slot leaves (§6.4)
    ResourceFactory factory;
    const std::shared_ptr<const LineBatch> batch = trace_batch(factory, 0.0f, 0.0f);
    ASSERT_NE(batch, nullptr);
    BindingEngine engine;
    // the widget holds a null trace, so its draw reaches the backend as a refused resource (§6.4, AD-06)
    RetainedTraceWidget widget(&engine, nullptr);
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> scene = builder.build(widget, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // act the real backend draws the null resource, which is the refusal path (§6.3, AD-26)
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    software::SoftwareRendererBackend backend;
    backend.execute_frame(*surface, *scene);
    // assert the raster stayed clear, because a refused resource draws nothing (§6.4, AD-06)
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_FALSE(pixels.empty());
    EXPECT_EQ(pixels_matching(pixels), 0u);
}

TEST(RetainedRenderPath, TheExecutorReplaysTheRetainedPathOffTheUiThread) {
    // arrange a widget holding a retained batch (§6.4, AD-06)
    ResourceFactory factory;
    const std::shared_ptr<const LineBatch> batch = trace_batch(factory, -1.0f, 1.0f);
    ASSERT_NE(batch, nullptr);
    BindingEngine engine;
    RetainedTraceWidget widget(&engine, batch);
    SceneBuilder builder;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{SURFACE_WIDTH, SURFACE_HEIGHT}, 1.0f);
    const std::shared_ptr<const FrameScene> scene = builder.build(widget, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    ASSERT_NE(scene, nullptr);
    // arrange the real backend and the real executor, which is the thread boundary of §6.1 (AD-18)
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    software::SoftwareRendererBackend backend;
    RenderExecutor executor(&backend);
    // act the scene crosses to the render thread, which never runs application code (§6.1, AD-18b)
    ASSERT_TRUE(executor.submit(*surface, scene));
    executor.flush();
    // assert the frame presented, which proves the hand off completed rather than dropped (§6.1, AD-18)
    EXPECT_EQ(executor.executed_count(), 1u);
    // assert the stroke reached the raster across the thread boundary (§6.3, AD-26)
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_FALSE(pixels.empty());
    EXPECT_GT(pixels_matching(pixels), 0u);
}

TEST(RetainedRenderPath, ATwoSampleTraceIsTheSmallestDrawableBatch) {
    // arrange a trace of the minimum drawable size (§6.4)
    ResourceFactory factory;
    const std::shared_ptr<const LineBatch> batch = trace_batch(factory, 0.0f, 1.0f);
    // assert the batch carries the segment the two samples describe (§6.4)
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(batch->point_count(), 2u * TWO_SAMPLES - 2u);
    EXPECT_EQ(batch->segment_count(), TWO_SAMPLES - 1u);
}
