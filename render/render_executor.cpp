#include "render/render_executor.h"

#include "render/frame_profile.h"

namespace albor
{
    RenderExecutor::RenderExecutor(RendererBackend* backend) :
        m_backend(backend) {
        // an executor without a backend has nothing to execute with, so it never starts a thread (§6.1)
        if (m_backend == nullptr) {
            m_stopping = true;
            return;
        }

        // stage two runs on its own thread, so the ui thread is never blocked by drawing (§6.1, AD-18b)
        m_thread = std::thread([this] { run(); });
    }

    RenderExecutor::~RenderExecutor() {
        // destruction stops the render thread, so no callback outlives the executor (§6.1)
        stop();
    }

    bool RenderExecutor::submit(RenderSurface& surface, std::shared_ptr<const FrameScene> scene) {
        // without a backend there is no render thread to hand the scene to (§6.1)
        if (m_backend == nullptr)
            return false;
        // a scene is immutable, so it crosses the thread boundary as a const shared pointer (§10.5, §13.2)
        if (!scene)
            return false;
        {
            // the lock is taken because the render thread pops while the ui thread submits (§6.1)
            const std::lock_guard<std::mutex> lock(m_mutex);
            // a stopped executor accepts nothing, because its thread is gone (§6.1)
            if (m_stopping)
                return false;
            // a pending frame the new one replaces is a frame the ui thread dropped, which the profile counts (§6.1)
            if (!m_requests.empty())
                profile_coalesced();
            // an older pending frame is dropped, so the render thread always draws the newest state (§6.1)
            m_requests.clear();
            // append to list of pending frame requests
            m_requests.emplace_back(&surface, scene);
        }
        // the render thread is woken so that the frame is presented without waiting for the next native event (§6.1)
        m_frame_condition.notify_one();
        // indicate scene was enqueued
        return true;
    }

    bool RenderExecutor::pending() const {
        // the lock is taken because the render thread pops while the ui thread asks (§6.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        // check queue is empty
        return !m_requests.empty();
    }

    void RenderExecutor::flush() {
        // the lock is taken so that the predicate cannot change between the check and the wait (§6.1)
        std::unique_lock<std::mutex> lock(m_mutex);
        // the wait covers both a queued frame and a frame that is currently executing (§6.1)
        m_condition.wait(lock, [this] { return m_requests.empty() && !m_busy; });
    }

    void RenderExecutor::stop() {
        {
            // the lock is taken because the render thread reads the stopping flag while it waits (§6.1)
            const std::lock_guard<std::mutex> lock(m_mutex);
            // stopping twice would join a thread twice, so the second call is a no operation (§6.1)
            if (m_stopping)
                return;
            m_stopping = true;
        }
        // the render thread is woken so that it observes the stopping flag and returns (§6.1)
        m_frame_condition.notify_all();
        // a thread that was never started cannot be joined, so only a live one is (§6.1)
        if (m_thread.joinable())
            m_thread.join();
    }

    std::uint64_t RenderExecutor::executed_count() const {
        // the lock is taken because the render thread counts while the ui thread reads (§6.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_executed;
    }

    std::uint64_t RenderExecutor::dropped_count() const {
        // the lock is taken because the render thread counts while the ui thread reads (§6.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

    void RenderExecutor::run() {
        // the loop ends only when the ui thread asked it to, so a pending frame never keeps it alive (§6.1)
        while (true) {
            {
                // the lock is taken because the ui thread submits and stops while the render thread waits (§6.1)
                std::unique_lock<std::mutex> lock(m_mutex);
                // the wait ends on a new frame or on the stopping flag, never on a timer (§6.1)
                m_frame_condition.wait(lock, [this] { return !m_requests.empty() || m_stopping; });
                // a stop with no pending frame ends the thread immediately (§6.1)
                if (m_stopping && m_requests.empty())
                    return;
            }
            // the frame runs without the lock, so a submit from the ui thread never blocks on drawing (§6.1)
            execute_one();
        }
    }

    void RenderExecutor::execute_one() {
        FrameRequest request;
        {
            // the lock is taken only long enough to take the frame and mark the executor busy (§6.1)
            const std::lock_guard<std::mutex> lock(m_mutex);
            // a frame queued while this one executed is left for the next pass (§6.1)
            if (m_requests.empty())
                return;
            request = std::move(m_requests.front());
            m_requests.pop_front();
            m_busy = true;
        }
        // a frame for a surface that no longer exists cannot be drawn, so it is dropped (§6.1)
        if (request.surface == nullptr || !request.scene) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_busy = false;
            m_condition.notify_all();
            return;
        }
        // a scene at stale metrics is dropped rather than repaired, because a repair resizes the surface here (§6.1, D9.1)
        if (request.surface->metrics() != request.scene->metrics()) {
            // the drop is counted and the flush waiters released under the lock (§6.1)
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_dropped++;
            m_busy = false;
            m_condition.notify_all();
            return;
        }
        // stage two only touches the immutable scene and the surface, never application state (§6.1, AD-18)
        const FrameProfileScope render_scope;
        m_backend->execute_frame(*request.surface, *request.scene);
        // the whole stage two call is what the render thread spends on the frame, which the profile reports (§6.1)
        profile_render_frame(render_scope.elapsed_ms());
        {
            // the lock is taken to publish the count and to release the flush waiters (§6.1)
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_executed++;
            m_busy = false;
        }
        // a ui thread blocked in flush is released now that the frame is done (§6.1)
        m_condition.notify_all();
    }
} // namespace albor
