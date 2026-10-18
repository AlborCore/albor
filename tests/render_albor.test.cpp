#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "core/paint.h"
#include "render/frame_scene.h"
#include "render/path_resource.h"
#include "render/render-albor/albor_batch.h"
#include "render/render-albor/albor_contour.h"
#include "render/render-albor/albor_geometry.h"
#include "render/render-albor/albor_renderer_backend.h"
#include "render/render_context.h"
#include "render/renderer_backend.h"
#include "window/window_metrics.h"

using namespace albor;
using namespace albor::render;

namespace
{
    // the smallest context a surface can hand out, which the device-less shell never draws into (§6.2)
    class ShellRenderContext final : public RenderContext
    {
    public:
        void save() override {}
        void restore() override {}

        void set_transform(const Transform& transform) override {
            // record the transform the shell is asked to adopt (§6.2)
            m_transform = transform;
        }

        [[nodiscard]] const Transform& transform() const override { return m_transform; }

        void clip_rect(const Rect& rect) override {
            // record the axis aligned clip the shell is asked to adopt (§6.2)
            m_clip = rect;
        }

        void clip_rounded_rect(const Rect& rect, float) override {
            // record the rounded clip the shell is asked to adopt (§6.2)
            m_clip = rect;
        }

        [[nodiscard]] const Rect& clip() const override { return m_clip; }

        void set_opacity(float opacity) override {
            // record the opacity the shell is asked to adopt (§6.1)
            m_opacity = opacity;
        }

        [[nodiscard]] float opacity() const override { return m_opacity; }

        void draw_rect(const Rect&, const Paint&) override {}

        void draw_rounded_rect(const Rect&, float, const Paint&) override {}

        void draw_polyline(const std::span<const Point>&, const StrokeStyle&) override {}

        void draw_shadow(const Rect&, float, const Shadow&) override {}

        void draw_text(const TextLayout&, Point, const Paint&) override {}

        void draw_line_batch(const std::shared_ptr<const LineBatch>&, const Transform&, const StrokeStyle&) override {}

        void draw_path(const std::shared_ptr<const PathResource>&, const Transform&, const Paint&) override {}

        void draw_picture(const std::shared_ptr<const PictureResource>&, const Transform&) override {}

        void draw_image(const std::shared_ptr<const ImageResource>&, const Rect&, const Paint&) override {}

    private:
        // the transform the shell last adopted (§6.2)
        Transform m_transform;

        // the clip the shell last adopted (§6.2)
        Rect m_clip;

        // the opacity the shell last adopted, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;
    };

    // the surface test double, which reports itself unusable and counts the frames the shell never begins (§6.2)
    class ShellRenderSurface final : public RenderSurface
    {
    public:
        // how many frames the shell began, which stays zero because it refuses stage two (§6.1)
        std::size_t begun = 0;

        // how many resizes the shell asked for, which stays zero because it owns no surface (§6.2)
        std::size_t resizes = 0;

        [[nodiscard]] bool valid() const override { return false; }

        [[nodiscard]] const WindowMetrics& metrics() const override { return m_metrics; }

        void resize(const WindowMetrics& metrics) override {
            // count the resize, so a test proves the shell never reallocates a surface (§6.2)
            resizes++;
            // record the extent the shell is asked to present at (§6.1)
            m_metrics = metrics;
        }

        RenderContext& begin_frame() override {
            // count the frame, so a test proves the shell never replays a scene (§6.1)
            begun++;
            return m_context;
        }

        void end_frame() override {}

    private:
        // the metrics the surface reports, which start at the test extent (§6.1)
        WindowMetrics m_metrics = WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f);

        // the context the surface hands out, which the shell never draws into (§6.2)
        ShellRenderContext m_context;
    };
} // namespace

TEST(AlborRendererBackendChecks, ReportsNotReadyBeforeTheDeviceLands) {
    // arrange
    albor::render::AlborRendererBackend backend;
    // act, assert
    EXPECT_FALSE(backend.ready());
}

TEST(AlborRendererBackendChecks, RefusesASurfaceBeforeTheDeviceLands) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const int window_object = 0;
    const NativeWindowHandle handle = NativeWindowHandle::from_object(&window_object);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{320.0f, 240.0f}, 2.0f);
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(handle, metrics);
    // assert
    EXPECT_EQ(surface, nullptr);
}

