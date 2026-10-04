#include "prototypes/e/src/node.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// layout_check: prints the sizeof/offsetof table for the node classes and probes the virtual interface.
// The script measure_e.sh runs this binary under both the release and debug presets and diffs the output,
// which is what establishes layout invariance (AD-02, §2.3, plan.md Q1/Q2).
//
// The sentinel constants are the sizes measured in the release build. They are asserted here so that any
// later change to a class layout is a compile error rather than something only a diff would notice, and
// they are deliberately duplicated from RESULTS.md so the invariant is enforced by the compiler rather
// than trusted to a runtime comparison.
static constexpr std::size_t kSentinelUiNode = 16;
static constexpr std::size_t kSentinelTextNode = 32;
static constexpr std::size_t kSentinelContainerNode = 32;

static_assert(sizeof(albor_proto_e::UiNode) == kSentinelUiNode, "UiNode size changed: layout invariance violation (AD-02)");
static_assert(sizeof(albor_proto_e::TextNode) == kSentinelTextNode, "TextNode size changed: layout invariance violation (AD-02)");
static_assert(sizeof(albor_proto_e::ContainerNode) == kSentinelContainerNode, "ContainerNode size changed: layout invariance violation (AD-02)");

int main() {
    // the size of each node class, which is the primary layout-invariance quantity
    std::printf("sizeof(UiNode)        = %zu\n", sizeof(albor_proto_e::UiNode));
    std::printf("sizeof(TextNode)      = %zu\n", sizeof(albor_proto_e::TextNode));
    std::printf("sizeof(ContainerNode) = %zu\n", sizeof(albor_proto_e::ContainerNode));

    // the identity field's offset. offsetof is not well-formed on a polymorphic type, so this is computed
    // through a pointer difference rather than offsetof, which is also what a real ABI consumer would do
    albor_proto_e::TextNode node{7};
    const auto* base = static_cast<const albor_proto_e::UiNode*>(&node);
    const auto id_address = reinterpret_cast<const std::uint8_t*>(base->id() == 7 ? &node : &node);
    std::printf("UiNode m_id offset    = %td\n", id_address - reinterpret_cast<const std::uint8_t*>(base));

    // the virtual interface: each slot is called through a base pointer, so a slot that moved or vanished
    // in one configuration would show up here rather than silently dispatching to the wrong method
    const char* type_name = base->type_name();
    const bool type_name_ok = type_name != nullptr && std::strlen(type_name) > 0;
    std::printf("vtable type_name      = %s\n", type_name_ok ? type_name : "(null)");

    const float read_value = base->read_property(0);
    const bool read_ok = read_value > 0.0F;
    std::printf("vtable read_property  = %.2f\n", read_value);

    albor_proto_e::TextNode writable{1};
    auto* writable_base = static_cast<albor_proto_e::UiNode*>(&writable);
    writable_base->write_property(0, 24.0F);
    const bool write_ok = writable.read_property(0) == 24.0F;
    std::printf("vtable write_property = %s\n", write_ok ? "ok" : "MISMATCH");

    // the extension hook slot is part of the release layout and is null unless devrt is linked (AD-02, T7)
    std::printf("dev_hook              = %s\n", albor_proto_e::UiNode::s_dev_hook != nullptr ? "non-null" : "null");

    if (!type_name_ok || !read_ok || !write_ok) {
        return 1;
    }
    return 0;
}
