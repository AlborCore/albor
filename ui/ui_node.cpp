#include "ui/ui_node.h"

#include <algorithm>
#include <utility>

namespace albor
{
    bool NodeIdentity::identified() const {
        // an author assigned id is the only identity automation may select on (AD-11)
        return !explicit_id.empty();
    }

    UiNode::UiNode() = default;

    UiNode::~UiNode() {
        // the children are owned, so detaching them here keeps a partially destroyed subtree consistent (§8.1)
        for (auto& child : m_children)
            child->m_parent = nullptr;
    }

    UiNode* UiNode::parent() const {
        // a root node has no parent (§8.1)
        return m_parent;
    }

    const std::vector<std::unique_ptr<UiNode>>& UiNode::children() const {
        // the children are handed out in insertion order (§8.1)
        return m_children;
    }

    std::size_t UiNode::child_count() const { return m_children.size(); }

    UiNode* UiNode::child_at(std::size_t index) const {
        // an index past the end names no child (§8.1)
        if (index >= m_children.size())
            return nullptr;
        return m_children[index].get();
    }

    UiNode& UiNode::add_child(std::unique_ptr<UiNode> child) {
        // a null child is refused rather than stored, so a failed allocation cannot become a dangling entry (§8.1)
        if (!child)
            return *this;
        // the child learns its parent before it is stored, so a handler that walks up finds one (§8.1)
        child->m_parent = this;
        // a fresh child has never drawn, so it starts dirty (§6.1)
        child->m_render_dirty = true;
        child->m_layout_dirty = true;
        m_children.push_back(std::move(child));
        // adding a subtree changes what the parent draws, so the parent asks for a frame (§6.1)
        invalidate_layout();
        return *this;
    }