TEST(AlborRendererBackendChecks, ResizesNothingBeforeTheDeviceLands) {
    // arrange
    albor::render::AlborRendererBackend backend;
    ShellRenderSurface surface;
    const WindowMetrics before = surface.metrics();
    const WindowMetrics resized = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    // act
    backend.resize_surface(surface, resized);
    // assert
    EXPECT_EQ(surface.resizes, 0u);
    EXPECT_EQ(surface.metrics(), before);
}

TEST(AlborRendererBackendChecks, RefusesAnInvalidHandleBeforeTheDeviceLands) {
    // arrange
    albor::render::AlborRendererBackend backend;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{320.0f, 240.0f}, 2.0f);
    // act
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(NativeWindowHandle{}, metrics);
    // assert
    EXPECT_EQ(surface, nullptr);
}

TEST(AlborRendererBackendChecks, ExecutesNothingBeforeTheDeviceLands) {
    // arrange
    albor::render::AlborRendererBackend backend;
    ShellRenderSurface surface;
    const FrameScene scene(WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    backend.execute_frame(surface, scene);
    // assert
    EXPECT_EQ(surface.begun, 0u);
}

TEST(AlborRendererBackendFactoryChecks, InstallsTheInRepoBackend) {
    // arrange
    set_renderer_backend_factory(nullptr);
    albor::render::install_albor_renderer_backend();
    // act
    const std::unique_ptr<RendererBackend> backend = create_renderer_backend();
    // assert
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->ready());
    // the shared factory returns to empty, so no later suite inherits this backend (§6.2)
    set_renderer_backend_factory(nullptr);
}

TEST(AlborRendererBackendFactoryChecks, ReplacesAnAlreadyInstalledFactory) {
    // arrange a foreign factory that builds nothing, which the in-repo installer must replace (§6.2)
    set_renderer_backend_factory([] { return std::unique_ptr<RendererBackend>(); });
    albor::render::install_albor_renderer_backend();
    // act
    const std::unique_ptr<RendererBackend> backend = create_renderer_backend();
    // assert
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->ready());
    // the shared factory returns to empty, so no later suite inherits this backend (§6.2)
    set_renderer_backend_factory(nullptr);
}

TEST(ContourChecks, BuildsOneClosedContourFromAClosedSquare) {
    // arrange
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[3] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 10.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_TRUE(contours[0].closed);
    EXPECT_EQ(contours[0].points.size(), 3u);
}

TEST(ContourChecks, NormalizesTheWindingToAPositiveArea) {
    // arrange a square recorded against the winding the expansion normalizes away
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 0.0f, .y = 10.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_GT(polygon_area(contours[0].points), 0.0f);
}

TEST(ContourChecks, LeavesAnOpenContourOpen) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_FALSE(contours[0].closed);
    EXPECT_EQ(contours[0].points.size(), 2u);
}

TEST(ContourChecks, StampsUnitDirectionsAlongTheLeavingSegments) {
    // arrange a square recorded in the winding the normalization keeps
    const PathVerb verbs[5] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 0.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    // the first point leaves downward and the second leaves across, which the wrap closes back up
    EXPECT_NEAR(contours[0].points[0].dx, 0.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[0].dy, 1.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[1].dx, 1.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[1].dy, 0.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[2].dx, 0.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[2].dy, -1.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[3].dx, -1.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[3].dy, 0.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[0].length, 10.0f, 0.0001f);
}

TEST(JoinChecks, MarksASquareConvex) {
    // arrange
    Contour contour = rect_contour(Rect{.x = 0.0f, .y = 0.0f, .width = 10.0f, .height = 10.0f});
    // act
    calculate_joins(contour, 1.0f, Join::Miter, 4.0f);
    // assert
    EXPECT_TRUE(contour.convex);
    // every point of a square turns left, which the winding normalization guarantees
    for (const ContourPoint& point : contour.points)
        EXPECT_TRUE((point.flags & Left) != 0u);
}

TEST(JoinChecks, MarksAnLShapeConcave) {
    // arrange an L whose corner at the notch turns against the others
    const PathVerb verbs[6] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[5] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 4.0f, .y = 10.0f}, Point{.x = 4.0f, .y = 4.0f}, Point{.x = 10.0f, .y = 4.0f}};
    // act
    std::vector<Contour> contours = build_contours(verbs, points);
    ASSERT_EQ(contours.size(), 1u);
    calculate_joins(contours[0], 1.0f, Join::Miter, 4.0f);
    // assert
    EXPECT_FALSE(contours[0].convex);
}

