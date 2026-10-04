#include "prototypes/e/src/ir_tree.h"
#include "prototypes/e/src/node.h"
#include "prototypes/e/src/ui_patch.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// conformance: dual-engine conformance test (AD-25, §2.4, plan.md T4-T5)
// builds a 16-node tree, serialises, deserialises, compares properties, applies UiPatch, re-compares

namespace
{

    // property slot count per node matches the prototype workload
    static constexpr uint16_t kPropCount = 4;

    // total node count from plan.md: 4 ContainerNodes + 12 TextNodes = 16
    static constexpr uint32_t kNodeCount = 16;
    static constexpr uint32_t kContainerCount = 4;

    // compare every property slot between original and deserialised node lists
    bool compare_trees(const std::vector<albor_proto_e::UiNode*>& original, const std::vector<albor_proto_e::UiNode*>& loaded, const char* phase) {
        bool all_match = true;
        for (uint32_t n = 0; n < static_cast<uint32_t>(original.size()); ++n) {
            for (uint16_t i = 0; i < kPropCount; ++i) {
                const float expected = original[n]->read_property(i);
                const float got = loaded[n]->read_property(i);
                // bitwise comparison: no arithmetic involved so exact equality is required (plan.md)
                if (expected != got) {
                    std::printf("MISMATCH [%s] node_id=%u local_offset=%u: expected %f got %f\n", phase, original[n]->id(), i, static_cast<double>(expected), static_cast<double>(got));
                    all_match = false;
                }
            }
        }
        if (all_match)
            std::printf("MATCH [%s]\n", phase);
        return all_match;
    }

} // namespace

int main() {
    // step 1: build a tree of 16 nodes (4 ContainerNode + 12 TextNode), ids 0-15
    std::vector<std::unique_ptr<albor_proto_e::UiNode>> owned;
    owned.reserve(kNodeCount);
    for (uint32_t i = 0; i < kContainerCount; ++i)
        owned.push_back(std::make_unique<albor_proto_e::ContainerNode>(i));
    for (uint32_t i = kContainerCount; i < kNodeCount; ++i)
        owned.push_back(std::make_unique<albor_proto_e::TextNode>(i));

    // step 2: set distinct initial property values so a default-equality false positive is impossible
    for (auto& node : owned) {
        for (uint16_t prop = 0; prop < kPropCount; ++prop) {
            // use node_id * 10 + prop as a unique, non-zero value
            const float val = static_cast<float>(node->id() * 10u + prop);
            node->write_property(prop, val);
        }
    }

    // build a raw-pointer view for the compare helper and serialiser
    std::vector<albor_proto_e::UiNode*> original;
    original.reserve(kNodeCount);
    for (auto& n : owned)
        original.push_back(n.get());

    // step 3: serialise the tree to an IrTree
    const albor_proto_e::IrTree ir = albor_proto_e::serialize(original);

    // step 4: deserialise to a fresh set of nodes
    std::vector<std::unique_ptr<albor_proto_e::UiNode>> loaded_owned = albor_proto_e::deserialize(ir);

    // build raw-pointer view of the deserialised set
    std::vector<albor_proto_e::UiNode*> loaded;
    loaded.reserve(loaded_owned.size());
    for (auto& n : loaded_owned)
        loaded.push_back(n.get());

    // step 5: compare before patch (T4)
    bool ok = compare_trees(original, loaded, "before-patch");

    // step 6: build a UiPatch: SetProperty on nodes 1, 7, 14; ReplaceBinding on node 3 offset 0
    albor_proto_e::UiPatch patch;
    patch.set_props.push_back({std::to_string(1u), 0, 55.0f});
    patch.set_props.push_back({std::to_string(7u), 2, 77.0f});
    patch.set_props.push_back({std::to_string(14u), 3, 88.0f});
    // ReplaceBinding treated as SetProperty in this prototype (plan.md, RESULTS.md note)
    patch.replace_bindings.push_back({std::to_string(3u), 0, 99.0f});

    // step 7: apply the patch to both trees
    albor_proto_e::apply(patch, original);
    albor_proto_e::apply(patch, loaded);

    // step 8: re-compare after patch (T5)
    ok = compare_trees(original, loaded, "after-patch") && ok;

    // step 9: return 0 if all comparisons passed
    return ok ? 0 : 1;
}
