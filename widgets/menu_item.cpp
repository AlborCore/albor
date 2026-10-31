#include "widgets/menu_item.h"

#include <algorithm>
#include <utility>

#include "commands/command.h"
#include "text/text_engine.h"

namespace albor
{
    namespace
    {
        // the inset a menu row leaves beside its label and accelerator (§8.2)
        constexpr float MENU_ITEM_PADDING = 12.0f;

        // the gap between a label and its accelerator, which keeps the two runs apart (§8.2)
        constexpr float MENU_ITEM_GAP = 24.0f;

        // the smallest height a menu row takes, because a target shorter than this is hard to point at (§8.2)
        constexpr float MENU_ITEM_MINIMUM_HEIGHT = 22.0f;
    } // namespace

    MenuItem::MenuItem(BindingEngine* engine, TextEngine* text_engine, std::string label, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_label(std::move(label)) {
        // the label is measured once up front, so the row has a height before the first layout pass (§9.1)
        if (m_text_engine == nullptr)
            return;
        // the run is measured unbounded, because the row sizes to its content (§9.1)
        m_label_layout = m_text_engine->measure(m_label, 0.0f, label_font_size());
    }

    float MenuItem::label_font_size() const {
        // a themed item takes the body size the design language declared (§8.4)
        if (theme() != nullptr)
            return theme()->typography().body;
        // an unthemed item takes the same size every call site does (§9.1)
        return TextEngine::DEFAULT_FONT_SIZE;
    }

    void MenuItem::measure(const SizeConstraints& constraints, Size& out_size) {
        // a row with no engine keeps the minimum extent, because it cannot measure its text (§9.1)
        if (m_text_engine != nullptr) {
            // the offered width bounds the label, which wraps rather than overflowing the menu (§9.1)
            const float offered = constraints.maximum.width > 0.0f ? std::max(0.0f, constraints.maximum.width - MENU_ITEM_PADDING * 2.0f) : 0.0f;
            // the label and the accelerator are measured at the row size (§9.1)
            m_label_layout = m_text_engine->measure(m_label, offered, label_font_size());
            // the accelerator is a short run that never wraps, because it is one spelling (§18.5)
            m_shortcut_layout = m_text_engine->measure(m_shortcut, 0.0f, label_font_size());
        }
        // the accelerator takes a gap only when it carries text, because two empty runs must not widen the row (§18.5)
        const float shortcut_width = m_shortcut_layout.empty() ? 0.0f : MENU_ITEM_GAP + m_shortcut_layout.size.width;
        // the row is the label, the gap and the accelerator inside the insets (§8.2)
        Size content{.width = m_label_layout.size.width + shortcut_width + MENU_ITEM_PADDING * 2.0f, .height = std::max({m_label_layout.size.height, m_shortcut_layout.size.height, MENU_ITEM_MINIMUM_HEIGHT})};
        // the offered constraints are final, because the menu has the last word on width (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so the menu can query it (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void MenuItem::arrange(const Rect& bounds) {
        // the component arranges its bounds, because a menu item has no children (§8.2)
        Component::arrange(bounds);
    }

    void MenuItem::paint_content(RenderContext& context) {
        // a hidden row records nothing, because the menu already skipped it (§6.1)
        if (!visible())
            return;
        // the label colour follows the state, so a disabled row fades without a second literal (§6.1)
        Color label_color = Color{0.92f, 0.94f, 0.96f, 1.0f};
        // a themed row takes the role colour the palette carries (§6.1)
        if (theme() != nullptr)
            label_color = resolve(theme()->palette().label_foreground, label_color);
        // a disabled row is dimmed, because it stays visible but refuses activation (§18.2)
        if (disabled())
            label_color = Color::rgba(label_color.red * 0.55f, label_color.green * 0.55f, label_color.blue * 0.55f, label_color.alpha);
        // the label sits at the leading inset, centred on the row height (§8.2)
        if (!m_label_layout.empty()) {
            // the origin is the top left of the run box, which the row centres vertically (§9.1)
            const Point origin{.x = bounds().x + MENU_ITEM_PADDING, .y = bounds().y + (bounds().height - m_label_layout.size.height) * 0.5f};
            // the run is recorded over the row plate (§6.1)
            context.draw_text(m_label_layout, origin, Paint{.fill = label_color});
        }
        // an item with no accelerator draws none, because there is no text to place (§18.5)
        if (m_shortcut_layout.empty())
            return;
        // the accelerator is dimmer than the label, because it is secondary information (§18.5)
        const Color shortcut_color = disabled() ? label_color : Color::rgba(label_color.red, label_color.green, label_color.blue, 0.62f);
        // the run sits at the trailing inset, centred on the same row (§18.5)
        const Point origin{.x = bounds().x + bounds().width - MENU_ITEM_PADDING - m_shortcut_layout.size.width, .y = bounds().y + (bounds().height - m_shortcut_layout.size.height) * 0.5f};
        // the accelerator is recorded after the label, which is the reading order (§18.5)
        context.draw_text(m_shortcut_layout, origin, Paint{.fill = shortcut_color});
    }

    bool MenuItem::hit_test(Point logical_point) const {
        // a hidden item is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled item stays in place but refuses input (§18.2)
        if (disabled())
            return false;
        // the point must land inside the arranged row (§8.3)
        const Rect& b = bounds();
        return !b.empty() && logical_point.x >= b.x && logical_point.x < b.x + b.width && logical_point.y >= b.y && logical_point.y < b.y + b.height;
    }

    void MenuItem::dispatch_event(Event& event) {
        // a hidden item takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the hover transitions run first, so the row plate is in the right state (§8.3)
        handle_hover_transitions(event);
        // escape dismisses the row focus, which is the menu dismissal a keyboard offers (§8.3)
        if (event.kind == EventKind::KeyDown && event.key == Key::Escape) {
            // the event is claimed once the focused row gives it up (§8.3)
            if (focused()) {
                // the ring goes away with the focus, which repaints the row (§8.3)
                set_focused(false);
                // the key is consumed, so no ancestor also dismisses (§8.3)
                event.handled = true;
            }
            return;
        }
        // return and space activate the focused row, which is what a keyboard route means (§8.3)
        if (event.kind == EventKind::KeyDown && (event.key == Key::Return || event.key == Key::Space)) {
            // a disabled item refuses the key, because it refuses activation (§18.2)
            if (disabled())
                return;
            // a modified key belongs to an application shortcut, so activation ignores it (§18.5)
            if (!event.modifiers.none())
                return;
            // the key is claimed, so no ancestor reacts to it (§8.3)
            event.handled = true;
            // the command is invoked through the menu route, because the row is what the user chose (§18.3)
            activate(InvocationSource::Menu);
            return;
        }
        // a pointer press inside the row activates it (§8.3)
        if (event.kind != EventKind::PointerDown || disabled() || !hit_test(event.position))
            return;
        // the press is claimed, so no ancestor reacts to it (§8.3)
        event.handled = true;
        // the command is invoked through the menu route, which is the path a menu item owns (§18.3)
        activate(InvocationSource::Menu);
    }

    bool MenuItem::accepts_focus() const {
        // a visible enabled row is a focus stop, which is what arrow traversal moves between (§8.3)
        return visible() && !disabled();
    }

    SemanticNode MenuItem::semantic_node() const {
        // the role is a menu item, because a reader announces what the row is (§10.1)
        SemanticNode node{SemanticRole::MenuItem};
        // the author assigned id travels with the node, because automation selects by it (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the label is the accessible name, and the id stands in for a row with no text (§10.1, AD-11)
        node.set_name(m_label.empty() ? identity().explicit_id : m_label);
        // the bounds travel with the node, because an adapter places the announcement at the row (§10.1)
        node.set_bounds(bounds());
        // a disabled row reports the state that refuses activation (§10.1)
        node.set_disabled(disabled());
        // the focus flag follows the traversal ring (§10.1)
        node.set_focused(focused());
        // the accelerator is the value a reader speaks beside the name (§18.5)
        node.set_value(m_shortcut);
        // a menu item is not checkable, so the check stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the activate action runs the same command a press runs, while the node refuses it when disabled (§10.1, §18.3)
        (void)node.add_action("activate", [this] { mutable_owner(this)->activate(InvocationSource::Accessibility); });
        return node;
    }

    void MenuItem::set_label(std::string label) {
        // an unchanged label redraws nothing, because the row looks the same (§6.1)
        if (m_label == label)
            return;
        // the label is stored first, so the invalidation walks a row that already carries it (§6.1)
        m_label = std::move(label);
        // a new label is measured eagerly, because the row width follows its text (§9.1)
        if (m_text_engine != nullptr)
            m_label_layout = m_text_engine->measure(m_label, 0.0f, label_font_size());
        // a new label changes the row extent, so both layout passes run (§8.2)
        invalidate_layout();
    }

    const std::string& MenuItem::label() const {
        // the label is what the row draws and announces (§10.1)
        return m_label;
    }

    void MenuItem::set_shortcut(const ShortcutTable& table, Shortcut shortcut) {
        // the table spells the accelerator, which is the one text a menu may present (§18.5)
        set_shortcut_text(table.display(shortcut));
    }

    void MenuItem::set_shortcut_text(std::string text) {
        // an unchanged spelling redraws nothing, because the row looks the same (§18.5)
        if (m_shortcut == text)
            return;
        // the text is stored first, so the invalidation walks a row that already carries it (§18.5)
        m_shortcut = std::move(text);
        // a new accelerator is measured eagerly, because the row width follows it (§9.1)
        if (m_text_engine != nullptr)
            m_shortcut_layout = m_text_engine->measure(m_shortcut, 0.0f, label_font_size());
        // a new accelerator changes the row extent, so both layout passes run (§8.2)
        invalidate_layout();
    }

    const std::string& MenuItem::shortcut_text() const {
        // the spelling is what the row draws beside its label (§18.5)
        return m_shortcut;
    }

    void MenuItem::set_on_invoke(std::function<void()> handler) {
        // the handler is swapped in place, because a later press must run the new work (§8.3)
        m_on_invoke = std::move(handler);
    }

    void MenuItem::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the row looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so unbinding a command restores what the author declared (§18.7)
        m_disabled = disabled;
        // a bound command owns applicability, so the imperative write is recorded and not applied (§18.7)
        if (command() != nullptr)
            return;
        // a disabled row fades and leaves focus traversal, so the frame changes (§6.1)
        invalidate();
    }