TEST(TransformInverseChecks, InvertsAScaleAndTranslationRoundTrip) {
    // arrange
    const Transform transform{.scale_x = 2.0f, .skew_x = 0.0f, .translate_x = 5.0f, .skew_y = 0.0f, .scale_y = 3.0f, .translate_y = 7.0f};
    const Point point{.x = 3.0f, .y = 4.0f};
    // act
    const Transform inverse = invert_transform(transform);
    // assert
    const Point mapped = transform.apply(point);
    const Point restored = inverse.apply(mapped);
    EXPECT_NEAR(restored.x, point.x, 0.0001f);
    EXPECT_NEAR(restored.y, point.y, 0.0001f);
}

TEST(TransformInverseChecks, InvertsASkewRoundTrip) {
    // arrange a transform with both skews and a translation
    const Transform transform{.scale_x = 1.5f, .skew_x = 0.25f, .translate_x = 3.0f, .skew_y = -0.5f, .scale_y = 2.0f, .translate_y = -4.0f};
    const Point point{.x = 7.0f, .y = -2.0f};
    // act
    const Transform inverse = invert_transform(transform);
    // assert
    const Point mapped = transform.apply(point);
    const Point restored = inverse.apply(mapped);
    EXPECT_NEAR(restored.x, point.x, 0.0001f);
    EXPECT_NEAR(restored.y, point.y, 0.0001f);
}

TEST(TransformInverseChecks, InvertsASingularToTheIdentity) {
    // arrange a transform whose linear part cannot invert
    const Transform transform{.scale_x = 0.0f, .skew_x = 0.0f, .translate_x = 5.0f, .skew_y = 0.0f, .scale_y = 0.0f, .translate_y = 7.0f};
    // act
    const Transform inverse = invert_transform(transform);
    // assert
    EXPECT_EQ(inverse, Transform{});
}

TEST(CurveDivsChecks, GrowsSegmentsWithTheRadius) {
    // act
    const int small = curve_divs(1.0f, 3.14159265358979f, 0.25f);
    const int large = curve_divs(50.0f, 3.14159265358979f, 0.25f);
    // assert
    EXPECT_GT(large, small);
}

TEST(RectContourChecks, WindsAPositiveArea) {
    // arrange
    const Contour contour = rect_contour(Rect{.x = 1.0f, .y = 2.0f, .width = 20.0f, .height = 10.0f});
    // act, assert
    EXPECT_EQ(contour.points.size(), 4u);
    EXPECT_TRUE(contour.closed);
    EXPECT_GT(polygon_area(contour.points), 0.0f);
}

TEST(RoundedRectContourChecks, StaysInsideTheRectangle) {
    // arrange
    const Contour contour = rounded_rect_contour(Rect{.x = 1.0f, .y = 2.0f, .width = 20.0f, .height = 10.0f}, 3.0f, 0.25f);
    // act, assert
    EXPECT_TRUE(contour.closed);
    // every arc point sits inside the rectangle the corners round
    for (const ContourPoint& point : contour.points) {
        EXPECT_GE(point.position.x, 1.0f);
        EXPECT_LE(point.position.x, 21.0f);
        EXPECT_GE(point.position.y, 2.0f);
        EXPECT_LE(point.position.y, 12.0f);
    }
}

TEST(RoundedRectContourChecks, WindsAPositiveArea) {
    // arrange
    const Contour contour = rounded_rect_contour(Rect{.x = 1.0f, .y = 2.0f, .width = 20.0f, .height = 10.0f}, 3.0f, 0.25f);
    // act, assert
    EXPECT_GT(polygon_area(contour.points), 0.0f);
}

TEST(ExpandFillChecks, InsetsTheRingAndExpandsTheFringeOfASquare) {
    // arrange
    Contour contour = rect_contour(Rect{.x = 0.0f, .y = 0.0f, .width = 10.0f, .height = 10.0f});
    FrameBatch batch;
    // act
    const EmittedRange range = expand_fill(batch, contour);
    // assert
    EXPECT_GT(range.index_count, 0u);
    ASSERT_EQ(contour.convex, true);
    // the fan covers the four inset ring vertices, which the fringe strip then runs beside
    ASSERT_EQ(batch.vertices.size(), 14u);
    EXPECT_NEAR(batch.vertices[0].x, 0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[0].y, 0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[0].lateral, 1.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[1].x, 0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[1].y, 9.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].x, 9.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].y, 9.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[3].x, 9.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[3].y, 0.5f, 0.0001f);
    // the fringe crosses from the inset ring to half a fringe outside, per corner
    EXPECT_NEAR(batch.vertices[4].x, 0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].y, 0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].lateral, 1.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[5].x, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[5].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[5].lateral, 0.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[13].x, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[13].y, -0.5f, 0.0001f);
    // the fan over the four ring vertices and the eight fringe triangles carry thirty indices
    EXPECT_EQ(batch.indices.size(), 30u);
    // the fringe midpoints sit on the original boundary, which the coverage ramps across
    EXPECT_NEAR((batch.vertices[4].x + batch.vertices[5].x) * 0.5f, 0.0f, 0.0001f);
}

