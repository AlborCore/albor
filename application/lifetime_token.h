#pragma once

#include <atomic>
#include <memory>

namespace albor
{
    // the shared state a lifetime token observes, so a token stays safe after its source is gone (§4.2)
    class LifetimeState
    {
    public:
        // report whether the source is still alive
        [[nodiscard]] bool alive() const;

        // invalidate the state, which every token of the source observes at once
        void expire();

    private:
        // the liveness flag, which a token reads from any thread
        std::atomic<bool> m_alive{true};
    };

    // the non owning half of a lifetime token, which a background task holds across threads (§4.2)
    class LifetimeToken
    {
    public:
        // the default token is expired, because it guards nothing
        LifetimeToken() = default;

        // report whether the guard still protects a live source
        [[nodiscard]] bool alive() const;

        // report whether the guard is expired, which is what a dropped task checks (§4.2)
        [[nodiscard]] bool expired() const;

        // compare two tokens by identity of the state they observe
        friend bool operator==(const LifetimeToken& lhs, const LifetimeToken& rhs) = default;

    private:
        // build a token over a shared state, which only a source does
        explicit LifetimeToken(std::shared_ptr<LifetimeState> state);

        // the observed state, which stays alive as long as the source holds it
        std::shared_ptr<LifetimeState> m_state;

        friend class LifetimeTokenSource;
    };

    // the owner of a lifetime token, which invalidates every token it handed out when it dies (§4.2)
    class LifetimeTokenSource
    {
    public:
        LifetimeTokenSource();

        // invalidate the source before it is destroyed, so a task already queued observes it
        ~LifetimeTokenSource();

        LifetimeTokenSource(const LifetimeTokenSource&) = delete;
        LifetimeTokenSource& operator=(const LifetimeTokenSource&) = delete;
        LifetimeTokenSource(LifetimeTokenSource&&) = delete;
        LifetimeTokenSource& operator=(LifetimeTokenSource&&) = delete;

        // hand out a token that observes this source
        [[nodiscard]] LifetimeToken token() const;

        // report whether the source is still alive
        [[nodiscard]] bool alive() const;

        // invalidate the source early, which drops every queued task that carries its token (§4.2)
        void invalidate();

    private:
        // the state every token of this source observes
        std::shared_ptr<LifetimeState> m_state;
    };
} // namespace albor
