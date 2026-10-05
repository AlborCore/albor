#pragma once

#include <functional>
#include <utility>

#include "runtime/property.h"

namespace albor
{
    // the unit of work a poster carries onto the UI thread, which is the task signature of §4.1
    using PostedWork = std::function<void()>;

    // the callback a background worker hands its mutation to, which posts onto the UI thread (§4.1, §10.5)
    //
    // the signature is spelled with a plain std::function rather than the application Task alias so that the
    // reactive runtime never includes the application module, which keeps the §7 layer below the §4 layer
    using TaskPoster = std::function<void(PostedWork)>;

    // the thread safe value a worker thread mutates and the UI thread reads (§7.1, §10.5, §13.2)
    template <typename T>
    class Observable
    {
    public:
        // build an observable over an initial value, which a component declares directly (§7.1)
        explicit Observable(T initial_value) :
            m_snapshot(std::make_shared<const T>(std::move(initial_value))) {}

        // build an observable that posts through the given callback, which an application installs (§7.1, §4.1)
        Observable(T initial_value, TaskPoster poster) :
            m_snapshot(std::make_shared<const T>(std::move(initial_value))), m_poster(std::move(poster)) {}

        // build an observable over a snapshot, which a worker already produced (§10.5)
        explicit Observable(Snapshot<T> snapshot) :
            m_snapshot(snapshot ? std::move(snapshot) : std::make_shared<const T>()) {}

        Observable(const Observable&) = delete;
        Observable& operator=(const Observable&) = delete;
        Observable(Observable&&) = delete;
        Observable& operator=(Observable&&) = delete;
        ~Observable() = default;

        // read the current value on the UI thread (§7.1)
        [[nodiscard]] const T& get() const;

        // hand out the immutable snapshot the UI thread reads, which never mutates under it (§10.5, §13.2)
        [[nodiscard]] Snapshot<T> snapshot() const;

        // hand out a fresh mutable copy, which a worker fills before posting it (§10.5, §13.2)
        [[nodiscard]] std::shared_ptr<T> mutable_snapshot() const;

        // mutate on the UI thread, which swaps the snapshot in place (§7.1, §10.5)
        void set(T new_value);

        // swap in a snapshot on the UI thread, which is how a bulk update lands atomically (§10.5, §13.2)
        void set_snapshot(Snapshot<T> snapshot);

        // mutate from a background thread by posting the swap onto the UI thread (§7.1, §4.1, §10.5)
        bool post_set(T new_value);

        // install the callback that carries a posted mutation to the UI thread (§4.1)
        void set_poster(TaskPoster poster);

        // report whether a posted mutation can reach the UI thread, which is false without a poster (§7.1)
        [[nodiscard]] bool postable() const;

        // hand out how many mutations were posted, which a test asserts on (§7.1)
        [[nodiscard]] std::uint64_t post_count() const;

        // compare two observables by the value the UI thread sees
        friend bool operator==(const Observable& lhs, const Observable& rhs) { return lhs.get() == rhs.get(); }

    private:
        // the snapshot the UI thread reads, which a mutation replaces wholesale (§10.5, §13.2)
        Snapshot<T> m_snapshot;

        // the callback that carries a posted mutation to the UI thread (§4.1)
        TaskPoster m_poster;

        // how many mutations were posted, which a test asserts on (§7.1)
        std::uint64_t m_posts = 0;
    };

    template <typename T>
    const T& Observable<T>::get() const {
        // the UI thread reads through the snapshot, which a mutation never mutates in place (§10.5, §13.2)
        return *m_snapshot;
    }
    template <typename T>
    Snapshot<T> Observable<T>::snapshot() const {
        // the snapshot is handed out as a const pointer, so no consumer can write through it (§10.5, §13.2)
        return m_snapshot;
    }
    template <typename T>
    std::shared_ptr<T> Observable<T>::mutable_snapshot() const {
        // a worker receives its own copy, so the live value is never exposed to a write (§10.5, §13.2)
        return std::make_shared<T>(*m_snapshot);
    }
    template <typename T>
    void Observable<T>::set(T new_value) {
        // a UI thread mutation replaces the whole snapshot, which readers observe atomically (§10.5, §13.2)
        m_snapshot = std::make_shared<const T>(std::move(new_value));
    }
    template <typename T>
    void Observable<T>::set_snapshot(Snapshot<T> snapshot) {
        // an empty snapshot would leave the reader with nothing, so it is refused (§10.5)
        if (!snapshot)
            return;
        // the bulk update lands as one swap, so no reader observes a half written value (§10.5, §13.2)
        m_snapshot = std::move(snapshot);
    }
    template <typename T>
    bool Observable<T>::post_set(T new_value) {
        // a mutation without a poster has no way to reach the UI thread, so it is refused (§7.1, §4.1)
        if (!m_poster)
            return false;
        // the new value is captured by move into the posted work, so the worker keeps no reference to it (§10.5)
        m_poster([this, posted = std::move(new_value)]() mutable {
            // the swap runs on the UI thread, where every read of the observable happens (§10.5, §13.2)
            this->set(std::move(posted));
        });
        // the count records the handoff, which a test asserts against (§7.1)
        m_posts++;
        return true;
    }
    template <typename T>
    void Observable<T>::set_poster(TaskPoster poster) {
        // the application installs its task queue as the poster once, at startup (§4.1)
        m_poster = std::move(poster);
    }
    template <typename T>
    bool Observable<T>::postable() const {
        // a posted mutation needs a poster to reach the UI thread (§7.1, §4.1)
        return static_cast<bool>(m_poster);
    }
    template <typename T>
    std::uint64_t Observable<T>::post_count() const {
        return m_posts;
    }
} // namespace albor