TEST(ExpandFillChecks, RefusesAContourWithoutARing) {
    // arrange an open two point contour, which no fill can cover
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const EmittedRange range = expand_fill(batch, contours[0]);
    // assert
    EXPECT_EQ(range.index_count, 0u);
    EXPECT_EQ(batch.vertices.size(), 0u);
    EXPECT_EQ(batch.indices.size(), 0u);
}

TEST(ExpandStrokeChecks, WrapsARoundCappedLineInTheFringe) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Round, Join::Round, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // two four segment round caps close over the two point body, which the strip indices span
    EXPECT_EQ(batch.vertices.size(), 20u);
    EXPECT_EQ(batch.indices.size(), (20u - 2u) * 3u);
    // the stroke band plus its fringe stays inside the true arc bounds, which the chord approximation sags within
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_GE(min_x, -0.51f);
    EXPECT_LE(max_x, 14.51f);
    EXPECT_GE(min_y, -0.51f);
    EXPECT_LE(max_y, 4.51f);
    // the caps reach past the end points, which the straight body alone never would
    EXPECT_LT(min_x, 2.0f);
    EXPECT_GT(max_x, 12.0f);
}

TEST(ExpandStrokeChecks, EmulatesTheCoverageOfAThinStroke) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 0.25f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 0.5f, 0.0001f);
}

TEST(ExpandStrokeChecks, RampsTheLongitudinalCoverageAcrossAButtCap) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    ASSERT_GE(batch.vertices.size(), 8u);
    EXPECT_NEAR(batch.vertices[0].x, 1.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[0].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[0].longitudinal, 0.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[1].x, 1.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[1].y, 4.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[1].longitudinal, 0.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].x, 2.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].longitudinal, 1.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[3].x, 2.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[3].y, 4.5f, 0.0001f);
    // the end cap mirrors it past the far end
    EXPECT_NEAR(batch.vertices[4].x, 11.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].longitudinal, 1.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[6].x, 12.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[6].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[6].longitudinal, 0.0f, 0.0001f);
}

TEST(PackColorChecks, PremultipliesAndPacksRedFirst) {
    // act
    const std::uint32_t packed = pack_color(Color::rgba(1.0f, 0.0f, 0.0f, 0.5f));
    // assert
    EXPECT_EQ(packed, 0x80000080u);
}

TEST(PackColorChecks, PacksAnOpaqueGreenIntoTheGreenByte) {
    // act
    const std::uint32_t packed = pack_color(Color::rgb(0.0f, 1.0f, 0.0f));
    // assert
    EXPECT_EQ(packed, 0xFF00FF00u);
}

TEST(ContourChecks, DropsATrailingPointThatRepeatsTheStart) {
    // arrange a square whose close repeats the first point
    const PathVerb verbs[5] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 0.0f, .y = 0.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_TRUE(contours[0].closed);
    EXPECT_EQ(contours[0].points.size(), 3u);
}

TEST(ContourChecks, OpensAContourForALeadingLine) {
    // arrange a verb stream that lines before any move
    const PathVerb verbs[2] = {PathVerb::LineTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 5.0f, .y = 5.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_EQ(contours[0].points.size(), 2u);
}

TEST(ContourChecks, ReportsNoAreaUnderThreePoints) {
    // arrange an open two point run
    const std::vector<ContourPoint> points(2);
    // act, assert
    EXPECT_EQ(polygon_area(points), 0.0f);
}

TEST(ContourChecks, IgnoresVerbsWithoutPoints) {
    // arrange verbs whose point list is empty
    const PathVerb verbs[3] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::Close};
    const std::span<const Point> points;
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    ASSERT_EQ(contours.size(), 1u);
    EXPECT_TRUE(contours[0].points.empty());
    EXPECT_TRUE(contours[0].closed);
}

