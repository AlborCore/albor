#pragma once

#include <vector>

#include "core/geometry.h"
#include "focus/focus_manager.h"
#include "input/event_filter.h"
#include "input/input_dispatcher.h"
#include "ui/ui_event.h"
#include "ui/ui_node.h"

namespace albor
{
    // the registry a key is looked up in, which is declared ahead of its definition so the header stays light (§18.5)
    class CommandRegistry;

    // the per window input driver, which owns the focus ring and routes the events of §8.3 into one tree
    class InputController
    {
    public:
        InputController() = default;

        InputController(const InputController&) = delete;
        InputController& operator=(const InputController&) = delete;
        InputController(InputController&&) = delete;
        InputController& operator=(InputController&&) = delete;
        ~InputController() = default;

        // point the controller at the tree it routes into, which the caller owns throughout its life (§8.3)
        void attach(UiNode& root);

        // hand out the tree the controller routes into, which is null before attach (§8.3)
        [[nodiscard]] UiNode* root() const;

        // route a normalized event into the tree, which is what the application input handler invokes (§8.3)
        void deliver(Event& event);

        // hand out the per window focus manager, which the tree owner asks about the caret (§8.3)
        [[nodiscard]] FocusManager& focus();

        // hand out the per window focus manager for reading (§8.3)
        [[nodiscard]] const FocusManager& focus() const;

        // report whether a pointer drag is captured, which the tests assert on (§8.3)
        [[nodiscard]] bool capturing() const;

        // report the node the pointer is over, which is the innermost hovered node (§8.3)
        [[nodiscard]] UiNode* hovered() const;

        // replay the exits after the tree changed under the pointer, which an application calls once it mutated it (§8.3)
        void refresh_hover();

        // replay the exits for a subtree that is leaving the tree, which the tree reports before it goes (§8.3)
        void release_subtree(UiNode* node);

        // point the controller at the accelerators of an application, which is null while it declares none (§18.5)
        void set_command_registry(CommandRegistry* registry);

        // hand out the accelerators the controller routes through, which is null while none were installed (§18.5)
        [[nodiscard]] CommandRegistry* command_registry() const;

        // hand out how many keys an accelerator has claimed, which the routing tests assert on (§18.5)
        [[nodiscard]] std::size_t shortcut_invocations() const;

    private:
        // find the deepest focusable node under the point, which click to focus lands on (§8.3)
        [[nodiscard]] UiNode* focusable_under(UiNode& node, Point point) const;

        // refresh the hover chain against the point, which synthesizes the enter and leave transitions (§8.3)
        void update_hover(Point point);

        // replay a leave to every hovered node, which a tree change under the pointer needs (§8.3)
        void clear_hover();

        // hand out the innermost node of a chain, or null when the chain is empty (§8.3)
        [[nodiscard]] static UiNode* deepest_of(const std::vector<UiNode*>& chain);

        // route a positioned event, which moves the focus and captures a drag on a claimed press (§8.3)
        void deliver_pointer(Event& event);

        // route a keyboard event, which traverses the ring before the caret gets it (§8.3)
        void deliver_key(Event& event);

        // offer the key to a bound accelerator, which returns whether one claimed it (§18.5)
        [[nodiscard]] bool deliver_shortcut(const Event& event);

        // the tree the controller routes into, which the caller owns
        UiNode* m_root = nullptr;

        // the per window focus manager of §8.3, which the controller owns
        FocusManager m_focus;

        // the node dragging, which claims the moves until the button comes up (§8.3)
        UiNode* m_capture = nullptr;

        // the nodes the pointer is currently over, deepest first, which the transitions are diffed against (§8.3)
        std::vector<UiNode*> m_hover_chain;

        // the accelerators this window routes keys through, which is null while the application declares none (§18.5)
        CommandRegistry* m_commands = nullptr;

        // how many keys an accelerator has claimed, which the routing tests assert on (§18.5)
        std::size_t m_shortcut_invocations = 0;
    };
} // namespace albor
