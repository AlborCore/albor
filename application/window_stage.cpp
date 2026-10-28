#include "application/window_stage.h"

#include <chrono>

#include "core/dev_hook.h"
#include "input/input_controller.h"
#include "render/frame_profile.h"
#include "render/frame_scene.h"
#include "render/render_executor.h"
#include "ui/frame_pipeline.h"
#include "ui/ui_node.h"
#include "window/window.h"

namespace albor
{
    WindowStage::WindowStage(Window& window, RendererBackend& renderer, TextEngine& text, RootBuilder builder, const MainWindowConfig& config) :
        m_renderer(&renderer), m_window(&window), m_text(&text), m_always_render(config.always_render), m_scene_source(std::move(config.scene_source)) {
        // the tree is built once over the engines, because the widgets read them for the whole run (§8.1)
        std::unique_ptr<UiNode> root = builder ? builder(m_engine, text) : nullptr;
        // an application that draws by hand brings no tree, which is what the scene source is for (§6.1)
        m_has_tree = root != nullptr;
        // stage one owns the tree and the record pass, and an empty root is a window that draws by hand (§6.1)
        m_pipeline = std::make_unique<FramePipeline>(m_engine, root ? std::move(root) : std::make_unique<UiNode>());
        m_pipeline->set_background(config.background);
        // an engine that draws outlines hands the pipeline the resolver its embedded runs need (§6.1, §6.4, AD-34)
        m_pipeline->set_outline_resolver(text.glyph_outline_resolver());
        // stage two runs off the ui thread, so the ui thread is never blocked by drawing (§6.1, AD-18b)
        m_executor = std::make_unique<RenderExecutor>(m_renderer);
        m_pipeline->set_executor(m_executor.get());
        // the surface wraps the window's native layer, which the backend refuses when it cannot attach (§6.3, AD-16)
        m_surface = m_renderer->create_surface(window.native_handle(), window.metrics());
        if (!m_surface)
            return;
        m_pipeline->set_surface(m_surface.get());
        // the frame tick advances the animations, and their writes settle before the scene is recorded (§7.3, AD-04d)
        m_pipeline->set_animation_engine(&m_animations);
        // an animation asks the window for the frame its next pass needs, which is the window's pacing tick (§6.1, §7.3)
        m_animations.set_invalidation_handler([&window] { window.request_frame(); });
        // a settled graph reaches the same tick, because an invalidation is what a frame request means (§6.1, §7.3)
        m_pipeline->set_frame_request([&window] { window.request_frame(); });
        // the first delta is measured from now, because no frame has been built yet (§7.3)
        m_last_frame = std::chrono::steady_clock::now();
        // the controller routes the normalized events into the tree (§8.3)
        m_input = std::make_unique<InputController>();
        m_input->attach(m_pipeline->root());
        // a popup belongs to its control rather than the automation root, so it keeps the main binding (§10.3, §5.1)
        if (window.options().role != WindowRole::Popup) {
            // every main window offers its root, because automation observes a hand drawn scene too (§10.3)
            const dev::WindowBinding binding{.root = &m_pipeline->root(), .engine = &m_engine, .text = m_text, .surface = m_surface.get(), .executor = m_executor.get(), .has_tree = m_has_tree};
            dev::bind_root(binding);
        }
    }

    WindowStage::~WindowStage() {
        // the dev extension is told the tree went away, because it must never patch a destroyed window (§12.1, §5.2)
        if (m_surface && m_pipeline && m_window != nullptr && m_window->options().role != WindowRole::Popup)
            dev::unbind_root(&m_pipeline->root());
    }

    bool WindowStage::wired() const {
        // every access dereferences this wiring, so the stage answers as one (§6.1)
        return m_surface != nullptr && m_pipeline != nullptr && m_executor != nullptr && m_input != nullptr;
    }

