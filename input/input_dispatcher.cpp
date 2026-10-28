#include "input/input_dispatcher.h"

#include "input/event_filter.h"

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
        // an overlay is offered before every ordinary child, because it draws above them (§8.3)
        const auto& overlays = node.overlays();
        for (auto position = overlays.size(); position > 0; position--) {
            // a claimed event stops the walk, so a popup press does not also reach a sibling (§8.3)
            if (event.handled)
                return;
            // a hidden overlay is out of reach, which keeps a dismissed popup inert (§8.3)
            UiNode* overlay = overlays[position - 1].get();
            if (overlay == nullptr || !overlay->visible())
                continue;
            // a positioned event enters only the overlay the point reaches (§8.3)
            if (EventFilter::positioned(event) && !overlay->hit_test(event.position))
                continue;
            // the overlay owns the event path while it is open (§8.3)
            route_down(*overlay, event);
        }
        // every caller arrives with an unclaimed event, and the loop re-checks before each child (§8.3)
        for (auto position = node.child_count(); position > 0; position--) {
            // a claimed event stops the walk, so no sibling reacts to the same event (§8.3)
            if (event.handled)
                return;
            // a hidden child is out of reach, which keeps a hidden page from receiving an event (§8.3)
            UiNode* child = node.child_at(position - 1);
            if (child == nullptr || !child->visible())
                continue;
            // a positioned event enters only the child the point reaches, which keeps a decorative title out (§8.3)
            if (EventFilter::positioned(event) && !child->hit_test(event.position))
                continue;
            // the deepest child that claims the event ends the walk, which is what makes the topmost control win (§8.3)
            route_down(*child, event);
        }
        // no descendant claimed the event, so the node itself gets it as the next candidate (§8.3)
        if (!event.handled)
            node.dispatch_event(event);
    }

    void InputDispatcher::dispatch_to_focused(UiNode& root, Event& event, UiNode* focused) {
        // a claimed event is not offered again, because the walk stops at the handler that took it (§8.3)
        if (event.handled)
            return;
        // a focus that is not in the tree or is hidden falls back to the whole tree (§8.3)
        if (focused == nullptr || !descends_from(root, *focused) || !focused->visible_in_tree()) {
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

    void InputDispatcher::hover_chain(UiNode& root, Point logical_point, std::vector<UiNode*>& out_chain) {
        // an overlay owns the point before an ordinary child, because it draws above them (§8.3)
        const auto& overlays = root.overlays();
        bool overlay_hit = false;
        for (auto position = overlays.size(); position > 0; position--) {
            // the overlay is skipped when the point misses it or it is hidden (§8.3)
            UiNode* overlay = overlays[position - 1].get();
            if (overlay == nullptr || !overlay->hit_test(logical_point))
                continue;
            // the overlay chain is collected before the root itself (§8.3)
            hover_chain(*overlay, logical_point, out_chain);
            // the ordinary children are skipped, because the overlay holds the point (§8.3)
            overlay_hit = true;
            // the walk stops at the overlay that held the point (§8.3)
            break;
        }
        // the children are searched last added first, because the topmost control owns the point (§8.3)
        for (auto position = root.child_count(); position > 0 && !overlay_hit; position--) {
            UiNode* child = root.child_at(position - 1);
            // a child that misses the point holds nothing inside it either, so the walk stops there (§8.3)
            if (child == nullptr || !child->hit_test(logical_point))
                continue;
            // the deepest chain comes first, because a leave has to reach the innermost node before its parent (§8.3)
            hover_chain(*child, logical_point, out_chain);
            // the walk stops at the child that held the point, because above it there is only its own chain (§8.3)
            break;
        }
        // the node joins the chain after its own descendants, which keeps the chain deepest first (§8.3)
        if (root.hit_test(logical_point))
            out_chain.push_back(&root);
    }

    void InputDispatcher::dispatch_to_chain(const std::vector<UiNode*>& chain, Event& event) {
        // the chain is deepest first, which is the order a transition has to arrive in (§8.3)
        for (UiNode* node : chain) {
            // a node that left the tree mid walk is skipped, because the events own the tree (§8.3)
            if (node == nullptr)
                continue;
            // every node in the chain sees the transition, because a hover is not claimed like a press (§8.3)
            node->dispatch_event(event);
        }
        // a transition is delivered to the whole chain, so handled is deliberately not consulted here (§8.3)
        event.handled = false;
    }
} // namespace albor
