#pragma once

#include <cstddef>
#include <vector>

#include "core/dev_hook.h"

namespace albor::automation
{
    // one tree the host can observe, which is the opaque handover the core seam carries (§12.1)
    using HostBinding = dev::WindowBinding;

    // the binding stack whose last entry is the active tree, which is the host's whole root model (§10.3)
    class HostBindings
    {
    public:
        // add or reactivate a tree, which moves an existing root to the most recent position (§10.3)
        void bind(const dev::WindowBinding& binding);

        // remove a tree that is closing, which leaves the next most recent one active (§10.3)
        void unbind(void* root);

        // report whether no tree is bound, which is what stops the host (§10.3)
        [[nodiscard]] bool empty() const;

        // hand out how many trees are bound, which a test asserts the duplicate rule against (§10.3)
        [[nodiscard]] std::size_t count() const;

        // hand out the active tree, or null when none is bound (§10.3)
        [[nodiscard]] const HostBinding* active() const;

        // hand out the newest binding that carries a reloadable tree, or null when none does (§6.1, §10.3)
        [[nodiscard]] const HostBinding* active_tree() const;

    private:
        // the bound trees in activation order, where the back is active (§10.3)
        std::vector<HostBinding> m_bindings;
    };
} // namespace albor::automation
