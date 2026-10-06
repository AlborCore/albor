#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "prototypes/e/src/node.h"

namespace albor_proto_e
{

    // set a property on a node identified by explicit id: key (AD-11, §18.1)
    struct SetPropertyPatch
    {
        std::string node_id;
        uint16_t local_offset;
        float value;
    };

    // replace a binding constant on a node identified by explicit id: key (AD-11, §18.1)
    // note: ReplaceBinding is modelled as SetProperty in this prototype because no live
    // binding evaluator exists yet; the limitation is recorded in RESULTS.md
    struct ReplaceBindingPatch
    {
        std::string node_id;
        uint16_t local_offset;
        float new_const;
    };

    // a UiPatch bundles both patch kinds for atomic application to a live tree (AD-25)
    struct UiPatch
    {
        std::vector<SetPropertyPatch> set_props;
        std::vector<ReplaceBindingPatch> replace_bindings;
    };

    // apply all patches in p to the nodes in the tree, matching by explicit id: string (AD-11)
    void apply(const UiPatch& p, std::vector<UiNode*>& nodes);

} // namespace albor_proto_e
