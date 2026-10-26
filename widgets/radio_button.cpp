#include "widgets/radio_button.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "widgets/radio_group.h"

namespace albor
{
    RadioButton::RadioButton(BindingEngine* engine, TextEngine* text_engine, int value, std::string label, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_label(std::move(label)), m_value(value) {
        // the label is measured at construction, because the row needs its width before the first layout (§9.1)
        if (m_text_engine != nullptr)
            m_label_layout = m_text_engine->measure(m_label, TextEngine::UNBOUNDED_WIDTH, label_font_size());
    }

    void RadioButton::measure(const SizeConstraints& constraints, Size& out_size) {
        // the circle and the label sit in one row, so the row is as wide as both of them (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the label is measured at the size the theme hierarchy names, inside the room the row offers (§9.1)
        if (m_text_engine != nullptr) {
            // the circle and the gap sit before the text, so they are taken out of the offered width (§8.2)
            const float gap_hint = m_label.empty() ? 0.0f : metrics.radio_button_gap;
            // an unbounded parent leaves the run at its natural width, while a bounded one wraps inside the row (§8.2)
            const float offered = std::isfinite(constraints.maximum.width) ? std::max(0.0f, constraints.maximum.width - metrics.radio_button_size.width - gap_hint) : TextEngine::UNBOUNDED_WIDTH;
            // the run is measured at the room the parent offered, which keeps a long label inside its row (§9.1)
            m_label_layout = m_text_engine->measure(m_label, offered, label_font_size());
        }
        // the row is the circle, the gap and the run, and as tall as the taller of the two (§8.2)
        const float gap = m_label_layout.empty() ? 0.0f : metrics.radio_button_gap;
        // the row is the circle, the gap and the label, which is the content a choice draws (§8.2)
        Size content{.width = metrics.radio_button_size.width + gap + m_label_layout.size.width, .height = std::max(metrics.radio_button_size.height, m_label_layout.size.height)};
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

    void RadioButton::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    Rect RadioButton::circle_rect() const {
        // the metric decides the circle, which stays round whatever the row is arranged into (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the circle is centred on the row and shrinks into a short row, because it never paints outside it (§8.2)
        const float extent = std::min({bounds().width, bounds().height, metrics.radio_button_size.width, metrics.radio_button_size.height});
        const float y = bounds().y + (bounds().height - extent) * 0.5f;
        return Rect::from_origin_size(Point{.x = bounds().x, .y = y}, Size{.width = extent, .height = extent});
    }

    void RadioButton::paint_content(RenderContext& context) {
        // a row with no area draws nothing, because there is no circle to paint (§6.2)
        if (bounds().empty())
            return;
        // the metrics answer the circle size and the outline, which the theme owns when installed (§8.4)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the circle is the fixed metric, which the outline and the dot both share (§8.2)
        const Rect circle = circle_rect();
        // the chosen circle carries the accent fill, which is what makes the dot read (§6.1)
        if (m_checked)
            context.draw_rounded_rect(circle, circle.height * 0.5f, {.fill = resolve(fill_colors(), Color{0.20f, 0.55f, 0.90f, 1.0f})});
        // the outline is the ring every choice carries, chosen or not (§6.1)
        context.draw_rounded_rect(circle, circle.height * 0.5f, {.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = resolve(outline_colors(), Color{0.85f, 0.87f, 0.90f, 1.0f}), .stroke_width = metrics.radio_button_outline});
        // a chosen circle carries a centred dot, which is the radio mark rather than the checkbox tick (§8.4)
        if (m_checked) {
            // the dot is a quarter of the circle, which leaves a visible ring around it (§8.4)
            const float dot_extent = std::max(2.0f, circle.width * 0.45f);
            const Rect dot = Rect::from_origin_size(Point{.x = circle.x + (circle.width - dot_extent) * 0.5f, .y = circle.y + (circle.height - dot_extent) * 0.5f}, Size{.width = dot_extent, .height = dot_extent});
            // the dot reads over the accent, which the dot role resolves from the button label (§6.1)
            context.draw_rounded_rect(dot, dot_extent * 0.5f, {.fill = dot_color()});
        }
        // a label run draws beside the circle, which is what makes the row readable (§9.1)
        if (!m_label_layout.empty()) {
            // the run starts after the circle and the gap, which the theme owns (§8.2)
            const Point origin{.x = circle.x + circle.width + metrics.radio_button_gap, .y = circle.y + (circle.height - m_label_layout.size.height) * 0.5f};
            // the label is clipped to the row alone, because the circle outline may sit on the edge (§6.2)
            context.save();
            // the clip is the arranged row, which is the only area the label owns (§6.2)
            context.clip_rect(bounds());
            // the label takes the state colour, which a disabled row dims (§6.1)
            context.draw_text(m_label_layout, origin, {.fill = resolve(theme() != nullptr ? theme()->palette().label_foreground : nullptr, Color{0.92f, 0.94f, 0.96f, 1.0f})});
            // the saved clip comes off before the row ends, so a sibling draws at the state this row found (§6.2)
            context.restore();
        }
    }

    bool RadioButton::hit_test(Point logical_point) const {
        // a hidden or disabled choice is not a hit, because it draws nothing and refuses input (§8.3)
        if (!visible() || disabled() || bounds().empty())
            return false;
        // the whole row is the target, because the label is part of the choice (§8.3)
        return logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height;
    }

    void RadioButton::dispatch_event(Event& event) {
        // a hidden choice takes no event at all (§8.3)
        if (!visible())
            return;
        // the transitions run first, so a press is a state before it selects (§8.3)
        handle_hover_transitions(event);
        // a space selects the focused choice, which is the keyboard path of a radio group (§8.3)
        if (event.kind == EventKind::KeyDown && event.key == Key::Space) {
            // a disabled choice or a modified key refuses the activation (§8.3)
            if (disabled() || !event.modifiers.none())
                return;
            // the event is claimed, so no ancestor reacts to the same key (§8.3)
            event.handled = true;
            // the choice is selected, which the group turns into its single selection (§8.3)
            activate();
            return;
        }
        // a press is the only other event that selects a choice (§8.3)
        if (event.kind != EventKind::PointerDown || disabled() || !hit_test(event.position))
            return;
        // the event is claimed, so no ancestor reacts to the same press (§8.3)
        event.handled = true;
        // the choice is selected, which the group turns into its single selection (§8.3)
        activate();
    }

    SemanticNode RadioButton::semantic_node() const {
        // the role is a radio button, because the choice belongs to one single-selection group (§10.1)
        SemanticNode node{SemanticRole::RadioButton};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the label is what a reader speaks, and the identity stands in when the label is empty (§10.1)
        node.set_name(m_label.empty() ? identity().explicit_id : m_label);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled choice reports the disabled state, which refuses activation (§10.1)
        node.set_disabled(disabled());
        // the check state is the chosen state, which a reader announces for a radio button (§10.1)
        node.set_check(m_checked ? CheckState::Checked : CheckState::Unchecked);
        // the selected state mirrors the choice, which the selection item adapter reads (§10.1)
        node.set_selected(m_checked);
        // the activate action runs the same selection a press does (§10.1)
        if (m_on_select)
            (void)node.add_action("activate", [control = mutable_owner(this)] { control->activate(); });
        return node;
    }

    void RadioButton::set_value(int value) {
        // an unchanged value changes nothing, because the group already matches the same choice (§8)
        if (m_value == value)
            return;
        // the value is stored, because the group matches against it on the next selection (§8)
        m_value = value;
        // a value change can change which choice the group owns, so a parent group reapplies its selection (§8)
        if (auto* group = dynamic_cast<RadioGroup*>(parent()); group != nullptr)
            group->apply_selection();
    }

    int RadioButton::value() const {
        // the value is what the group compares its selected value against (§8)
        return m_value;
    }

    void RadioButton::set_label(std::string label) {
        // an unchanged label redraws nothing, because the row looks the same (§6.1)
        if (m_label == label)
            return;
        // the label is stored first, so the invalidation walks a row that already carries it (§8)
        m_label = std::move(label);
        // the run is measured again, because the row width follows the text (§9.1)
        if (m_text_engine != nullptr)
            m_label_layout = m_text_engine->measure(m_label, TextEngine::UNBOUNDED_WIDTH, label_font_size());
        // a wider or narrower label changes the row, so the layout passes run (§8.2)
        invalidate_layout();
        // the label is painted, so the frame is redrawn too (§6.1)
        invalidate();
    }

    const std::string& RadioButton::label() const {
        // the label is what the row draws and the semantics speak (§10.1)
        return m_label;
    }

    void RadioButton::set_checked(bool checked) {
        // an unchanged state redraws nothing, because the circle looks the same (§6.1)
        if (m_checked == checked)
            return;
        // the state is stored first, so the invalidation walks a row that already carries it (§8)
        m_checked = checked;
        // a chosen circle changes its fill and dot, so the frame is redrawn (§6.1)
        invalidate();
    }

    bool RadioButton::checked() const {
        // the state is what the paint pass and the semantic node both read (§8, §10.1)
        return m_checked;
    }

    void RadioButton::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the row looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the redraw walks a choice that already carries it (§6.1)
        m_disabled = disabled;
        // a disabled row dims and refuses input, so the frame is redrawn (§6.1)
        invalidate();
    }

    bool RadioButton::disabled() const {
        // the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void RadioButton::set_on_select(std::function<void()> handler) {
        // the group installs the selection work once, which a press then runs (§8.3)
        m_on_select = std::move(handler);
    }

    void RadioButton::activate() {
        // a choice with no group cannot select itself, which a detached radio is (§8.3)
        if (m_on_select)
            m_on_select();
    }

    float RadioButton::label_font_size() const {
        // the hierarchy answers, because a radio label is body copy (§8.4, §9.1)
        return theme() != nullptr ? theme()->typography().body : 13.0f;
    }

    const Color* RadioButton::outline_colors() const {
        // without a theme the table is unused, because the resolver returns the control's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // a radio shares the checkbox outline, because both draw one state ring (§6.1)
        return theme()->palette().checkbox_outline;
    }

    const Color* RadioButton::fill_colors() const {
        // without a theme the table is unused, because the resolver returns the control's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // a chosen radio shares the checkbox fill, because both carry the accent (§6.1)
        return theme()->palette().checkbox_fill;
    }

    Color RadioButton::dot_color() const {
        // a disabled choice dims the dot, which the label palette answers by state (§6.1)
        if (theme() == nullptr)
            return Color{0.96f, 0.97f, 0.99f, 1.0f};
        // the label palette answers by state, which dims the dot when the choice is disabled (§6.1)
        return theme()->palette().button_label[static_cast<std::size_t>(state())];
    }
} // namespace albor
