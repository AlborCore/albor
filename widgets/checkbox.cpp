#include <algorithm>
#include <utility>

#include "commands/command.h"
#include "widgets/checkbox.h"

namespace albor
{
    CheckBox::CheckBox(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void CheckBox::measure(const SizeConstraints& constraints, Size& out_size) {
        // the extent comes from the theme when one is installed, so a sizing decision lives in one place (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // a checkbox is a fixed square, because its extent comes from the control metric rather than from content (§8.2)
        Size content{.width = metrics.checkbox_box.width, .height = metrics.checkbox_box.height};
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

    void CheckBox::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    Rect CheckBox::focus_ring_rect() const {
        // the ring surrounds the fixed square, which is the visual the user aims at (§8.4)
        return box_rect();
    }

    Rect CheckBox::box_rect() const {
        // the metric is the control size, because the box is a fixed square whatever it is arranged into (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // a parent may arrange the control wider than the box, so the square sits at the leading edge (§8.2)
        const float extent = std::min(bounds().width, metrics.checkbox_box.width);
        // the height is the same value, because a checkbox is a square (§8)
        return Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y}, Size{.width = extent, .height = metrics.checkbox_box.height});
    }

    void CheckBox::paint_content(RenderContext& context) {
        // a box with no area draws nothing, because there is no outline to stroke (§6.2)
        if (bounds().empty())
            return;
        // the square is the fixed metric, because the box does not stretch with its arranged bounds (§8.2)
        const Rect box = box_rect();
        // the metrics answer the radius and the glyph, which is the theme's decision when one is installed (§8.4)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // a checked or mixed box carries a fill, which is the only way the accent reads without a glyph (§6.1)
        if (m_checked || m_indeterminate) {
            // the fill resolves by state, so a hover and a press differ without a second literal (§6.1)
            const Paint fill_paint{.fill = resolve(fill_colors(), m_check_fill)};
            // a themed fill shares the box radius, so the accent reads as one rounded shape (§8.4)
            if (metrics.checkbox_corner_radius > 0.0f)
                context.draw_rounded_rect(box, metrics.checkbox_corner_radius, fill_paint);
            // without a themed radius the fill is the square the box always drew (§6.1)
            else
                context.draw_rect(box, fill_paint);
        }
        // the outline resolves by state, so the border reads as part of the control (§6.1)
        if (metrics.checkbox_corner_radius > 0.0f)
            context.draw_rounded_rect(box, metrics.checkbox_corner_radius, {.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = resolve(outline_colors(), m_outline), .stroke_width = outline_width()});
        // without a themed radius the outline is the hard rectangle the box always drew (§8.4)
        else
            context.draw_rect(box, {.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = resolve(outline_colors(), m_outline), .stroke_width = outline_width()});
        // a mixed box draws the dash before the mark, because one is checked and the other is not (§10.1)
        if (m_indeterminate)
            paint_indeterminate_dash(context, box);
        // a checked box draws the check mark, which is the glyph a native box carries (§8.4)
        else if (m_checked)
            paint_check_mark(context, box);
    }

    void CheckBox::paint_check_mark(RenderContext& context, const Rect& box) const {
        // a box without a themed glyph keeps its plain fill, because the glyph is a theme decision (§8.4)
        if (theme() == nullptr || theme()->metrics().checkbox_glyph_stroke <= 0.0f)
            return;
        // the glyph colour is the role the theme answers, which reads over the accent fill (§8.4)
        const Color glyph = resolve(glyph_colors(), Color{0.96f, 0.97f, 0.99f, 1.0f});
        // the mark starts at the left quarter where the short leg begins (§8.4)
        const Point start{.x = box.x + box.width * 0.25f, .y = box.y + box.height - box.height * 0.30f};
        // the mark bends at the bottom centre, which is where the two legs meet (§8.4)
        const Point elbow{.x = box.x + box.width * 0.42f, .y = box.y + box.height - box.height * 0.12f};
        // the mark ends at the right quarter where the long leg reaches (§8.4)
        const Point end{.x = box.x + box.width - box.width * 0.08f, .y = box.y + box.height * 0.15f};
        // the three points join into one check mark stroke (§8.4)
        const Point points[] = {start, elbow, end};
        // the glyph strokes with the theme decided width, which is what makes the mark visible (§8.4)
        context.draw_polyline(points, {.color = glyph, .width = theme()->metrics().checkbox_glyph_stroke});
    }

