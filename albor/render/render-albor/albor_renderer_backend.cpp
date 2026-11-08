#include "albor/render/render-albor/albor_renderer_backend.h"

#include <optional>

#include "albor/render/draw_command.h"
#include "albor/render/render-albor/albor_backend_support.h"
#include "albor/render/renderer_backend.h"

namespace albor::render
{
    AlborRendererBackend::AlborRendererBackend() :
        m_batch_buffer_allocations(std::make_shared<std::atomic<std::uint64_t>>(0)) {}

    AlborRendererBackend::~AlborRendererBackend() = default;

    bool AlborRendererBackend::ready() const {
        // the platform device decides whether a frame can be drawn at all (§6.2)
        return backend_device_available();
    }

    std::unique_ptr<RenderSurface> AlborRendererBackend::create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a surface is refused while the device is not ready or the extent has no area, which one test covers (§6.2)
        const bool acceptable = ready() && metrics.valid();
        // the platform builds the surface over the handle, which a null handle leaves offscreen (§6.2)
        return acceptable ? backend_create_surface(handle, metrics, m_batch_buffer_allocations) : nullptr;
    }

    void AlborRendererBackend::resize_surface(RenderSurface& surface, const WindowMetrics& metrics) {
        // a resize of an unusable surface is a no operation rather than an error (§6.2)
        if (!surface.valid())
            return;
        // metrics with no area cannot be presented, so the previous size is kept (§6.2)
        if (!metrics.valid())
            return;
        // the extent is written here, on the thread that owns the surface (§5.3, D9.1)
        surface.resize(metrics);
    }

    FrameOutcome AlborRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
        // a frame needs a surface that could draw, and a surface that cannot is not a refusal to retry (§6.1)
        if (!surface.valid())
            return FrameOutcome::Presented;
        // a scene at stale metrics is refused rather than repaired here, exactly as the reference does (§6.1, D9.1)
        if (surface.metrics() != scene.metrics())
            return FrameOutcome::RefusedStaleMetrics;
        // the whole surface is the clip every command was recorded under, which a partial frame narrows below (§6.1)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size);
        // a translucent background cannot be reproduced by the opaque Copy replacement, so it needs a full frame (§6.1)
        const bool opaque_background = scene.background().alpha >= 1.0f;
        // the surface is told before begin, because the target load and the final present depend on it (§6.1, §6.3)
        const bool partial = scene.is_partial() && opaque_background && surface.set_damage_region(scene.damage_rect());
        // a full frame clears the target, which also establishes the base a later partial frame needs (§6.1)
        if (!partial)
            surface.set_damage_region(std::nullopt);
        // a culled list without a retained target is missing the draws outside the region, so it cannot be drawn (§6.1)
        if (scene.culls_commands() && !partial)
            return FrameOutcome::RefusedNoRetention;
        // stage two begins the frame and draws the immutable scene into it (§6.1, AD-18)
        RenderContext& context = surface.begin_frame();
        if (partial) {
            // the damage is the only area the frame may touch, so the replay is clipped to it (§6.1)
            const Rect damage = *scene.damage_rect();
            context.save();
            context.clip_rect(damage);
            // the damaged area is replaced by the background rather than blended over the old pixels (§6.1)
            context.draw_rect(damage, {.fill = scene.background(), .composition = CompositionMode::Copy});
            // only the commands that can reach the damage replay, which is what skips the untouched vertices (§6.1)
            replay_commands(scene.commands(), context, full, damage);
            context.restore();
        }
        else {
            // a full frame paints the background over the whole surface, which the clear of phase one is (§6.1)
            context.draw_rect(full, {.fill = scene.background()});
            // the commands replay through the one shared replay, which every backend calls (§6.1, §6.2)
            replay_commands(scene.commands(), context, full);
        }
        // the frame finishes once the immutable scene is fully recorded (§6.1)
        surface.end_frame();
        // the surface drew the whole scene, which is what the pipeline expects of a presented frame (§6.1)
        return FrameOutcome::Presented;
    }

    std::uint64_t AlborRendererBackend::batch_buffer_allocations() const {
        // the counter is always present, because every surface reports into it (§6.3, AD-06)
        return m_batch_buffer_allocations->load(std::memory_order_relaxed);
    }

    void install_albor_renderer_backend() {
        // the factory is what the composition root installs before the application runs (§6.3)
        set_renderer_backend_factory([] { return std::make_unique<AlborRendererBackend>(); });
    }
} // namespace albor::render
