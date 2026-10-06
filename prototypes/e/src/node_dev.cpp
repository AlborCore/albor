#include "prototypes/e/src/node_dev.h"

namespace albor_proto_e
{

    namespace
    {

        // global side table for the dev runtime; absent from Release at link time (AD-04)
        DevSideTable g_dev_side_table;

        // dev hook body: registers the node in the side table and records its id
        void dev_hook_body(void* node, uint32_t node_id) {
            // cast the opaque pointer to the base type and register it
            g_dev_side_table.register_node(static_cast<UiNode*>(node), node_id);
        }

        // static object whose constructor installs the hook at library load time (AD-02, §2.3)
        struct DevHookInstaller
        {
            DevHookInstaller() noexcept { UiNode::s_dev_hook = &dev_hook_body; }
        };

        // the installer runs before main(); Release never links this translation unit (AD-04)
        const DevHookInstaller g_dev_hook_installer;

    } // namespace

    // A linker only pulls an object file out of a static archive when something references a symbol in it. The
    // installer above is deliberately in an anonymous namespace, so nothing outside this file can name it and the
    // whole file would be dropped: the hook would silently stay null and the Dev build would prove nothing while
    // still reporting a layout identical to Release. This externally visible anchor gives the archive a symbol to
    // resolve, which is what makes the installer actually run.
    extern "C" const void* albor_prototype_e_devrt_link_anchor = &g_dev_hook_installer;

    // Accessor used by the layout check to prove the hook is installed in Dev and null in Release (T7).
    DevExtensionHook dev_extension_hook() noexcept { return UiNode::s_dev_hook; }

    void DevSideTable::register_node(UiNode* /*node*/, uint32_t node_id) {
        // insert a default entry if the node has not been seen before
        m_entries.try_emplace(node_id, DevNodeEntry{});
    }

    DevNodeEntry* DevSideTable::find(uint32_t node_id) {
        // look up the entry by node identity key (AD-11)
        auto it = m_entries.find(node_id);
        return (it != m_entries.end()) ? &it->second : nullptr;
    }

} // namespace albor_proto_e
