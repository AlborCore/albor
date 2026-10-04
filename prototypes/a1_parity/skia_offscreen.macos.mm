// the skia half of prototype a1-parity: an offscreen renderer fed the identical geometry dawn is fed, so the two
// rasterisers can be compared pixel for pixel
//
// this is the cpu raster backend, which is the deterministic reference for what the geometry should look like: a gpu
// backend is what a1.3 measured, but comparing it needs a metal render target this skia build only exposes through a
// narrower api, so the ganesh column is deferred rather than faked
//
// every skia type stays in this file, so the prototype's own public header speaks no skia (AD-05)

#include "prototypes/a1_parity/skia_offscreen.h"

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPoint.h>
#include <skia/core/SkSpan.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkVertices.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace albor::a1_parity
{

    namespace
    {

        // the pixel format the readback compares in, which both renderers allocate
        SkImageInfo image_info(std::size_t pixel_width, std::size_t pixel_height) { return SkImageInfo::Make(static_cast<int>(pixel_width), static_cast<int>(pixel_height), SkColorType::kRGBA_8888_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB()); }

    } // namespace

    class SkiaRasterRenderer final : public OffscreenRenderer
    {
    public:
        SkiaRasterRenderer() :
            m_state(new State()) {}

        ~SkiaRasterRenderer() override {
            // the surface owns pixels that reference nothing else, so it can go first
            m_state->surface.reset();
            delete m_state;
        }

        [[nodiscard]] Engine engine() const override { return Engine::skia_raster; }

        [[nodiscard]] bool create(std::size_t pixel_width, std::size_t pixel_height) override {
            m_state->pixel_width = pixel_width;
            m_state->pixel_height = pixel_height;

            // the raster backend needs no gpu at all, which is exactly what makes it the deterministic reference
            m_state->surface = SkSurfaces::Raster(image_info(pixel_width, pixel_height));
            return m_state->surface != nullptr;
        }

        void render(const RenderRequest& request) override;

        [[nodiscard]] bool read_back(PixelBuffer& out) const override;

    private:
        struct State
        {
            sk_sp<SkSurface> surface;
            std::size_t pixel_width = 0;
            std::size_t pixel_height = 0;
        };

        State* m_state = nullptr;
    };

    void SkiaRasterRenderer::render(const RenderRequest& request) {
        if (m_state->surface == nullptr || request.resources == nullptr) {
            return;
        }

        SkCanvas* canvas = m_state->surface->getCanvas();

        // the same clear dawn clears to, so an empty frame compares equal and a diff only ever reports drawn geometry
        canvas->clear(SkColor4f{kClearRed, kClearGreen, kClearBlue, 1.0F});

        // both engines expand a segment into a filled quad, so the comparison is two rasterisers over the same
        // rendering style rather than a raster stroke against a quad strip; a native skia stroke is a different
        // rendering style and comparing it to dawn's quad proves nothing about output parity
        SkPaint paint;
        paint.setColor(SkColor4f{kStrokeRed / 255.0F, kStrokeGreen / 255.0F, kStrokeBlue / 255.0F, 1.0F});
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kFill_Style);

        // the endpoints go through the view transform first and the perpendicular is taken afterwards, in pixel space,
        // which is what makes the stroke cosmetic and matches what dawn's shader does per instance
        const float half_width = request.resources->stroke_width * 0.5F;
        const std::size_t count = std::min(request.instances, request.resources->lines.size());
        std::vector<SkPoint> corners;
        std::vector<std::uint16_t> indices;
        corners.reserve(count * 4U);
        indices.reserve(count * 6U);
        for (std::size_t i = 0; i < count; i++) {
            const albor::a1_dawn::Segment& line = request.resources->lines[i];
            const SkPoint start = SkPoint::Make(request.view.map_x(line.x0), request.view.map_y(line.y0));
            const SkPoint end = SkPoint::Make(request.view.map_x(line.x1), request.view.map_y(line.y1));

            const float dx = end.fX - start.fX;
            const float dy = end.fY - start.fY;
            const float length = std::sqrt(dx * dx + dy * dy);
            const float nx = length > 0.0F ? -dy / length * half_width : 0.0F;
            const float ny = length > 0.0F ? dx / length * half_width : 0.0F;

            const auto base = static_cast<std::uint16_t>(i * 4U);
            corners.push_back(SkPoint::Make(start.fX + nx, start.fY + ny));
            corners.push_back(SkPoint::Make(start.fX - nx, start.fY - ny));
            corners.push_back(SkPoint::Make(end.fX - nx, end.fY - ny));
            corners.push_back(SkPoint::Make(end.fX + nx, end.fY + ny));
            indices.push_back(base);
            indices.push_back(static_cast<std::uint16_t>(base + 1U));
            indices.push_back(static_cast<std::uint16_t>(base + 2U));
            indices.push_back(base);
            indices.push_back(static_cast<std::uint16_t>(base + 2U));
            indices.push_back(static_cast<std::uint16_t>(base + 3U));
        }
        if (corners.empty()) {
            return;
        }

        // one vertex batch, drawn as triangles, which is the same geometry dawn's shader generates per instance
        const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, static_cast<int>(corners.size()), corners.data(), nullptr, nullptr, static_cast<int>(indices.size()), indices.data());
        canvas->drawVertices(vertices, SkBlendMode::kSrc, paint);
    }

    bool SkiaRasterRenderer::read_back(PixelBuffer& out) const {
        if (m_state->surface == nullptr) {
            return false;
        }

        // snapshot then read back, so what is compared is what was drawn rather than the surface's own storage
        sk_sp<SkImage> image = m_state->surface->makeImageSnapshot();
        if (image == nullptr) {
            return false;
        }

        const SkImageInfo info = image->imageInfo();
        const std::size_t row_bytes = static_cast<std::size_t>(info.bytesPerPixel()) * static_cast<std::size_t>(info.width());

        // the pixels are premultiplied, and both renderers draw an opaque stroke over an opaque clear, so the bytes are
        // directly comparable without unpremultiplying anything
        std::vector<std::uint8_t> pixels(row_bytes * static_cast<std::size_t>(info.height()));
        if (!image->readPixels(info, pixels.data(), row_bytes, 0, 0)) {
            return false;
        }

        out.allocate(static_cast<std::size_t>(info.width()), static_cast<std::size_t>(info.height()));
        const std::size_t copy = std::min(out.rgba.size(), pixels.size());
        std::memcpy(out.rgba.data(), pixels.data(), copy);
        return true;
    }

    OffscreenRenderer* make_skia_renderer(Engine) { return new SkiaRasterRenderer(); }

} // namespace albor::a1_parity
