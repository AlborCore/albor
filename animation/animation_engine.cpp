#include "animation/animation_engine.h"

#include <algorithm>
#include <utility>

namespace albor
{
    AnimationHandle AnimationEngine::schedule(AnimationTick tick, AnimationDuration duration) {
        // an animation with no work cannot be advanced, so the registration is refused (§7.3)
        if (!tick)
            return 0;
        // a zero duration would complete on the first frame without a usable progress, so it is refused (§7.3)
        if (duration <= AnimationDuration::zero())
            return 0;
        // the single pass animation is the repeating one with nothing left to repeat (§7.3)
        return register_animation(std::move(tick), duration, 0);
    }

    AnimationHandle AnimationEngine::schedule_repeating(AnimationTick tick, AnimationDuration duration, AnimationRepeats repeats) {
        // a repeating animation refuses exactly what a single pass one refuses, because a pass is still a pass (§7.3)
        if (!tick)
            return 0;
        // a zero duration would restart on every frame without a usable progress, so it is refused (§7.3)
        if (duration <= AnimationDuration::zero())
            return 0;
        // the animation is registered over the passes it was asked for (§7.3)
        return register_animation(std::move(tick), duration, repeats);
    }

    AnimationHandle AnimationEngine::register_animation(AnimationTick tick, AnimationDuration duration, AnimationRepeats repeats) {
        // the handle is taken before the entry is stored, because the entry records its own handle (§7.3)
        const AnimationHandle handle = m_next_handle++;
        // the entry starts at zero elapsed and active, which is what a just scheduled animation is (§7.3)
        m_animations.push_back(Running{.handle = handle, .tick = std::move(tick), .duration = duration, .elapsed = 0.0f, .repeats = repeats, .active = true});
        // the first frame is requested, because nothing has been drawn yet at the starting progress (§6.1, §7.3)
        if (m_invalidation)
            m_invalidation();
        return handle;
    }

    bool AnimationEngine::cancel(AnimationHandle handle) {
        // locate the animation the handle names (§7.3)
        Running* running = find(handle);
        // a handle that names no running animation has nothing to end (§7.3)
        if (running == nullptr)
            return false;
        // the animation is marked finished, so the next advance drops it (§7.3)
        running->active = false;
        // the frame is requested, because a cancelled animation leaves a value the tree must redraw (§6.1, §7.3)
        if (m_invalidation)
            m_invalidation();
        return true;
    }

    bool AnimationEngine::running(AnimationHandle handle) const {
        // an animation is running when its entry is still active (§7.3)
        const Running* running = find(handle);
        // a handle that names no entry is not running (§7.3)
        if (running == nullptr)
            return false;
        return running->active;
    }

    std::size_t AnimationEngine::active_count() const {
        // only the active entries count, because a finished one is waiting to be dropped (§7.3)
        return static_cast<std::size_t>(std::count_if(m_animations.begin(), m_animations.end(), [](const Running& running) { return running.active; }));
    }

    std::size_t AnimationEngine::advance(float delta_seconds) {
        // the finished entries are dropped, which is the whole cleanup of an advance (§7.3)
        m_animations.erase(std::remove_if(m_animations.begin(), m_animations.end(), [](const Running& running) { return !running.active; }), m_animations.end());
        // a frame with nothing running does no work, which is what keeps an idle tree cheap (§7.3)
        if (m_animations.empty())
            return 0;
        // the tick is invoked once per running animation, so the loop counts what it advanced (§7.3)
        std::size_t ticked = 0;
        for (auto& running : m_animations) {
            // the elapsed time accumulates against the duration, which is what drives the progress (§7.3)
            running.elapsed += delta_seconds;
            // the duration is read per entry because each animation may have its own (§7.3)
            const float span = static_cast<float>(std::chrono::duration<float>(running.duration).count());
            // schedule refused a non positive duration, so the divisor is always usable and the clamp is what keeps a frame that outran the animation from reporting past the end (§7.3)
            const float normalized = std::clamp(running.elapsed / span, 0.0f, 1.0f);
            // the work runs, which is what writes through the animation channel of §7.3 (AD-04d)
            const bool continues = running.tick(normalized);
            // a tick that continues keeps the animation alive for the next frame (§7.3)
            if (continues)
                ticked++;
            // a tick that refuses ends the animation immediately, which is the early exit of §7.3
            if (!continues)
                running.active = false;
            // an animation out of passes is finished, which the drop below removes (§7.3)
            if (normalized >= 1.0f && running.repeats == 0)
                running.active = false;
            // an animation with a pass left rewinds instead, so the next frame reports the progress of that pass (§7.3)
            if (normalized >= 1.0f && running.repeats > 0) {
                // a finite repeat count is spent by the pass it just ran, while the forever count is never spent (§7.3)
                if (running.repeats != ANIMATION_FOREVER)
                    running.repeats--;
                // the elapsed time rewinds by one span, which is what restarts the progress from zero (§7.3)
                running.elapsed -= span;
            }
        }
        // the finished entries are dropped immediately, so active_count never reports a finished animation (§7.3)
        m_animations.erase(std::remove_if(m_animations.begin(), m_animations.end(), [](const Running& running) { return !running.active; }), m_animations.end());
        // the frame is requested, because an animation that is still running needs another one (§6.1, §7.3)
        if (!m_animations.empty() && m_invalidation)
            m_invalidation();
        return ticked;
    }

    float AnimationEngine::progress(AnimationHandle handle) const {
        // locate the animation the handle names (§7.3)
        const Running* running = find(handle);
        // a handle that names no animation reports no progress (§7.3)
        if (running == nullptr)
            return 0.0f;
        // the duration is read as seconds, because the elapsed time is kept in seconds, and schedule already refused a non positive one, so the divisor is always usable (§7.3)
        const float span = static_cast<float>(std::chrono::duration<float>(running->duration).count());
        // the running animation has not finished, so the ratio is inside the unit interval without a clamp (§7.3)
        return running->elapsed / span;
    }

    void AnimationEngine::set_invalidation_handler(std::function<void()> handler) {
        // the handler is installed on the engine, which the frame pipeline drives (§6.1, §7.3)
        m_invalidation = std::move(handler);
    }

    void AnimationEngine::cancel_all() {
        // an engine with nothing running clears nothing, so no frame is requested (§7.3)
        if (m_animations.empty())
            return;
        // every entry is dropped at once, which is what a teardown wants (§7.3)
        m_animations.clear();
        // the frame is requested, because the cancelled animations left values the tree must redraw (§6.1, §7.3)
        if (m_invalidation)
            m_invalidation();
    }

    AnimationEngine::Running* AnimationEngine::find(AnimationHandle handle) {
        // the first entry with the handle is the animation, because handles are unique (§7.3)
        const auto found = std::find_if(m_animations.begin(), m_animations.end(), [handle](const Running& running) { return running.handle == handle; });
        // a handle that names no entry has no animation (§7.3)
        if (found == m_animations.end())
            return nullptr;
        return &*found;
    }

    const AnimationEngine::Running* AnimationEngine::find(AnimationHandle handle) const {
        // the first entry with the handle is the animation, because handles are unique (§7.3)
        const auto found = std::find_if(m_animations.begin(), m_animations.end(), [handle](const Running& running) { return running.handle == handle; });
        // a handle that names no entry has no animation (§7.3)
        if (found == m_animations.end())
            return nullptr;
        return &*found;
    }
} // namespace albor