TEST(ContourChecks, SealsNothingWithoutAContour) {
    // arrange a close that follows no move
    const PathVerb verbs[1] = {PathVerb::Close};
    const std::span<const Point> points;
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    EXPECT_EQ(contours.size(), 0u);
}

TEST(ContourChecks, StampsCornerOnEveryRecordedPoint) {
    // arrange a closed triangle
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[3] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 10.0f}};
    // act
    const std::vector<Contour> contours = build_contours(verbs, points);
    // assert
    for (const ContourPoint& point : contours[0].points)
        // every recorded point corners at, which the join calculation bevels from
        EXPECT_TRUE((point.flags & Corner) != 0u);
}

TEST(ExpandFillChecks, SplitsASharpSpikeCorner) {
    // arrange a spike whose notch turns past the fill miter limit on the right hand side
    const PathVerb verbs[5] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 5.0f, .y = 9.0f}, Point{.x = 5.0f, .y = 2.0f}, Point{.x = 8.0f, .y = 6.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    ASSERT_EQ(contours.size(), 1u);
    FrameBatch batch;
    // act
    const EmittedRange range = expand_fill(batch, contours[0]);
    // assert
    EXPECT_GT(range.index_count, 0u);
    EXPECT_FALSE(contours[0].convex);
    // the notch corner splits into the two edge insets, which the averaged normal never reaches
    bool found_first = false;
    bool found_second = false;
    for (const BatchVertex& vertex : batch.vertices) {
        if (std::abs(vertex.x - 4.5f) < 0.001f && std::abs(vertex.y - 2.0f) < 0.001f)
            found_first = true;
        if (std::abs(vertex.x - 5.4f) < 0.001f && std::abs(vertex.y - 1.7f) < 0.001f)
            found_second = true;
    }
    EXPECT_TRUE(found_first);
    EXPECT_TRUE(found_second);
}

TEST(ExpandStrokeChecks, StitchesASmallSquareThroughItsInnerBevels) {
    // arrange a square whose half width dwarfs its side, which the inner side of every join bevels
    Contour contour = rect_contour(Rect{.x = 0.0f, .y = 0.0f, .width = 2.0f, .height = 2.0f});
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contour, 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    EXPECT_TRUE(contour.convex);
    // four mitered joins of ten vertices close through the two wrap vertices
    EXPECT_EQ(batch.vertices.size(), 42u);
    EXPECT_EQ(batch.indices.size(), (42u - 2u) * 3u);
}

TEST(ExpandStrokeChecks, BevelsTheCornersOfANotch) {
    // arrange an L stroked under a miter limit every corner exceeds
    const PathVerb verbs[7] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[6] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 4.0f, .y = 10.0f}, Point{.x = 4.0f, .y = 4.0f}, Point{.x = 10.0f, .y = 4.0f}, Point{.x = 10.0f, .y = 0.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 1.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    EXPECT_FALSE(contours[0].convex);
    // the band plus its fringe reaches half a fringe past the shape bounds on every side
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_NEAR(min_x, -2.5f, 0.001f);
    EXPECT_NEAR(max_x, 12.5f, 0.001f);
    EXPECT_NEAR(min_y, -2.5f, 0.001f);
    EXPECT_NEAR(max_y, 12.5f, 0.001f);
}

TEST(ExpandStrokeChecks, ArcsTheJoinsOfARoundNotch) {
    // arrange the same notch stroked round, which arcs every corner it widens
    const PathVerb verbs[6] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[5] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 4.0f, .y = 10.0f}, Point{.x = 4.0f, .y = 4.0f}, Point{.x = 10.0f, .y = 4.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Round, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // the arcs keep the band inside the fringe bounds the corners reach
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_GE(min_x, -2.51f);
    EXPECT_LE(max_x, 12.51f);
    EXPECT_GE(min_y, -2.51f);
    EXPECT_LE(max_y, 12.51f);
}

TEST(ExpandStrokeChecks, SquaresTheCapsOfALine) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Square, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // the start cap reaches a half width behind the end and the end cap mirrors it
    ASSERT_EQ(batch.vertices.size(), 8u);
    EXPECT_NEAR(batch.vertices[0].x, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[0].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[0].longitudinal, 0.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].x, 0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[2].longitudinal, 1.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].x, 13.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[4].longitudinal, 1.0f, 0.0001f);
    EXPECT_NEAR(batch.vertices[6].x, 14.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[6].y, -0.5f, 0.0001f);
    EXPECT_NEAR(batch.vertices[6].longitudinal, 0.0f, 0.0001f);
}

