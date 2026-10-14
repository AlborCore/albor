#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "core/align.h"
#include "core/constraints.h"
#include "core/geometry.h"
#include "core/paint.h"
#include "render/render_context.h"
#include "ui/ui_event.h"

namespace albor
{
    // the animation core a widget drives itself with, declared ahead so this header stays light (§7.3)
    class AnimationEngine;
    // the node of the UI tree, which owns its children and the two pass layout contract (§8.1, §8.2)
    class UiNode
    {
    public:
        // build a root node, which every other node is added to
        UiNode();

        virtual ~UiNode();

        UiNode(const UiNode&) = delete;
        UiNode& operator=(const UiNode&) = delete;
        UiNode(UiNode&&) = delete;
        UiNode& operator=(UiNode&&) = delete;

        // hand out the parent, which is null for a root node
        [[nodiscard]] UiNode* parent() const;

        // hand out the children, in the order they were added (§8.1)
        [[nodiscard]] const std::vector<std::unique_ptr<UiNode>>& children() const;

        // hand out how many children the node has
        [[nodiscard]] std::size_t child_count() const;

        // hand out the child at an index, returning null when the index is past the end (§8.1)
        [[nodiscard]] UiNode* child_at(std::size_t index) const;

        // adopt a child and append it, which takes ownership (§8.1)
        UiNode& add_child(std::unique_ptr<UiNode> child);

        // adopt a child at an index, which clamps a past the end index to the end (§8.1)
        UiNode& insert_child(std::size_t index, std::unique_ptr<UiNode> child);

        // remove a child and hand it back to the caller, returning null when the node is not a child (§8.1)
        [[nodiscard]] std::unique_ptr<UiNode> remove_child(UiNode* child);

        // measure the node against the constraints a parent offers it (§8.2)
        virtual void measure(const SizeConstraints& constraints, Size& out_size);

        // assign the final bounds of the node, which a parent does after measuring (§8.2)
        virtual void arrange(const Rect& bounds);

        // record the drawing commands of the node into the context, which stage one replays (§6.1)
        virtual void render(RenderContext& context);

        // report whether the point lands on this node or on one of its descendants (§8.3)
        [[nodiscard]] virtual bool hit_test(Point logical_point) const;

        // offer the event to this node only, because the dispatcher walks the tree so each node is offered once (§8.3)
        virtual void dispatch_event(Event& event);

        // hand out the bounds a parent arranged this node to (§8.2)
        [[nodiscard]] const Rect& bounds() const;

        // hand out the size the node measured itself to (§8.2)
        [[nodiscard]] const Size& measured_size() const;

        // set how the node sits on the horizontal axis inside the space its parent gives it (§8.2)
        void set_horizontal_align(Align align);

        // hand out how the node sits on the horizontal axis, which is inherited until an author sets it (§8.2)
        [[nodiscard]] Align horizontal_align() const;

        // set how the node sits on the vertical axis inside the space its parent gives it (§8.2)
        void set_vertical_align(Align align);

        // hand out how the node sits on the vertical axis, which is inherited until an author sets it (§8.2)
        [[nodiscard]] Align vertical_align() const;

        // request a redraw of this node and of everything above it (§6.1)
        void invalidate();

        // mark the layout dirty and request the measure and arrange passes (§8.2)
        void invalidate_layout();

        // mark only the arrange dirty, which is what a change of position asks for (§8.2)
        void invalidate_arrange();

        // report whether the node asked for a redraw since the last frame (§6.1)
        [[nodiscard]] bool render_dirty() const;

        // report whether the node asked for either layout pass since the last frame (§8.2)
        [[nodiscard]] bool layout_dirty() const;

        // report whether the node asked to be measured again since the last frame (§8.2)
        [[nodiscard]] bool measure_dirty() const;

        // report whether the node asked to be arranged again since the last frame (§8.2)
        [[nodiscard]] bool arrange_dirty() const;

        // clear both dirty flags, which the scene builder does after it built a frame (§6.1)
        void clear_dirty();

        // report whether the node draws at all, because an invisible node is skipped by stage one (§6.1)
        [[nodiscard]] bool visible() const;

        // show or hide the node, which invalidates it so the change reaches the screen (§6.1)
        virtual void set_visible(bool visible);

        // set how opaque the node and its subtree draw, which dims rather than removes them (§6.1)
        void set_opacity(float opacity);

        // hand out how opaque the node draws, where one is fully opaque (§6.1)
        [[nodiscard]] float opacity() const;

        // set how wide the node strokes its own border, where zero is no border at all (§6.1)
        void set_border_width(float width);

