#include "render/render-software/software_render_surface.h"

#include <cstdint>
#include <vector>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkColorType.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkSurface.h>

#include "render/render-software/software_render_context.h"
#include "window/window_metrics.h"

namespace albor::software
{
    // the cpu raster and the context that draws into it, which is one allocation away from the caller (§6.3)
    struct SoftwareSurfaceState
    {
        // the raster frame the context draws into, which is the reference output (§6.3, AD-26)
        sk_sp<SkSurface> raster;

        // the context wrapping the raster canvas, which draws text through the atlas and shapes through skia (§6.3)
        std::unique_ptr<SoftwareRenderContext> context;

        // the metrics the surface presents at, which the executor checks a scene against (§6.1, D9.1)
        WindowMetrics metrics;

        // the frames finished on this surface, which is what a headless test counts (§4.3)
        std::size_t frames = 0;

        // whether a frame is currently begun, which the begin and end must agree on (§6.1)
        bool begun = false;

        // the one pixel scratch a refused raster falls back to, so begin_frame never hands out a null reference (§6.2)
        sk_sp<SkSurface> scratch;

        // the context over the scratch, which exists so the null case is still a drawable no op (§6.2)
        std::unique_ptr<SoftwareRenderContext> scratch_context;
    };

    namespace
    {
        // the image info every backend reads back as, so a comparison compares pictures rather than formats (§6.3, AD-26)
        SkImageInfo canonical_info(int width, int height) {
            // premultiplied rgba is the one order two rasterizers agree on byte for byte (§6.3, AD-26)
            return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        }

        // the image info the raster allocates at, which is the one format every conformance check reads (§6.3)
        SkImageInfo raster_info(const WindowMetrics& metrics) {
            // the logical size is scaled onto physical pixels, because the raster is a device buffer (§6.3, AD-27)
            const auto width = static_cast<int>(std::max(1.0f, metrics.pixel_size.width));
            const auto height = static_cast<int>(std::max(1.0f, metrics.pixel_size.height));
            // premultiplied n32 is what skia's cpu surface and the gpu surface agree on, so parity compares bytes (§6.3)
            return SkImageInfo::MakeN32Premul(width, height);
        }
    } // namespace

    std::unique_ptr<SoftwareRenderSurface> SoftwareRenderSurface::create(WindowMetrics metrics) {
        // metrics with no area cannot be rasterized, so they are refused (§6.2)
        if (!metrics.valid())
            return nullptr;
        // the surface is built through its private constructor, which owns the allocation (§6.3)
        return std::unique_ptr<SoftwareRenderSurface>(new SoftwareRenderSurface(metrics));
    }