TEST(ExpandStrokeChecks, RefusesAContourWithoutASegment) {
    // arrange a move that starts nothing
    const PathVerb verbs[1] = {PathVerb::MoveTo};
    const Point points[1] = {Point{.x = 3.0f, .y = 3.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 0.0f, 0.0001f);
    EXPECT_EQ(batch.vertices.size(), 0u);
}

TEST(ExpandFillChecks, FillsARoundedRectangleConvexly) {
    // arrange
    Contour contour = rounded_rect_contour(Rect{.x = 0.0f, .y = 0.0f, .width = 20.0f, .height = 10.0f}, 3.0f, 0.25f);
    FrameBatch batch;
    // act
    const EmittedRange range = expand_fill(batch, contour);
    // assert
    EXPECT_GT(range.index_count, 0u);
    EXPECT_TRUE(contour.convex);
    // the ring stays inside the shape and the fringe reaches half a fringe past it
    for (const BatchVertex& vertex : batch.vertices) {
        // the full coverage seed sits on the inset ring, which stays inside the shape
        if (vertex.lateral >= 1.0f) {
            EXPECT_GE(vertex.x, 0.49f);
            EXPECT_LE(vertex.x, 19.51f);
            EXPECT_GE(vertex.y, 0.49f);
            EXPECT_LE(vertex.y, 9.51f);
        }
        else {
            EXPECT_GE(vertex.x, -0.51f);
            EXPECT_LE(vertex.x, 20.51f);
            EXPECT_GE(vertex.y, -0.51f);
            EXPECT_LE(vertex.y, 10.51f);
        }
    }
}

TEST(ExpandStrokeChecks, StrokesAClosedSquarePlainly) {
    // arrange a square whose half width stays inside every join limit
    Contour contour = rect_contour(Rect{.x = 0.0f, .y = 0.0f, .width = 10.0f, .height = 10.0f});
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contour, 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // four plain stations of two vertices close through the two wrap vertices
    EXPECT_EQ(batch.vertices.size(), 10u);
    EXPECT_EQ(batch.indices.size(), (10u - 2u) * 3u);
    // the band plus its fringe reaches half a fringe past the square on every side
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_NEAR(min_x, -2.5f, 0.001f);
    EXPECT_NEAR(max_x, 12.5f, 0.001f);
    EXPECT_NEAR(min_y, -2.5f, 0.001f);
    EXPECT_NEAR(max_y, 12.5f, 0.001f);
}

TEST(ExpandStrokeChecks, StrokesAZigzagOpenly) {
    // arrange an open three segment polyline whose corners stay inside every join limit
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo};
    const Point points[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 20.0f, .y = 10.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // the caps and the two plain stations carry the strip
    EXPECT_EQ(batch.vertices.size(), 12u);
    // the band plus its fringe and the butt caps bound the polyline
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_NEAR(min_x, -0.5f, 0.001f);
    EXPECT_NEAR(max_x, 20.5f, 0.001f);
    EXPECT_NEAR(min_y, -2.5f, 0.001f);
    EXPECT_NEAR(max_y, 12.5f, 0.001f);
}

TEST(ExpandStrokeChecks, MitersAConcaveNotchInwardly) {
    // arrange a small L whose notch bevels inward while the miter holds outward
    const PathVerb verbs[7] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[6] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 2.0f}, Point{.x = 1.0f, .y = 2.0f}, Point{.x = 1.0f, .y = 1.0f}, Point{.x = 2.0f, .y = 1.0f}, Point{.x = 2.0f, .y = 0.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    EXPECT_FALSE(contours[0].convex);
    // six mitered joins of ten vertices close through the two wrap vertices
    EXPECT_EQ(batch.vertices.size(), 62u);
    EXPECT_EQ(batch.indices.size(), (62u - 2u) * 3u);
}

TEST(ExpandStrokeChecks, ArcsTheCornersOfAnOpenZigzag) {
    // arrange an open zigzag stroked round, which arcs both corners
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo};
    const Point points[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 20.0f, .y = 10.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Round, Join::Round, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // the round caps and the two arced joins stay inside the fringe bounds
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_GE(min_x, -2.51f);
    EXPECT_LE(max_x, 22.51f);
    EXPECT_GE(min_y, -2.51f);
    EXPECT_LE(max_y, 12.51f);
}

TEST(ExpandStrokeChecks, BevelsTheSpikeOfAnOpenSliver) {
    // arrange an open spike whose only join turns past the miter limit
    const PathVerb verbs[3] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo};
    const Point points[3] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 0.5f}, Point{.x = 0.0f, .y = 1.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 1.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // the bevelled spike stays inside the fringe bounds the caps reach
    float min_x = batch.vertices[0].x;
    float max_x = min_x;
    float min_y = batch.vertices[0].y;
    float max_y = min_y;
    for (const BatchVertex& vertex : batch.vertices) {
        min_x = std::min(min_x, vertex.x);
        max_x = std::max(max_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_y = std::max(max_y, vertex.y);
    }
    EXPECT_GE(min_x, -0.71f);
    EXPECT_LE(max_x, 12.51f);
    EXPECT_GE(min_y, -4.01f);
    EXPECT_LE(max_y, 5.01f);
}

TEST(ContourChecks, MergesAPointThatRepeatsItsPredecessor) {
    // arrange a square whose second corner repeats the first
    const PathVerb verbs[6] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point repeated[5] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 0.0f}};
    const Point clean[4] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 0.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 10.0f}, Point{.x = 10.0f, .y = 0.0f}};
    // act
    const std::vector<Contour> with_repeat = build_contours(verbs, repeated);
    const std::vector<Contour> without_repeat = build_contours(verbs, clean);
    // assert
    ASSERT_EQ(with_repeat.size(), 1u);
    ASSERT_EQ(without_repeat.size(), 1u);
    // the repeated point merges away, so the two contours flatten identically
    ASSERT_EQ(with_repeat[0].points.size(), without_repeat[0].points.size());
    for (std::size_t index = 0; index < with_repeat[0].points.size(); index++) {
        EXPECT_NEAR(with_repeat[0].points[index].position.x, without_repeat[0].points[index].position.x, 0.0001f);
        EXPECT_NEAR(with_repeat[0].points[index].position.y, without_repeat[0].points[index].position.y, 0.0001f);
    }
}

