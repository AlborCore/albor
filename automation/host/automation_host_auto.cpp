#include "automation/host/automation_host.h"

#include <cstdio>

#include "core/dev_hook.h"

namespace
{
    // the host observes the same tree the reload extension does, so a coverage build runs both (§12.1, AD-02)
    void bind_root(const albor::dev::WindowBinding& binding) {
        // the singleton is reached at load, which is the only moment the process has a window (§10.3)
        albor::automation::AutomationHost::instance().bind(binding);
    }

    // the host releases the tree when the window closes, which is how it stops without an application call (§10.3)
    void unbind_root(void* root) {
        // the singleton is reached at load, which is the only moment the process has a window (§10.3)
        albor::automation::AutomationHost::instance().unbind(root);
    }

    // the installer registers itself at load, which is what makes the host transparent to the application (§10.3, AD-02)
    struct HostInstaller
    {
        // the constructor runs before main, which is when the whole archive target is linked (§10.3)
        HostInstaller() {
            // the pair is appended, because the reload extension may observe the same tree (§12.1, AD-02)
            const bool installed = albor::dev::add_root_binding(&bind_root, &unbind_root);
            // a refusal is reported, because a silent refusal leaves the application without automation (§10.3, AD-10)
            if (!installed)
                std::fprintf(stderr, "albor automation: the host could not register its tree observer\n");
        }
    };

    // the installer is a global, which is the only way a whole archive target runs at load (§10.3)
    HostInstaller g_host_installer;
} // namespace
