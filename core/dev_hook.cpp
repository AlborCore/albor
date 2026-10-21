#include "core/dev_hook.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace albor::dev
{
    namespace
    {
        // how many extensions may observe one tree, which is enough for reload and automation side by side (§12.1, AD-02)
        constexpr std::size_t MAX_ROOT_BINDINGS = 8;

        // one extension's pair of entry points, which the runtime calls for every bound tree (§12.1, AD-02)
        struct RootBinding
        {
            // the entry point that receives a live tree and its engines (§12.1)
            RootBindFunction bind = nullptr;

            // the entry point that releases a tree the extension holds (§5.2)
            RootUnbindFunction unbind = nullptr;
        };

        // the installed entry point, which a release leaves empty (§12.1, AD-02, AD-04)
        std::atomic<ReloadFunction> g_reload{nullptr};

        // the installed bindings, which a release leaves empty because the extensions that need them are excluded (AD-04)
        std::array<RootBinding, MAX_ROOT_BINDINGS> g_bindings{};

        // how many entries of the table are live, which is the only field the hot path reads (§12.1, AD-02)
        std::atomic<std::size_t> g_binding_count{0};
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
        // the table is cleared first, because a replace must not leave an observer from the previous installation (§12.1)
        g_binding_count.store(0, std::memory_order_release);
        // null pair is reset, leaves hook empty (§12.1)
        if (bind == nullptr || unbind == nullptr)
            return;
        // the pair is written before the count, because a reader that sees the entry must see both pointers (§12.1, AD-02)
        g_bindings[0] = RootBinding{bind, unbind};
        // the release publishes the entry, which pairs with the acquire every call site uses (§12.1, AD-02)
        g_binding_count.store(1, std::memory_order_release);
    }

    bool add_root_binding(RootBindFunction bind, RootUnbindFunction unbind) {
        // an incomplete pair cannot observe or release a tree, so it is refused rather than half-installed (§12.1, AD-02)
        if (bind == nullptr || unbind == nullptr)
            return false;
        // the current count is read with acquire, because a previous registration published its entry with release (§12.1)
        const std::size_t count = g_binding_count.load(std::memory_order_acquire);
        // full table reported to installer (§12.1)
        if (count >= MAX_ROOT_BINDINGS)
            return false;
        // the pair is written at the first free index, which keeps the registration order stable (§12.1)
        g_bindings[count] = RootBinding{bind, unbind};
        // the release publishes the new entry, because bind_root reads the count before it walks the table (§12.1, AD-02)
        g_binding_count.store(count + 1, std::memory_order_release);
        // binding was added
        return true;
    }

    bool binder_present() {
        // the count is read with acquire, which pairs with the release every installer used (§12.1, AD-02)
        return g_binding_count.load(std::memory_order_acquire) != 0;
    }

    void bind_root(void* root, void* engine, void* text) {
        // the count is read once, because every observer registered before this call must receive the same tree (§12.1)
        const std::size_t count = g_binding_count.load(std::memory_order_acquire);
        // each observer is called in registration order, because the reload extension and the host are independent (§12.1)
        for (std::size_t index = 0; index < count; ++index) {
            // the entry is read after the count, because the release that published it happened before the count (§12.1)
            const RootBindFunction entry = g_bindings[index].bind;
            // the tree and its engines are handed over, which is what a reload needs to patch (§12.1, AD-02)
            entry(root, engine, text);
        }
    }

    void unbind_root(void* root) {
        // the count is read once, because every observer that saw the bind must see the release (§5.2)
        const std::size_t count = g_binding_count.load(std::memory_order_acquire);
        // each observer is told in registration order, and each one releases only the root it owns (§12.1, §5.2)
        for (std::size_t index = 0; index < count; ++index) {
            // the entry is read after the count, because the release that published it happened before the count (§12.1)
            const RootUnbindFunction entry = g_bindings[index].unbind;
            // the root is named, because the extension releases only the binding that root owned (§12.1, §5.2)
            entry(root);
        }
    }
} // namespace albor::dev