    void CheckBox::paint_indeterminate_dash(RenderContext& context, const Rect& box) const {
        // a box without a themed glyph keeps its plain fill, because the glyph is a theme decision (§8.4)
        if (theme() == nullptr)
            return;
        // the glyph colour is the role the theme answers, which reads over the accent fill (§8.4)
        const Color glyph = resolve(glyph_colors(), Color{0.96f, 0.97f, 0.99f, 1.0f});
        // the dash spans the middle half of the box, which is what a mixed symbol reads as (§8.4)
        const Rect dash = Rect::from_origin_size(Point{.x = box.x + box.width * 0.20f, .y = box.y + box.height * 0.5f - theme()->metrics().checkbox_glyph_stroke * 0.5f}, Size{.width = box.width * 0.60f, .height = std::max(theme()->metrics().checkbox_glyph_stroke, 2.0f)});
        // the dash draws as one rounded bar over the accent fill (§8.4)
        context.draw_rounded_rect(dash, dash.height * 0.5f, {.fill = glyph});
    }

    bool CheckBox::hit_test(Point logical_point) const {
        // a hidden box is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled box is not a hit, because it refuses input (§8.3)
        if (disabled())
            return false;
        // the press must land on the square itself, which is what the eye sees as the control (§8.3)
        const Rect box = box_rect();
        const bool inside = logical_point.x >= box.x && logical_point.x < box.x + box.width && logical_point.y >= box.y && logical_point.y < box.y + box.height;
        // a press outside the box falls through to whatever is behind it (§8.3)
        return inside && !bounds().empty();
    }

    void CheckBox::dispatch_event(Event& event) {
        // a hidden box takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run first, so a press is a state before it is a toggle (§8.3)
        handle_hover_transitions(event);
        // a key that toggles the box is a press, which the focused box receives (§8.3)
        if (event.kind == EventKind::KeyDown && event.key == Key::Space) {
            // a disabled box does not claim the key, because it refuses input (§8.3)
            if (disabled())
                return;
            // a modified key is an application shortcut, so a plain toggle ignores it (§8.3)
            if (!event.modifiers.none())
                return;
            // the event is claimed, so no ancestor reacts to the same key (§8.3)
            event.handled = true;
            // the toggle runs once, which is what a key press means (§8.3)
            toggle();
            return;
        }
        // a press is the only other event that toggles a box (§8.3)
        if (event.kind != EventKind::PointerDown) {
            // any other event belongs to a parent, so the box does not claim it (§8.3)
            return;
        }
        // a disabled box does not claim a press, because it refuses input (§8.3)
        if (disabled())
            return;
        // a press outside the box belongs to whatever is behind it (§8.3)
        if (!hit_test(event.position))
            return;
        // the event is claimed, so no ancestor reacts to the same press (§8.3)
        event.handled = true;
        // the toggle runs once, which is what a press means (§8.3)
        toggle();
    }

    SemanticNode CheckBox::semantic_node() const {
        // the role is a checkbox, because a press flips a boolean (§10.1)
        SemanticNode node{SemanticRole::Checkbox};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the accessible name is the identity the author assigned, because an unlabelled box has no text (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled box reports the disabled state, which is what refuses activation (§10.1)
        node.set_disabled(disabled());
        // the focus flag is part of the announced state, so an adapter can follow the ring (§10.1)
        node.set_focused(focused());
        // the check state mirrors the stored boolean, which is what a screen reader announces (§10.1)
        node.set_check(m_indeterminate ? CheckState::Indeterminate : m_checked ? CheckState::Checked : CheckState::Unchecked);
        // the toggle action runs the same path a press does, and its identifier is discarded because a widget names actions by label (§10.1) the action body carries no disabled guard, because SemanticNode::invoke refuses every action while the node it belongs to is disabled, which is where that rule belongs (§10.1)
        (void)node.add_action("toggle", [control = mutable_owner(this)] {
            // the toggle runs through the shared command when one is bound, as a press does (§18.7)
            control->toggle();
        });
        return node;
    }

    void CheckBox::set_checked(bool checked) {
        // an unchanged state redraws nothing, because the box looks the same (§6.1)
        if (m_checked == checked)
            return;
        // the state is stored first, so the invalidation walks a box that already knows it is checked (§8.3)
        m_checked = checked;
        // a checked box changes its fill, so the frame must be redrawn (§6.1)
        invalidate();
    }

    bool CheckBox::checked() const {
        // the state is what the semantic check state and the render pass both read (§10.1)
        return m_checked;
    }

    void CheckBox::set_indeterminate(bool indeterminate) {
        // an unchanged state redraws nothing, because the box looks the same (§6.1)
        if (m_indeterminate == indeterminate)
            return;
        // the state is stored first, so the invalidation walks a box that already knows it is mixed (§8.3)
        m_indeterminate = indeterminate;
        // a mixed box changes its fill and its glyph, so the frame must be redrawn (§6.1, §10.1)
        invalidate();
    }

    bool CheckBox::indeterminate() const {
        // the state is what the semantic check state and the render pass both read (§10.1)
        return m_indeterminate;
    }

    void CheckBox::set_on_toggle(std::function<void(bool)> handler) {
        // the handler is swapped in place, because a press after this point must run the new work (§8.3)
        m_on_toggle = std::move(handler);
    }

    void CheckBox::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the box looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so unbinding a command later restores what the form declared (§18.7)
        m_disabled = disabled;
        // a bound command owns the state, so the imperative write is recorded and not applied (§18.7)
        if (command() != nullptr)
            return;
        // a disabled box stops accepting presses, so the frame changes (§6.1)
        invalidate();
    }

