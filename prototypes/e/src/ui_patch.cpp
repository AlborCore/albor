#include "prototypes/e/src/ui_patch.h"

#include <string>

namespace albor_proto_e
{

    void apply(const UiPatch& p, std::vector<UiNode*>& nodes) {
        // apply each SetProperty patch by matching the node's id string (AD-11, §18.1)
        for (const auto& patch : p.set_props) {
            for (auto* node : nodes) {
                if (std::to_string(node->id()) == patch.node_id)
                    node->write_property(patch.local_offset, patch.value);
            }
        }
        // apply each ReplaceBinding patch as a direct property write (no live evaluator yet)
        for (const auto& patch : p.replace_bindings) {
            for (auto* node : nodes) {
                if (std::to_string(node->id()) == patch.node_id)
                    node->write_property(patch.local_offset, patch.new_const);
            }
        }
    }

} // namespace albor_proto_e
