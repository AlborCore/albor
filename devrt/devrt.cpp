#include "core/dev_hook.h"
#include "devrt/ir.h"
#include "devrt/reload_adapter.h"

namespace albor::devrt
{
    namespace
    {
        // the tree the last reload patched, which is what an extension keeps between reloads (§12.1)
        UiNode* g_root = nullptr;

        // the factory the host installed, because the extension builds no element of its own (§12.1, AD-02)
        ElementFactory g_factory = nullptr;

        // the report of the last reload, which a developer watches while typing (§12.1)
        ReloadReport g_last;

        // the entry point the runtime calls, which parses a definition and patches the tree (§12.1, AD-02)
        void handle_reload(const std::string& ir) {
            // no tree means nothing to patch, which is not an error a developer needs to see (§12.1)
            if (g_root == nullptr)
                return;
            // the definition is read, because a malformed one must not touch a running tree (§12.1)
            const std::optional<IrNode> parsed = parse_ir(ir);
            // a definition that does not read leaves the tree exactly as it was (§12.1)
            if (!parsed)
                return;
            // the tree is reconciled, which is what a reload means (§12.1)
            g_last = apply_reload(*g_root, *parsed, g_factory);
        }
    } // namespace

    // bind the extension to a tree, which a Dev configuration does once at startup (§12.1, AD-02)
    extern "C" void albor_devrt_bind(UiNode* root, ElementFactory factory);

    // the report of the last reload, which the host reads to show a developer what happened (§12.1)
    ReloadReport last_report();

    extern "C" void albor_devrt_bind(UiNode* root, ElementFactory factory) {
        // the tree is remembered, because a reload patches it (§12.1)
        g_root = root;
        // the factory is remembered, because the extension builds no element itself (§12.1, AD-02)
        g_factory = factory;
        // the runtime is told where the entry point is, which is the whole of the hook (§12.1, AD-02)
        dev::install(&handle_reload);
    }

    ReloadReport last_report() {
        // the report is returned whole, which is what the host reads (§12.1)
        return g_last;
    }
} // namespace albor::devrt
