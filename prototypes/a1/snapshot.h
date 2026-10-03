#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace albor::a1
{

    namespace detail
    {

        // the tiny spin lock that guards a snapshot swap, because libc++ has no atomic shared_ptr yet
        class SpinLock
        {
        public:
            SpinLock() = default;

            SpinLock(const SpinLock&) = delete;
            SpinLock& operator=(const SpinLock&) = delete;
            SpinLock(SpinLock&&) = delete;
            SpinLock& operator=(SpinLock&&) = delete;

            // take the lock, spinning until it is free
            void lock() const {
                while (m_held.test_and_set(std::memory_order_acquire)) {
                    // a pause hint keeps two threads from fighting the lock as hard as possible
                    std::atomic_thread_fence(std::memory_order_acquire);
                }
            }

            // release the lock so the next reader can take it
            void unlock() const { m_held.clear(std::memory_order_release); }

        private:
            // the flag that carries the ownership
            mutable std::atomic_flag m_held = ATOMIC_FLAG_INIT;
        };

        // the guard that releases a spin lock however the scope is left
        class SpinLockGuard
        {
        public:
            // take the lock for the lifetime of the guard
            explicit SpinLockGuard(const SpinLock& lock) :
                m_lock(lock) {
                m_lock.lock();
            }

            ~SpinLockGuard() { m_lock.unlock(); }

            SpinLockGuard(const SpinLockGuard&) = delete;
            SpinLockGuard& operator=(const SpinLockGuard&) = delete;

        private:
            // the lock this guard owns
            const SpinLock& m_lock;
        };

    } // namespace detail

    // the immutable snapshot that reaches the custom render path, because only a swap may cross a thread (§10.5, §13.2)
    template <typename T>
    class Snapshot
    {
    public:
        Snapshot() = default;

        Snapshot(const Snapshot&) = delete;
        Snapshot& operator=(const Snapshot&) = delete;
        Snapshot(Snapshot&&) = delete;
        Snapshot& operator=(Snapshot&&) = delete;

        // post a new immutable value, which is the thread safe path a producer uses (§13.2)
        void post_set(std::shared_ptr<const T> value) {
            // take the old value out under the lock and release it outside, so no deleter runs while held
            std::shared_ptr<const T> retired;
            {
                const detail::SpinLockGuard guard(m_lock);
                retired = std::move(m_value);
                m_value = std::move(value);
            }
        }

        // read the current immutable value, which every thread may do at any time
        [[nodiscard]] std::shared_ptr<const T> get() const {
            // copy the pointer out under the lock, which only bumps a refcount
            std::shared_ptr<const T> value;
            {
                const detail::SpinLockGuard guard(m_lock);
                value = m_value;
            }

            // hand the snapshot back, so the caller owns a stable value no matter what the producer posts next
            return value;
        }

    private:
        // the lock that makes the swap atomic, and the value every reader observes
        mutable detail::SpinLock m_lock;
        std::shared_ptr<const T> m_value;
    };

} // namespace albor::a1
