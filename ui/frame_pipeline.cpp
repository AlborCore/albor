#include "ui/frame_pipeline.h"

namespace albor
{
    FramePipeline::FramePipeline(BindingEngine& engine, std::unique_ptr<UiNode> root) :
        m_engine(engine), m_root(std::move(root)) {
        // a pipeline without a root has nothing to draw, so the handler stays unset (§6.1)
        if (m_root == nullptr)
            return;
        // the invalidation handler sits on the root, so a deep change requests exactly one frame (§6.1)
        m_root->set_invalidation_handler([this] {
            // the request is counted before it is forwarded, so a test sees what the tree asked for (§6.1)
            m_requests++;
            // the window frame request is what turns the invalidation into a presentation (§5.3, §6.1)
            if (m_frame_request)
                m_frame_request();
        });
    }

    FramePipeline::~FramePipeline() {
        // the tree is settled once more, so no binding body outlives the ui tree it captured (§7.3)
        m_engine.settle();
    }

    UiNode& FramePipeline::root() {
        // the root is never null for a pipeline built over a tree (§6.1)
        return *m_root;
    }

    void FramePipeline::set_frame_request(FrameRequestCallback request) {
        // the callback is what an invalidation reaches, which is the window request_frame (§5.3)
        m_frame_request = std::move(request);
    }

    std::shared_ptr<const FrameScene> FramePipeline::build_if_dirty(WindowMetrics metrics) {
        // the reactive graph settles first, because a binding may have invalidated a node (§7.3, §6.1)
        m_engine.settle();
        // a clean tree needs no frame, so nothing is built (§6.1)
        if (!pending())
            return nullptr;
        // the scene is built and handed over, which is the whole of stage one (§6.1, AD-18)
        return build(metrics);
    }

    std::shared_ptr<const FrameScene> FramePipeline::build(WindowMetrics metrics) {
        // the reactive graph settles before anything is recorded, because a binding may still be dirty (§7.3, §6.1)
        m_engine.settle();
        // the layout and the record pass run on the ui thread, which is what stage one is (§6.1, AD-18)
        m_last_scene = m_builder.build(*m_root, metrics, m_background);
        // the frame is counted after a successful build, so a failed build is not counted (§6.1)
        m_frames++;
        return m_last_scene;
    }

    void FramePipeline::set_executor(RenderExecutor* executor) {
        // the executor is owned by the caller, because the window and its surface outlive the pipeline (§6.1)
        m_executor = executor;
    }

    void FramePipeline::set_surface(RenderSurface* surface) {
        // the surface is owned by the renderer backend, which is also outside the pipeline (§6.1)
        m_surface = surface;
    }

    bool FramePipeline::submit() {
        // the last built scene is what the hand off carries, because stage two never rebuilds (§6.1, AD-18)
        return submit(m_last_scene);
    }

    bool FramePipeline::submit(const std::shared_ptr<const FrameScene>& scene) {
        // a pipeline without an executor has nowhere to hand the scene, so the call fails (§6.1)
        if (m_executor == nullptr || m_surface == nullptr)
            return false;
        // a null scene would cross the thread boundary with nothing to draw, so it is refused (§6.1)
        if (!scene)
            return false;
        // the immutable scene crosses as a const shared pointer, which is the hand off of §10.5 (§6.1, AD-18)
        return m_executor->submit(*m_surface, scene);
    }

    bool FramePipeline::pending() const {
        // a pipeline without a root has nothing dirty (§6.1)
        if (m_root == nullptr)
            return false;
        // the tree is dirty when it or any descendant asked for a frame (§6.1)
        return m_root->render_dirty();
    }

    std::uint64_t FramePipeline::frame_count() const { return m_frames; }

    std::uint64_t FramePipeline::request_count() const { return m_requests; }

    void FramePipeline::set_animation_engine(AnimationEngine* engine) {
        // the engine is owned by the caller, because an application may outlive or outlive its window either way (§7.3)
        m_animations = engine;
    }

    AnimationEngine* FramePipeline::animation_engine() const {
        // the engine is the one the caller attached, which is null when no animation is driven (§7.3)
        return m_animations;
    }

    void FramePipeline::advance_animations(float delta_seconds) {
        // a pipeline without an engine has nothing to advance, which is the phase 2 shape (§7.3)
        if (m_animations == nullptr)
            return;
        // the animations run first, because an animation write marks its dependents dirty (§7.3, AD-04d)
        m_animations->advance(delta_seconds);
        // the graph settles immediately after, so the frame records the animated values and not the previous ones (§7.3, §6.1)
        m_engine.settle();
    }

    void FramePipeline::set_background(Color background) {
        // the background change reaches the next frame, because the tree is already scheduled (§6.1)
        m_background = background;
        // a background change is a visual change, so the root asks for a frame (§6.1)
        if (m_root != nullptr)
            m_root->invalidate();
    }
} // namespace albor