        // hand out how wide the node strokes its own border, which a per state control resolves through (§6.1)
        [[nodiscard]] virtual float border_width() const;

        // replace the colour the node strokes its own border with, which is independent of its fill (§6.1)
        void set_border_color(Color color);

        // hand out the colour the node strokes its own border with, which a per state control resolves through (§6.1)
        [[nodiscard]] virtual Color border_color() const;

        // set the shadow the node casts, which an empty shadow removes (§6.1)
        void set_drop_shadow(Shadow shadow);

        // hand out the shadow the node casts, which is empty while none is set (§6.1)
        [[nodiscard]] const Shadow& drop_shadow() const;

        // report whether the node refuses input, which a focus traversal and a hit test rely on (§8.3)
        [[nodiscard]] virtual bool disabled() const;

        // install the callback an invalidation reaches, which the frame pipeline turns into a frame request (§6.1)
        void set_invalidation_handler(std::function<void()> handler);

        // install the handler told when a subtree is about to leave the tree, which the input module needs (§8.3)
        void set_detaching_handler(std::function<void(UiNode*)> handler);

        // hand out the identity the author assigned, which automation and semantics key on (AD-11)
        [[nodiscard]] const NodeIdentity& identity() const;

        // assign the author identity, which is what makes a node addressable by id (AD-11, §18.1)
        void set_explicit_id(std::string id);

        // assign the in tree identifier, which the scene builder hands out once
        void set_node_id(NodeId node);

        // hand the engine to this node and its descendants, which is how a widget schedules an animation (§7.3)
        void set_animation_engine(AnimationEngine* engine);

        // hand out the engine this node was given, which is null when no animation is driven (§7.3)
        [[nodiscard]] AnimationEngine* animation_engine() const;

        // walk the node and its descendants depth first, which the semantic tree and the builder share (§8.1)
        void visit(const std::function<void(UiNode&)>& visitor);
        // walk this node and its descendants depth first for reading, which the tests assert on
        void visit(const std::function<void(const UiNode&)>& visitor) const;

    protected:
        // tell the observer that a subtree is about to leave, which runs while the node is still alive (§8.3)
        void notify_detaching(UiNode* node);

        // push the node opacity onto the context, which dims the whole subtree below it (§6.1)
        void push_opacity(RenderContext& context) const;

        // pop what the push put on the context, which a sibling draws after (§6.1)
        void pop_opacity(RenderContext& context) const;

        // record the children without pushing this node's opacity, because a component already pushed it (§6.1)
        void render_children(RenderContext& context);

        // arrange the child only when it asked for a pass or its bounds would move, which keeps a relayout local (§8.2)
        void arrange_if_changed(UiNode& child, const Rect& bounds);

        // the final bounds a parent arranged this node to (§8.2)
        Rect m_bounds;

        // the size the node measured itself to, which arrange falls back on (§8.2)
        Size m_measured_size;

        // the owning parent, which is null for a root node (§8.1)
        UiNode* m_parent = nullptr;

        // the owned children, in the order they were added (§8.1)
        std::vector<std::unique_ptr<UiNode>> m_children;

        // the engine a widget schedules through, which the pipeline hands down the tree (§7.3)
        AnimationEngine* m_animations = nullptr;

        // the identity of the node, whose explicit id is the authoritative one (AD-11)
        NodeIdentity m_identity;

        // the callback an invalidation reaches, which the frame pipeline installs on the root (§6.1)
        std::function<void()> m_invalidation_handler;

        // the handler told about a detaching subtree, which only the root carries (§8.3)
        std::function<void(UiNode*)> m_detaching_handler;

        // whether the node asked for a redraw since the last frame (§6.1)
        bool m_render_dirty = true;

        // whether the node asked to be measured again since the last frame (§8.2)
        bool m_measure_dirty = true;

        // whether the node asked to be arranged again since the last frame (§8.2)
        bool m_arrange_dirty = true;

        // whether the node draws at all (§6.1)
        bool m_visible = true;

        // how opaque the node draws, where one is fully opaque and zero still records (§6.1)
        float m_opacity = 1.0f;

        // how wide the node strokes its own border, where zero means it draws no border (§6.1)
        float m_border_width = 0.0f;

        // the colour the node strokes its own border with, which is set independently of the fill (§6.1)
        Color m_border_color = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);

        // the shadow the node casts, which is empty while none is set (§6.1)
        Shadow m_drop_shadow;

        // how the node sits on the horizontal axis, which a parent reads when it arranges (§8.2)
        Align m_horizontal_align = Align::Inherit;

        // how the node sits on the vertical axis, which a parent reads when it arranges (§8.2)
        Align m_vertical_align = Align::Inherit;
    };
} // namespace albor
