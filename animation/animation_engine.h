#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace albor
{
    // the identifier of a running animation, where zero names no animation (§7.3)
    using AnimationHandle = std::uint64_t;

    // how long an animation runs, which the frame delta accumulates against (§7.3)
    using AnimationDuration = std::chrono::milliseconds;

    // the work one animation performs, which receives the progress from zero to one and reports whether it continues (§7.3)
    using AnimationTick = std::function<bool(float)>;

    // the engine that advances every running animation from the frame scheduler (§7.3, AD-04d)
    class AnimationEngine
    {
    public:
        // build an engine over no animations, which schedule then adds (§7.3)
        AnimationEngine() = default;

        AnimationEngine(const AnimationEngine&) = delete;
        AnimationEngine& operator=(const AnimationEngine&) = delete;
        AnimationEngine(AnimationEngine&&) = delete;
        AnimationEngine& operator=(AnimationEngine&&) = delete;
        ~AnimationEngine() = default;

        // register a time based animation and hand back its handle, which cancel and running then key on (§7.3)
        [[nodiscard]] AnimationHandle schedule(AnimationTick tick, AnimationDuration duration);

        // end an animation early, which reports whether the handle named a running animation (§7.3)
        bool cancel(AnimationHandle handle);

        // report whether the handle still names a running animation (§7.3)
        [[nodiscard]] bool running(AnimationHandle handle) const;

        // hand out how many animations are running, which a test asserts on (§7.3)
        [[nodiscard]] std::size_t active_count() const;

        // advance every running animation and drop the finished ones, which the frame pipeline calls (§7.3, §6.1)
        std::size_t advance(float delta_seconds);

        // hand out the normalized progress of a running animation, which a test asserts on (§7.3)
        [[nodiscard]] float progress(AnimationHandle handle) const;

        // hand out the callback a running or finished animation asks for, which drives another frame (§6.1, §7.3)
        void set_invalidation_handler(std::function<void()> handler);

        // end every running animation, which a teardown does before the tree is destroyed (§7.3)
        void cancel_all();

    private:
        // one registered animation with the progress it has reached (§7.3)
        struct Running
        {
            // the handle the animation is registered under, which is what cancel and running key on (§7.3)
            AnimationHandle handle = 0;

            // the work the animation performs on every frame (§7.3)
            AnimationTick tick;

            // how long the animation runs in total (§7.3)
            AnimationDuration duration{0};

            // how much of the duration has elapsed, which the frame delta accumulates (§7.3)
            float elapsed = 0.0f;

            // whether the animation is still running, which cancel clears (§7.3)
            bool active = true;
        };

        // locate a running animation by handle, returning null when the handle names none (§7.3)
        [[nodiscard]] Running* find(AnimationHandle handle);

        // locate a running animation by handle for reading, which const callers use (§7.3)
        [[nodiscard]] const Running* find(AnimationHandle handle) const;

        // the registered animations, in scheduling order (§7.3)
        std::vector<Running> m_animations;

        // the handle the next scheduled animation receives, which starts at one so zero names none (§7.3)
        AnimationHandle m_next_handle = 1;

        // the callback an animation asks for, which is what drives the next frame (§6.1, §7.3)
        std::function<void()> m_invalidation;
    };
} // namespace albor
