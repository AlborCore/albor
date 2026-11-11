#include "albor/input/input_controller.h"

#include <algorithm>
#include <utility>

#include "albor/commands/command_registry.h"

namespace albor
{
    void InputController::attach(UiNode& root) {
        // the tree is adopted without ownership, because the scene owner keeps it alive (§8.3)
        m_root = &root;
        // a detaching subtree is reported before it leaves, while the node is still alive to be told (§8.3)
        root.set_detaching_handler([this](UiNode* node) { release_subtree(node); });
    }

    void InputController::release_subtree(UiNode* node) {
        // a leaving subtree drops the caret it held, because a hidden control must not keep receiving keys (§8.3)
        UiNode* focused = m_focus.focused();
        if (focused != nullptr) {
            // the focused node descends from the leaving subtree, so the caret moves out first (§8.3)
            for (const UiNode* current = focused; current != nullptr; current = current->parent()) {
                if (current == node) {
                    m_focus.clear_focus();
                    break;
                }
            }
        }
        // a leaving subtree reports the drag capture it held, because moves must not reach a hidden control (§8.3)
        bool leaves_capture = false;
        if (m_capture != nullptr) {
            // the captured node descends from the leaving subtree, so the capture goes with it (§8.3)
            for (const UiNode* current = m_capture; current != nullptr; current = current->parent()) {
                if (current == node) {
                    leaves_capture = true;
                    break;
                }
            }
        }
        // the captured node is still alive and still in the tree, so it is told before the mutation (§8.3)
        if (leaves_capture) {
            // the generation advances, because a dispatch walking this press must not arm a new capture (§8.3)
            m_capture_generation++;
            // the cancel is the ending the node would never receive from a release (§8.3)
            release_capture(PointerCancelReason::Detached);
        }
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
        // every routed pointer event records the position a later cancel hands back (§8.3)
        m_last_pointer = event.position;
        // a cancel ends the capture, which is not a gesture the tree can claim by position (§8.3)
        if (event.kind == EventKind::PointerCancel) {
            // only a live capture is the event's to claim, because a cancel without a drag has nothing to end (§8.3)
            const bool captured = m_capture != nullptr;
            // the capture drops through the one path that tells its node (§8.3)
            cancel_pointer_capture(event.cancel_reason);
            // the cancel is claimed exactly when it ended a drag (§8.3)
            event.handled = captured;
            return;
        }
        // a request that already reached the controller carries its own gesture, so no hover is diffed for it (§8.3)
        if (event.kind == EventKind::ContextMenu) {
            deliver_context_menu(event);
            return;
        }
        // every positioned event refreshes the hover chain, so a press also knows what it landed on (§8.3)
        update_hover(event.position);
        // a synthesized transition is the whole event, because it was already routed to the chain (§8.3)
        if (event.kind == EventKind::PointerEnter || event.kind == EventKind::PointerLeave)
            return;
        // a secondary press asks for a context menu, which is a gesture and not a press the tree sees (§8.3)
        if (event.kind == EventKind::PointerDown && event.button == PointerButton::Right) {
            // the request keeps the press position, because the menu opens where the pointer landed (§8.3)
            Event request{.kind = EventKind::ContextMenu, .position = event.position, .button = event.button};
            deliver_context_menu(request);
            event.handled = request.handled;
            return;
        }
        // a press with no drag yet routes by position and may start a capture (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // a press while a drag is alive belongs to the captured node, which honors the capture (§8.3)
            if (m_capture != nullptr) {
                InputDispatcher::dispatch_to_focused(*m_root, event, m_capture);
                return;
            }
            // the sequence is read before dispatch, because a press may close the surface it lands on (§8.3)
            const std::uint64_t generation = m_capture_generation;
            UiNode* claimed = InputDispatcher::dispatch(*m_root, event);
            // only a claimed press moves the caret or arms a drag, because an unclaimed one is empty space (§8.3)
            if (!event.handled)
                return;
            // a hide during dispatch already ended the drag, so it must not be armed again (§8.3)
            if (generation != m_capture_generation)
                return;
            // the primary button is the one that moves the caret, while a middle press leaves the focus alone (§8.3)
            if (event.button == PointerButton::Left) {
                // the deepest focusable node under the press names what the pointer landed on (§8.3)
                UiNode* target = focusable_under(*m_root, event.position);
                // only a control under the press changes the cue, because empty space keeps the last focus (§8.3)
                if (target != nullptr) {
                    // the pointer lowers the cue, which is the modality that must not paint a keyboard ring (§8.3)
                    m_focus.hide_focus_cue();
                    // a control that takes pointer focus moves the caret, while preserve and none keep it (§8.3)
                    if (target->pointer_focus_policy() == PointerFocusPolicy::Take)
                        m_focus.request_focus(target, FocusReason::Pointer);
                }
            }
            // the node that claimed the press carries the drag, so moves keep reaching it outside its bounds (§8.3)
            if (claimed != nullptr && !claimed->disabled() && claimed->hit_test(event.position)) {
                // the drag remembers which button armed it, because only that release ends it (§8.3)
                m_capture = claimed;
                m_capture_button = event.button;
            }
            return;
        }
        // a release that ends a drag reaches the captured node before the capture drops (§8.3)
        if (event.kind == EventKind::PointerUp && m_capture != nullptr) {
            // the release bubbles from the captured node, which is the last event of the drag (§8.3)
            InputDispatcher::dispatch_to_focused(*m_root, event, m_capture);
            // only the button that armed the drag ends it, so another release leaves the capture armed (§8.3)
            if (event.button == m_capture_button)
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

    void InputController::deliver_context_menu(Event& event) {
        // a live drag owns the pointer, so the request asks the captured node before the tree (§8.3)
        if (m_capture != nullptr) {
            InputDispatcher::dispatch_to_focused(*m_root, event, m_capture);
            return;
        }
        // a request with no drag follows the pointer, which is the secondary press path of §8.3
        InputDispatcher::dispatch(*m_root, event);
    }

    void InputController::deliver_key(Event& event) {
        // a key press is the keyboard modality, which raises the cue a pointer lowered (§8.3)
        if (event.kind == EventKind::KeyDown)
            m_focus.reveal_focus();
        // the escape key cancels a drag before anything else, which is the abort path of §8.3
        if (event.kind == EventKind::KeyDown && event.key == Key::Escape) {
            // a live drag is the first escape target, because it owns the pointer until it aborts (§8.3)
            if (m_capture != nullptr) {
                // the capture drops through the cancel path, so the node never mistakes the abort for a release (§8.3)
                cancel_pointer_capture(PointerCancelReason::Escape);
                // the escape itself is claimed, because it cancelled the drag the pointer owned (§8.3)
                event.handled = true;
                return;
            }
            // the focused control sees escape first, because a popup must close before focus is cleared (§8.3)
            if (UiNode* focused = m_focus.focused())
                InputDispatcher::dispatch_to_focused(*m_root, event, focused);
            // an escape the control declined is left unhandled, so a dialog can dismiss on it (§8.3)
            return;
        }
        // the menu key and shift f10 ask for the context menu of what holds the focus (§8.3)
        if (event.kind == EventKind::KeyDown && (event.key == Key::Menu || (event.key == Key::F10 && event.modifiers.shift))) {
            // the caret names the node, because a keyboard request carries no pointer position (§8.3)
            UiNode* anchor = m_focus.focused() != nullptr ? m_focus.focused() : hovered();
            // a tree with no caret and no hover has nothing to anchor a menu at (§8.3)
            if (anchor != nullptr) {
                // the centre of the node is where a keyboard menu opens, because a key carries no position (§8.3)
                const Rect& area = anchor->bounds();
                Event request{.kind = EventKind::ContextMenu, .position = Point{area.x + area.width * 0.5f, area.y + area.height * 0.5f}, .button = PointerButton::Right};
                InputDispatcher::dispatch_to_focused(*m_root, request, anchor);
                // a request a node took is the whole key, so nothing else runs (§8.3)
                if (request.handled) {
                    event.handled = true;
                    return;
                }
            }
            // an unhandled request leaves the key to the accelerators, which may bind it (§18.5)
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
        // a modified key is an application shortcut, which the standards reserve before the caret sees it (§8.3)
        if (!event.modifiers.none() && deliver_shortcut(event))
            return;
        // every other key reaches the caret and then its ancestors, which is the keyboard path of §8.3
        InputDispatcher::dispatch_to_focused(*m_root, event, m_focus.focused());
        // an unmodified accelerator only runs when the caret declined the key, so text entry always wins (§18.5)
        if (!event.handled)
            (void)deliver_shortcut(event);
    }

    bool InputController::deliver_shortcut(const Event& event) {
        // a window with no accelerators routes nothing, which is an application that declared none (§18.5)
        if (m_commands == nullptr)
            return false;
        // the accelerator is the key plus the modifiers that were held, which is what the table indexed (§18.5)
        Command* command = m_commands->find_shortcut(Shortcut{.key = event.key, .modifiers = event.modifiers});
        // an unbound accelerator belongs to the caret, so the walk continues (§18.5)
        if (command == nullptr)
            return false;
        // the key is claimed whether or not the command ran, so a disabled command cannot fall through (§18.5)
        (void)command->invoke(CommandInvocation{.command = command->id(), .source = InvocationSource::Keyboard});
        // the claim is counted, which is what the routing tests assert on (§18.5)
        m_shortcut_invocations++;
        return true;
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

    void InputController::cancel_pointer_capture(PointerCancelReason reason) {
        // the next dispatch must not arm a capture the window cannot release, so the generation advances (§8.3)
        m_capture_generation++;
        // the capture drops through the one path that tells its node, because a silent drop leaves it dragging (§8.3)
        release_capture(reason);
    }

    void InputController::release_capture(PointerCancelReason reason) {
        // a controller with no live capture has nothing to tell, which makes a repeated cancel a no op (§8.3)
        if (m_capture == nullptr)
            return;
        // the captured node is read before the capture is cleared, because the cancel is delivered to it (§8.3)
        UiNode* captured = m_capture;
        // the button that armed the drag is what the cancel names, so a widget can match the gesture (§8.3)
        const PointerButton button = m_capture_button;
        // the capture drops before the node is told, so a handler that asks the controller sees no drag (§8.3)
        m_capture = nullptr;
        // the drag forgets its button with the node, because a stale button must not end a later drag (§8.3)
        m_capture_button = PointerButton::Left;
        // the cancel carries the position the pointer last held, because the abort has no point of its own (§8.3)
        Event cancel{.kind = EventKind::PointerCancel, .position = m_last_pointer, .button = button, .cancel_reason = reason};
        // the captured node is the only owner of the gesture, so it is told exactly once (§8.3)
        InputDispatcher::dispatch_to_chain(std::vector<UiNode*>{captured}, cancel);
    }

    void InputController::set_command_registry(CommandRegistry* registry) {
        // the registry is borrowed, because the application owns it and outlives every window (§18.5)
        m_commands = registry;
    }

    CommandRegistry* InputController::command_registry() const { return m_commands; }

    std::size_t InputController::shortcut_invocations() const { return m_shortcut_invocations; }

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
