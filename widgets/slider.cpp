#include "widgets/slider.h"

#include <algorithm>
#include <cmath>

#include "commands/command.h"

namespace albor
{
    // the amount one key press moves the value, because a semantic action carries no exact target (§10.1)
    constexpr float STEP = 0.05f;

    Slider::Slider(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void Slider::measure(const SizeConstraints& constraints, Size& out_size) {
        // a slider has no content, so its extent comes from the themed control metric (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the control measures to the themed extent (§8.2)
        Size content{.width = metrics.slider_extent.width, .height = metrics.slider_extent.height};
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

    void Slider::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void Slider::paint_content(RenderContext& context) {
        // a slider with no area draws nothing, because there is no track to draw (§6.2)
        if (bounds().empty())
            return;
        // the track metrics come from the theme when one is installed (§6.1)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the thickness is what the theme says, or the value the slider always drew with (§6.1)
        const float thickness = theme() != nullptr ? metrics.slider_track_thickness : 4.0f;
        // the thumb extent is what the theme says, or the value the slider always drew with (§6.1)
        const float thumb_extent = theme() != nullptr ? metrics.slider_thumb : 12.0f;
        // the track is a thin band centred vertically, because a slider reads as a horizontal line (§6.1)
        const Rect track = Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y + (bounds().height - thickness) * 0.5f}, Size{.width = bounds().width, .height = thickness});
        // a themed track carries a radius, which is what rounds it into a capsule (§8.4)
        const float track_radius = theme() != nullptr ? metrics.slider_track_radius : 0.0f;
        // the track colour resolves by state, so a hover and a press differ without a second literal (§6.1)
        const Paint track_paint{.fill = resolve(track_colors(), m_track_color)};
        // a themed radius means a capsule, which is the shape a native track is (§8.4)
        if (track_radius > 0.0f)
            context.draw_rounded_rect(track, track_radius, track_paint);
        // without a themed radius the track is the hard band the slider always drew (§6.1)
        else
            context.draw_rect(track, track_paint);
        // the travel of the thumb is the track minus the thumb, so it never overhangs either end (§6.1)
        const float travel = std::max(0.0f, bounds().width - thumb_extent);
        // a track with no travel has no reachable position, so the thumb is left off (§6.2)
        if (travel <= 0.0f)
            return;
        // the fraction of the range the value sits at, which is what places the thumb (§8)
        const float fraction = (m_value - m_minimum) / (m_maximum - m_minimum);
        // the filled portion runs from the left edge up to the thumb centre, which is what a filled track reads as (§8.4)
        const float filled_width = thumb_extent * 0.5f + travel * fraction;
        // the fill resolves by state like the track, where a transparent entry draws nothing (§8.4)
        const Paint fill_paint{.fill = resolve(fill_colors(), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f))};
        // a themed fill draws from the minimum to the thumb, which is where the accent ends (§8.4)
        if (fill_paint.fills() && filled_width > 0.0f) {
            // the fill is a capsule that shares the track's radius, so the two endpoints read as one line (§8.4)
            const Rect filled = Rect::from_origin_size(Point{.x = track.x, .y = track.y}, Size{.width = filled_width, .height = track.height});
            context.draw_rounded_rect(filled, track_radius, fill_paint);
        }
        // the discrete ticks draw when the theme asked for them, which is what a stepped control names (§8.4)
        paint_ticks(context, metrics, track, travel, thumb_extent);
        // the rect the thumb occupies is shared with the ring, because both read the same place (§8.4)
        const Rect thumb = thumb_rect(metrics);
        // a themed thumb lifts on a shadow, which is what elevation means (§8.4)
        if (theme() != nullptr && metrics.slider_thumb_shadow > 0.0f) {
            // the shadow falls straight down and lifts with the themed strength (§8.4)
            context.draw_shadow(thumb, metrics.slider_thumb_radius, Shadow{.color = Color::rgba(0.0f, 0.0f, 0.0f, metrics.slider_thumb_shadow), .offset = Point{0.0f, 2.0f}, .blur = 2.0f});
        }
        // the thumb colour resolves by state, so it is what the user grabs in every state (§6.1)
        const Paint thumb_paint{.fill = resolve(thumb_colors(), m_thumb_color)};
        // a themed thumb carries a radius, which a half extent turns into a circle (§8.4)
        const float thumb_radius = theme() != nullptr ? metrics.slider_thumb_radius : 0.0f;
        // a themed radius means a round thumb, which is what a native knob is (§8.4)
        if (thumb_radius > 0.0f)
            context.draw_rounded_rect(thumb, thumb_radius, thumb_paint);
        // without a themed radius the thumb is the square the slider always drew (§6.1)
        else
            context.draw_rect(thumb, thumb_paint);
    }

    void Slider::paint_ticks(RenderContext& context, const ControlMetrics& metrics, const Rect& track, float travel, float thumb_extent) const {
        // a slider without a themed tick length draws none, because the token is the decision (§8.4)
        if (theme() == nullptr || metrics.slider_tick_length <= 0.0f)
            return;
        // the tick colour resolves with the track, which is what a groove sits under (§8.4)
        const Color tick = resolve(track_colors(), m_track_color);
        // the number of reachable steps is what the range and the step settle for (§8.4)
        const float step_count = std::floor((m_maximum - m_minimum) / std::max(metrics.slider_step, 1.0e-6f));
        // a step count below one has no tick to place, because the track holds a single position (§8.4)
        if (step_count < 1.0f)
            return;
        // the ticks sit just under the track, which is where a stepped control names them (§8.4)
        for (std::int32_t index = 0; index <= static_cast<std::int32_t>(step_count); ++index) {
            // the fraction of the range this tick names, which is where the thumb centre would sit (§8.4)
            const float fraction = static_cast<float>(index) / step_count;
            // the tick is centred on the thumb position, which is what makes the answer exact (§8.4)
            const Rect tick_mark = Rect::from_origin_size(Point{.x = bounds().x + thumb_extent * 0.5f + travel * fraction - metrics.slider_tick_width * 0.5f, .y = track.y + track.height + 1.0f}, Size{.width = metrics.slider_tick_width, .height = metrics.slider_tick_length});
            // the tick is a small round stroke, which is what a marker reads as (§8.4)
            context.draw_rounded_rect(tick_mark, metrics.slider_tick_width * 0.5f, {.fill = tick});
        }
    }

    Rect Slider::thumb_rect(const ControlMetrics& metrics) const {
        // the thumb extent is what the theme says, or the value the slider always drew with (§6.1)
        const float thumb_extent = theme() != nullptr ? metrics.slider_thumb : 12.0f;
        // the travel of the thumb is the track minus the thumb, so it never overhangs either end (§6.1)
        const float travel = std::max(0.0f, bounds().width - thumb_extent);
        // the fraction of the range the value sits at, which is what places the thumb (§8)
        const float fraction = (m_value - m_minimum) / (m_maximum - m_minimum);
        // the thumb is a square centred on the track at the value position (§6.1)
        return Rect::from_origin_size(Point{.x = bounds().x + travel * fraction, .y = bounds().y + (bounds().height - thumb_extent) * 0.5f}, Size{.width = thumb_extent, .height = thumb_extent});
    }

    Rect Slider::focus_ring_rect() const {
        // without a theme no ring draws, so the answered rect is never read (§8.4)
        if (theme() == nullptr)
            return bounds();
        // the ring surrounds the thumb, which is the part the user steers (§8.4)
        return thumb_rect(theme()->metrics());
    }
    bool Slider::hit_test(Point logical_point) const {
        // a hidden slider is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled slider is not a hit, because it refuses input (§8.3)
        if (disabled())
            return false;
        // the press must land inside the arranged bounds, with the far edge excluded so neighbours stay separate (§8.3)
        const bool inside = logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height;
        // a press outside the track falls through to whatever is behind the slider (§8.3)
        return inside && !bounds().empty();
    }

    void Slider::dispatch_event(Event& event) {
        // a hidden slider takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run first, so a press is a state before it is a drag (§8.3)
        handle_hover_transitions(event);
        // a key that steers the value reaches the focused slider (§8.3)
        if (event.kind == EventKind::KeyDown && (event.key == Key::ArrowRight || event.key == Key::ArrowUp || event.key == Key::ArrowLeft || event.key == Key::ArrowDown)) {
            // a disabled slider does not claim the key, because it refuses input (§8.3)
            if (disabled())
                return;
            // a modified key is an application shortcut, so a plain step ignores it (§8.3)
            if (!event.modifiers.none())
                return;
            // the increment is what the theme says, or the value the slider always moved by (§10.1)
            const float increment = theme() != nullptr ? theme()->metrics().slider_step : 0.05f;
            // the key decides the direction, and a left or down key is the negative one (§8.3)
            const float delta = (event.key == Key::ArrowRight || event.key == Key::ArrowUp) ? increment : -increment;
            // the event is claimed, so no ancestor reacts to the same key (§8.3)
            event.handled = true;
            // the value moves by one step, which step reports when it changed (§8.3)
            step(delta);
            return;
        }
        // a move that continues a drag steers the value, which is what dragging means (§8.3)
        if (event.kind == EventKind::PointerMove && m_dragging) {
            // the event is claimed, so no ancestor reacts to the same move (§8.3)
            event.handled = true;
            // the thumb follows the pointer, which is how the value is steered (§8)
            jump_to(event.position);
            return;
        }
        // a release ends a drag, which leaves the value where the last move put it (§8.3)
        if (event.kind == EventKind::PointerUp && m_dragging) {
            // the event is claimed, so no ancestor reacts to the same release (§8.3)
            event.handled = true;
            // the drag ends with the release, so the next press starts a fresh one (§8.3)
            m_dragging = false;
            return;
        }
        // a press is the only other event that moves the value, because a drag arrives as press then move (§8.3)
        if (event.kind != EventKind::PointerDown) {
            // any other event belongs to a parent, so the slider does not claim it (§8.3)
            return;
        }
        // a disabled slider does not claim a press, because it refuses input (§8.3)
        if (disabled())
            return;
        // a press outside the track belongs to whatever is behind the slider (§8.3)
        if (!hit_test(event.position))
            return;
        // the event is claimed, so no ancestor reacts to the same press (§8.3)
        event.handled = true;
        // a press starts a drag, which the moves continue and the release ends (§8.3)
        m_dragging = true;
        // the thumb jumps to the press, which is how a press on a track is answered (§8)
        jump_to(event.position);
    }

    void Slider::jump_to(Point logical_position) {
        // the thumb extent is what the theme says, or the value the slider always moved by (§6.1)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        const float thumb_extent = theme() != nullptr ? metrics.slider_thumb : 12.0f;
        // a track with no length has no reachable position, so the value stays where it was (§8)
        const float travel = bounds().width - thumb_extent;
        if (travel <= 0.0f)
            return;
        // the press offset from the left edge becomes the fraction of the travel (§8)
        const float fraction = std::clamp((logical_position.x - bounds().x - thumb_extent * 0.5f) / travel, 0.0f, 1.0f);
        // the value is derived from the fraction, so the thumb lands where the pointer is (§8)
        const float moved = m_minimum + (m_maximum - m_minimum) * fraction;
        // an unchanged value changes nothing, so a position on the current thumb is not reported (§8.3)
        if (moved == m_value)
            return;
        // the value is stored first, so the invalidation walks a slider that already carries the new value (§8)
        m_value = moved;
        // a moved thumb changes the frame, so the node asks for a redraw (§6.1)
        invalidate();
        // a slider with no handler still moves, because the value is the control (§8.3)
        if (!m_on_change)
            return;
        // the handler receives the new value, which is what a change reports (§8.3)
        m_on_change(m_value);
    }

    SemanticNode Slider::semantic_node() const {
        // the role is a slider, because the user drags a continuous value (§10.1)
        SemanticNode node{SemanticRole::Slider};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the accessible name is the identity the author assigned, because a slider carries no text of its own (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled slider reports the disabled state, which is what refuses adjustment (§10.1)
        node.set_disabled(disabled());
        // a slider is not checkable, so the check state stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the name carries the value, because the numeric value is what a screen reader must speak (§10.1)
        node.set_name(identity().explicit_id + " " + std::to_string(m_value));
        // the step actions move by one step because a semantic action carries no value, and their identifiers are discarded (§10.1) the action bodies carry no disabled guard, because SemanticNode::invoke refuses every action while the node it belongs to is disabled, which is where that rule belongs (§10.1)
        (void)node.add_action("increment", [control = mutable_owner(this)] {
            // the value moves up by one step, which is what a key press on a focused slider does (§8.3)
            control->step(STEP);
        });
        // the matching step action moves down, which completes the pair assistive technology expects (§10.1)
        (void)node.add_action("decrement", [control = mutable_owner(this)] {
            // the value moves down by one step, which is the other direction a key press takes (§8.3)
            control->step(-STEP);
        });
        return node;
    }

    void Slider::set_value(float value) {
        // the value is clamped into the range, because a slider cannot report outside it (§8)
        const float clamped = std::clamp(value, m_minimum, m_maximum);
        // an unchanged value redraws nothing, because the slider looks the same (§6.1)
        if (m_value == clamped)
            return;
        // the value is stored first, so the invalidation walks a slider that already carries it (§8)
        m_value = clamped;
        // a moved thumb changes the frame, so the node asks for a redraw (§6.1)
        invalidate();
    }

    float Slider::value() const {
        // the value is what the render pass and the semantic node both read (§8)
        return m_value;
    }

    void Slider::set_range(float min, float max) {
        // a range that does not ascend has no valid positions, so such a range is refused rather than clamped (§8)
        if (!(max > min))
            return;
        // an unchanged range leaves the value alone, because nothing about the control changed (§8)
        if (m_minimum == min && m_maximum == max)
            return;
        // the range is stored first, so the invalidation walks a slider that already knows its new bounds (§8)
        m_minimum = min;
        m_maximum = max;
        // the value is re-clamped, because a new range can exclude the one the slider had (§8)
        set_value(m_value);
        // a range change moves the thumb, so the frame must be redrawn (§6.1)
        invalidate();
    }

    float Slider::minimum() const {
        // the bottom of the range is what the value is clamped up to (§8)
        return m_minimum;
    }

    float Slider::maximum() const {
        // the top of the range is what the value is clamped down to (§8)
        return m_maximum;
    }

    void Slider::set_on_change(std::function<void(float)> handler) {
        // the handler is swapped in place, because a press after this point must run the new work (§8.3)
        m_on_change = std::move(handler);
    }

    void Slider::step(float delta) {
        // a disabled slider refuses a step, because it refuses input (§8.3)
        if (disabled())
            return;
        // the value moves by the signed amount, which set_value clamps into the range (§8)
        const float before = m_value;
        // the value is clamped rather than wrapped, because a slider cannot report outside its range (§8)
        set_value(m_value + delta);
        // an unchanged value reports nothing, because a step past the end changed nothing (§8.3)
        if (m_value == before)
            return;
        // a bound command is invoked with the new value, which is what an action that carries a value receives (§18.3)
        if (command() != nullptr) {
            (void)command()->invoke(CommandInvocation{.command = command()->id(), .argument = static_cast<double>(m_value), .selection = {}, .source = InvocationSource::Pointer});
            return;
        }
        // the handler receives the new value, which is what a change reports (§8.3)
        if (m_on_change)
            m_on_change(m_value);
    }

    void Slider::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the slider looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so unbinding a command later restores what the form declared (§18.7)
        m_disabled = disabled;
        // a bound command owns the state, so the imperative write is recorded and not applied (§18.7)
        if (command() != nullptr)
            return;
        // a disabled slider stops accepting presses, so the frame changes (§6.1)
        invalidate();
    }

    bool Slider::disabled() const {
        // a bound command is the single answer, which is what three consumers reading one object means (§18.7)
        if (command() != nullptr)
            return !command()->state().enabled;
        // without a command the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void Slider::apply_command_state(const CommandState& state) {
        // a slider presents no label and disabled reads the command, so applicability is the only field to write (§18.2)
        UiNode::set_visible(state.visible);
        // the track and the thumb repaint through the disabled role, which the state resolver reads (§6.2)
        invalidate();
    }

    void Slider::set_track_color(Color color) {
        // an unchanged colour redraws nothing, because the slider looks the same (§6.1)
        if (m_track_color == color)
            return;
        // the colour is stored first, so the invalidation walks a slider that already fills the new track (§6.1)
        m_track_color = color;
        // a track colour change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    void Slider::set_thumb_color(Color color) {
        // an unchanged colour redraws nothing, because the slider looks the same (§6.1)
        if (m_thumb_color == color)
            return;
        // the colour is stored first, so the invalidation walks a slider that already fills the new thumb (§6.1)
        m_thumb_color = color;
        // a thumb colour change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }
    const Color* Slider::track_colors() const {
        // without a theme the table is unused, because the resolver returns the slider's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().slider_track;
    }

    const Color* Slider::thumb_colors() const {
        // without a theme the table is unused, because the resolver returns the slider's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().slider_thumb;
    }

    const Color* Slider::fill_colors() const {
        // without a theme the table is unused, because no fill is drawn at all (§8.4)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the accent fills the covered track (§8.4)
        return theme()->palette().slider_fill;
    }
} // namespace albor
