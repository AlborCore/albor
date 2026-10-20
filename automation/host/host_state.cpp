#include "automation/host/host_state.h"

#include <algorithm>

namespace albor::automation
{
    void HostBindings::bind(void* root, void* engine, void* text) {
        // an existing root is removed first, because binding it again must not duplicate its observation (§10.3)
        const auto existing = std::ranges::find(m_bindings, root, &HostBinding::root);
        // the old entry is erased before the new one is appended, so the same tree cannot appear twice (§10.3)
        if (existing != m_bindings.end())
            m_bindings.erase(existing);
        // the tree is appended last, which makes it the active one (§10.3)
        m_bindings.push_back(HostBinding{.root = root, .engine = engine, .text = text});
    }

    void HostBindings::unbind(void* root) {
        // an unknown root is ignored, because closing one window must not release another binding (§10.3)
        const auto existing = std::ranges::find(m_bindings, root, &HostBinding::root);
        // the entry is erased only when it was found, which leaves every other tree untouched (§10.3)
        if (existing != m_bindings.end())
            m_bindings.erase(existing);
    }

    bool HostBindings::empty() const {
        // the stack is empty only when no tree is left, which is what triggers a stop (§10.3)
        return m_bindings.empty();
    }

    std::size_t HostBindings::count() const {
        // the count is the number of live trees, which a duplicate test reads back (§10.3)
        return m_bindings.size();
    }

    const HostBinding* HostBindings::active() const {
        // an empty stack has no active tree, which every semantic command checks (§10.3)
        if (m_bindings.empty())
            return nullptr;
        // the most recently bound tree is active, because it is the window the user just opened (§10.3)
        return &m_bindings.back();
    }
} // namespace albor::automation
