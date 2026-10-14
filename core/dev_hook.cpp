#include "core/dev_hook.h"

#include <atomic>

namespace albor::dev
{
    namespace
    {
        // the installed entry point, which a release leaves empty (§12.1, AD-02, AD-04)
        std::atomic<ReloadFunction> g_reload{nullptr};

        // the installed binder, which a release leaves empty because the extension that needs it is excluded (AD-04)
        std::atomic<RootBindFunction> g_bind{nullptr};

        // the installed release, which is how a closing window stops the extension naming a dead tree (§5.2)
        std::atomic<RootUnbindFunction> g_unbind{nullptr};
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

    void install_root_binder(RootBindFunction bind, RootUnbindFunction unbind) {
        // the pointers are stored, because the extension arrives after the runtime is built (§12.1, AD-02)
        g_unbind.store(unbind, std::memory_order_release);
        g_bind.store(bind, std::memory_order_release);
    }

    bool binder_present() {
        // the pointer is read with acquire, which pairs with the release the installer used (§12.1, AD-02)
        return g_bind.load(std::memory_order_acquire) != nullptr;
    }

    void bind_root(void* root, void* engine, void* text) {
        // the pointer is read once, because an extension may be torn down between the check and the call (§12.1)
        const RootBindFunction entry = g_bind.load(std::memory_order_acquire);
        // a build with no extension has no tree to patch, and a release must not carry the feature (§12.1, AD-02)
        if (entry == nullptr)
            return;
        // the tree and its engines are handed over, which is what a reload needs to patch (§12.1, AD-02)
        entry(root, engine, text);
    }

    void unbind_root(void* root) {
        // the pointer is read once, because an extension may be torn down between the check and the call (§12.1)
        const RootUnbindFunction entry = g_unbind.load(std::memory_order_acquire);
        // a build with no extension holds no tree, so there is nothing to release (§12.1, AD-02)
        if (entry == nullptr)
            return;
        // the root is named, because the extension releases only the binding that root owned (§12.1, §5.2)
        entry(root);
    }
} // namespace albor::dev
