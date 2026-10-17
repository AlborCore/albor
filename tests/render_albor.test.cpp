#include <cstddef>
#include <memory>
#include <span>

#include <gtest/gtest.h>

#include "core/paint.h"
#include "render/frame_scene.h"
#include "render/render-albor/albor_renderer_backend.h"
#include "render/render_context.h"
#include "render/renderer_backend.h"
#include "window/window_metrics.h"

using namespace albor;

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
