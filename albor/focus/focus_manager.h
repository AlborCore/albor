#pragma once

#include <vector>

#include "albor/ui/ui_node.h"

namespace albor
{
    // where a focus request came from, because a pointer and a key must not produce the same cue (§8.3)
    enum class FocusReason
    {
        Pointer,
        Keyboard,
        Programmatic,
        Accessibility
    };

    // report whether the reason shows the keyboard focus cue, because only a pointer origin hides it (§8.3)
    [[nodiscard]] constexpr bool shows_focus_cue(FocusReason reason) {
        // a pointer press is the one origin that leaves the control unmarked, which is the focus visible rule (§8.3)
        return reason != FocusReason::Pointer;
    }

    // the per window focus manager, which holds a non owning pointer because the tree owns the nodes (§8.3)
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

        // move the focus to a node with the reason it was requested, which decides the cue it carries (§8.3)
        void request_focus(UiNode* node, FocusReason reason = FocusReason::Programmatic);

        // drop the focus, which is what losing the window does (§8.3)
        void clear_focus();

        // show the cue for the focused node, which any keyboard interaction asks for (§8.3)
        void reveal_focus();

        // hide the cue without dropping the owner, which a pointer press on a control asks for (§8.3)
        void hide_focus_cue();

        // tell the manager whether the window holds key status, which hides and restores the cue (§5.3, §8.3)
        void set_window_active(bool active);

        // hand out the focused node, which is null when the window has no focus (§8.3)
        [[nodiscard]] UiNode* focused() const;

        // report whether the effective cue is visible, which a test or an inspector reads (§8.3)
        [[nodiscard]] bool focus_visible() const;

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
        void move_focus(UiNode* node, FocusReason reason);

        // hand the effective cue to the owner, which is the desired cue while the window is active (§8.3)
        void apply_focus_visibility();

        // the focused node, which the ui tree owns (§8.3)
        UiNode* m_focused = nullptr;

        // whether the owner asked for the cue, which a key interaction raises and a pointer press lowers (§8.3)
        bool m_focus_visible = false;

        // whether the window holds key status, which hides the cue without dropping the owner (§8.3)
        bool m_window_active = true;

        // the callback a focus change reaches, which the frame pipeline installs (§6.1, §8.3)
        std::function<void()> m_focus_changed;

        // how many times the focus moved, which the tests assert on (§8.3)
        std::size_t m_changes = 0;
    };
} // namespace albor