    void WindowStage::run_frame(const Window& window) {
        // an unwired stage has nothing to build, which the window betrayed at creation time (§6.1)
        if (!wired())
            return;
        // the tick interval is recorded before the frame work, because a stalled ui thread shows there first (§6.1)
        profile_ui_tick();
        // an application that owns its scene builds it here, and stage two still executes it off the ui thread (§6.1)
        if (m_scene_source) {
            const FrameProfileScope build_scope;
            const std::shared_ptr<const FrameScene> scene = m_scene_source(window.metrics(), m_frame_index);
            // the build time is read before the scene is checked, because a source may draw for a while and return nothing
            const double build_ms = build_scope.elapsed_ms();
            // a source that produced nothing has no frame to present, which is what an empty tick means (§6.1)
            if (!scene)
                return;
            if (m_frame_hook)
                m_frame_hook();
            const FrameProfileScope submit_scope;
            m_executor->submit(*m_surface, scene);
            // the source path reports the same split as the tree path, so the two runs stay comparable (§6.1)
            profile_ui_frame(build_ms, submit_scope.elapsed_ms());
            m_frame_index++;
            return;
        }
        // the instant is read first, because the delta is measured against the frame before this one (§7.3)
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        // an idle tree skips the advance, which is what keeps a window with no animations cheap (§7.3)
        if (m_animations.active_count() == 0) {
            // the engine is told the gap happened, so an animation scheduled during it starts at the next frame (§7.3)
            m_animations.begin_idle();
        }
        else {
            // the animations run before the scene is recorded, so the frame carries the values they wrote (§7.3, AD-04d)
            m_pipeline->advance_animations(std::chrono::duration<float>(now - m_last_frame).count());
        }
        // the frame is dated, because the next delta is measured from it (§7.3)
        m_last_frame = now;
        // stage one settles the graph and records the scene, stage two executes it (§6.1, AD-18)
        const FrameProfileScope build_scope;
        const std::shared_ptr<const FrameScene> scene = m_always_render ? m_pipeline->build(window.metrics()) : m_pipeline->build_if_dirty(window.metrics());
        // the build time is read before the scene is checked, because a clean tree still pays the settle and the record
        const double build_ms = build_scope.elapsed_ms();
        // a retained tree that found nothing dirty produces no scene, so the tick presents nothing (§6.1)
        if (!scene)
            return;
        // whatever an application derived from the settled graph is refreshed before the scene is presented (§6.1, §10.1)
        if (m_frame_hook)
            m_frame_hook();
        const FrameProfileScope submit_scope;
        m_pipeline->submit(scene);
        // the build and the submit are what the ui thread spends on the frame, which the profile reports (§6.1)
        profile_ui_frame(build_ms, submit_scope.elapsed_ms());
        m_frame_index++;
    }

    void WindowStage::resized(RendererBackend& renderer, const Window& window) {
        // a stage that never drew has no surface to resize, which the layout invalidation still answers (§6.3)
        if (!wired())
            return;
        // the executor drains first, because the surface may still be in a frame (§6.1, D9.1)
        m_executor->flush();
        // the surface takes the metrics the window reported, which the next scene is built for (§5.3, D9.1)
        renderer.resize_surface(*m_surface, window.metrics());
        // the tree is remeasured, because the window now presents at other metrics (§8.2)
        m_pipeline->root().invalidate_layout();
    }

    void WindowStage::cancel_pointer_capture() {
        // an unwired stage has no input to cancel, which the window betrayed at creation time (§8.3)
        if (!m_input)
            return;
        // the controller drops the drag, because the window it belonged to left the screen (§8.3)
        m_input->cancel_pointer_capture();
    }

    void WindowStage::deliver(Event& event) {
        // an unwired stage has no input to deliver, which the window betrayed at creation time (§8.3)
        if (!m_input)
            return;
        // the event routes first, which is what the caret the anchor follows answers (§8.3)
        m_input->deliver(event);
        // the focused node is where the caret lives, which the anchor is read against (§8.3)
        const UiNode* focused = m_input->focus().focused();
        const CaretTarget* target = dynamic_cast<const CaretTarget*>(focused);
        // a focus with no caret has no anchor to update, which is what keeps the last one (§8.3)
        if (target != nullptr)
            m_window->set_text_anchor(target->caret_rect());
        // a focus that edits a document publishes its selection, which is what an input method replaces by range (§8.3)
        if (const auto* document = dynamic_cast<const TextInputTarget*>(focused); document != nullptr)
            m_window->set_text_selection(document->selection_range());
    }

    void WindowStage::set_command_registry(CommandRegistry* registry) {
        // a stage that never wired has no input to route a key through, which the window betrayed at creation (§18.5)
        if (!m_input)
            return;
        // the registry is borrowed by the controller, which routes accelerators through it (§18.5)
        m_input->set_command_registry(registry);
    }

    UiNode& WindowStage::root() const {
        // the tree is the one the pipeline drives, because the two stages are its two halves (§6.1, AD-18)
        return m_pipeline->root();
    }

    bool WindowStage::has_tree() const {
        // an application that draws by hand brought no tree, which is what the scene source is for (§6.1, §8.1)
        return m_has_tree;
    }

    void WindowStage::set_background(Color background) {
        // an unwired stage clears nothing, which is the state it was refused in (§6.1)
        if (!wired())
            return;
        // the background is what stage one clears the surface with, which a themed surface writes (§6.1, §8.4)
        m_pipeline->set_background(background);
    }

    void WindowStage::set_frame_hook(std::function<void()> hook) {
        // the hook is owned here, because it runs on every frame of this window and nowhere else (§6.1)
        m_frame_hook = std::move(hook);
    }

    std::uint64_t WindowStage::frame_count() const {
        // a tick that produced no scene counted no frame, which is what this reports (§6.1)
        return m_frame_index;
    }

    BindingEngine& WindowStage::binding_engine() {
        // the engine is a member, so a caller holding this reference never sees it replaced (§7.3)
        return m_engine;
    }

    TextEngine& WindowStage::text_engine() {
        // the engine is owned by the application and outlives the stage, so the borrow stays valid (§9.1)
        return *m_text;
    }

    AnimationEngine& WindowStage::animations() {
        // the engine is a member, so a widget holding this reference never sees it replaced (§7.3)
        return m_animations;
    }
} // namespace albor
