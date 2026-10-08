#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "render/frame_scene.h"
#include "render/render_context.h"
#include "render/renderer_backend.h"

namespace albor
{
    // one surface with the immutable scene a render thread is about to execute (§6.1, AD-18)
    struct FrameRequest
    {
        // the surface the frame presents into
        RenderSurface* surface = nullptr;

        // the immutable scene the render thread executes
        std::shared_ptr<const FrameScene> scene;
    };

    // the second stage of the frame pipeline, which executes immutable scenes off the ui thread (§6.1, AD-18)
    class RenderExecutor
    {
    public:
        // build an executor over a backend, which is never null for a live executor
        explicit RenderExecutor(RendererBackend* backend);

        // stop the render thread and drop every scene that has not executed yet (§6.1)
        ~RenderExecutor();

        RenderExecutor(const RenderExecutor&) = delete;
        RenderExecutor& operator=(const RenderExecutor&) = delete;
        RenderExecutor(RenderExecutor&&) = delete;
        RenderExecutor& operator=(RenderExecutor&&) = delete;

        // hand an immutable scene to the render thread, which drops an older pending frame (§6.1)
        bool submit(RenderSurface& surface, std::shared_ptr<const FrameScene> scene);

        // report whether a scene is waiting for the render thread
        [[nodiscard]] bool pending() const;

        // block until the render thread has no pending frame, which a test synchronizes on (§6.1)
        void flush();

        // stop the render thread after it finishes the frame it already started (§6.1)
        void stop();

        // hand out how many frames the render thread executed
        [[nodiscard]] std::uint64_t executed_count() const;

        // hand out how many frames were dropped because the scene was built at metrics the surface does not present at
        [[nodiscard]] std::uint64_t dropped_count() const;

    private:
        // the render thread body, which executes one scene per available frame (§6.1)
        void run();

        // execute one frame, which is the whole of stage two (§6.1)
        void execute_one();

        // the backend that owns the graphics device
        RendererBackend* m_backend;

        // the render thread, which never runs application code (§6.1)
        std::thread m_thread;

        // the frame waiting to execute, where at most one is kept (§6.1)
        std::deque<FrameRequest> m_requests;

        // the lock every executor operation holds
        mutable std::mutex m_mutex;

        // the condition the ui thread waits on to submit and to flush (§6.1)
        std::condition_variable m_condition;

        // the condition the render thread waits on for the next frame (§6.1)
        std::condition_variable m_frame_condition;

        // how many frames the render thread executed
        std::uint64_t m_executed = 0;

        // how many frames were dropped at stale metrics, which the ui thread answers by resizing the surface (§6.1, D9.1)
        std::uint64_t m_dropped = 0;

        // whether the render thread was asked to stop
        bool m_stopping = false;

        // whether a frame is currently executing, which is what flush waits for (§6.1)
        bool m_busy = false;
    };
} // namespace albor