    UiNode& UiNode::insert_child(std::size_t index, std::unique_ptr<UiNode> child) {
        // a null child is refused rather than stored (§8.1)
        if (!child)
            return *this;
        // an index past the end appends, which is what a caller that computed an index from a stale tree wants (§8.1)
        const std::size_t position = std::min(index, m_children.size());
        child->m_parent = this;
        child->m_render_dirty = true;
        child->m_layout_dirty = true;
        m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(position), std::move(child));
        // reordering a subtree changes what the parent draws (§6.1)
        invalidate_layout();
        return *this;
    }

    std::unique_ptr<UiNode> UiNode::remove_child(UiNode* child) {
        // the child is located by identity, because the tree owns the pointers (§8.1)
        const auto found = std::find_if(m_children.begin(), m_children.end(), [child](const std::unique_ptr<UiNode>& candidate) { return candidate.get() == child; });
        // a node that is not a child is not removed (§8.1)
        if (found == m_children.end())
            return nullptr;
        std::unique_ptr<UiNode> removed = std::move(*found);
        m_children.erase(found);
        // the detached node is a root again, so a later walk up does not reach a dying parent (§8.1)
        removed->m_parent = nullptr;
        // the subtree left the tree, so the parent asks for a frame (§6.1)
        invalidate_layout();
        return removed;
    }

    void UiNode::measure(const SizeConstraints& constraints, Size& out_size) {
        // a container measures itself as the largest of its children, which is the measure phase of §8.2
        Size total{};
        for (const auto& child : m_children) {
            // each child is measured unconstrained, because the container only learns the extent from the result (§8.2)
            Size child_size{};
            child->measure(SizeConstraints::unconstrained(), child_size);
            // the container is at least as wide and as tall as its largest child (§8.2)
            total.width = std::max(total.width, child_size.width);
            total.height = std::max(total.height, child_size.height);
        }
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void UiNode::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the arrange phase pushes the same bounds into every child, which a container overrides (§8.2)
        for (const auto& child : m_children)
            child->arrange(bounds);
    }

    void UiNode::render(RenderContext& context) {
        // a container draws nothing itself, but still records its children (§6.1)
        for (const auto& child : m_children)
            child->render(context);
    }

    bool UiNode::hit_test(Point logical_point) const {
        // the children are hit tested last added first, because the topmost one wins (§8.3)
        for (auto position = m_children.size(); position > 0; position--)
            if (m_children[position - 1]->hit_test(logical_point))
                return true;
        // a container with no visible content under the point is not a hit (§8.3)
        return false;
    }

    void UiNode::dispatch_event(Event& event) {
        // the children are offered the event in reverse order, matching the hit test order (§8.3)
        for (auto position = m_children.size(); position > 0; position--) {
            // a claimed event stops the walk, so no ancestor acts on it twice (§8.3)
            if (event.handled)
                return;
            m_children[position - 1]->dispatch_event(event);
        }
    }

    const Rect& UiNode::bounds() const {
        // the bounds are the ones a parent assigned (§8.2)
        return m_bounds;
    }

    const Size& UiNode::measured_size() const {
        // the measured size is what the measure phase produced (§8.2)
        return m_measured_size;
    }

    void UiNode::invalidate() {
        // the node itself asks for a redraw (§6.1)
        m_render_dirty = true;
        // the redraw must reach the surface, so the request walks up to the root (§6.1)
        if (m_parent != nullptr)
            m_parent->invalidate();
        // only the root carries the handler, so exactly one frame is requested per invalidation (§6.1)
        if (m_invalidation_handler && m_parent == nullptr)
            m_invalidation_handler();
    }

    void UiNode::invalidate_layout() {
        // a layout change also changes what the node draws (§8.2)
        m_render_dirty = true;
        // the measure and arrange passes run from the root, so the dirty flag bubbles (§8.2)
        m_layout_dirty = true;
        // the request walks up to the root, which owns the frame scheduling (§8.2)
        if (m_parent != nullptr)
            m_parent->invalidate_layout();
        // only the root schedules a frame, so a deep change requests exactly one (§6.1)
        if (m_invalidation_handler && m_parent == nullptr)
            m_invalidation_handler();
    }

    bool UiNode::render_dirty() const { return m_render_dirty; }

    bool UiNode::layout_dirty() const { return m_layout_dirty; }

    void UiNode::clear_dirty() {
        // the flags clear after a frame, so the next invalidation is distinguishable (§6.1)
        m_render_dirty = false;
        m_layout_dirty = false;
        // the children cleared as well, because a frame covers the whole subtree (§6.1)
        for (const auto& child : m_children)
            child->clear_dirty();
    }

    bool UiNode::visible() const { return m_visible; }

    void UiNode::set_visible(bool visible) {
        // an unchanged visibility invalidates nothing (§6.1)
        if (m_visible == visible)
            return;
        // the flag is stored first, so the invalidation walks a subtree that already knows it is hidden (§6.1)
        m_visible = visible;
        // showing or hiding a node changes what the frame contains (§6.1)
        invalidate_layout();
    }

    void UiNode::set_invalidation_handler(std::function<void()> handler) {
        // the handler belongs to the root, so a caller installs it on the root alone (§6.1)
        m_invalidation_handler = std::move(handler);
    }

    const NodeIdentity& UiNode::identity() const {
        // the identity is what automation and semantics read (AD-11)
        return m_identity;
    }

    void UiNode::set_explicit_id(std::string id) {
        // the author assigned id is authoritative and is never overwritten by a correlation hint (AD-11)
        m_identity.explicit_id = std::move(id);
    }

    void UiNode::set_node_id(NodeId node) {
        // the in tree identifier is a correlation hint, which automation must never select on (AD-11)
        m_identity.node = node;
    }

    void UiNode::visit(const std::function<void(UiNode&)>& visitor) {
        // the node itself comes before its children, which is the order a depth first walk uses (§8.1)
        visitor(*this);
        for (const auto& child : m_children)
            child->visit(visitor);
    }

    void UiNode::visit(const std::function<void(const UiNode&)>& visitor) const {
        // the node itself comes before its children, which is the order a depth first walk uses (§8.1)
        visitor(*this);
        for (const auto& child : m_children)
            // the const overload is named explicitly, because a const unique_ptr still yields a mutable child (§8.1)
            static_cast<const UiNode&>(*child).visit(visitor);
    }
} // namespace albor
