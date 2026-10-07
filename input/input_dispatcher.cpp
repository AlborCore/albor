#include "input/input_dispatcher.h"

namespace albor
{
    void InputDispatcher::dispatch(UiNode& root, Event& event) {
        // a claimed event is not offered again, because the walk stops at the handler that took it (§8.3)
        if (event.handled)
            return;
        // the walk starts at the children, because the deepest handler should win and the node is offered last (§8.3)
        route_down(root, event);
    }

    void InputDispatcher::route_down(UiNode& node, Event& event) {
        // every caller arrives with an unclaimed event, and the loop re-checks before each child (§8.3)
        for (auto position = node.child_count(); position > 0; position--) {
            // a claimed event stops the walk, so no sibling reacts to the same event (§8.3)
            if (event.handled)
                return;
            // the deepest child that claims the event ends the walk, which is what makes the topmost control win (§8.3)
            route_down(*node.child_at(position - 1), event);
        }
        // no descendant claimed the event, so the node itself gets it as the next candidate (§8.3)
        if (!event.handled)
            node.dispatch_event(event);
    }

    void InputDispatcher::dispatch_to_focused(UiNode& root, Event& event, UiNode* focused) {
        // a claimed event is not offered again, because the walk stops at the handler that took it (§8.3)
        if (event.handled)
            return;
        // a focus that is not part of the tree cannot be routed to, so the whole tree is walked instead (§8.3)
        if (focused == nullptr || !descends_from(root, *focused)) {
            // the ordinary depth first walk is the fallback, because a key press must reach something (§8.3)
            dispatch(root, event);
            // exit
            return;
        }
        // the focused node is offered the event first, because a key press belongs to the caret (§8.3)
        route_up(*focused, event);
    }

    void InputDispatcher::route_up(UiNode& node, Event& event) {
        // the node itself sees the event before its parent, because the innermost handler should win (§8.3)
        node.dispatch_event(event);
        // a claimed event ends the bubble, so no ancestor acts on the same key press (§8.3)
        if (event.handled)
            return;
        // a root node has no parent to bubble to, so the walk ends there (§8.3)
        UiNode* parent = node.parent();
        if (parent == nullptr)
            return;
        // the event continues up from the parent, which is the bubble half of the keyboard path (§8.3)
        route_up(*parent, event);
    }

    bool InputDispatcher::descends_from(const UiNode& root, const UiNode& node) {
        // the walk starts at the candidate and climbs, so the answer is whether it ever reaches the root (§8.3)
        for (const UiNode* current = &node; current != nullptr; current = current->parent()) {
            // reaching the root means the candidate really belongs to this tree (§8.3)
            if (current == &root)
                return true;
        }
        // the climb ran out without reaching the root, so the candidate is a node of some other tree (§8.3)
        return false;
    }

    bool InputDispatcher::hits(UiNode& root, Point logical_point) {
        // the tree answers for itself, because hit testing is the node contract of §8.3
        return root.hit_test(logical_point);
    }
} // namespace albor
