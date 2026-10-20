#include "render/render-blend2d/blend2d_render_surface.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <blend2d.h>

#include "render/render-blend2d/blend2d_render_context.h"
#include "window/window_metrics.h"

// the read back below swaps red and blue, which is correct only on little endian, which is all supported targets (§6.3)
static_assert(std::endian::native == std::endian::little, "blend2d read-back assumes little-endian PRGB32 memory");

namespace albor::blend2d
{
    // the raster and the context that draws into it, which is one allocation away from the caller (§6.3)
    struct Blend2DSurfaceState
    {
        // the raster frame the context draws into, which is the physical pixel buffer (§6.3, AD-27)
        BLImage raster;

        // the context over the raster, which draws logical coordinates onto physical pixels (§6.3)
        BLContext context;

        // the draw context of the frame being drawn, rebuilt per begin so it folds scale into the meta (§6.3)
        std::unique_ptr<Blend2DRenderContext> draw_context;

        // the metrics the surface presents at, which the executor checks a scene against (§6.1, D9.1)
        WindowMetrics metrics;

        // the frames finished on this surface, which is what a headless test counts (§4.3)
        std::size_t frames = 0;

        // whether a frame is currently begun, which the begin and end must agree on (§6.1)
        bool begun = false;
    };

    std::unique_ptr<Blend2DRenderSurface> Blend2DRenderSurface::create(WindowMetrics metrics) {
        // metrics with no area cannot be rasterized, so they are refused (§6.2)
        if (!metrics.valid())
            return nullptr;
        // the surface is built through its private constructor, which owns the allocation (§6.3)
        return std::unique_ptr<Blend2DRenderSurface>(new Blend2DRenderSurface(metrics));
    }

    Blend2DRenderSurface::Blend2DRenderSurface(WindowMetrics metrics) :
        m_state(std::make_unique<Blend2DSurfaceState>()) {
        // the metrics are recorded first, because the raster is allocated against them (§6.2)
        m_state->metrics = metrics;
        // the raster is physical pixel sized and premultiplied, matching the skia reference (§6.3, AD-26)
        m_state->raster.create(static_cast<int>(std::max(1.0f, metrics.pixel_size.width)), static_cast<int>(std::max(1.0f, metrics.pixel_size.height)), BL_FORMAT_PRGB32);
    }

    Blend2DRenderSurface::~Blend2DRenderSurface() = default;

    bool Blend2DRenderSurface::valid() const {
        // the surface is valid exactly when the raster allocated, which is what every frame needs (§6.2)
        return m_state != nullptr && !m_state->raster.empty();
    }

    const WindowMetrics& Blend2DRenderSurface::metrics() const {
        // the metrics are the ones the surface was built or last resized to (§6.2)
        return m_state->metrics;
    }

    void Blend2DRenderSurface::resize(const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!valid())
            return;
        // metrics with no area cannot be rasterized, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        // a frame in flight would draw into a replaced raster, so the resize ends it before reallocating (§6.1)
        if (m_state->begun) {
            m_state->context.end();
            m_state->begun = false;
        }
        // the metrics move first, because the executor compares a scene against them (§6.1, D9.1)
        m_state->metrics = metrics;
        // the raster is reallocated at the new physical size, which is what a cpu raster offers (§6.3)
        m_state->raster.create(static_cast<int>(std::max(1.0f, metrics.pixel_size.width)), static_cast<int>(std::max(1.0f, metrics.pixel_size.height)), BL_FORMAT_PRGB32);
        // the old draw context points at the replaced raster, so it is dropped until the next begin rebuilds it (§6.3)
        m_state->draw_context.reset();
    }

    RenderContext& Blend2DRenderSurface::begin_frame() {
        // the logical extent the frame spans, which the draw context records as its full clip (§6.2)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, m_state->metrics.logical_size);
        // a surface that did not allocate hands out a context over no target, so a caller never receives null (§6.2)
        if (!valid()) {
            m_state->draw_context = std::make_unique<Blend2DRenderContext>(nullptr, m_state->metrics.scale_factor, full);
            return *m_state->draw_context;
        }
        // the context is bound to the raster, which resets its state to the defaults (§6.3)
        m_state->context.begin(m_state->raster);
        // the draw context is built after begin, so its constructor folds the device scale into the meta (§6.3)
        m_state->draw_context = std::make_unique<Blend2DRenderContext>(&m_state->context, m_state->metrics.scale_factor, full);
        // the raster is cleared to transparent, because a frame paints its own background over it (§6.1)
        m_state->context.clearAll();
        // the frame is marked begun, so an unbalanced end cannot pass for a balanced one (§6.1)
        m_state->begun = true;
        // the context is the one that wraps this raster (§6.1)
        return *m_state->draw_context;
    }

    void Blend2DRenderSurface::end_frame() {
        // a surface that did not allocate finished nothing (§6.2)
        if (!valid())
            return;
        // an unbalanced end would count a frame that never began, so it is ignored (§6.1)
        if (!m_state->begun)
            return;
        // a rounded clip the frame left open is closed before the raster ends (§6.2)
        if (m_state->draw_context != nullptr)
            m_state->draw_context->finish();
        // the frame is marked finished, which is what a headless test counts (§4.3)
        m_state->begun = false;
        // blend2d end flushes synchronously, which is the whole of cpu presentation (§6.3)
        m_state->context.end();
        m_state->frames++;
    }

    std::vector<std::uint8_t> Blend2DRenderSurface::read_pixels() const {
        // an unusable surface has no pixels, so an empty read is the honest answer (§6.2)
        if (!valid())
            return {};
        // a read is of the last finished frame, so an unfinished one refuses; both backends must agree (§6.3, AD-26)
        if (m_state->frames == 0 || m_state->begun)
            return {};
        BLImageData data;
        // a raster blend2d cannot describe is refused, because a half read is not a screenshot (§6.3)
        if (m_state->raster.getData(&data) != BL_SUCCESS)
            return {};
        const int width = data.size.w;
        const int height = data.size.h;
        // an extent with no area has no pixels to read back (§6.3)
        if (width <= 0 || height <= 0)
            return {};
        // the read is tightly packed, because a conformance check compares the canonical rgba order (§6.3, AD-26)
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        for (int y = 0; y < height; y++) {
            // the source row honours the padded stride blend2d reports, so an alignment does not shift the picture (§6.3)
            const std::uint8_t* source = static_cast<const std::uint8_t*>(data.pixelData) + static_cast<std::intptr_t>(y) * data.stride;
            std::uint8_t* destination = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
            for (int x = 0; x < width; x++) {
                // prgb32 is blue, green, red and alpha in memory, so red and blue swap into the canonical rgba (§6.3)
                destination[0] = source[2];
                destination[1] = source[1];
                destination[2] = source[0];
                destination[3] = source[3];
                source += 4;
                destination += 4;
            }
        }
        return pixels;
    }

    std::size_t Blend2DRenderSurface::frame_count() const {
        // the count is the frames this surface finished, which a headless test asserts on (§4.3)
        return m_state->frames;
    }
} // namespace albor::blend2d
