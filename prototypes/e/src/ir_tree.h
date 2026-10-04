#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "prototypes/e/src/node.h"

namespace albor_proto_e
{

    // serialised representation of one node in the IR (§17.2)
    struct IrNode
    {
        uint32_t id;
        char type[16];
        float props[4];
    };

    // an IrTree is a flat list of serialised nodes
    using IrTree = std::vector<IrNode>;

    // serialise a live node tree to an IrTree for transfer to devrt (§17.2)
    [[nodiscard]] IrTree serialize(const std::vector<UiNode*>& nodes);

    // deserialise an IrTree back to a fresh set of owned nodes (§17.2)
    [[nodiscard]] std::vector<std::unique_ptr<UiNode>> deserialize(const IrTree& tree);

} // namespace albor_proto_e
