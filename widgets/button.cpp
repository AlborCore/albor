#include "widgets/button.h"

#include <algorithm>
#include <utility>

#include "text/text_engine.h"

namespace albor
{
    Button::Button(BindingEngine* engine, TextEngine* text_engine, std::string label, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_label(std::move(label)) {
        // a button with no engine cannot measure, so the label stays empty and the plate falls back to the minimum extent (§9.1, AD-14)
        if (m_text_engine == nullptr)
            return;
        // the label is measured once up front, so the plate is sized before the first layout pass reaches it (§9.1, AD-14)
        m_label_layout = m_text_engine->measure(m_label, 0.0f, TextEngine::DEFAULT_FONT_SIZE);
    }

    void Button::measure(const SizeConstraints& constraints, Size& out_size) {
        // a button with no engine keeps whatever it measured, because it cannot measure at all (§9.1, AD-14)
        if (m_text_engine != nullptr) {
            // the offered width bounds the run, because a narrow parent wraps the plate's label (§8.2, §9.1)
            const float offered_width = constraints.maximum.width > 0.0f ? constraints.maximum.width : 0.0f;
            // the run is measured at the default button size, because the plate carries no separate size knob (§9.1)
            m_label_layout = m_text_engine->measure(m_label, offered_width, TextEngine::DEFAULT_FONT_SIZE);
        }
        // the minimum comes from the theme when one is installed, so a sizing decision is configured in one place (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // a push control must stay grabbable, so the label drives the plate and a default covers an empty label (§8.2)
        Size content{.width = std::max(m_label_layout.size.width, metrics.button_minimum.width), .height = std::max(m_label_layout.size.height, metrics.button_minimum.height)};
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

    void Button::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void Button::paint_content(RenderContext& context) {
        // an empty label draws no run, because a button may carry no text at all (§6.2)
        if (m_label_layout.empty())
            return;
        // the label colour resolves by state, so a hover and a press read differently without a second literal (§6.1)
        const Color label_color = resolve(label_colors(), Color{0.96f, 0.97f, 0.99f, 1.0f});
        // the run is centred on the plate as a box, where every line places its baseline (§6.1, §9.1)
        const Point origin{.x = bounds().x + (bounds().width - m_label_layout.size.width) * 0.5f, .y = bounds().y + (bounds().height - m_label_layout.size.height) * 0.5f};
        // the run is recorded over the plate, which is the last thing the frame draws for this button (§6.1)
        context.draw_text(m_label_layout, origin, {.fill = label_color});
    }

    bool Button::hit_test(Point logical_point) const {
        // a hidden button is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled button is not a hit, because it refuses input (§8.3)
        if (m_disabled)
            return false;
        // bounds
        const Rect& b = bounds();
        // the press must land inside the arranged bounds, with the far edge excluded so neighbours stay separate (§8.3)
        const bool inside = logical_point.x >= b.x && logical_point.x < b.x + b.width && logical_point.y >= b.y && logical_point.y < b.y + b.height;
        // a press outside the plate falls through to whatever is behind the button (§8.3)
        return inside && !b.empty();
    }

    void Button::dispatch_event(Event& event) {
        // a hidden button takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run first, so a press is a state before it is an activation (§8.3)
        handle_hover_transitions(event);
        // a key that activates the button is a press, which the focused button receives (§8.3)
        if (event.kind == EventKind::KeyDown && (event.key == Key::Return || event.key == Key::Space)) {
            // a disabled button does not claim the key, because it refuses input (§8.3)
            if (m_disabled)
                return;
            // a modified key is an application shortcut, so a plain activation ignores it (§8.3)
            if (!event.modifiers.none())
                return;
            // the event is claimed, so no ancestor reacts to the same key (§8.3)
            event.handled = true;
            // a button with no handler still claims the key, because it swallowed it (§8.3)
            if (!m_on_click)
                return;
            // the handler runs once, which is what an activation means (§8.3)
            m_on_click();
            return;
        }
        // a press is the only other event that activates a button (§8.3)
        if (event.kind != EventKind::PointerDown) {
            // any other event belongs to a parent, so the button does not claim it (§8.3)
            return;
        }
        // a disabled button does not claim a press, because it refuses input (§8.3)
        if (m_disabled)
            return;
        // a press outside the plate belongs to whatever is behind the button (§8.3)
        if (!hit_test(event.position))
            return;
        // the event is claimed, so no ancestor reacts to the same press (§8.3)
        event.handled = true;
        // a button with no handler still claims the press, because it swallowed it (§8.3)
        if (!m_on_click)
            return;
        // the handler runs once, which is what a press means (§8.3)
        m_on_click();
    }

    SemanticNode Button::semantic_node() const {
        // the role is a button, because a press performs an action (§10.1)
        SemanticNode node{SemanticRole::Button};
        // the accessible name is the label, because that is what a screen reader must speak (§10.1)
        node.set_name(m_label);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled button reports the disabled state, which is what refuses activation (§10.1)
        node.set_disabled(m_disabled);
        // a button is not checkable, so the check state stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the activate action runs the same handler a press does, and its identifier is discarded because a widget names actions by label (§10.1) the action body carries no disabled guard, because SemanticNode::invoke refuses every action while the node it belongs to is disabled, which is where that rule belongs (§10.1)
        (void)node.add_action("activate", [this] {
            // a button with no handler exposes the action but does nothing, because there is no work (§10.1)
            if (!m_on_click)
                return;
            // the handler runs, which is what invoking an action means (§10.1)
            m_on_click();
        });
        return node;
    }

    void Button::set_label(std::string label) {
        // an unchanged label redraws nothing, because the button looks the same (§6.1)
        if (m_label == label)
            return;
        // the label is stored first, so the invalidation walks a button that already carries the new text (§6.1)
        m_label = std::move(label);
        // a button with no engine has nothing to remeasure, because it cannot measure at all (§9.1, AD-14)
        if (m_text_engine != nullptr)
            // the run is measured eagerly, because the plate is as wide as the text it carries (§9.1, AD-14)
            m_label_layout = m_text_engine->measure(m_label, 0.0f, TextEngine::DEFAULT_FONT_SIZE);
        // a new label resizes the plate, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    const std::string& Button::label() const {
        // the label is what the semantic node announces as the name (§10.1)
        return m_label;
    }

    void Button::set_on_click(std::function<void()> handler) {
        // the handler is swapped in place, because a press after this point must run the new work (§8.3)
        m_on_click = std::move(handler);
    }

    void Button::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the button looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the invalidation walks a button that already refuses input (§8.3)
        m_disabled = disabled;
        // a disabled button fades and stops accepting presses, so the frame changes (§6.1)
        invalidate();
    }

    bool Button::disabled() const {
        // the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void Button::set_background(Color background) {
        // an unchanged colour redraws nothing, because the button looks the same (§6.1)
        if (m_background == background)
            return;
        // the colour is stored first, so the invalidation walks a button that already paints the new plate (§6.1)
        m_background = background;
        // a plate colour change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color Button::background() const {
        // a themed button resolves by state, so a hover and a press differ without a second literal (§6.1)
        if (theme() != nullptr)
            return resolve(plate_colors(), m_background);
        // an unthemed button keeps the inline fade, because it is the only visual it had (§6.1)
        if (m_disabled)
            return Color::rgba(m_background.red * 0.5f, m_background.green * 0.5f, m_background.blue * 0.5f, m_background.alpha);
        // the plate fill is what the base records before the label (§6.2)
        return m_background;
    }

    const Color* Button::plate_colors() const {
        // without a theme the table is unused, because the resolver returns the widget's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().button_plate;
    }

    const Color* Button::label_colors() const {
        // without a theme the table is unused, because the resolver returns the widget's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().button_label;
    }

} // namespace albor
