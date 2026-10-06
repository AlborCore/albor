#include "focus/focus_manager.h"

#include <algorithm>
#include <utility>

namespace albor
{
    void FocusManager::request_focus(UiNode* node) {
        // a null node cannot take the focus, so the request is refused rather than stored (§8.3)
        if (node == nullptr)
            return;
        // a node that cannot take the focus is refused, so tab traversal stays consistent (§8.3)
        if (!focusable(*node))
            return;
        // focusing the node that already has it changes nothing, so no frame is requested (§8.3)
        if (m_focused == node)
            return;
        // the focus moves through the one path both requests and traversals share (§8.3)
        move_focus(node);
    }

    void FocusManager::clear_focus() {
        // a manager with no focus clears nothing, so no frame is requested (§8.3)
        if (m_focused == nullptr)
            return;
        // the focus moves to nothing, which is what losing the window does (§8.3)
        move_focus(nullptr);
    }

    UiNode* FocusManager::focused() const {
        // the focused node is the one the keyboard path routes to (§8.3)
        return m_focused;
    }

    bool FocusManager::has_focus(const UiNode* node) const {
        // a null node holds no focus, so the answer is never true for one (§8.3)
        if (node == nullptr)
            return false;
        // the focus belongs to exactly one node at a time (§8.3)
        return m_focused == node;
    }

    void FocusManager::focus_next(UiNode& root) {
        // tab moves forward through the depth first order of §8.3
        focus_at(root, 1);
    }

    void FocusManager::focus_previous(UiNode& root) {
        // shift tab moves backward through the same depth first order of §8.3
        focus_at(root, -1);
    }

    bool FocusManager::focusable(const UiNode& node) {
        // a hidden node takes no focus, because the user cannot see where the caret is (§8.3)
        if (!node.visible())
            return false;
        // a node with no area takes no focus, because there is nothing to point at (§8.3)
        if (node.bounds().empty())
            return false;
        // every visible node with area can take the focus, which is what phase 3 tab traversal relies on (§8.3)
        return true;
    }

    void FocusManager::set_focus_changed_handler(std::function<void()> handler) {
        // the handler is installed on the manager, which the frame pipeline owns alongside the tree (§6.1, §8.3)
        m_focus_changed = std::move(handler);
    }

    std::size_t FocusManager::change_count() const {
        // the count is how many times the focus actually moved (§8.3)
        return m_changes;
    }

    std::vector<UiNode*> FocusManager::focusable_nodes(UiNode& root) {
        // the walk is depth first and in insertion order, which is the tab order a user expects (§8.3)
        std::vector<UiNode*> nodes;
        root.visit([&nodes](UiNode& node) {
            // only a node that can take the focus enters the order, so tab skips containers (§8.3)
            if (focusable(node))
                nodes.push_back(&node);
        });
        return nodes;
    }

    void FocusManager::focus_at(UiNode& root, int direction) {
        // the depth first order is the ring tab walks, so the whole subtree is one sequence (§8.3)
        const std::vector<UiNode*> nodes = focusable_nodes(root);
        // a tree with nothing focusable has no ring, so the focus stays where it is (§8.3)
        if (nodes.empty())
            return;
        // the size is converted once, because the ring wraps in both directions (§8.3)
        const auto count = static_cast<int>(nodes.size());
        // a focus that is not in this tree starts at the first node going forward and the last going back (§8.3)
        int index = direction > 0 ? -1 : count;
        // a focus inside the ring is located, because tab continues from where the caret already is (§8.3)
        const auto current = std::find(nodes.begin(), nodes.end(), m_focused);
        if (current != nodes.end())
            index = static_cast<int>(std::distance(nodes.begin(), current));
        // the index moves one step and wraps, which is what makes tab cycle through the whole ring (§8.3)
        const int moved = ((index + direction) % count + count) % count;
        // the focus moves to the node the walk landed on (§8.3)
        move_focus(nodes[static_cast<std::size_t>(moved)]);
    }

    void FocusManager::move_focus(UiNode* node) {
        // the previous holder is cleared first, so a node is never focused twice (§8.3)
        if (m_focused != nullptr) {
            // the node knows its focus changed, because that is what its semantic state reports (§8.3, §10.1)
            m_focused->invalidate();
        }
        // the new holder is stored, so the keyboard path routes to it (§8.3)
        m_focused = node;
        // the new holder is invalidated, because a focus ring or a caret is a visual change (§8.3)
        if (m_focused != nullptr)
            m_focused->invalidate();
        // the change is counted before it is forwarded, so a test sees what the manager did (§8.3)
        m_changes++;
        // the frame request reaches the tree, because a focus change must be presented (§6.1, §8.3)
        if (m_focus_changed)
            m_focus_changed();
    }
} // namespace albor