    bool MenuItem::disabled() const {
        // a bound command is the one answer every consumer shares (§18.7)
        if (command() != nullptr)
            return !command()->state().enabled;
        // without a command the row owns its own state (§8.3)
        return m_disabled;
    }

    Color MenuItem::background() const {
        // a disabled row draws no hover plate, because it cannot be chosen (§18.2)
        if (disabled())
            return Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
        // a pressed row is stronger than a hovered one, which is the state hierarchy §8.3 describes (§6.1)
        if (pressed())
            return Color::rgba(0.20f, 0.55f, 0.90f, 0.28f);
        // a hovered row shows the accent fill softly, which marks where a press would land (§6.1)
        if (hovered())
            return Color::rgba(0.20f, 0.55f, 0.90f, 0.16f);
        // an idle row is transparent, so the menu surface shows through (§6.1)
        return Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void MenuItem::apply_command_state(const CommandState& state) {
        // the published label is the one the row presents, which is the sharing §18.7 promises (§18.2)
        if (!state.label.empty())
            set_label(state.label);
        // applicability is the other published field, which the row follows without a copy (§18.2)
        UiNode::set_visible(state.visible);
        // the disabled state changes the plate and the text, so the row repaints (§6.1)
        invalidate();
    }

    void MenuItem::activate(InvocationSource source) {
        // a bound command is invoked through the shared object, so a menu, a button and a key record one entry (§18.3)
        if (command() != nullptr) {
            (void)command()->invoke(CommandInvocation{.command = command()->id(), .argument = {}, .selection = {}, .source = source});
            return;
        }
        // a row with no command and no handler does nothing, because there is no work (§10.1)
        if (!m_on_invoke)
            return;
        // the local handler runs once, which is what a press means (§8.3)
        m_on_invoke();
    }
} // namespace albor
