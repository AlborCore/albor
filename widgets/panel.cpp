#include "widgets/panel.h"

#include <algorithm>
#include <utility>

namespace albor
{
    Panel::Panel(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    float Panel::resolved_padding() const {
        // an explicit inset wins, because the author named it on this panel (§8.2)
        if (m_padding >= 0.0f)
            return m_padding;
        // a themed panel takes the design system's inset, which is one decision for the surface (§8.2)
        if (theme() != nullptr)
            return theme()->metrics().panel_padding;
        // the unthemed fallback keeps the panel readable without an installed theme (§8.2)
        return default_metrics().panel_padding;
    }

    void Panel::measure(const SizeConstraints& constraints, Size& out_size) {
        // the inset is resolved once, because every child and the total read it (§8.2)
        const float inset = resolved_padding();
        // the children see only the space the padding leaves, which keeps the panel from overflowing (§8.2)
        SizeConstraints inner = constraints;
        // the minimum shrinks on both axes, because the inset is part of the panel extent (§8.2)
        inner.minimum.width = std::max(0.0f, constraints.minimum.width - inset * 2.0f);
        inner.minimum.height = std::max(0.0f, constraints.minimum.height - inset * 2.0f);
        // the preferred extent shrinks for the same reason (§8.2)
        inner.preferred.width = std::max(0.0f, constraints.preferred.width - inset * 2.0f);
        inner.preferred.height = std::max(0.0f, constraints.preferred.height - inset * 2.0f);
        // the ceiling shrinks while an infinite ceiling stays infinite (§8.2)
        inner.maximum.width = std::max(0.0f, constraints.maximum.width - inset * 2.0f);
        inner.maximum.height = std::max(0.0f, constraints.maximum.height - inset * 2.0f);
        // the content accumulates the widest child and the stacked height (§8.2)
        Size content{};
        // the gap between two children is part of the stacked extent, so it is counted between them (§8.2)
        bool first = true;
        for (const auto& child : children()) {
            // a hidden child takes no space, because the panel does not show it (§8.2)
            if (!child->visible())
                continue;
            // each child measures inside the inset space, because that is the space the panel gives it (§8.2)
            Size child_size{};
            child->measure(inner, child_size);
            // the widest child decides the content width (§8.2)
            content.width = std::max(content.width, child_size.width);
            // a gap joins two visible children, while the first child starts at the top (§8.2)
            if (!first)
                content.height += m_spacing;
            // the child height joins the stack, which is the column contract (§8.2)
            content.height += child_size.height;
            // the first visible child is recorded, so the second child takes a gap (§8.2)
            first = false;
        }
        // the insets are added back, because the panel surrounds the content it frames (§8.2)
        const Size total{.width = content.width + inset * 2.0f, .height = content.height + inset * 2.0f};
        // the offered constraints are final, because the parent has the last word (§8.2)
        m_measured_size = constraints.constrain(total);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Panel::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, which the content sits inside (§8.2)
        m_bounds = bounds;
        // the inset is resolved once, because every child uses the same frame (§8.2)
        const float inset = resolved_padding();
        // an inset wider than the panel leaves no content area, which is better than a negative rectangle (§8.2)
        const float left = std::min(inset, bounds.width);
        const float top = std::min(inset, bounds.height);
        const float right = std::min(inset, std::max(0.0f, bounds.width - left));
        const float bottom = std::min(inset, std::max(0.0f, bounds.height - top));
        // the children are placed top to bottom inside the inset frame (§8.2)
        float y = bounds.y + top;
        // the content width is what both insets leave (§8.2)
        const float width = std::max(0.0f, bounds.width - left - right);
        // the bottom inset bounds the stack, which keeps a child from painting past the panel (§8.2)
        const float bottom_edge = bounds.y + bounds.height - bottom;
        // the first visible child takes the top edge, while a gap precedes every later one (§8.2)
        bool first = true;
        for (const auto& child : children()) {
            // a hidden child is not placed, because the panel draws nothing for it (§8.2)
            if (!child->visible())
                continue;
            // a gap separates this child from the previous visible one (§8.2)
            if (!first)
                y += m_spacing;
            // the child keeps its measured height, because a column does not stretch its children (§8.2)
            const float height = std::min(child->measured_size().height, std::max(0.0f, bottom_edge - y));
            // the horizontal alignment is resolved against the full content width a panel implies (§8.2)
            const Align horizontal = resolve_align(child->horizontal_align(), Align::Stretch);
            // the child width is the full inner width for a stretching child and its measured width otherwise (§8.2)
            const float child_width = horizontal == Align::Stretch ? width : std::min(child->measured_size().width, width);
            // the origin shifts a centred or end aligned child across the content width (§8.2)
            float child_x = bounds.x + left;
            if (horizontal == Align::Center)
                child_x += (width - child_width) * 0.5f;
            else if (horizontal == Align::End)
                child_x += width - child_width;
            // the child takes its aligned width and its measured height, which is the column contract (§8.2)
            arrange_if_changed(*child, Rect::from_origin_size(Point{.x = child_x, .y = y}, Size{.width = child_width, .height = height}));
            // the next child starts below this one (§8.2)
            y += height;
            // the first visible child is recorded, so the next child takes a gap (§8.2)
            first = false;
        }
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    SemanticNode Panel::semantic_node() const {
        // the role is a group, because a panel frames related controls rather than acting itself (§10.1)
        SemanticNode node{SemanticRole::Group};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the name is the author identity, because a panel carries no caption of its own (§10.1)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a panel is a grouping container, so it is not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        return node;
    }

    bool Panel::accepts_focus() const {
        // a panel is a container, so the focus belongs to its content (§8.3)
        return false;
    }

    void Panel::set_padding(float padding) {
        // an unchanged inset remeasures nothing, because the layout did not change (§8.2)
        if (m_padding == padding)
            return;
        // the inset is stored first, so the invalidation walks a panel that already carries it (§8.2)
        m_padding = padding;
        // a new inset changes the space the children receive, so both layout passes run (§8.2)
        invalidate_layout();
    }

    float Panel::padding() const {
        // the resolved inset is what the measure and arrange passes read (§8.2)
        return resolved_padding();
    }

    void Panel::set_spacing(float spacing) {
        // a negative gap would pull children over each other, so it is clamped to zero (§8.2)
        const float clamped = std::max(0.0f, spacing);
        // an unchanged gap remeasures nothing, because the stack did not change (§8.2)
        if (m_spacing == clamped)
            return;
        // the gap is stored first, so the invalidation walks a panel that already carries it (§8.2)
        m_spacing = clamped;
        // a new gap changes the stacked extent, so both layout passes run (§8.2)
        invalidate_layout();
    }

    float Panel::spacing() const {
        // the gap is part of the panel's own contract, so it is handed back as stored (§8.2)
        return m_spacing;
    }

    void Panel::set_background(Color background) {
        // an unchanged authored face redraws nothing, because the panel looks the same (§6.1)
        if (m_background_set && m_background == background)
            return;
        // the face is stored first, so the invalidation walks a panel that already paints it (§6.1)
        m_background = background;
        // an authored face wins over the theme, which is what a one-off surface asks for (§6.1)
        m_background_set = true;
        // a face change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color Panel::background() const {
        // an authored face wins over the theme, which is what a one-off surface asks for (§6.1)
        if (!m_background_set && theme() != nullptr)
            return resolve(theme()->palette().panel_background, m_background);
        // the panel keeps its own face when the author wrote one or no theme is installed (§6.1)
        return m_background;
    }

    void Panel::set_corner_radius(float radius) {
        // an unchanged radius redraws nothing, because the corner is the same (§6.2)
        if (m_radius == radius)
            return;
        // the radius is stored first, so the invalidation walks a panel that already carries it (§6.2)
        m_radius = radius;
        // a corner change is visual only, so the layout passes are left alone (§6.2)
        invalidate();
    }

    float Panel::corner_radius() const {
        // an explicit radius wins, because the author wrote it for this panel (§6.2)
        if (m_radius >= 0.0f)
            return m_radius;
        // a themed panel takes the card radius the design system names (§6.2)
        if (theme() != nullptr)
            return theme()->metrics().panel_radius;
        // the unthemed fallback keeps the panel visibly rounded (§6.2)
        return default_metrics().panel_radius;
    }

    void Panel::set_border_width(float width) {
        // an unchanged stroke redraws nothing, because the border is the same (§8.4)
        if (m_border == width)
            return;
        // the width is stored first, so the invalidation walks a panel that already strokes it (§8.4)
        m_border = width;
        // a stroke change is visual only, so the layout passes are left alone (§8.4)
        invalidate();
    }

    float Panel::border_width() const {
        // an explicit stroke wins, because the author wrote it for this panel (§8.4)
        if (m_border >= 0.0f)
            return m_border;
        // a themed panel takes the card hairline the design system names (§8.4)
        if (theme() != nullptr)
            return theme()->metrics().panel_border_width;
        // the unthemed fallback keeps a hairline so the card reads as one (§8.4)
        return default_metrics().panel_border_width;
    }

    void Panel::set_border_color(Color color) {
        // the node stores the colour first, so the border resolver reads the authored value (§8.4)
        UiNode::set_border_color(color);
        // an authored border colour wins over the theme, which is what a one-off surface asks for (§8.4)
        m_border_color_set = true;
        // a border change is visual only, so the layout passes are left alone (§8.4)
        invalidate();
    }

    Color Panel::border_color() const {
        // a themed panel resolves the border by state until the author writes one (§8.4)
        if (!m_border_color_set && theme() != nullptr)
            return resolve(theme()->palette().panel_border, UiNode::border_color());
        // an authored or unthemed panel keeps the colour the node stored (§8.4)
        return UiNode::border_color();
    }
} // namespace albor
