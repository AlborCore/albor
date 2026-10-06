#pragma once

#include "ui/ui_event.h"
#include "ui/ui_node.h"

namespace albor
{
    // the stateless router that hands a normalized event to the node under it, which is the input half of §8.3
    class InputDispatcher
    {
    public:
        // offer the event to the deepest node under it, stopping at the first handler that claims it (§8.3)
        static void dispatch(UiNode& root, Event& event);

        // offer the event to the focused node and then to its ancestors, which is the keyboard path of §8.3
        static void dispatch_to_focused(UiNode& root, Event& event, UiNode* focused);

        // report whether the point lands on the tree, which is what a platform adapter asks before dispatching (§8.3)
        [[nodiscard]] static bool hits(UiNode& root, Point logical_point);

    private:
        // offer the event to every child of the node and then to the node, which is the pointer path of §8.3
        static void route_down(UiNode& node, Event& event);

        // offer the event to the node and then to its ancestors, which is the keyboard path of §8.3
        static void route_up(UiNode& node, Event& event);

        // report whether the node belongs to the subtree, which keeps a stale focus pointer from being routed to (§8.3)
        [[nodiscard]] static bool descends_from(const UiNode& root, const UiNode& node);
    };
} // namespace albor
