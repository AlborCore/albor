#include "prototypes/e/src/ir_tree.h"

#include <cstring>
#include <string_view>

namespace albor_proto_e
{

    // property slot count per node matches the prototype workload (plan.md)
    static constexpr uint16_t kPropCount = 4;

    IrTree serialize(const std::vector<UiNode*>& nodes) {
        // allocate one IrNode per live node
        IrTree tree;
        tree.reserve(nodes.size());
        for (const auto* node : nodes) {
            IrNode ir{};
            // copy the node identity (AD-11, §18.1)
            ir.id = node->id();
            // copy the type name into the fixed buffer, ensuring null termination
            const char* tn = node->type_name();
            std::strncpy(ir.type, tn, sizeof(ir.type) - 1);
            ir.type[sizeof(ir.type) - 1] = '\0';
            // snapshot every property slot
            for (uint16_t i = 0; i < kPropCount; ++i)
                ir.props[i] = node->read_property(i);
            tree.push_back(ir);
        }
        return tree;
    }

    std::vector<std::unique_ptr<UiNode>> deserialize(const IrTree& tree) {
        // reconstruct a fresh set of owned nodes from the serialised form
        std::vector<std::unique_ptr<UiNode>> nodes;
        nodes.reserve(tree.size());
        for (const auto& ir : tree) {
            std::unique_ptr<UiNode> node;
            // re-instantiate by type string so no RTTI is required in Release (AD-01)
            if (std::string_view{ir.type} == "TextNode")
                node = std::make_unique<TextNode>(ir.id);
            else
                node = std::make_unique<ContainerNode>(ir.id);
            // restore each property from the snapshot
            for (uint16_t i = 0; i < kPropCount; ++i)
                node->write_property(i, ir.props[i]);
            nodes.push_back(std::move(node));
        }
        return nodes;
    }

} // namespace albor_proto_e
