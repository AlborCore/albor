#include "widgets/numeric_stepper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace albor
{
    NumericStepper::NumericStepper(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine) {
        // the value text is built first, because the first measure has to know how wide the number is (§9.1)
        update_value_text();
    }

    void NumericStepper::measure(const SizeConstraints& constraints, Size& out_size) {
        // the value is measured at the size the hierarchy names, which is the run the plate holds (§9.1)
        if (m_text_engine != nullptr)
            m_value_layout = m_text_engine->measure(m_value_text, TextEngine::UNBOUNDED_WIDTH, label_font_size());
        // the plate is at least the themed extent and wide enough to hold the value and both step zones (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the text sits between the two zones, so both widths are part of the content (§8.2)
        const float text_width = m_value_layout.size.width + metrics.numeric_stepper_button_width * 2.0f + metrics.numeric_stepper_padding * 2.0f;
        // the content is the wider of the themed extent and the measured run, and the taller of the two (§8.2)
        Size content{.width = std::max(metrics.numeric_stepper_extent.width, text_width), .height = std::max(metrics.numeric_stepper_extent.height, m_value_layout.size.height + metrics.numeric_stepper_padding * 2.0f)};
        // the children are measured too, because a widget is a component (§8.1, §8.2)
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

    void NumericStepper::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void NumericStepper::paint_content(RenderContext& context) {
        // a stepper with no area draws nothing, because there is no plate to paint (§6.2)
        if (bounds().empty())
            return;
        // the plate clips its own content, because a parent may shrink it below the value's natural width (§6.2)
        context.save();
        // the clip is the arranged plate, which is the only area the control owns (§6.2)
        context.clip_rect(bounds());
        // the plate is the whole control, which is what the user presses and reads (§6.1)
        const float radius = theme() != nullptr ? theme()->metrics().corner_radius : 4.0f;
        // a themed radius rounds the plate, which is what makes it read as one control (§8.4)
        if (radius > 0.0f)
            context.draw_rounded_rect(bounds(), radius, {.fill = plate_color()});
        // without a radius the plate is the hard rectangle the control always drew (§6.1)
        else
            context.draw_rect(bounds(), {.fill = plate_color()});
        // the value is centred between the two step zones, which is where a spinner reads (§9.1)
        if (!m_value_layout.empty()) {
            // the middle of the plate is the centre of the run, which is what makes the number readable (§9.1)
            const Point origin{.x = bounds().x + (bounds().width - m_value_layout.size.width) * 0.5f, .y = bounds().y + (bounds().height - m_value_layout.size.height) * 0.5f};
            // the value takes the label colour, which a disabled stepper dims (§6.1)
            context.draw_text(m_value_layout, origin, {.fill = label_color()});
        }
        // the metrics decide how wide a step zone is, which is the same target on both sides (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the mark is a short line, which is what a minus and a plus share (§8.4)
        const float mark_width = std::min(10.0f, metrics.numeric_stepper_button_width * 0.45f);
        // the two marks sit at the centre of their zones, which keeps them away from the value (§8.4)
        const float zone_width = step_zone_width();
        const float left_x = bounds().x + zone_width * 0.5f;
        const float right_x = bounds().x + bounds().width - zone_width * 0.5f;
        // both marks sit on the vertical centre of the plate (§8.4)
        const float centre_y = bounds().y + bounds().height * 0.5f;
        // the minus is one horizontal line, which reads as the decrement (§8.4)
        context.draw_rect(Rect::from_origin_size(Point{.x = left_x - mark_width * 0.5f, .y = centre_y - 1.0f}, Size{.width = mark_width, .height = 2.0f}), {.fill = label_color()});
        // the plus is the same horizontal line plus a vertical one, which reads as the increment (§8.4)
        context.draw_rect(Rect::from_origin_size(Point{.x = right_x - mark_width * 0.5f, .y = centre_y - 1.0f}, Size{.width = mark_width, .height = 2.0f}), {.fill = label_color()});
        context.draw_rect(Rect::from_origin_size(Point{.x = right_x - 1.0f, .y = centre_y - mark_width * 0.5f}, Size{.width = 2.0f, .height = mark_width}), {.fill = label_color()});
        // the saved clip comes off last, so a sibling draws at the state this control found (§6.2)
        context.restore();
    }

    bool NumericStepper::hit_test(Point logical_point) const {
        // a hidden or disabled stepper is not a hit, because it draws nothing and refuses input (§8.3)
        if (!visible() || disabled() || bounds().empty())
            return false;
        // the whole plate is the target, because both step zones sit inside it (§8.3)
        return logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height;
    }

    void NumericStepper::dispatch_event(Event& event) {
        // a hidden stepper takes no event at all (§8.3)
        if (!visible())
            return;
        // the transitions run first, so a press is a state before it steps (§8.3)
        handle_hover_transitions(event);
        // a press on a zone steps once, which is the pointer path of a spinner (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // a disabled stepper or a press outside the plate refuses the step (§8.3)
            if (disabled() || !hit_test(event.position))
                return;
            // the zone decides the direction, and the value sits in the middle (§8.3)
            const int zone = zone_at(event.position);
            // a press on the value itself names no direction, so it is left to a parent (§8.3)
            if (zone == 0)
                return;
            // the event is claimed, so no ancestor reacts to the same press (§8.3)
            event.handled = true;
            // the value moves one step in the zone's direction (§8.3)
            step_by(zone > 0 ? 1.0f : -1.0f);
            return;
        }
        // a wheel notch steps the value, which is what a hovered spinner does (§8.3)
        if (event.kind == EventKind::Wheel) {
            // a disabled stepper refuses the wheel, and a zero notch names no direction (§8.3)
            if (disabled() || event.scroll == 0.0f || !hit_test(event.position))
                return;
            // the event is claimed, so the scroll does not also move a parent viewport (§8.3)
            event.handled = true;
            // a positive notch increments, which is the direction the pointer wheel turns (§8.3)
            step_by(event.scroll > 0.0f ? 1.0f : -1.0f);
            return;
        }
        // a modified key belongs to the application, so the stepper only answers an unmodified key down (§8.3)
        if (event.kind != EventKind::KeyDown || !event.modifiers.none() || disabled())
            return;
        switch (event.key) {
        case Key::ArrowUp:
        case Key::ArrowRight:
            // the event is claimed, so no ancestor steps with the same key (§8.3)
            event.handled = true;
            // the value moves one step forward (§8.3)
            step_by(1.0f);
            return;
        case Key::ArrowDown:
        case Key::ArrowLeft:
            // the event is claimed, so no ancestor steps with the same key (§8.3)
            event.handled = true;
            // the value moves one step back (§8.3)
            step_by(-1.0f);
            return;
        case Key::PageUp:
            // the event is claimed, so no ancestor pages with the same key (§8.3)
            event.handled = true;
            // a page is ten steps, which is the coarse move a bounded value needs (§8.3)
            step_by(10.0f);
            return;
        case Key::PageDown:
            // the event is claimed, so no ancestor pages with the same key (§8.3)
            event.handled = true;
            // a page back is ten steps, which mirrors the forward move (§8.3)
            step_by(-10.0f);
            return;
        case Key::Home:
            // the event is claimed, so no ancestor moves with the same key (§8.3)
            event.handled = true;
            // home names the lower bound (§8.3)
            set_value(m_minimum);
            return;
        case Key::End:
            // the event is claimed, so no ancestor moves with the same key (§8.3)
            event.handled = true;
            // end names the upper bound (§8.3)
            set_value(m_maximum);
            return;
        default:
            return;
        }
    }

    SemanticNode NumericStepper::semantic_node() const {
        // the role is a numeric stepper, because the user steps a bounded number (§10.1)
        SemanticNode node{SemanticRole::NumericStepper};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the identity names the control, which a reader speaks (§10.1)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled stepper reports the disabled state, which refuses activation (§10.1)
        node.set_disabled(disabled());
        // the focus flag is part of the announced state, so an adapter can follow the ring (§10.1)
        node.set_focused(focused());
        // the value is the number the control currently carries, which a reader speaks (§10.1)
        node.set_value(m_value_text);
        // the range is what a platform range pattern exposes, and a write clamps into it (§10.1, §10.2)
        node.set_range(SemanticRange{.value = m_value, .minimum = m_minimum, .maximum = m_maximum, .step = m_step}, [control = mutable_owner(this)](double value) { control->set_value(static_cast<float>(value)); });
        // a stepper is not checkable, because it carries a number rather than a toggle (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the two stepping actions are what automation can drive on the control (§10.1)
        (void)node.add_action("increment", [control = mutable_owner(this)] { control->step_by(1.0f); });
        (void)node.add_action("decrement", [control = mutable_owner(this)] { control->step_by(-1.0f); });
        return node;
    }

    void NumericStepper::set_value(float value) {
        // a non finite binding is ignored, because a NaN would poison every later step and clamp (§8)
        if (!std::isfinite(value))
            return;
        // the request is kept before the clamp, so a later wider range can recover the authored value (§8)
        m_requested_value = value;
        // the value is clamped into the bounds, because a binding may produce a number outside them (§8)
        apply_clamped_value(std::clamp(value, m_minimum, m_maximum));
    }

    float NumericStepper::value() const {
        // the value is what the plate and the semantic node both read (§8)
        return m_value;
    }

    void NumericStepper::set_minimum(float minimum) {
        // a non finite bound is ignored, because the clamp requires an ordered range (§8)
        if (!std::isfinite(minimum))
            return;
        // the lower bound is stored, and the upper bound moves with it when the range would invert (§8)
        const bool changed = m_minimum != minimum || m_maximum < minimum;
        // the lower bound is stored first, because the clamp below reads the new range (§8)
        m_minimum = minimum;
        // an upper bound below the lower one moves with it, because a range has no inverted state (§8)
        if (m_maximum < m_minimum)
            m_maximum = m_minimum;
        // the requested value is clamped into the new range, which can recover an earlier out-of-range request (§8)
        apply_clamped_value(std::clamp(m_requested_value, m_minimum, m_maximum));
        // a changed range repaints the control, even when the value stayed inside it (§6.1)
        if (changed)
            invalidate();
    }

    float NumericStepper::minimum() const {
        // the bound is what the value clamp and the home key read (§8)
        return m_minimum;
    }

    void NumericStepper::set_maximum(float maximum) {
        // a non finite bound is ignored, because the clamp requires an ordered range (§8)
        if (!std::isfinite(maximum))
            return;
        // the upper bound is stored, and the lower bound moves with it when the range would invert (§8)
        const bool changed = m_maximum != maximum || m_minimum > maximum;
        // the upper bound is stored first, because the clamp below reads the new range (§8)
        m_maximum = maximum;
        // a lower bound above the upper one moves with it, because a range has no inverted state (§8)
        if (m_minimum > m_maximum)
            m_minimum = m_maximum;
        // the requested value is clamped into the new range, which can recover an earlier out-of-range request (§8)
        apply_clamped_value(std::clamp(m_requested_value, m_minimum, m_maximum));
        // a changed range repaints the control, even when the value stayed inside it (§6.1)
        if (changed)
            invalidate();
    }

    float NumericStepper::maximum() const {
        // the bound is what the value clamp and the end key read (§8)
        return m_maximum;
    }

    void NumericStepper::set_step(float step) {
        // a non finite step is ignored, because a NaN would make every later move non finite (§8)
        if (!std::isfinite(step))
            return;
        // the step is the written magnitude, which keeps a negative value from reversing the buttons (§8)
        m_step = std::max(std::fabs(step), 0.0001f);
    }

    float NumericStepper::step() const {
        // the step is what a press, a wheel and a key all move by (§8)
        return m_step;
    }

    void NumericStepper::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the plate looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the redraw walks a control that already carries it (§8)
        m_disabled = disabled;
        // a disabled plate dims and refuses input, so the frame is redrawn (§6.1)
        invalidate();
    }

    bool NumericStepper::disabled() const {
        // the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void NumericStepper::set_on_change(std::function<void(float)> handler) {
        // the work is stored, which the next actual value move runs (§8.3)
        m_on_change = std::move(handler);
    }

    void NumericStepper::step_by(float steps) {
        // the value moves by the step amount, and the setter clamps and reports it (§8)
        set_value(m_value + steps * m_step);
    }

    void NumericStepper::apply_clamped_value(float value) {
        // an unchanged value redraws nothing, because the plate looks the same (§6.1)
        if (m_value == value)
            return;
        // the value is stored first, so the invalidation walks a control that already carries it (§8)
        m_value = value;
        // the formatted run changes with the value, which is what the plate draws (§9.1)
        update_value_text();
        // the run width may change, so the layout passes run before the paint (§8.2)
        invalidate_layout();
        // the plate shows the new number, so the frame is redrawn too (§6.1)
        invalidate();
        // the installed work is told, which is what the value signal reports (§8.3)
        if (m_on_change)
            m_on_change(m_value);
    }

    void NumericStepper::update_value_text() {
        // the formatted run is what the plate and the semantic node share (§9.1, §10.1)
        m_value_text = format_value();
        // the run is measured again, because the plate width follows it (§9.1)
        if (m_text_engine != nullptr)
            m_value_layout = m_text_engine->measure(m_value_text, TextEngine::UNBOUNDED_WIDTH, label_font_size());
    }

    std::string NumericStepper::format_value() const {
        // a value that rounds to zero at six decimals is written as zero, so a tiny negative never reads as -0 (§9.1)
        if (m_value > -0.0000005f && m_value < 0.0000005f)
            return "0";
        // the buffer holds the fixed precision form, which is then shortened to the shortest correct run (§9.1)
        char buffer[64] = {};
        // six decimals keep a fine step exact enough, and the trim below removes trailing zeros (§9.1)
        std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(m_value));
        // the text owns the buffer, because the trim rebuilds it (§9.1)
        std::string text(buffer);
        // a run with a decimal point loses trailing zeros, which makes an integer read as an integer (§9.1)
        while (text.size() > 1 && text.back() == '0')
            text.pop_back();
        // a remaining decimal point is removed too, because no fractional part is left (§9.1)
        if (!text.empty() && text.back() == '.')
            text.pop_back();
        // the shortened run is what the plate draws (§9.1)
        return text;
    }

    float NumericStepper::label_font_size() const {
        // the hierarchy answers, because a value is body copy (§8.4, §9.1)
        return theme() != nullptr ? theme()->typography().body : 13.0f;
    }

    int NumericStepper::zone_at(Point logical_point) const {
        // the zone shrinks with the plate, so the two sides never overlap on a narrow control (§8.2)
        const float zone_width = step_zone_width();
        // a point in the left zone names the decrement (§8.3)
        if (logical_point.x >= bounds().x && logical_point.x < bounds().x + zone_width)
            return -1;
        // a point in the right zone names the increment (§8.3)
        if (logical_point.x >= bounds().x + bounds().width - zone_width && logical_point.x < bounds().x + bounds().width)
            return 1;
        // every other point is the value area, which names no direction (§8.3)
        return 0;
    }

    float NumericStepper::step_zone_width() const {
        // the metric names the ideal zone, and half the plate is the most two zones may take together (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the smaller of the two keeps the decrement and increment targets disjoint (§8.2)
        return std::min(metrics.numeric_stepper_button_width, bounds().width * 0.5f);
    }

    Color NumericStepper::plate_color() const {
        // an unthemed stepper carries the accent the framework uses for a plate (§6.1)
        if (theme() == nullptr)
            return Color{0.20f, 0.55f, 0.90f, 1.0f};
        // the plate palette answers by state, which dims a disabled stepper (§6.1)
        return theme()->palette().button_plate[static_cast<std::size_t>(state())];
    }

    Color NumericStepper::label_color() const {
        // an unthemed stepper keeps white text, which reads over its accent plate (§6.1)
        if (theme() == nullptr)
            return Color{0.96f, 0.97f, 0.99f, 1.0f};
        // the label palette answers by state, which dims a disabled stepper (§6.1)
        return theme()->palette().button_label[static_cast<std::size_t>(state())];
    }
} // namespace albor