TEST(ContourChecks, ClearsTheDirectionOfAZeroLengthSegment) {
    // arrange a contour whose stamped directions are then collapsed by moving a point onto its successor
    const PathVerb verbs[3] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo};
    const Point points[3] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 0.0f}, Point{.x = 20.0f, .y = 0.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    // act
    contours[0].points[1].position = contours[0].points[0].position;
    stamp_directions(contours[0]);
    // assert
    EXPECT_NEAR(contours[0].points[0].dx, 0.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[0].dy, 0.0f, 0.0001f);
    EXPECT_NEAR(contours[0].points[0].length, 0.0f, 0.0001f);
}

TEST(CurveDivsChecks, KeepsTheTwoSegmentFloor) {
    // act
    const int segments = curve_divs(0.001f, 3.14159265358979f, 0.25f);
    // assert
    EXPECT_EQ(segments, 2);
}

TEST(CurveDivsChecks, RefusesADegenerateRadiusOrTolerance) {
    // act
    const int radiusless = curve_divs(0.0f, 3.14159265358979f, 0.25f);
    const int toleranceless = curve_divs(3.0f, 3.14159265358979f, 0.0f);
    // assert
    EXPECT_EQ(radiusless, 2);
    EXPECT_EQ(toleranceless, 2);
}

TEST(RoundedRectContourChecks, FallsBackToTheRectangleWithoutARadius) {
    // arrange
    const Rect rect = Rect{.x = 1.0f, .y = 2.0f, .width = 20.0f, .height = 10.0f};
    // act
    const Contour rounded = rounded_rect_contour(rect, 0.0f, 0.25f);
    const Contour plain = rect_contour(rect);
    // assert
    ASSERT_EQ(rounded.points.size(), plain.points.size());
    for (std::size_t index = 0; index < rounded.points.size(); index++)
        EXPECT_EQ(rounded.points[index].position, plain.points[index].position);
}

TEST(RoundedRectContourChecks, ClampsANegativeRadiusToTheRectangle) {
    // arrange
    const Rect rect = Rect{.x = 0.0f, .y = 0.0f, .width = 10.0f, .height = 10.0f};
    // act
    const Contour contour = rounded_rect_contour(rect, -3.0f, 0.25f);
    // assert
    EXPECT_EQ(contour.points.size(), 4u);
    for (const ContourPoint& point : contour.points) {
        EXPECT_TRUE(std::isfinite(point.position.x));
        EXPECT_TRUE(std::isfinite(point.position.y));
    }
}

