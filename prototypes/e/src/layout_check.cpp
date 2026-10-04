#include "prototypes/e/src/node.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// layout_check: prints sizeof/offsetof table for UiNode, TextNode, ContainerNode and returns 0
// the bench script runs this binary under both Release and Debug presets and diffs the output (plan.md)

// sentinel sizes are inserted after the first Release run (step 8 of the implementation plan)
// update these constants with the measured values and recompile to enable the static_assert guards
static constexpr std::size_t kSentinelUiNode = 0;
static constexpr std::size_t kSentinelTextNode = 0;
static constexpr std::size_t kSentinelContainerNode = 0;

int main() {
    // print the size of each node class
    std::printf("sizeof(UiNode)        = %zu\n", sizeof(albor_proto_e::UiNode));
    std::printf("sizeof(TextNode)      = %zu\n", sizeof(albor_proto_e::TextNode));
    std::printf("sizeof(ContainerNode) = %zu\n", sizeof(albor_proto_e::ContainerNode));

    // print the offset of the identity field in UiNode (AD-11, §18.1)
    std::printf("offsetof(UiNode, m_id) = %zu\n", offsetof(albor_proto_e::UiNode, m_id));

    // verify the virtual interface exists by taking the address of each pure virtual via a concrete
    // subclass; a slot that disappears would break the vtable index (AD-02, §2.3)
    albor_proto_e::TextNode probe{0};
    const albor_proto_e::UiNode* base = &probe;

    // call each virtual method on the base pointer to confirm the vtable slot is present
    const char* type_name = base->type_name();
    const bool type_name_ok = (type_name != nullptr && std::strlen(type_name) > 0);
    std::printf("vtable::type_name slot exists: %s\n", type_name_ok ? "yes" : "no");

    // confirm read_property dispatches through the vtable
    const float rp = base->read_property(0);
    const bool read_prop_ok = (rp >= 0.0f);
    std::printf("vtable::read_property slot exists: %s\n", read_prop_ok ? "yes" : "no");

    // confirm write_property dispatches through the vtable (use a temporary)
    albor_proto_e::TextNode writable{1};
    albor_proto_e::UiNode* writable_base = &writable;
    writable_base->write_property(0, 24.0f);
    const bool write_prop_ok = (writable.read_property(0) == 24.0f);
    std::printf("vtable::write_property slot exists: %s\n", write_prop_ok ? "yes" : "no");

    // print the dev hook address; Release must show 0x0, Dev must show a non-null address (T7)
    std::printf("dev_hook address: %p\n", reinterpret_cast<const void*>(albor_proto_e::UiNode::s_dev_hook));

    // return non-zero if any vtable check failed
    if (!type_name_ok || !read_prop_ok || !write_prop_ok)
        return 1;

    // sentinel guard: enabled once the Release sizes are known (step 8); zeroes skip the check
    if constexpr (kSentinelUiNode != 0) {
        static_assert(sizeof(albor_proto_e::UiNode) == kSentinelUiNode, "UiNode size changed — layout invariance violation (AD-02)");
        static_assert(sizeof(albor_proto_e::TextNode) == kSentinelTextNode, "TextNode size changed — layout invariance violation (AD-02)");
        static_assert(sizeof(albor_proto_e::ContainerNode) == kSentinelContainerNode, "ContainerNode size changed — layout invariance violation (AD-02)");
    }

    return 0;
}
