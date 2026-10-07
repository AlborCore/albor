#include "widgets/slider.h"

#include <algorithm>

namespace albor
{
    // the width a slider measures to, because a slider is horizontal and needs room to drag (§8.2)
    constexpr float SLIDER_WIDTH = 120.0f;

    // the height a slider measures to, because a track is thin (§8.2)
    constexpr float SLIDER_HEIGHT = 16.0f;

    // the thickness of the track, which is drawn centred on the control (§6.1)
    constexpr float TRACK_THICKNESS = 4.0f;

    // the extent of the thumb along the track, which is what the user grabs (§6.1)
    constexpr float THUMB_SIZE = 12.0f;

    // the amount one key press moves the value, because a semantic action carries no exact target (§10.1)
    constexpr float STEP = 0.05f;

    Slider::Slider(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void Slider::measure(const SizeConstraints& constraints, Size& out_size) {
        // a slider has no content, so its extent comes from the control metric (§8.2)
        const Size content{.width = SLIDER_WIDTH, .height = SLIDER_HEIGHT};
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Slider::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the preferred size follows the arranged one, because the parent has now decided (§8.2)
        m_preferred_size = m_measured_size;
    }

    void Slider::render(RenderContext& context) {
        // a slider with no area draws nothing, because there is no track to draw (§6.2)
        if (bounds().empty())
            return;
        // the track is a thin band centred vertically, because a slider reads as a horizontal line (§6.1)
        const Rect track = Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y + (bounds().height - TRACK_THICKNESS) * 0.5f}, Size{.width = bounds().width, .height = TRACK_THICKNESS});
        // the track is filled, because the whole control must be visible before it is touched (§6.1)
        context.draw_rect(track, {.fill = m_track_color});
        // the travel of the thumb is the track minus the thumb, so it never overhangs either end (§6.1)
        const float travel = std::max(0.0f, bounds().width - THUMB_SIZE);
        // a track with no travel has no reachable position, so the thumb is left off (§6.2)
        if (travel <= 0.0f)
            return;
        // the fraction of the range the value sits at, which is what places the thumb (§8)
        const float fraction = (m_value - m_minimum) / (m_maximum - m_minimum);
        // the thumb is a square centred on the track at the value position (§6.1)
        const Rect thumb = Rect::from_origin_size(Point{.x = bounds().x + travel * fraction, .y = bounds().y + (bounds().height - THUMB_SIZE) * 0.5f}, Size{.width = THUMB_SIZE, .height = THUMB_SIZE});
        // the thumb is filled, because it is what the user grabs (§6.1)
        context.draw_rect(thumb, {.fill = m_thumb_color});
    }

    bool Slider::hit_test(Point logical_point) const {
        // a hidden slider is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled slider is not a hit, because it refuses input (§8.3)
        if (m_disabled)
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
        // a key that steers the value reaches the focused slider (§8.3)
        if (event.kind == EventKind::KeyDown && (event.key == Key::ArrowRight || event.key == Key::ArrowUp || event.key == Key::ArrowLeft || event.key == Key::ArrowDown)) {
            // a disabled slider does not claim the key, because it refuses input (§8.3)
            if (m_disabled)
                return;
            // a modified key is an application shortcut, so a plain step ignores it (§8.3)
            if (!event.modifiers.none())
                return;
            // the right and up arrows move the value up, which is the forward direction (§8)
            const float delta = (event.key == Key::ArrowRight || event.key == Key::ArrowUp) ? STEP : -STEP;
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
        if (m_disabled)
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
        // a track with no length has no reachable position, so the value stays where it was (§8)
        const float travel = bounds().width - THUMB_SIZE;
        if (travel <= 0.0f)
            return;
        // the press offset from the left edge becomes the fraction of the travel (§8)
        const float fraction = std::clamp((logical_position.x - bounds().x - THUMB_SIZE * 0.5f) / travel, 0.0f, 1.0f);
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
        // the accessible name is the identity the author assigned, because a slider carries no text of its own (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled slider reports the disabled state, which is what refuses adjustment (§10.1)
        node.set_disabled(m_disabled);
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
        if (m_disabled)
            return;
        // the value moves by the signed amount, which set_value clamps into the range (§8)
        const float before = m_value;
        // the value is clamped rather than wrapped, because a slider cannot report outside its range (§8)
        set_value(m_value + delta);
        // an unchanged value reports nothing, because a step past the end changed nothing (§8.3)
        if (m_value == before)
            return;
        // the handler receives the new value, which is what a change reports (§8.3)
        if (m_on_change)
            m_on_change(m_value);
    }

    void Slider::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the slider looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the invalidation walks a slider that already refuses input (§8.3)
        m_disabled = disabled;
        // a disabled slider stops accepting presses, so the frame changes (§6.1)
        invalidate();
    }

    bool Slider::disabled() const {
        // the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
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
} // namespace albor
