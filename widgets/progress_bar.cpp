#include "widgets/progress_bar.h"

#include <algorithm>

namespace albor
{
    ProgressBar::ProgressBar(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void ProgressBar::measure(const SizeConstraints& constraints, Size& out_size) {
        // a progress bar has no content, so its extent comes from the themed control metric (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the control measures to the themed extent (§8.2)
        Size content{.width = metrics.progress_bar_extent.width, .height = metrics.progress_bar_extent.height};
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

    void ProgressBar::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void ProgressBar::paint_content(RenderContext& context) {
        // a bar with no area draws nothing, because there is no track to draw (§6.2)
        if (bounds().empty())
            return;
        // the metrics come from the theme when one is installed (§6.1)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the thickness is what the theme says, or the value the bar always drew with (§6.1)
        const float thickness = metrics.progress_bar_track_thickness;
        // the track is a thin band centred vertically, because progress reads as a horizontal line (§6.1)
        const Rect track = Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y + (bounds().height - thickness) * 0.5f}, Size{.width = bounds().width, .height = thickness});
        // a themed track carries a radius, which is what rounds it into a capsule (§8.4)
        const float track_radius = metrics.progress_bar_radius;
        // the track colour resolves by state, so the disabled paint differs without a second literal (§6.1)
        const Paint track_paint{.fill = resolve(track_colors(), m_track_color)};
        // a themed radius means a capsule, which is the shape a native bar is (§8.4)
        if (track_radius > 0.0f)
            context.draw_rounded_rect(track, track_radius, track_paint);
        // without a themed radius the track is a hard band (§6.1)
        else
            context.draw_rect(track, track_paint);
        // an empty bar has no fill, because there is no progress to show (§8)
        if (m_value <= 0.0f)
            return;
        // the fill runs from the left edge for the fraction of the track the value names (§8)
        const Rect filled = Rect::from_origin_size(Point{track.x, track.y}, Size{.width = track.width * m_value, .height = track.height});
        // the fill colour resolves by state, where a transparent entry draws nothing (§6.1)
        const Paint fill_paint{.fill = resolve(fill_colors(), m_fill_color)};
        // a transparent fill records nothing, because the theme chose to show no progress there (§6.1)
        if (!fill_paint.fills())
            return;
        // a themed radius is capped at half the fill, so a short fill still rounds cleanly (§8.4)
        const float fill_radius = std::min(track_radius, filled.width * 0.5f);
        // the fill shares the track radius, so the two endpoints read as one line (§8.4)
        if (fill_radius > 0.0f)
            context.draw_rounded_rect(filled, fill_radius, fill_paint);
        // without a themed radius the fill is the hard band the track carries (§6.1)
        else
            context.draw_rect(filled, fill_paint);
    }

    SemanticNode ProgressBar::semantic_node() const {
        // the role is a progress bar, because the user reads a value rather than drags one (§10.1)
        SemanticNode node{SemanticRole::ProgressBar};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled bar reports the disabled state, which is what dims its paint (§10.1)
        node.set_disabled(disabled());
        // a progress bar is not checkable, so the check state stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the name carries the fraction, because the numeric value is what a screen reader must speak (§10.1)
        node.set_name(identity().explicit_id + " " + std::to_string(m_value));
        return node;
    }

    void ProgressBar::set_value(float value) {
        // the value is clamped into the unit range, because progress is a fraction rather than an absolute number (§8)
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        // an unchanged value redraws nothing, because the bar looks the same (§6.1)
        if (m_value == clamped)
            return;
        // the value is stored first, so the invalidation walks a bar that already carries it (§8)
        m_value = clamped;
        // a moved fill changes the frame, so the node asks for a redraw (§6.1)
        invalidate();
    }

    float ProgressBar::value() const {
        // the value is what the render pass and the semantic node both read (§8)
        return m_value;
    }

    void ProgressBar::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the bar looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the redraw walks a bar that already carries it (§6.1)
        m_disabled = disabled;
        // a disabled change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    bool ProgressBar::disabled() const {
        // the bar owns the state, because it presents no command and takes no input (§8.3)
        return m_disabled;
    }

    void ProgressBar::set_track_color(Color color) {
        // an unchanged colour redraws nothing, because the bar looks the same (§6.1)
        if (m_track_color == color)
            return;
        // the colour is stored first, so the invalidation walks a bar that already fills the new track (§6.1)
        m_track_color = color;
        // a track colour change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    void ProgressBar::set_fill_color(Color color) {
        // an unchanged colour redraws nothing, because the bar looks the same (§6.1)
        if (m_fill_color == color)
            return;
        // the colour is stored first, so the invalidation walks a bar that already fills with the new colour (§6.1)
        m_fill_color = color;
        // a fill colour change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    const Color* ProgressBar::track_colors() const {
        // without a theme the table is unused, because the resolver returns the bar's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().progress_bar_track;
    }

    const Color* ProgressBar::fill_colors() const {
        // without a theme the table is unused, because the resolver returns the bar's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the accent fills the progress the bar carries (§6.1)
        return theme()->palette().progress_bar_fill;
    }
} // namespace albor
