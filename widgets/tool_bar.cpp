#include "widgets/tool_bar.h"

#include <algorithm>
#include <utility>

namespace albor
{
    ToolBar::ToolBar(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void ToolBar::measure(const SizeConstraints& constraints, Size& out_size) {
        // the children see only the space the padding leaves, which keeps the strip from overflowing (§8.2)
        const SizeConstraints inner = SizeConstraints::at_most(Size{.width = std::max(0.0f, constraints.maximum.width - m_padding * 2.0f), .height = std::max(0.0f, constraints.maximum.height - m_padding * 2.0f)});
        // the content accumulates the widths and the tallest visible child (§8.2)
        Size content{};
        // a gap joins two visible children, while the first one starts at the leading edge (§8.2)
        bool first = true;
        for (const auto& child : children()) {
            // a hidden child takes no space, because the strip does not show it (§8.2)
            if (!child->visible())
                continue;
            // each child measures inside the inset space, because that is what the strip gives it (§8.2)
            Size child_size{};
            child->measure(inner, child_size);
            // the widths accumulate, because the children sit side by side (§8.2)
            content.width += child_size.width;
            // the strip is as tall as its tallest visible child (§8.2)
            content.height = std::max(content.height, child_size.height);
            // a gap separates this child from the previous visible one (§8.2)
            if (!first)
                content.width += m_spacing;
            // the first visible child is recorded, so the second one takes a gap (§8.2)
            first = false;
        }
        // the insets are added back, because the strip surrounds the controls it frames (§8.2)
        const Size total{.width = content.width + m_padding * 2.0f, .height = content.height + m_padding * 2.0f};
        // the offered constraints are final, because the parent has the last word (§8.2)
        m_measured_size = constraints.constrain(total);
        // the preferred size follows the measured one, so a parent can query it (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void ToolBar::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, which the children sit inside (§8.2)
        m_bounds = bounds;
        // the insets are capped at the extent, so a wide padding still keeps children inside (§8.2)
        const float left = std::min(m_padding, bounds.width);
        const float top = std::min(m_padding, bounds.height);
        const float right = std::min(m_padding, std::max(0.0f, bounds.width - left));
        const float bottom = std::min(m_padding, std::max(0.0f, bounds.height - top));
        // the children are placed left to right inside the inset frame (§8.2)
        float x = bounds.x + left;
        // the height is what both insets leave (§8.2)
        const float height = std::max(0.0f, bounds.height - top - bottom);
        // the right inset bounds the run, which keeps a child from painting past the strip (§8.2)
        const float right_edge = bounds.x + bounds.width - right;
        // the first visible child takes the leading edge, while a gap precedes every later one (§8.2)
        bool first = true;
        for (const auto& child : children()) {
            // a hidden child is not placed, because the strip draws nothing for it (§8.2)
            if (!child->visible())
                continue;
            // a gap separates this child from the previous visible one (§8.2)
            if (!first)
                x += m_spacing;
            // the child keeps its measured width, because a strip does not stretch its controls (§8.2)
            const float width = std::min(child->measured_size().width, std::max(0.0f, right_edge - x));
            // the child sits at the vertical centre, which is what a strip of controls does (§8.2)
            const float child_height = std::min(child->measured_size().height, height);
            const float y = bounds.y + top + (height - child_height) * 0.5f;
            // the child takes its measured extent, which is the row contract (§8.2)
            arrange_if_changed(*child, Rect::from_origin_size(Point{.x = x, .y = y}, Size{.width = width, .height = child_height}));
            // the next child starts after this one (§8.2)
            x += width;
            // the first visible child is recorded, so the next one takes a gap (§8.2)
            first = false;
        }
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    SemanticNode ToolBar::semantic_node() const {
        // the role is a group, because a strip frames the controls it owns (§10.1)
        SemanticNode node{SemanticRole::Group};
        // the author id travels with the node, because automation selects by it (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the id is the name, because a strip carries no caption of its own (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement at the strip (§10.1)
        node.set_bounds(bounds());
        // a container is not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        return node;
    }

    bool ToolBar::accepts_focus() const {
        // a strip is a container, so the focus belongs to one of its controls (§8.3)
        return false;
    }

    void ToolBar::set_padding(float padding) {
        // the inset is clamped at zero, because a negative one would pull children outside the strip (§8.2)
        const float clamped = std::max(0.0f, padding);
        // an unchanged inset remeasures nothing, because the layout did not change (§8.2)
        if (m_padding == clamped)
            return;
        // the inset is stored first, so the invalidation walks a strip that already carries it (§8.2)
        m_padding = clamped;
        // a new inset changes the space the children receive, so both layout passes run (§8.2)
        invalidate_layout();
    }

    float ToolBar::padding() const { return m_padding; }

    void ToolBar::set_spacing(float spacing) {
        // the gap is clamped at zero, because a negative one would overlap children (§8.2)
        const float clamped = std::max(0.0f, spacing);
        // an unchanged gap remeasures nothing, because the row did not change (§8.2)
        if (m_spacing == clamped)
            return;
        // the gap is stored first, so the invalidation walks a strip that already carries it (§8.2)
        m_spacing = clamped;
        // a new gap changes the measured extent, so both layout passes run (§8.2)
        invalidate_layout();
    }

    float ToolBar::spacing() const { return m_spacing; }

    void ToolBar::set_background(Color background) {
        // an unchanged face redraws nothing, because the strip looks the same (§6.1)
        if (m_background == background)
            return;
        // the face is stored first, so the invalidation walks a strip that already paints it (§6.1)
        m_background = background;
        // a face change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color ToolBar::background() const {
        // a themed strip takes the panel face, which is the surface a group sits on (§6.1)
        if (theme() != nullptr)
            return resolve(theme()->palette().panel_background, m_background);
        // an unthemed strip keeps its own face, which is the surface it always had (§6.1)
        return m_background;
    }

    void ToolBar::set_corner_radius(float radius) {
        // the radius is clamped at zero, because a negative one would mean nothing (§6.2)
        const float clamped = std::max(0.0f, radius);
        // an unchanged radius redraws nothing, because the face looks the same (§6.2)
        if (m_radius == clamped)
            return;
        // the radius is stored first, so the invalidation walks a strip that already draws it (§6.2)
        m_radius = clamped;
        // a radius change is visual only, so the layout passes are left alone (§6.2)
        invalidate();
    }

    float ToolBar::corner_radius() const { return m_radius; }
} // namespace albor
