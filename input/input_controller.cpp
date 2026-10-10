#include "input/input_controller.h"

#include <algorithm>
#include <utility>

namespace albor
{
    void InputController::attach(UiNode& root) {
        // the tree is adopted without ownership, because the scene owner keeps it alive (§8.3)
        m_root = &root;
        // a detaching subtree is reported before it leaves, while the node is still alive to be told (§8.3)
        root.set_detaching_handler([this](UiNode* node) { release_subtree(node); });
    }

    void InputController::release_subtree(UiNode* node) {
        // the chain runs deepest first, so the node's descendants stand before it and its ancestors behind (§8.3)
        const auto found = std::find(m_hover_chain.begin(), m_hover_chain.end(), node);
        // a subtree that was never hovered holds nothing to exit (§8.3)
        if (found == m_hover_chain.end())
            return;
        // the going nodes are copied out first, because cutting the chain below would leave nothing to read (§8.3)
        const std::vector<UiNode*> leaving(m_hover_chain.begin(), std::next(found));
        // the subtree is cut off the front, so nothing points at what is going away (§8.3)
        m_hover_chain.erase(m_hover_chain.begin(), std::next(found));
        // the deepest first is replayed, because an exit has to reach the inner node before its parent (§8.3)
        for (UiNode* leaving_node : leaving) {
            // the exit is replayed while the node is still alive, which is the whole point of the order (§8.3)
            const Rect& bounds = leaving_node->bounds();
            Event leave{.kind = EventKind::PointerLeave, .position = Point{bounds.x, bounds.y}};
            InputDispatcher::dispatch_to_chain(std::vector<UiNode*>{leaving_node}, leave);
        }
    }

    UiNode* InputController::root() const {
        // the tree is the one the controller was pointed at (§8.3)
        return m_root;
    }

    UiNode* InputController::hovered() const {
        // the hovered node is the innermost of the chain, because a child covers its parent (§8.3)
        return deepest_of(m_hover_chain);
    }

    UiNode* InputController::deepest_of(const std::vector<UiNode*>& chain) {
        // an empty chain means the pointer is over nothing (§8.3)
        if (chain.empty())
            return nullptr;
        // the chain is built deepest first, so its front is the node that owns the point (§8.3)
        return chain.front();
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
        // every positioned event refreshes the hover chain, so a press also knows what it landed on (§8.3)
        update_hover(event.position);
        // a synthesized transition is the whole event, because it was already routed to the chain (§8.3)
        if (event.kind == EventKind::PointerEnter || event.kind == EventKind::PointerLeave)
            return;
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
    void InputController::refresh_hover() {
        // a controller without a tree has nothing hovered, because the chain follows the tree (§8.3)
        if (m_root == nullptr) {
            m_hover_chain.clear();
            return;
        }
        // a controller that never saw a pointer has nothing to replay, so a refresh invents nothing (§8.3)
        if (m_hover_chain.empty())
            return;
        // the last point of the innermost hovered node is where the pointer was, which is what the replay uses (§8.3)
        const Point point{m_hover_chain.front()->bounds().x, m_hover_chain.front()->bounds().y};
        // the exits are replayed first, so nothing keeps a hover pointing at content that is gone (§8.3)
        clear_hover();
        // the chain is rebuilt against the tree as it now stands, so the new state is correct at once (§8.3)
        update_hover(point);
    }

    void InputController::update_hover(Point point) {
        // the new chain is collected before anything is told, because a diff needs both sides (§8.3)
        std::vector<UiNode*> next;
        // a controller without a tree has no chain, so the refresh is a no op (§8.3)
        if (m_root != nullptr)
            InputDispatcher::hover_chain(*m_root, point, next);
        // a chain that did not change produces no transition, because a hover is not a move (§8.3)
        if (next == m_hover_chain)
            return;
        // the nodes that left are told first, because a leave must reach the inner node before its parent (§8.3)
        for (UiNode* node : m_hover_chain) {
            // a node that is still under the pointer keeps its hover, so it is not told again (§8.3)
            if (std::find(next.begin(), next.end(), node) != next.end())
                continue;
            Event leave{.kind = EventKind::PointerLeave, .position = point};
            InputDispatcher::dispatch_to_chain(std::vector<UiNode*>{node}, leave);
        }
        // the nodes that arrived are told after, so a move reads as leaving before entering (§8.3)
        for (UiNode* node : next) {
            // a node that was already under the pointer was not told it left, so it is not told again (§8.3)
            if (std::find(m_hover_chain.begin(), m_hover_chain.end(), node) != m_hover_chain.end())
                continue;
            Event enter{.kind = EventKind::PointerEnter, .position = point};
            InputDispatcher::dispatch_to_chain(std::vector<UiNode*>{node}, enter);
        }
        // the chain is replaced only after the transitions, so a handler asking for it sees the new state (§8.3)
        m_hover_chain = std::move(next);
    }

    void InputController::clear_hover() {
        // an empty chain has nothing to leave (§8.3)
        if (m_hover_chain.empty())
            return;
        // the exit events are replayed in chain order, because a stale hover must not survive the change (§8.3)
        for (UiNode* node : m_hover_chain) {
            // a node that left the tree is skipped, because the events own the tree (§8.3)
            if (node == nullptr)
                continue;
            Event leave{.kind = EventKind::PointerLeave, .position = Point{node->bounds().x, node->bounds().y}};
            InputDispatcher::dispatch_to_chain(std::vector<UiNode*>{node}, leave);
        }
        // the chain is emptied, so the next move starts from nothing (§8.3)
        m_hover_chain.clear();
    }
} // namespace albor
