#include "application/lifetime_token.h"

namespace albor
{
    bool LifetimeState::alive() const {
        // the flag is read with acquire ordering, so a token never observes a source that is already gone (§4.2)
        return m_alive.load(std::memory_order_acquire);
    }

    void LifetimeState::expire() {
        // release ordering publishes the expiry to every token that reads the flag afterwards (§4.2)
        m_alive.store(false, std::memory_order_release);
    }

    bool LifetimeToken::alive() const {
        // a token over no state protects nothing, so the task carrying it is dropped (§4.2)
        if (!m_state)
            return false;
        return m_state->alive();
    }

    bool LifetimeToken::expired() const {
        // expiry is the absence of life, which is the only condition a queued task checks (§4.2)
        return !alive();
    }

    LifetimeToken::LifetimeToken(std::shared_ptr<LifetimeState> state) :
        m_state(std::move(state)) {}

    LifetimeTokenSource::LifetimeTokenSource() :
        m_state(std::make_shared<LifetimeState>()) {}

    LifetimeTokenSource::~LifetimeTokenSource() {
        // a source that dies without an explicit invalidation still drops the tasks that carry its token (§4.2)
        invalidate();
    }

    LifetimeToken LifetimeTokenSource::token() const {
        // every token of a source observes the same state, so invalidating once expires them all (§4.2)
        return LifetimeToken(m_state);
    }

    bool LifetimeTokenSource::alive() const {
        // an invalidated source reads as dead before it is destroyed (§4.2)
        return m_state && m_state->alive();
    }

    void LifetimeTokenSource::invalidate() {
        // invalidating twice is harmless, because the second pass finds the state already expired (§4.2)
        if (!m_state)
            return;
        m_state->expire();
    }
} // namespace albor