TEST(RoundedRectContourChecks, CornersEveryArcPoint) {
    // arrange
    const Contour contour = rounded_rect_contour(Rect{.x = 0.0f, .y = 0.0f, .width = 20.0f, .height = 10.0f}, 3.0f, 0.25f);
    // act, assert
    for (const ContourPoint& point : contour.points)
        // the arc points corner at, which the join calculation bevels from
        EXPECT_TRUE((point.flags & Corner) != 0u);
}

TEST(ExpandFillChecks, BevelsASharpApexWithoutAMiterSpike) {
    // arrange a sliver whose apex turns almost all the way back
    const PathVerb verbs[4] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[3] = {Point{.x = 0.0f, .y = 0.0f}, Point{.x = 10.0f, .y = 0.1f}, Point{.x = 20.0f, .y = 0.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const EmittedRange range = expand_fill(batch, contours[0]);
    // assert
    EXPECT_GT(range.index_count, 0u);
    // the apex bevels its outer edge, so no fringe vertex shoots down the capped miter normal
    for (const BatchVertex& vertex : batch.vertices) {
        EXPECT_LE(std::abs(vertex.y), 1.0f);
    }
}

TEST(ExpandStrokeChecks, ReportsTheRampOfAPlainStroke) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.ramp, 2.5f, 0.0001f);
}

TEST(ExpandStrokeChecks, ReportsTheRampOfAClampedHairline) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 0.25f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_NEAR(emission.ramp, 1.0f, 0.0001f);
    EXPECT_NEAR(emission.alpha, 0.5f, 0.0001f);
}

TEST(ExpandStrokeChecks, RefusesAZeroWidthStroke) {
    // arrange
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 0.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_EQ(emission.range.index_count, 0u);
    EXPECT_NEAR(emission.alpha, 0.0f, 0.0001f);
    EXPECT_EQ(batch.vertices.size(), 0u);
}

TEST(ExpandStrokeChecks, AlternatesTheStripWinding) {
    // arrange a butt capped line, whose two caps carry four vertex strips
    const PathVerb verbs[2] = {PathVerb::MoveTo, PathVerb::LineTo};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_GT(emission.range.index_count, 0u);
    // the first strip pair winds forwards and the second backwards, which a culling backend needs
    ASSERT_GE(batch.indices.size(), 6u);
    EXPECT_EQ(batch.indices[0], 0u);
    EXPECT_EQ(batch.indices[1], 1u);
    EXPECT_EQ(batch.indices[2], 2u);
    EXPECT_EQ(batch.indices[3], 2u);
    EXPECT_EQ(batch.indices[4], 1u);
    EXPECT_EQ(batch.indices[5], 3u);
}

TEST(ExpandStrokeChecks, WrapsAClosedTwoPointContourWithoutCaps) {
    // arrange a closed contour whose two points enclose no area
    const PathVerb verbs[3] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::Close};
    const Point points[2] = {Point{.x = 2.0f, .y = 2.0f}, Point{.x = 12.0f, .y = 2.0f}};
    std::vector<Contour> contours = build_contours(verbs, points);
    FrameBatch batch;
    // act
    const StrokeEmission emission = expand_stroke(batch, contours[0], 2.0f, Cap::Butt, Join::Miter, 4.0f, 0.25f);
    // assert
    EXPECT_GT(emission.range.index_count, 0u);
    EXPECT_NEAR(emission.alpha, 1.0f, 0.0001f);
    // the loop carries two stations plus the wrap, which no cap extends
    EXPECT_EQ(batch.vertices.size(), 6u);
}

TEST(PackColorChecks, ClampsChannelsOutsideTheUnitRange) {
    // act
    const std::uint32_t packed = pack_color(Color{.red = 2.0f, .green = -1.0f, .blue = 0.0f, .alpha = 1.0f});
    // assert
    EXPECT_EQ(packed, 0xFF0000FFu);
}

TEST(PackColorChecks, PacksATransparentColourAsNothing) {
    // act
    const std::uint32_t packed = pack_color(Color::rgba(1.0f, 1.0f, 1.0f, 0.0f));
    // assert
    EXPECT_EQ(packed, 0x00000000u);
}

TEST(PackColorChecks, PacksANonFiniteChannelAsTheEmptyEndpoint) {
    // arrange a channel that compares to nothing, which a clamp would pass through
    const float nan = std::nanf("");
    // act
    const std::uint32_t packed = pack_color(Color{.red = nan, .green = 1.0f, .blue = 0.0f, .alpha = 1.0f});
    // assert
    EXPECT_EQ(packed, 0xFF00FF00u);
}
