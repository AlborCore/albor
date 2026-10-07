#pragma once

#include "core/geometry.h"
#include "focus/focus_manager.h"
#include "input/event_filter.h"
#include "input/input_dispatcher.h"
#include "ui/ui_event.h"
#include "ui/ui_node.h"

namespace albor
{
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

    private:
        // find the deepest focusable node under the point, which click to focus lands on (§8.3)
        [[nodiscard]] UiNode* focusable_under(UiNode& node, Point point) const;

        // route a positioned event, which moves the focus and captures a drag on a claimed press (§8.3)
        void deliver_pointer(Event& event);

        // route a keyboard event, which traverses the ring before the caret gets it (§8.3)
        void deliver_key(Event& event);

        // the tree the controller routes into, which the caller owns
        UiNode* m_root = nullptr;

        // the per window focus manager of §8.3, which the controller owns
        FocusManager m_focus;

        // the node dragging, which claims the moves until the button comes up (§8.3)
        UiNode* m_capture = nullptr;
    };
} // namespace albor
