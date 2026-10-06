#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "prototypes/e/src/node.h"

namespace albor_proto_e
{

    // side table entry: dev metadata for one node (keyed by node id, never in Release layout)
    struct DevNodeEntry
    {
        std::string debug_label;
        uint32_t reload_generation = 0;
    };

    // side table owned by the dev runtime; absent from Release at link time (AD-02, AD-04)
    class DevSideTable
    {
    public:
        DevSideTable() = default;
        ~DevSideTable() = default;

        DevSideTable(const DevSideTable&) = delete;
        DevSideTable& operator=(const DevSideTable&) = delete;
        DevSideTable(DevSideTable&&) = delete;
        DevSideTable& operator=(DevSideTable&&) = delete;

        // register a node in the side table when the dev hook fires
        void register_node(UiNode* node, uint32_t node_id);

        // return the entry for a node, or nullptr if not registered
        [[nodiscard]] DevNodeEntry* find(uint32_t node_id);

    private:
        std::unordered_map<uint32_t, DevNodeEntry> m_entries;
    };

    // Defined only in the dev runtime translation unit. Returns the installed extension hook, which is non-null
    // when devrt is linked and null in Release (AD-02, T7).
    [[nodiscard]] DevExtensionHook dev_extension_hook() noexcept;

} // namespace albor_proto_e
