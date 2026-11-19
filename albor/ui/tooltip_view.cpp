#include "albor/ui/tooltip_view.h"

#include <utility>

namespace albor
{
    TooltipView::TooltipView(std::string text, TextLayout layout, TooltipStyle style) :
        Component(nullptr), m_text(std::move(text)), m_layout(std::move(layout)), m_style(style) {
        // the cast belongs to the node, which the component background paints (§6.2)
        set_drop_shadow(m_style.shadow);
    }

    void TooltipView::measure(const SizeConstraints& constraints, Size& out_size) {
        // the bubble is as large as its run plus the padding, clamped by what the parent offers (§8.2)
        m_measured_size = constraints.constrain(bubble_size());
        // the preferred size is kept, because a parent may query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        // the measured extent is handed back, which is what the controller places (§8.3)
        out_size = m_measured_size;
    }

    void TooltipView::arrange(const Rect& bounds) {
        // the component places its children and keeps the assigned rect, which the rich path uses (§8.2)
        Component::arrange(bounds);
    }

    bool TooltipView::hit_test(Point logical_point) const {
        // the bubble is inert, so the pointer stays on the control that owns it and the tip stays open (§8.3)
        (void)logical_point;
        return false;
    }

    void TooltipView::paint_content(RenderContext& context) {
        // an empty run carries no glyphs, so recording it would draw nothing (§6.2)
        if (m_layout.empty())
            return;
        // the run is clipped to the plate, because a rounded bubble must not leak its text (§6.2)
        context.save();
        context.clip_rect(bounds());
        // the run starts inside the padding, which the style owns (§8.4)
        context.draw_text(m_layout, Point{.x = bounds().x + m_style.padding_horizontal, .y = bounds().y + m_style.padding_vertical}, {.fill = m_style.text});
        context.restore();
    }

    Color TooltipView::background() const {
        // the plate is the tooltip face, which a theme fills and the default keeps readable (§6.1, §8.4)
        return m_style.background;
    }

    float TooltipView::border_width() const {
        // the hairline is the style's, because a bubble over a dark surface needs one (§6.1)
        return m_style.border_width;
    }

    Color TooltipView::border_color() const {
        // the hairline colour is the style's, which a theme resolves beside the plate (§6.1)
        return m_style.border;
    }

    float TooltipView::corner_radius() const {
        // the bubble rounds at the style's corner, which a small plate keeps modest (§6.2)
        return m_style.corner_radius;
    }

    void TooltipView::set_content(std::string text, TextLayout layout) {
        // unchanged content shows the same bubble, so no frame is requested (§8.3)
        if (m_text == text && m_layout == layout)
            return;
        // the content is stored first, so a redraw reads the run it was given (§9.1)
        m_text = std::move(text);
        m_layout = std::move(layout);
        // a new run changes the extent the bubble needs, so the layout passes run again (§8.2)
        invalidate_layout();
    }

    const std::string& TooltipView::text() const {
        // the text is what the bubble presents, which a reader hears through the target (§8.3, §10.1)
        return m_text;
    }

    const TextLayout& TooltipView::layout() const {
        // the run is handed out whole, because a test and a scene replay both read it (§9.1)
        return m_layout;
    }

    void TooltipView::set_style(TooltipStyle style) {
        // an unchanged style paints the same bubble, so no frame is requested (§8.4)
        if (m_style == style)
            return;
        // the style is stored first, so the cast and the plate read the value they were given (§8.4)
        m_style = style;
        // the cast belongs to the node, which the component background paints (§6.2)
        set_drop_shadow(m_style.shadow);
        // the plate changed, so the frame is rebuilt (§6.1)
        invalidate();
    }

    const TooltipStyle& TooltipView::style() const {
        // the style is handed out for reading, because a test asserts the theme reached the bubble (§8.4)
        return m_style;
    }

    Rect TooltipView::place(Rect anchor, Rect viewport) {
        // the old visual area includes the cast, which paints outside the plate (§6.1)
        const Rect previous_visual = visual_bounds();
        // the bubble is placed against the anchor inside the view it floats over (§8.3)
        m_bounds = place_tooltip(anchor, bubble_size(), viewport, m_style.gap, m_style.margin);
        // a move damages both visual extents, because the old shadow fringe is still on the surface (§6.1)
        if (previous_visual != visual_bounds())
            invalidate(previous_visual.unioned(visual_bounds()));
        // the rect is handed back, because the controller reads where the bubble landed (§8.3)
        return m_bounds;
    }

    Size TooltipView::bubble_size() const {
        // the run is framed by the padding the style names, which is the whole bubble (§8.2)
        return Size{.width = m_layout.size.width + m_style.padding_horizontal * 2.0f, .height = m_layout.size.height + m_style.padding_vertical * 2.0f};
    }
} // namespace albor
