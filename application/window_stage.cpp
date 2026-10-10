#include "application/window_stage.h"

#include "input/input_controller.h"
#include "render/render_executor.h"
#include "ui/frame_pipeline.h"
#include "window/window.h"

namespace albor
{
    namespace
    {
        // the colour a main window clears its scene with, which the widgets read against (§6.1)
        const Color STAGE_BACKGROUND = Color::rgb(0.05f, 0.06f, 0.08f);
    } // namespace

    WindowStage::WindowStage(Window& window, RendererBackend& renderer, TextEngine& text, RootBuilder builder) :
        m_renderer(&renderer) {
        // the tree is built once over the engines, because the widgets read them for the whole run (§8.1)
        std::unique_ptr<UiNode> root = builder ? builder(m_engine, text) : std::make_unique<UiNode>();
        // stage one owns the tree and the layout and record passes (§6.1)
        m_pipeline = std::make_unique<FramePipeline>(m_engine, std::move(root));
        m_pipeline->set_background(STAGE_BACKGROUND);
        // stage two runs off the ui thread, so the ui thread is never blocked by drawing (§6.1, AD-18b)
        m_executor = std::make_unique<RenderExecutor>(m_renderer);
        m_pipeline->set_executor(m_executor.get());
        // the surface wraps the window's native layer, which the backend refuses when it cannot attach (§6.3, AD-16)
        m_surface = m_renderer->create_surface(window.native_handle(), window.metrics());
        if (!m_surface)
            return;
        m_pipeline->set_surface(m_surface.get());
        // the controller routes the normalized events into the tree (§8.3)
        m_input = std::make_unique<InputController>();
        m_input->attach(m_pipeline->root());
    }

    WindowStage::~WindowStage() = default;

    bool WindowStage::wired() const {
        // every access dereferences this wiring, so the stage answers as one (§6.1)
        return m_surface != nullptr && m_pipeline != nullptr && m_executor != nullptr && m_input != nullptr;
    }

    void WindowStage::run_frame(const Window& window) {
        // an unwired stage has nothing to build, which the window betrayed at creation time (§6.1)
        if (!wired())
            return;
        // stage one settles the graph and records the scene, stage two executes it (§6.1, AD-18)
        m_pipeline->submit(m_pipeline->build(window.metrics()));
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

    void WindowStage::deliver(Event& event) {
        // an unwired stage has no input to deliver, which the window betrayed at creation time (§8.3)
        if (!m_input)
            return;
        m_input->deliver(event);
    }

    std::uint64_t WindowStage::frame_count() const {
        // an unwired stage built nothing, which is the count it reports (§6.1)
        return m_pipeline ? m_pipeline->frame_count() : 0;
    }
} // namespace albor
