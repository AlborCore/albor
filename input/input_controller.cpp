#include "input/input_controller.h"

namespace albor
{
    void InputController::attach(UiNode& root) {
        // the tree is adopted without ownership, because the scene owner keeps it alive (§8.3)
        m_root = &root;
    }

    UiNode* InputController::root() const {
        // the tree is the one the controller was pointed at (§8.3)
        return m_root;
    }

    void InputController::deliver(Event& event) {
        // a controller without a tree has nothing to route into, so the event is dropped (§8.3)
        if (m_root == nullptr)
            return;
        // the pointer channel is the positioned one, which serves the hit test path (§8.3)
        if (EventFilter::positioned(event))
            deliver_pointer(event);
        // the keyboard channel is the key one, which serves the focus path (§8.3)
        else if (EventFilter::is_keyboard(event))
            deliver_key(event);
        // the text channel serves the input method, which follows the caret (§8.3)
        else
            InputDispatcher::dispatch_to_focused(*m_root, event, m_focus.focused());
    }

    UiNode* InputController::focusable_under(UiNode& node, Point point) const {
        // the children are searched last added first, because the topmost one wins (§8.3)
        for (auto position = node.child_count(); position > 0; position--) {
            // a child that misses the point hides nothing inside itself (§8.3)
            UiNode* child = node.child_at(position - 1);
            if (child == nullptr)
                continue;
            // a child that does not hit does not contain the point (§8.3)
            if (!child->hit_test(point))
                continue;
            // the deepest focusable node of the child is the one the click should land on (§8.3)
            if (UiNode* found = focusable_under(*child, point))
                return found;
        }
        // a node that misses the point has itself and everything below it out of reach (§8.3)
        if (!node.hit_test(point))
            return nullptr;
        // an unfocusable node cannot take the caret, so the search passes it over (§8.3)
        if (!FocusManager::focusable(node))
            return nullptr;
        // the node itself is the deepest focusable one under the point (§8.3)
        return &node;
    }

    void InputController::deliver_pointer(Event& event) {
        // a press with no drag yet routes by position and may start a capture (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // a second press while a drag is alive interrupts nothing, so it is dropped (§8.3)
            if (m_capture != nullptr)
                return;
            InputDispatcher::dispatch(*m_root, event);
            // only a claimed press moves the caret, because an unclaimed one is empty space (§8.3)
            if (!event.handled)
                return;
            // the deepest focusable node under the press takes the caret (§8.3)
            UiNode* target = focusable_under(*m_root, event.position);
            if (target != nullptr)
                m_focus.request_focus(target);
            // the target captures the drag, so the moves keep reaching it outside its bounds (§8.3)
            m_capture = target;
            return;
        }
        // a release that ends a drag reaches the captured node before the capture drops (§8.3)
        if (event.kind == EventKind::PointerUp && m_capture != nullptr) {
            // the release bubbles from the captured node, which is the last event of the drag (§8.3)
            InputDispatcher::dispatch_to_focused(*m_root, event, m_capture);
            // the capture is dropped after the release, so the next press starts a new drag (§8.3)
            m_capture = nullptr;
            return;
        }
        // a move or a wheel while a drag is alive reaches the captured node, so the control keeps the pointer (§8.3)
        if (m_capture != nullptr) {
            InputDispatcher::dispatch_to_focused(*m_root, event, m_capture);
            return;
        }
        // everything else routes by position, which follows the pointer like a hover (§8.3)
        InputDispatcher::dispatch(*m_root, event);
    }

    void InputController::deliver_key(Event& event) {
        // the escape key cancels a drag before anything else, which is the abort path of §8.3
        if (event.kind == EventKind::KeyDown && event.key == Key::Escape) {
            m_capture = nullptr;
            event.handled = true;
            return;
        }
        // a key up carries no traversal and no activation, so it is left alone (§8.3)
        if (event.kind != EventKind::KeyDown)
            return;
        // an unmodified tab moves the caret forward through the ring (§8.3)
        if (event.key == Key::Tab && event.modifiers.none()) {
            m_focus.focus_next(*m_root);
            event.handled = true;
            return;
        }
        // a shift tab moves the caret backward through the same ring (§8.3)
        if (event.key == Key::Tab && event.modifiers.shift) {
            m_focus.focus_previous(*m_root);
            event.handled = true;
            return;
        }
        // every other key reaches the caret and then its ancestors, which is the keyboard path of §8.3
        InputDispatcher::dispatch_to_focused(*m_root, event, m_focus.focused());
    }

    FocusManager& InputController::focus() {
        // the manager is the one the controller routes the caret through (§8.3)
        return m_focus;
    }

    const FocusManager& InputController::focus() const {
        // the reading accessor resolves exactly what the writing one does (§8.3)
        return m_focus;
    }

    bool InputController::capturing() const {
        // a capture exists exactly while a drag is alive (§8.3)
        return m_capture != nullptr;
    }
} // namespace albor
