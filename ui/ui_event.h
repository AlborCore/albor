#pragma once

#include <cstdint>
#include <string>

#include "core/geometry.h"

namespace albor
{
    // what a normalized input event asks of the tree, which is the portable half of §8.3
    enum class EventKind
    {
        // a pointer button went down at a logical position
        PointerDown,

        // a pointer button came up at a logical position
        PointerUp,

        // a pointer moved to a logical position
        PointerMove,

        // a wheel turned at a logical position
        Wheel,

        // a key went down
        KeyDown,

        // a key came up
        KeyUp,

        // committed text arrived from the input method
        TextInput
    };

    // one normalized input event, which platform adapters produce and the tree dispatches (§8.3)
    struct Event
    {
        // what the event asks of the tree
        EventKind kind = EventKind::PointerDown;

        // the logical position a pointer event carries, which is the origin for every other kind
        Point position;

        // the logical position the pointer event started from, which a drag measures against
        Point origin;

        // the wheel delta a wheel event carries, which is zero for every other kind
        float scroll = 0.0f;

        // the utf8 text a text input event carries, which is empty for every other kind
        std::string text;

        // whether a handler claimed the event, which stops the walk at this node (§8.3)
        bool handled = false;
    };

    // the identifier of a UI node inside one tree, where zero names no node
    using NodeId = std::uint64_t;

    // the portable identity of a node in the scene, which is the author assigned id (AD-11)
    struct NodeIdentity
    {
        // the identifier the node carries inside its own tree (§18.1)
        NodeId node = 0;

        // the author assigned id, which is empty when the author named nothing (AD-11)
        std::string explicit_id;

        // report whether the node carries an author assigned identity, which automation requires (AD-11)
        [[nodiscard]] bool identified() const;

        // compare two identities field by field
        friend bool operator==(const NodeIdentity& lhs, const NodeIdentity& rhs) = default;
    };
} // namespace albor
