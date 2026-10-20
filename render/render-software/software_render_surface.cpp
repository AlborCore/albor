#include "render/render-software/software_render_surface.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <blend2d.h>

#include "render/render-software/software_render_context.h"
#include "window/window_metrics.h"

namespace albor::software
{
    // the cpu raster and the context that draws into it, which is one allocation away from the caller (§6.3)
    struct SoftwareSurfaceState
    {
        // the raster frame the context draws into, which is the reference output (§6.3, AD-26)
        BLImage raster;

        // the context over the raster, which draws text through the atlas and everything else through blend2d (§6.3)
        BLContext context;

        // the context wrapping the raster, which the executor draws the frame through (§6.1)
        std::unique_ptr<SoftwareRenderContext> draw_context;

        // the metrics the surface presents at, which the executor checks a scene against (§6.1, D9.1)
        WindowMetrics metrics;

        // the frames finished on this surface, which is what a headless test counts (§4.3)
        std::size_t frames = 0;

        // whether a frame is currently begun, which the begin and end must agree on (§6.1)
        bool begun = false;

        // whether the raster allocation produced a target, which a refused surface reports (§6.2)
        bool allocated = false;

        // the one pixel scratch a refused raster falls back to, so begin_frame never hands out a null reference (§6.2)
        BLImage scratch;
    };

    namespace
    {
        // the extent a raster allocates at, which the device scale maps the logical size onto (§6.3, AD-27)
        int raster_width(const WindowMetrics& metrics) {
            // a raster needs at least one pixel, because a zero sized target cannot be allocated (§6.2)
            return std::max(1, static_cast<int>(metrics.pixel_size.width));
        }

        // the height a raster allocates at, which the device scale maps the logical size onto (§6.3, AD-27)
        int raster_height(const WindowMetrics& metrics) {
            // a raster needs at least one pixel, because a zero sized target cannot be allocated (§6.2)
            return std::max(1, static_cast<int>(metrics.pixel_size.height));
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
        // the raster is a cpu image, because this backend is the deterministic reference and has no gpu (§6.3, AD-26)
        m_state->allocated = m_state->raster.create(raster_width(metrics), raster_height(metrics), BL_FORMAT_PRGB32) == BL_SUCCESS;
        // the scratch is allocated anyway, because a caller may still ask for a context and must not get a null (§6.2)
        m_state->scratch.create(1, 1, BL_FORMAT_PRGB32);
    }

    SoftwareRenderSurface::~SoftwareRenderSurface() = default;

    bool SoftwareRenderSurface::valid() const {
        // the surface is valid exactly when the raster allocation produced a target (§6.2)
        return m_state != nullptr && m_state->allocated;
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
        // a frame that was begun is closed before its target is replaced (§6.1)
        m_state->context.end();
        // the metrics move first, because the executor compares a scene against them (§6.1, D9.1)
        m_state->metrics = metrics;
        // the raster is reallocated at the new size, which is what a cpu surface offers (§6.3)
        m_state->allocated = m_state->raster.create(raster_width(metrics), raster_height(metrics), BL_FORMAT_PRGB32) == BL_SUCCESS;
        // the old draw context points at the replaced raster, so it is dropped until the next begin rebuilds it (§6.3)
        m_state->draw_context.reset();
    }

    RenderContext& SoftwareRenderSurface::begin_frame() {
        // a frame that was not ended is closed, so the next begin starts from a clean context (§6.1)
        m_state->context.end();
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, m_state->metrics.logical_size);
        // a context over no target is the refusal the caller always receives instead of a null (§6.2)
        m_state->draw_context = std::make_unique<SoftwareRenderContext>(nullptr, 1.0f, full);
        m_state->begun = false;
        // an unusable surface draws into the scratch, because a caller must never receive a null context (§6.2)
        BLImage& target = valid() ? m_state->raster : m_state->scratch;
        if (m_state->context.begin(target) == BL_SUCCESS) {
            // the raster is cleared to transparent, because a frame paints its own background over it (§6.1)
            m_state->context.clearAll();
            // the context wraps the raster the frame draws into (§6.1)
            m_state->draw_context = std::make_unique<SoftwareRenderContext>(&m_state->context, valid() ? m_state->metrics.scale_factor : 1.0f, full);
            // only a real frame counts, because a refusal draws into a scratch nobody reads (§6.2)
            m_state->begun = valid();
        }
        return *m_state->draw_context;
    }

    void SoftwareRenderSurface::end_frame() {
        // an unusable surface finished nothing (§6.2)
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

    std::vector<std::uint8_t> SoftwareRenderSurface::read_pixels() const {
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

    std::size_t SoftwareRenderSurface::frame_count() const {
        // the count is the frames this surface finished, which a headless test asserts on (§4.3)
        return m_state->frames;
    }
} // namespace albor::software