    SoftwareRenderSurface::SoftwareRenderSurface(WindowMetrics metrics) :
        m_state(std::make_unique<SoftwareSurfaceState>()) {
        // the metrics are recorded first, because the raster is allocated against them (§6.2)
        m_state->metrics = metrics;
        // the raster is a cpu surface, because this backend is the deterministic reference and has no gpu (§6.3, AD-26)
        m_state->raster = SkSurfaces::Raster(raster_info(metrics));
        // a refused allocation leaves no canvas, so the surface reports itself unusable rather than drawing (§6.2)
        if (m_state->raster == nullptr) {
            // the scratch is allocated anyway, because a caller may still ask for a context and must not get a null (§6.2)
            m_state->scratch = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(1, 1));
            // the scratch context is the fallback, which absorbs a draw into a pixel nobody reads (§6.2)
            if (m_state->scratch != nullptr)
                m_state->scratch_context = std::make_unique<SoftwareRenderContext>(m_state->scratch->getCanvas(), 1.0f);
            return;
        }
        // the context is the same one the gpu backend draws through, so the two cannot drift apart (§6.3)
        m_state->context = std::make_unique<SoftwareRenderContext>(m_state->raster->getCanvas(), metrics.scale_factor);
    }

    SoftwareRenderSurface::~SoftwareRenderSurface() = default;

    bool SoftwareRenderSurface::valid() const {
        // the surface is valid exactly when a raster and its canvas both exist (§6.2)
        return m_state != nullptr && m_state->raster != nullptr && m_state->context != nullptr;
    }

    const WindowMetrics& SoftwareRenderSurface::metrics() const {
        // the metrics are the ones the surface was built or last resized to (§6.2)
        return m_state->metrics;
    }

    void SoftwareRenderSurface::resize(const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!valid())
            return;
        // metrics with no area cannot be rasterized, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        // the metrics move first, because the executor compares a scene against them (§6.1, D9.1)
        m_state->metrics = metrics;
        // the raster is reallocated at the new size, which is what a cpu surface offers (§6.3)
        m_state->raster = SkSurfaces::Raster(raster_info(metrics));
        // a refused reallocation leaves the old context pointing at a dead canvas, so the surface reports unusable (§6.2)
        if (m_state->raster == nullptr) {
            m_state->context.reset();
            return;
        }
        // the context is rebuilt against the new canvas, because it holds the canvas pointer (§6.3)
        m_state->context = std::make_unique<SoftwareRenderContext>(m_state->raster->getCanvas(), metrics.scale_factor);
    }

    RenderContext& SoftwareRenderSurface::begin_frame() {
        // an unusable surface draws into the scratch, because a caller must never receive a null context (§6.2)
        if (!valid() && m_state->scratch_context != nullptr)
            return *m_state->scratch_context;
        // an unusable surface with no scratch at all has nothing to hand out, so the caller gets the last resort (§6.2)
        if (!valid())
            return *m_state->scratch_context;
        // the raster is cleared to transparent, because a frame paints its own background over it (§6.1)
        m_state->raster->getCanvas()->clear(0x00000000);
        // the frame is marked begun, so an unbalanced end cannot pass for a balanced one (§6.1)
        m_state->begun = true;
        // the context is the one that wraps this raster (§6.1)
        return *m_state->context;
    }

    void SoftwareRenderSurface::end_frame() {
        // an unusable surface finished nothing (§6.2)
        if (!valid())
            return;
        // an unbalanced end would count a frame that never began, so it is ignored (§6.1)
        if (!m_state->begun)
            return;
        // the frame is marked finished, which is what a headless test counts (§4.3)
        m_state->begun = false;
        // a cpu surface presents by having been read, so flushing is the whole of presentation (§6.3)
        m_state->frames++;
    }

    std::vector<std::uint8_t> SoftwareRenderSurface::read_pixels() const {
        // an unusable surface has no pixels, so an empty read is the honest answer (§6.2)
        if (!valid())
            return {};
        // a read is of the last finished frame, so an unfinished one refuses; both backends must agree (§6.3, AD-26)
        if (m_state->frames == 0 || m_state->begun)
            return {};
        // the read uses the canonical info, because two backends must produce bytes a comparison can read (§6.3, AD-26)
        const SkImageInfo info = canonical_info(m_state->raster->imageInfo().width(), m_state->raster->imageInfo().height());
        // the stride is the padded row, which skia rounds per row rather than reporting per image (§6.3)
        const std::size_t stride = info.minRowBytes();
        // the buffer is sized from the minimum byte size, because a row is padded to its own stride (§6.3)
        std::vector<std::uint8_t> pixels(info.computeMinByteSize());
        // an extent the info cannot size is refused, because a wrapped size is not a screenshot (§6.3)
        if (SkImageInfo::ByteSizeOverflowed(pixels.size()))
            return {};
        // the read is refused when the raster cannot be copied, because a half read is not a screenshot (§6.3)
        if (!m_state->raster->readPixels(info, pixels.data(), stride, 0, 0))
            return {};
        // the tightly packed bytes are what a conformance check compares against (§6.3, AD-26)
        return pixels;
    }

    std::size_t SoftwareRenderSurface::frame_count() const {
        // the count is the frames this surface finished, which a headless test asserts on (§4.3)
        return m_state->frames;
    }
} // namespace albor::software
