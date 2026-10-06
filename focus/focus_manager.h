#pragma once

#include <vector>

#include "ui/ui_node.h"

namespace albor
{
    // the per window focus manager of §8.3, which owns which node holds the caret
    //
    // the manager stores a non owning pointer, because the ui tree owns the nodes and outlives any focus state (§8.3)
    class FocusManager
    {
    public:
        // build a manager over no node, which is the state of a window that has not been focused yet (§8.3)
        FocusManager() = default;

        FocusManager(const FocusManager&) = delete;
        FocusManager& operator=(const FocusManager&) = delete;
        FocusManager(FocusManager&&) = delete;
        FocusManager& operator=(FocusManager&&) = delete;
        ~FocusManager() = default;

        // move the focus to a node and clear the one that had it, which is what a click or a tab press does (§8.3)
        void request_focus(UiNode* node);

        // drop the focus, which is what losing the window does (§8.3)
        void clear_focus();

        // hand out the focused node, which is null when the window has no focus (§8.3)
        [[nodiscard]] UiNode* focused() const;

        // report whether the node holds the focus, which a semantic adapter reads (§10.1, §8.3)
        [[nodiscard]] bool has_focus(const UiNode* node) const;

        // move the focus to the next focusable node depth first and wrapping, which is what tab does (§8.3)
        void focus_next(UiNode& root);

        // move the focus to the previous focusable node depth first and wrapping, which is what shift tab does (§8.3)
        void focus_previous(UiNode& root);

        // report whether the node can take the focus at all, which every traversal rule depends on (§8.3)
        [[nodiscard]] static bool focusable(const UiNode& node);

        // set the callback a focus change reaches, which is what asks the tree for a frame (§6.1, §8.3)
        void set_focus_changed_handler(std::function<void()> handler);

        // hand out how many times the focus changed, which the tests assert on (§8.3)
        [[nodiscard]] std::size_t change_count() const;

    private:
        // gather the focusable nodes of the subtree in depth first order, which is the traversal order of §8.3
        [[nodiscard]] static std::vector<UiNode*> focusable_nodes(UiNode& root);

        // move the focus to the node at an index in the depth first order, which both traversals share (§8.3)
        void focus_at(UiNode& root, int direction);

        // clear the focus and then set it, so exactly one node is focused at a time (§8.3)
        void move_focus(UiNode* node);

        // the focused node, which the ui tree owns (§8.3)
        UiNode* m_focused = nullptr;

        // the callback a focus change reaches, which the frame pipeline installs (§6.1, §8.3)
        std::function<void()> m_focus_changed;

        // how many times the focus moved, which the tests assert on (§8.3)
        std::size_t m_changes = 0;
    };
} // namespace albor
