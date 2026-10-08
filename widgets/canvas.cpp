#include <algorithm>

#include "widgets/canvas.h"

namespace albor
{
    Canvas::Canvas(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void Canvas::measure(const SizeConstraints& constraints, Size& out_size) {
        // the declared extent is what the canvas offers, because a callback cannot be asked what it will draw (§8.2)
        Size content = m_intrinsic_size;
        // the children are measured too, because a widget is a component and they are part of its content (§8.1, §8.2)
        Size children{};
        UiNode::measure(constraints, children);
        // a widget is at least as large as its own content and as its children (§8.2)
        content.width = std::max(content.width, children.width);
        content.height = std::max(content.height, children.height);
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Canvas::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void Canvas::paint_content(RenderContext& context) {
        // the pass is counted before the callback runs, so a canvas that draws nothing still records that it was asked (§6.4)
        m_draw_count++;
        // an empty canvas draws nothing, because there is no callback to draw with (§6.4)
        if (!m_draw)
            return;
        // the callback records into the stage one context, which is what keeps the frame immutable (§6.1, AD-18)
        m_draw(context);
    }

    SemanticNode Canvas::semantic_node() const {
        // the role is a canvas, because the user may interact with what is drawn (§10.1)
        SemanticNode node{SemanticRole::Canvas};
        // the accessible name is the identity the author assigned, because a canvas carries no text (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a canvas that draws nothing is announced as disabled, because there is nothing to interact with (§10.1)
        node.set_disabled(!draws());
        return node;
    }

    void Canvas::set_draw(std::function<void(RenderContext&)> draw) {
        // the callback is swapped in place, because the next pass must run the new work (§6.4)
        m_draw = std::move(draw);
        // the content changed, so the next frame must be built (§6.1)
        invalidate();
    }

    bool Canvas::draws() const {
        // a canvas with no callback records nothing, so there is no canvas to interact with (§6.4)
        return static_cast<bool>(m_draw);
    }

    void Canvas::set_intrinsic_size(Size size) {
        // an unchanged extent remeasures nothing, because the layout did not change (§8.2)
        if (m_intrinsic_size == size)
            return;
        // the extent is stored first, so the invalidation walks a canvas that already knows its new size (§8.2)
        m_intrinsic_size = size;
        // a new extent changes what the canvas fills, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    Size Canvas::intrinsic_size() const {
        // the declared extent is what the measure pass used (§8.2)
        return m_intrinsic_size;
    }

    std::uint64_t Canvas::draw_count() const {
        // the count is how many times the callback was offered a context (§6.4)
        return m_draw_count;
    }
} // namespace albor
