#include "widgets/button.h"

#include <algorithm>
#include <utility>

#include "commands/command.h"
#include "render/image_resource.h"
#include "text/text_engine.h"

namespace albor
{
    Button::Button(BindingEngine* engine, TextEngine* text_engine, std::string label, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_label(std::move(label)) {
        // a button with no engine cannot measure, so the plate stays at its minimum extent (§9.1, AD-14)
        if (m_text_engine == nullptr)
            return;
        // the label is measured once up front, so the plate is sized before the first layout pass reaches it (§9.1, AD-14)
        m_label_layout = m_text_engine->measure(m_label, 0.0f, label_font_size());
    }

    float Button::label_font_size() const {
        // without a theme the label takes the size every other call site takes (§9.1)
        if (theme() == nullptr)
            return TextEngine::DEFAULT_FONT_SIZE;
        // the theme owns the hierarchy, so the label is set at the body size the design language decided (§8.4)
        return theme()->typography().body;
    }

    void Button::measure(const SizeConstraints& constraints, Size& out_size) {
        // a button with no engine keeps whatever it measured, because it cannot measure at all (§9.1, AD-14)
        if (m_text_engine != nullptr) {
            // the offered width bounds the run, because a narrow parent wraps the plate's label (§8.2, §9.1)
            const float offered_width = constraints.maximum.width > 0.0f ? constraints.maximum.width : 0.0f;
            // the run is measured at the hierarchy size the button carries (§9.1)
            m_label_layout = m_text_engine->measure(m_label, offered_width, label_font_size());
        }
        // the minimum comes from the theme when one is installed, so a sizing decision is configured in one place (§8.2)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // an icon sits in its own square, so the label is not the only content the plate sizes by (§6.4, §8.2)
        const bool has_icon = m_icon != nullptr;
        // the icon and label are separated when both are present, because two glyphs must not touch (§8.2)
        const float icon_gap = has_icon && !m_label_layout.empty() ? 6.0f : 0.0f;
        // the content width is the icon, the gap and the label, or just the label on the text-only path (§8.2)
        const float content_width = (has_icon ? resolved_icon_size() : 0.0f) + icon_gap + m_label_layout.size.width;
        // a padded control insets its content on both sides, which is what a native push control measures to (§8.2)
        const float padding = (has_icon || !m_label_layout.empty()) ? 2.0f * metrics.button_padding_horizontal : 0.0f;
        // a push control stays grabbable, so its content plus padding drives the plate (§8.2)
        Size content{.width = std::max(content_width + padding, metrics.button_minimum.width), .height = std::max(std::max(m_label_layout.size.height, has_icon ? resolved_icon_size() : 0.0f), metrics.button_minimum.height)};
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
        // a text-only button keeps the original centred label path, so its output is byte compatible (§6.2)
        if (m_icon == nullptr) {
            // an empty label draws no run, because a button may carry no text at all (§6.2)
            if (m_label_layout.empty())
                return;
            // the label colour resolves by state, so a hover and a press read differently without a second literal (§6.1)
            const Color label_color = resolve(label_colors(), Color{0.96f, 0.97f, 0.99f, 1.0f});
            // the run is centred on the plate as a box, where every line places its baseline (§6.1, §9.1)
            const Point origin{.x = bounds().x + (bounds().width - m_label_layout.size.width) * 0.5f, .y = bounds().y + (bounds().height - m_label_layout.size.height) * 0.5f};
            // the run is recorded over the plate, which is the last thing the frame draws for this button (§6.1)
            context.draw_text(m_label_layout, origin, {.fill = label_color});
            return;
        }
        // the icon square is the first half of the content group, which the plate centres (§6.4, §8.2)
        const float side = resolved_icon_size();
        // the gap sits between the icon and the label, while an icon-only button has no gap (§8.2)
        const float gap = m_label_layout.empty() ? 0.0f : 6.0f;
        // the group is what the plate centres, because an icon-only button centres its square alone (§8.2)
        const float group_width = side + gap + m_label_layout.size.width;
        // the group starts at the left edge the centred extent leaves (§8.2)
        const float group_x = bounds().x + (bounds().width - group_width) * 0.5f;
        // the icon hangs at the content's vertical centre, which is where a push control reads (§8.2)
        const Rect icon_rect = Rect::from_origin_size(Point{.x = group_x, .y = bounds().y + (bounds().height - side) * 0.5f}, Size{.width = side, .height = side});
        // the image is recorded under the plate's content, which the renderer samples into the square (§6.4)
        context.draw_image(m_icon, icon_rect, Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        // an icon-only button has no label to draw after the square (§8.2)
        if (m_label_layout.empty())
            return;
        // the label colour resolves by state, like the text-only path (§6.1)
        const Color label_color = resolve(label_colors(), Color{0.96f, 0.97f, 0.99f, 1.0f});
        // the label sits to the right of the icon, centred on the plate height (§8.2)
        const Point label_origin{.x = group_x + side + gap, .y = bounds().y + (bounds().height - m_label_layout.size.height) * 0.5f};
        // the run is recorded after the icon, which is the visible order of the group (§6.1)
        context.draw_text(m_label_layout, label_origin, {.fill = label_color});
    }

    bool Button::hit_test(Point logical_point) const {
        // a hidden button is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled button is not a hit, because it refuses input (§8.3)
        if (disabled())
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
            if (disabled())
                return;
            // a modified key is an application shortcut, so a plain activation ignores it (§8.3)
            if (!event.modifiers.none())
                return;
            // the event is claimed, so no ancestor reacts to the same key (§8.3)
            event.handled = true;
            // the activation runs once, which is what a key press means (§8.3)
            activate(InvocationSource::Pointer);
            return;
        }
        // a press is the only other event that activates a button (§8.3)
        if (event.kind != EventKind::PointerDown) {
            // any other event belongs to a parent, so the button does not claim it (§8.3)
            return;
        }
        // a disabled button does not claim a press, because it refuses input (§8.3)
        if (disabled())
            return;
        // a press outside the plate belongs to whatever is behind the button (§8.3)
        if (!hit_test(event.position))
            return;
        // the event is claimed, so no ancestor reacts to the same press (§8.3)
        event.handled = true;
        // the activation runs once, which is what a press means (§8.3)
        activate(InvocationSource::Pointer);
    }

    SemanticNode Button::semantic_node() const {
        // the role is a button, because a press performs an action (§10.1)
        SemanticNode node{SemanticRole::Button};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the name is the label, then the author id, then the role word for an icon-only control (§10.1, AD-11)
        node.set_name(m_label.empty() ? (identity().explicit_id.empty() ? std::string("Button") : identity().explicit_id) : m_label);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled button reports the disabled state, which is what refuses activation (§10.1)
        node.set_disabled(disabled());
        // the focus flag is part of the announced state, so an adapter can follow the ring (§10.1)
        node.set_focused(focused());
        // a button is not checkable, so the check state stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the activate action runs the same handler a press does, and its identifier is discarded because a widget names actions by label (§10.1) the action body carries no disabled guard, because SemanticNode::invoke refuses every action while the node it belongs to is disabled, which is where that rule belongs (§10.1)
        (void)node.add_action("activate", [this] {
            // the activation routes through the shared command when one is bound, as a press does (§18.7)
            mutable_owner(this)->activate(InvocationSource::Accessibility);
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
            m_label_layout = m_text_engine->measure(m_label, 0.0f, label_font_size());
        // a new label resizes the plate, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    const std::string& Button::label() const {
        // the label is what the semantic node announces as the name (§10.1)
        return m_label;
    }

    void Button::set_icon(std::shared_ptr<const ImageResource> icon) {
        // an unchanged handle redraws nothing, because the button shows the same picture (§6.4)
        if (m_icon == icon)
            return;
        // the handle is stored first, so the invalidation walks a button that already carries it (§6.4)
        m_icon = std::move(icon);
        // an icon changes the content extent, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    const std::shared_ptr<const ImageResource>& Button::icon() const {
        // the retained handle is what the paint records (§6.4)
        return m_icon;
    }

    void Button::set_icon_size(float size) {
        // a negative size is no size at all, so it follows the default like zero (§8.2)
        const float clamped = std::max(0.0f, size);
        // an unchanged size remeasures nothing, because the icon square did not move (§8.2)
        if (m_icon_size == clamped)
            return;
        // the size is stored first, so the invalidation walks a button that already carries it (§8.2)
        m_icon_size = clamped;
        // a new icon square changes the content extent, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    float Button::icon_size() const {
        // the resolved square is the explicit size or the compact default (§8.2)
        return resolved_icon_size();
    }

    float Button::resolved_icon_size() const {
        // an explicit size wins, because the author named it for this button (§8.2)
        if (m_icon_size > 0.0f)
            return m_icon_size;
        // the default square is the tool control icon rhythm, which keeps icon-only buttons compact (§8.2)
        return 16.0f;
    }

    void Button::set_on_click(std::function<void()> handler) {
        // the handler is swapped in place, because a press after this point must run the new work (§8.3)
        m_on_click = std::move(handler);
    }

    void Button::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the button looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so unbinding a command later restores what the form declared (§18.7)
        m_disabled = disabled;
        // a bound command owns the state, so the imperative write is recorded and not applied (§18.7)
        if (command() != nullptr)
            return;
        // a disabled button fades and stops accepting presses, so the frame changes (§6.1)
        invalidate();
    }

    bool Button::disabled() const {
        // a bound command is the single answer, which is what three consumers reading one object means (§18.7)
        if (command() != nullptr)
            return !command()->state().enabled;
        // without a command the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void Button::apply_command_state(const CommandState& state) {
        // the label is one of the three published fields, so a consumer never holds a copy of it (§18.2)
        set_label(state.label);
        // disabled reads the bound command, so applicability is the only field this control writes (§18.2)
        UiNode::set_visible(state.visible);
        // the plate and the label repaint through the disabled role, which the state resolver reads (§6.2)
        invalidate();
    }

    void Button::activate(InvocationSource source) {
        // a bound command is invoked through the shared object, so both routes record one entry (§18.4)
        if (command() != nullptr) {
            (void)command()->invoke(CommandInvocation{.command = command()->id(), .argument = {}, .selection = {}, .source = source});
            return;
        }
        // a button with no handler swallows the event without doing anything, because there is no work (§10.1)
        if (!m_on_click)
            return;
        // the handler runs once, which is what an activation means (§8.3)
        m_on_click();
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
        if (disabled())
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

    const Color* Button::border_colors() const {
        // without a theme the table is unused, because the resolver returns the node's own stroke (§8.4)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the border is a role like the plate (§8.4)
        return theme()->palette().button_border;
    }

    float Button::border_width() const {
        // without a theme the stroke keeps the width the node stored (§8.1)
        if (theme() == nullptr)
            return UiNode::border_width();
        // the theme owns the width, because a border is a sizing decision like the plate (§8.4)
        return theme()->metrics().button_border_width;
    }

    Color Button::border_color() const {
        // with a theme the border resolves by state, so a hover lifts it like the plate does (§8.4)
        return resolve(border_colors(), UiNode::border_color());
    }

} // namespace albor
