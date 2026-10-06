#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "animation/animation_engine.h"
#include "core/geometry.h"
#include "core/paint.h"
#include "render/frame_scene.h"
#include "render/render_executor.h"
#include "runtime/binding_engine.h"
#include "ui/scene_builder.h"
#include "ui/ui_node.h"
#include "window/window_metrics.h"

namespace albor
{
    // the callback a frame request reaches, which is the window request_frame of §5.3
    using FrameRequestCallback = std::function<void()>;

    // the driver that joins the reactive engine to the two stage frame pipeline (§6.1, AD-18)
    class FramePipeline
    {
    public:
        // build a pipeline over a reactive engine and the ui tree it drives (§6.1, §7.3)
        FramePipeline(BindingEngine& engine, std::unique_ptr<UiNode> root);

        // the pipeline owns the tree, so it cannot be copied or moved (§6.1)
        ~FramePipeline();

        FramePipeline(const FramePipeline&) = delete;
        FramePipeline& operator=(const FramePipeline&) = delete;
        FramePipeline(FramePipeline&&) = delete;
        FramePipeline& operator=(FramePipeline&&) = delete;

        // hand out the root of the driven tree, which the builder walks (§6.1)
        [[nodiscard]] UiNode& root();

        // install the callback an invalidation reaches, which is the window frame request (§5.3, §6.1)
        void set_frame_request(FrameRequestCallback request);

        // settle the reactive graph and build a frame when the tree asked for one (§6.1, §7.3)
        [[nodiscard]] std::shared_ptr<const FrameScene> build_if_dirty(WindowMetrics metrics);

        // settle the reactive graph and build a frame unconditionally (§6.1)
        [[nodiscard]] std::shared_ptr<const FrameScene> build(WindowMetrics metrics);

        // attach the render executor the pipeline hands its scenes to, which is the thread boundary of §6.1 (AD-18)
        void set_executor(RenderExecutor* executor);

        // hand the last built scene to the attached executor, which is the hand off of §6.1 (AD-18)
        bool submit();

        // hand a scene straight to the attached executor, which a caller builds itself (§6.1)
        bool submit(const std::shared_ptr<const FrameScene>& scene);

        // report whether the tree asked for a frame since the last build (§6.1)
        [[nodiscard]] bool pending() const;

        // hand out how many frames the pipeline built, which a test asserts on (§6.1)
        [[nodiscard]] std::uint64_t frame_count() const;

        // hand out how many frames an invalidation asked for, which a test asserts on (§6.1)
        [[nodiscard]] std::uint64_t request_count() const;

        // set the background the scene is cleared with (§6.1)
        void set_background(Color background);

        // set the surface the attached executor presents into, which the window owns (§6.1)
        void set_surface(RenderSurface* surface);

        // attach the animation engine the pipeline advances on every frame, which drives properties (§7.3, AD-04d)
        void set_animation_engine(AnimationEngine* engine);

        // hand out the attached animation engine, which a test asserts on (§7.3)
        [[nodiscard]] AnimationEngine* animation_engine() const;

        // advance the animations by a frame delta before the settle, because an animation write marks its dependents dirty (§7.3, AD-04d)
        void advance_animations(float delta_seconds);

    private:
        // the reactive engine whose settlement precedes every frame (§7.3)
        BindingEngine& m_engine;

        // the tree the pipeline lays out and records (§6.1)
        std::unique_ptr<UiNode> m_root;

        // the stage one builder, which owns the layout and the record pass (§6.1)
        SceneBuilder m_builder;

        // the callback an invalidation reaches, which is the window frame request (§6.1)
        FrameRequestCallback m_frame_request;

        // the background the scene is cleared with (§6.1)
        Color m_background;

        // the render executor that takes the immutable scene off the ui thread (AD-18)
        RenderExecutor* m_executor = nullptr;

        // the surface the executor presents into, which the window owns (§6.1)
        RenderSurface* m_surface = nullptr;

        // the scene the last build produced, which submit hands over (AD-18)
        std::shared_ptr<const FrameScene> m_last_scene;

        // the engine that advances animations on every frame, which the caller owns (§7.3, AD-04d)
        AnimationEngine* m_animations = nullptr;

        // how many frames the pipeline built (§6.1)
        std::uint64_t m_frames = 0;

        // how many frames an invalidation asked for (§6.1)
        std::uint64_t m_requests = 0;
    };
} // namespace albor
