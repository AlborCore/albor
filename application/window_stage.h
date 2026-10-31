#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "animation/animation_engine.h"
#include "application/main_window_config.h"
#include "core/paint.h"
#include "runtime/binding_engine.h"
#include "text/text_engine.h"
#include "ui/caret_target.h"
#include "ui/ui_event.h"

namespace albor
{
    class CommandRegistry;
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
        WindowStage(Window& window, RendererBackend& renderer, TextEngine& text, RootBuilder builder, const MainWindowConfig& config);

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

        // drop the pointer capture when the window leaves the screen, which ends a press that closed it (§8.3)
        void cancel_pointer_capture();

        // hand out the root of the driven tree, which is what an application installs a theme over (§6.1, §8.4)
        [[nodiscard]] UiNode& root() const;

        // report whether the window brought a tree, which a hand drawn scene source does not (§6.1, §8.1)
        [[nodiscard]] bool has_tree() const;

        // replace the colour the scene is cleared with, which a themed surface drives (§6.1, §8.4)
        void set_background(Color background);

        // point the window's input at the accelerators of an application, which is borrowed and outlives the stage (§18.5)
        void set_command_registry(CommandRegistry* registry);

        // install the callback that runs between a built scene and stage two, which refreshes derived state (§6.1)
        void set_frame_hook(std::function<void()> hook);

        // hand out how many frames the stage presented, which a test asserts on (§6.1)
        [[nodiscard]] std::uint64_t frame_count() const;

        // hand out the reactive engine the stage settles before every frame, which a host binds over (§7.3)
        [[nodiscard]] BindingEngine& binding_engine();

        // hand out the text engine the tree measures with, which a host reads (§9.1)
        [[nodiscard]] TextEngine& text_engine();

        // hand out the animation engine the frame tick advances, which a widget drives its animations on (§7.3, AD-04d)
        [[nodiscard]] AnimationEngine& animations();

    private:
        // the renderer that owns the surface, which the resize needs (§6.3)
        RendererBackend* m_renderer = nullptr;

        // the window the stage drives, which the text anchor reaches through (§8.3)
        Window* m_window = nullptr;

        // the engine the stage settles before every frame, which the tree declares properties over (§7.3)
        BindingEngine m_engine;

        // the engine the tree measures its text with, which every widget borrows for the whole run (§9.1)
        TextEngine* m_text = nullptr;

        // the animations the frame tick advances, which the tree schedules its own on (§7.3, AD-04d)
        AnimationEngine m_animations;

        // the instant the last frame was dated at, which the next frame delta is measured from (§7.3)
        std::chrono::steady_clock::time_point m_last_frame;

        // the number of frames this stage presented, which counts scenes rather than ticks (§6.1)
        std::uint64_t m_frame_index = 0;

        // whether a builder produced a tree, which is what decides between the pipeline and a scene source (§8.1)
        bool m_has_tree = false;

        // whether every tick renders even when the tree is clean, which is the explicit exception (§6.1)
        bool m_always_render = false;

        // the source an application that owns its scene installs, which replaces the pipeline's own build (§6.1)
        SceneSource m_scene_source;

        // the callback that runs between a built scene and stage two, which is where derived state lands (§6.1)
        std::function<void()> m_frame_hook;

        // the wiring, declared so the destructor releases the pipeline first and the surface last (§6.1)
        std::unique_ptr<RenderSurface> m_surface;
        std::unique_ptr<RenderExecutor> m_executor;
        std::unique_ptr<InputController> m_input;
        std::unique_ptr<FramePipeline> m_pipeline;
    };
} // namespace albor
