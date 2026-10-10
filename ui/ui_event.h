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

        // the pointer entered a node's bounds, which the dispatcher synthesizes at a boundary (§8.3)
        PointerEnter,

        // the pointer left a node's bounds, which the dispatcher synthesizes at a boundary (§8.3)
        PointerLeave,

        // a wheel turned at a logical position
        Wheel,

        // a key went down
        KeyDown,

        // a key came up
        KeyUp,

        // committed text arrived from the input method
        TextInput
    };

    // one portable key name, which a platform adapter normalizes a key event onto (§8.3)
    enum class Key
    {
        // no key, which every non keyboard event carries
        None,

        // the tab key, which moves focus forward unless shift is held
        Tab,

        // the return or enter key, which activates the focused control
        Return,

        // the escape key, which cancels a pending action
        Escape,

        // the left arrow key, which reaches a drag target on its left
        ArrowLeft,

        // the right arrow key, which reaches a drag target on its right
        ArrowRight,

        // the up arrow key, which reaches a drag target above
        ArrowUp,

        // the down arrow key, which reaches a drag target below
        ArrowDown,

        // the space key, which toggles or activates the focused control
        Space,

        // the delete key, which removes the selected content
        Delete
    };

    // the modifier keys held during an event, one flag per key (§8.3)
    struct Modifiers
    {
        // whether the shift key was held, which reverses a tab traversal
        bool shift = false;

        // whether the control key was held
        bool control = false;

        // whether the option or alt key was held
        bool option = false;

        // whether the command or meta key was held
        bool command = false;

        // report whether no key modifier is held
        [[nodiscard]] bool none() const;
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

        // the key a keyboard event carries, which is None for every other kind (§8.3)
        Key key = Key::None;

        // the modifier keys held during the event, which distinguishes shift tab (§8.3)
        Modifiers modifiers;

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
