#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "core/paint.h"
#include "runtime/binding_engine.h"
#include "text/text_engine.h"
#include "ui/ui_event.h"

namespace albor
{
    class FramePipeline;
    class InputController;
    class RendererBackend;
    class RenderSurface;
    class RenderExecutor;
    class UiNode;
    class Window;

    // the tree a main window drives, which the application builds over the engines it owns (§8.1)
    using RootBuilder = std::function<std::unique_ptr<UiNode>(BindingEngine&, TextEngine&)>;

    // the frame machinery of one main window, which the application wires once per created tree (§6.1, AD-18)
    class WindowStage final
    {
    public:
        // wire a stage over the window's native layer, the shared backend and the shared engine (§6.1, §6.2)
        WindowStage(Window& window, RendererBackend& renderer, TextEngine& text, RootBuilder builder);

        // stop the render thread, which the surface and the pipeline depend on (§6.1)
        ~WindowStage();

        WindowStage(const WindowStage&) = delete;
        WindowStage& operator=(const WindowStage&) = delete;
        WindowStage(WindowStage&&) = delete;
        WindowStage& operator=(WindowStage&&) = delete;

        // report whether the surface, the pipeline, the executor and the input all came up (§6.1)
        [[nodiscard]] bool wired() const;

        // build the scene for the window's metrics and hand it to the render thread, one two stage frame (§6.1, AD-18)
        void run_frame(const Window& window);

        // drain the render thread, resize the surface and remeasure the tree, which is what a resize answers (§5.3, D9.1)
        void resized(RendererBackend& renderer, const Window& window);

        // route one normalized event into the tree, which is where the widgets answer (§8.3)
        void deliver(Event& event);

        // hand out how many frames the stage built, which a test asserts on (§6.1)
        [[nodiscard]] std::uint64_t frame_count() const;

    private:
        // the renderer that owns the surface, which the resize needs (§6.3)
        RendererBackend* m_renderer = nullptr;

        // the engine the stage settles before every frame, which the tree declares properties over (§7.3)
        BindingEngine m_engine;

        // the wiring, declared so the destructor releases the pipeline first and the surface last (§6.1)
        std::unique_ptr<RenderSurface> m_surface;
        std::unique_ptr<RenderExecutor> m_executor;
        std::unique_ptr<InputController> m_input;
        std::unique_ptr<FramePipeline> m_pipeline;
    };
} // namespace albor