    bool CheckBox::disabled() const {
        // a bound command is the single answer, which is what three consumers reading one object means (§18.7)
        if (command() != nullptr)
            return !command()->state().enabled;
        // without a command the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void CheckBox::apply_command_state(const CommandState& state) {
        // a box presents no label and disabled reads the command, so applicability is the only field to write (§18.2)
        UiNode::set_visible(state.visible);
        // the fill repaints through the disabled role, which the state resolver reads (§6.2)
        invalidate();
    }

    void CheckBox::toggle() {
        // a mixed box resolves to checked on the next press, which is what a tri state control does (§10.1)
        if (m_indeterminate) {
            m_indeterminate = false;
            m_checked = true;
        }
        // the state flips before anything runs, so every observer sees the value the box moved to (§8.3)
        else {
            m_checked = !m_checked;
        }
        // a toggled box changes its fill and its glyph, so the frame must be redrawn (§6.1)
        invalidate();
        // a bound command is invoked with the new truth, which is what an action that carries a value receives (§18.3)
        if (command() != nullptr) {
            (void)command()->invoke(CommandInvocation{.command = command()->id(), .argument = m_checked, .selection = {}, .source = InvocationSource::Pointer});
            return;
        }
        // a box with no handler still toggles, because the state is the control (§8.3)
        if (!m_on_toggle)
            return;
        // the handler receives the new state, which is what a toggle reports (§8.3)
        m_on_toggle(m_checked);
    }

    void CheckBox::set_outline(Color outline) {
        // an unchanged colour redraws nothing, because the box looks the same (§6.1)
        if (m_outline == outline)
            return;
        // the colour is stored first, so the invalidation walks a box that already strokes the new colour (§6.1)
        m_outline = outline;
        // an outline change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color CheckBox::outline() const {
        // the outline is what the render pass strokes (§6.1)
        return m_outline;
    }

    void CheckBox::set_check_fill(Color check_fill) {
        // an unchanged colour redraws nothing, because the box looks the same (§6.1)
        if (m_check_fill == check_fill)
            return;
        // the colour is stored first, so the invalidation walks a box that already fills the new colour (§6.1)
        m_check_fill = check_fill;
        // a fill change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }
    float CheckBox::outline_width() const {
        // the width comes from the theme when one is installed (§6.1)
        if (theme() != nullptr)
            return theme()->metrics().checkbox_outline;
        // without a theme the box keeps the literal it always drew with (§6.1)
        return 2.0f;
    }

    const Color* CheckBox::fill_colors() const {
        // without a theme the table is unused, because the resolver returns the box's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().checkbox_fill;
    }

    const Color* CheckBox::outline_colors() const {
        // without a theme the table is unused, because the resolver returns the box's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().checkbox_outline;
    }

    const Color* CheckBox::glyph_colors() const {
        // without a theme the table is unused, because no glyph is drawn at all (§8.4)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the glyph is a role like the outline (§8.4)
        return theme()->palette().checkbox_glyph;
    }
} // namespace albor
