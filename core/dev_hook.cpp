#include "core/dev_hook.h"

#include <atomic>

namespace albor::dev
{
    namespace
    {
        // the installed entry point, which a release leaves empty (§12.1, AD-02, AD-04)
        std::atomic<ReloadFunction> g_reload{nullptr};
    } // namespace

    void install(ReloadFunction reload) {
        // the pointer is stored, because the extension arrives after the runtime is built (§12.1, AD-02)
        g_reload.store(reload, std::memory_order_release);
    }

    bool present() {
        // the pointer is read with acquire, which pairs with the release the installer used (§12.1, AD-02)
        return g_reload.load(std::memory_order_acquire) != nullptr;
    }

    void reload(const std::string& ir) {
        // the pointer is read once, because an extension may be torn down between the check and the call (§12.1)
        const ReloadFunction entry = g_reload.load(std::memory_order_acquire);
        // a build with no extension has nothing to reload, and a release must not carry the feature (§12.1, AD-02)
        if (entry == nullptr)
            return;
        // the definition is handed over, which is the whole of what a reload asks (§12.1, AD-02)
        entry(ir);
    }
} // namespace albor::dev
