#include "ui/ui_node.h"

#include <algorithm>
#include <utility>

namespace albor
{
    bool NodeIdentity::identified() const {
        // an author assigned id is the only identity automation may select on (AD-11)
        return !explicit_id.empty();
    }

    bool Modifiers::none() const {
        // a plain press carries no modifier, which is what a focus traversal expects (§8.3)
        return !shift && !control && !option && !command;
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
        child->m_measure_dirty = true;
        child->m_arrange_dirty = true;
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
        child->m_measure_dirty = true;
        child->m_arrange_dirty = true;
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
        // the observer is told first, while the child is still alive and still in the tree (§8.3)
        notify_detaching(child);
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
        // the offered ceiling travels down, because a parent knows the space the child has to fit in (§8.2)
        const SizeConstraints child_constraints = SizeConstraints::at_most(constraints.maximum);
        for (const auto& child : m_children) {
            // each child measures against that ceiling, so text and content wrap inside the space the parent gives (§8.2, §9.1)
            Size child_size{};
            child->measure(child_constraints, child_size);
            // the container is at least as wide and as tall as its largest child (§8.2)
            total.width = std::max(total.width, child_size.width);
            total.height = std::max(total.height, child_size.height);
        }
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void UiNode::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, while the measured size stands as the measure settled it (§8.2)
        m_bounds = bounds;
        // the arrange phase pushes the same bounds into every child, which a container overrides (§8.2)
        for (const auto& child : m_children)
            arrange_if_changed(*child, bounds);
    }

    void UiNode::render(RenderContext& context) {
        // a container draws nothing itself, but still records its children (§6.1)
        for (const auto& child : m_children) {
            // an invisible child is not visited, so the scene carries only what is on screen (§6.1)
            if (!child->visible())
                continue;
            child->render(context);
        }
    }

    bool UiNode::hit_test(Point logical_point) const {
        // the children are hit tested last added first, because the topmost one wins (§8.3)
        for (auto position = m_children.size(); position > 0; position--)
            if (m_children[position - 1]->hit_test(logical_point))
                return true;
        // a container with no visible content under the point is not a hit (§8.3)
        return false;
    }

    void UiNode::dispatch_event(Event&) {
        // the offer is the node only, because the dispatcher already walked the children and a second walk would deliver twice (§8.3)
    }

    const Rect& UiNode::bounds() const {
        // the bounds are the ones a parent assigned (§8.2)
        return m_bounds;
    }

    const Size& UiNode::measured_size() const {
        // the measured size is what the measure phase produced (§8.2)
        return m_measured_size;
    }

    void UiNode::set_horizontal_align(Align align) {
        // an unchanged alignment relayouts nothing, because the arrangement did not change (§8.2)
        if (m_horizontal_align == align)
            return;
        // the alignment is stored first, so the invalidation walks a tree that already knows the new one (§8.2)
        m_horizontal_align = align;
        // a parent arranges from this, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    Align UiNode::horizontal_align() const { return m_horizontal_align; }

    void UiNode::set_vertical_align(Align align) {
        // an unchanged alignment relayouts nothing, because the arrangement did not change (§8.2)
        if (m_vertical_align == align)
            return;
        // the alignment is stored first, so the invalidation walks a tree that already knows the new one (§8.2)
        m_vertical_align = align;
        // a parent arranges from this, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    Align UiNode::vertical_align() const { return m_vertical_align; }

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
        // the measure and arrange passes both run, because a changed extent moves the node (§8.2)
        m_measure_dirty = true;
        // the arrange runs as well, because the measure may settle on new bounds (§8.2)
        m_arrange_dirty = true;
        // the request walks up to the root, which owns the frame scheduling (§8.2)
        if (m_parent != nullptr)
            m_parent->invalidate_layout();
        // only the root schedules a frame, so a deep change requests exactly one (§6.1)
        if (m_invalidation_handler && m_parent == nullptr)
            m_invalidation_handler();
    }

    void UiNode::invalidate_arrange() {
        // a change of position also changes what the node draws (§8.2)
        m_render_dirty = true;
        // only the arrange is marked, because the extent the last measure settled on is unchanged (§8.2)
        m_arrange_dirty = true;
        // the request walks up to the root, which owns the frame scheduling (§8.2)
        if (m_parent != nullptr)
            m_parent->invalidate_arrange();
        // only the root schedules a frame, so a deep change requests exactly one (§6.1)
        if (m_invalidation_handler && m_parent == nullptr)
            m_invalidation_handler();
    }

    void UiNode::arrange_if_changed(UiNode& child, const Rect& bounds) {
        // a child that asked for no pass and would not move keeps the placement it already has (§8.2)
        if (!child.m_measure_dirty && !child.m_arrange_dirty && child.m_bounds == bounds)
            return;
        // anything else is arranged, because the pass or the move is what the child is owed (§8.2)
        child.arrange(bounds);
    }

    bool UiNode::render_dirty() const { return m_render_dirty; }

    bool UiNode::layout_dirty() const { return m_measure_dirty || m_arrange_dirty; }

    bool UiNode::measure_dirty() const { return m_measure_dirty; }

    bool UiNode::arrange_dirty() const { return m_arrange_dirty; }

    void UiNode::clear_dirty() {
        // the flags clear after a frame, so the next invalidation is distinguishable (§6.1)
        m_render_dirty = false;
        // both layout flags clear together, because a finished frame settled both passes (§6.1)
        m_measure_dirty = false;
        // the arrange flag clears with them, because the same frame placed every node (§6.1)
        m_arrange_dirty = false;
        // the children cleared as well, because a frame covers the whole subtree (§6.1)
        for (const auto& child : m_children)
            child->clear_dirty();
    }

    bool UiNode::visible() const { return m_visible; }

    bool UiNode::disabled() const {
        // a plain node is never disabled, because only a control carries the state (§8.3)
        return false;
    }

    void UiNode::set_visible(bool visible) {
        // an unchanged visibility invalidates nothing (§6.1)
        if (m_visible == visible)
            return;
        // a node about to hide is told first, while it is still in the tree and still hoverable (§8.3)
        if (!visible)
            notify_detaching(this);
        // the flag is stored first, so the invalidation walks a subtree that already knows it is hidden (§6.1)
        m_visible = visible;
        // showing or hiding a node changes what the frame contains (§6.1)
        invalidate_layout();
    }

    void UiNode::set_invalidation_handler(std::function<void()> handler) {
        // the handler belongs to the root, so a caller installs it on the root alone (§6.1)
        m_invalidation_handler = std::move(handler);
    }

    void UiNode::set_detaching_handler(std::function<void(UiNode*)> handler) {
        // the handler belongs to the root, so a caller installs it on the root alone (§8.3)
        m_detaching_handler = std::move(handler);
    }

    void UiNode::notify_detaching(UiNode* node) {
        // the notification climbs to the root, because only the root carries the handler (§8.3)
        if (m_parent != nullptr) {
            m_parent->notify_detaching(node);
            return;
        }
        // a root with no observer has nothing to tell, because nobody is watching the tree (§8.3)
        if (!m_detaching_handler)
            return;
        // the handler is handed the node, which is still alive, so it can replay the state it holds (§8.3)
        m_detaching_handler(node);
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
