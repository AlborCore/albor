#include "widgets/combo_box.h"

#include <algorithm>
#include <utility>

#include "semantics/semantic_provider.h"
#include "widgets/widget.h"

namespace albor
{
    // the top-level layer a popup draws through, which the root owns while the list is open (§8.3)
    class ComboBoxPopup final : public UiNode
    {
    public:
        // build the layer over the control it presents, which keeps the widget as the source of truth (§8.3)
        explicit ComboBoxPopup(ComboBox& owner) :
            m_owner(owner) {}

        // override: the layer takes no layout space, because the popup is positioned by its owner (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the constraints are ignored, because the layer is not part of the parent layout (§8.2)
            (void)constraints;
            // a zero extent keeps the parent from growing around the popup (§8.2)
            out_size = Size{};
        }

        // override: the layer has no arranged rectangle, because its paint is in root coordinates (§8.2)
        void arrange(const Rect& bounds) override {
            // the bounds are ignored, because the popup rectangle is resolved from the trigger (§8.2)
            (void)bounds;
        }

        // override: paint the list after the ordinary tree, which is what puts it above every sibling (§8.3)
        void render(RenderContext& context) override { m_owner.paint_popup(context); }

        // override: the open popup owns every pointer position, including the outside press that closes it (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override {
            // the layer takes every point while it is open, because it also owns the outside dismissal (§8.3)
            (void)logical_point;
            // a hidden owner has no live popup, so it is not a hit (§8.3)
            return m_owner.expanded() && m_owner.visible_in_tree();
        }

        // override: the popup forwards the event to the control that owns the list (§8.3)
        void dispatch_event(Event& event) override { m_owner.dispatch_event(event); }

        // override: the popup is a layer rather than a keyboard stop (§8.3)
        [[nodiscard]] bool accepts_focus() const override { return false; }

        // override: the popup holds no focusable children, because the control owns the list (§8.3)
        [[nodiscard]] bool focusable_children() const override { return false; }

    private:
        // the control whose popup geometry and choice the layer presents (§8.3)
        ComboBox& m_owner;
    };

    ComboBox::ComboBox(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine) {
        // the marker is measured up front, because the trigger always draws one (§9.1)
        remeasure();
    }

    ComboBox::~ComboBox() {
        // a widget that dies with an open popup removes its layer before the root unwinds (§8.3)
        if (m_popup == nullptr)
            return;
        // a rootless widget never added a layer, so there is nothing to detach (§8.3)
        if (UiNode* root = popup_root(); root != nullptr)
            (void)root->remove_overlay(m_popup);
        // the layer is gone, so the pointer no longer names a live node (§8.3)
        m_popup = nullptr;
    }

    UiNode* ComboBox::popup_root() {
        // a widget with no parent is not in a tree, so it has no layer to join (§8.3)
        UiNode* root = parent();
        if (root == nullptr)
            return nullptr;
        // the walk climbs to the topmost node, which is the root that paints last (§8.3)
        while (root->parent() != nullptr)
            root = root->parent();
        return root;
    }

    const ControlMetrics& ComboBox::metrics() const {
        // the installed theme owns the sizing, while an unthemed control keeps the shared fallback (§8.2)
        return theme() != nullptr ? theme()->metrics() : default_metrics();
    }

    float ComboBox::font_size() const {
        // the installed hierarchy sets the body size, while an unthemed control takes the engine default (§9.1)
        return theme() != nullptr ? theme()->typography().body : TextEngine::DEFAULT_FONT_SIZE;
    }

    void ComboBox::remeasure() {
        // a control with no engine cannot shape a run, so every layout stays empty (§9.1)
        if (m_text_engine == nullptr) {
            m_selected_layout = TextLayout{};
            m_placeholder_layout = TextLayout{};
            m_arrow_layout = TextLayout{};
            m_item_layouts.clear();
            return;
        }
        // the option runs are rebuilt in declaration order, because the popup paints that order (§9.1)
        m_item_layouts.resize(m_items.size());
        for (std::size_t index = 0; index < m_items.size(); index++)
            m_item_layouts[index] = m_text_engine->measure(m_items[index], TextEngine::UNBOUNDED_WIDTH, font_size());
        // the trigger run follows the selected option, or the hint when nothing is selected (§9.1)
        if (m_selected >= 0 && static_cast<std::size_t>(m_selected) < m_item_layouts.size())
            m_selected_layout = m_item_layouts[static_cast<std::size_t>(m_selected)];
        else
            m_selected_layout = TextLayout{};
        // the hint is measured beside the selection, because an empty control still shows it (§9.1)
        m_placeholder_layout = m_text_engine->measure(m_placeholder, TextEngine::UNBOUNDED_WIDTH, font_size());
        // the marker is one small run at the trailing edge, which is what opens the list (§9.1)
        m_arrow_layout = m_text_engine->measure("v", TextEngine::UNBOUNDED_WIDTH, font_size());
    }

    const TextLayout& ComboBox::selected_layout() const {
        // a selection with no shaped run falls back to the hint, which keeps an empty trigger readable (§9.1)
        if (m_selected >= 0 && !m_selected_layout.empty())
            return m_selected_layout;
        // the hint is what an empty control shows (§9.1)
        return m_placeholder_layout;
    }

    void ComboBox::measure(const SizeConstraints& constraints, Size& out_size) {
        // the runs are current before their extents are read, because a font or item change may have landed (§9.1)
        remeasure();
        // the theme metrics decide the trigger minimum and the inset (§8.2)
        const ControlMetrics& m = metrics();
        // the widest option decides the popup and the trigger, because a choice must be visible before it is chosen (§8.2)
        float widest = selected_layout().size.width;
        for (const TextLayout& layout : m_item_layouts)
            widest = std::max(widest, layout.size.width);
        // the trigger adds the inset, the marker and the gap between the value and the marker (§8.2)
        const float content_width = widest + m.text_field_padding * 2.0f + m_arrow_layout.size.width + 6.0f;
        // the trigger height is the larger of the value and the marker, and never below the themed minimum (§8.2)
        const float content_height = std::max(std::max(selected_layout().size.height, m_arrow_layout.size.height), m.text_field_minimum.height);
        // the offered constraints are final, because the parent has the last word (§8.2)
        m_measured_size = constraints.constrain(Size{.width = std::max(content_width, m.text_field_minimum.width), .height = content_height});
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void ComboBox::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, which the trigger uses as its whole plate (§8.2)
        m_bounds = bounds;
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    Rect ComboBox::trigger_rect() const {
        // the trigger is the control's own arranged bounds, which is where a press opens the list (§8.2)
        return m_bounds;
    }

    Rect ComboBox::popup_rect() const {
        // the popup hangs directly under the trigger, which is the conventional placement (§8.2)
        const ControlMetrics& m = metrics();
        // the item count decides the natural height, while the themed ceiling bounds it (§8.2)
        const float natural = static_cast<float>(m_items.size()) * m.combo_box_item_height + m.text_field_padding * 2.0f;
        // a popup never opens taller than its ceiling, because it would leave the window (§8.2)
        const float height = std::min(natural, m.combo_box_popup_max_height);
        // the popup is at least as wide as the trigger, and wider when an option needs it (§8.2)
        float widest = m_bounds.width;
        for (const TextLayout& layout : m_item_layouts)
            widest = std::max(widest, layout.size.width + m.text_field_padding * 2.0f);
        // an empty popup has no rectangle, because there are no options to draw (§8.2)
        if (m_items.empty())
            return Rect{};
        // the rectangle sits under the trigger and shares its leading edge (§8.2)
        return Rect::from_origin_size(Point{.x = m_bounds.x, .y = m_bounds.y + m_bounds.height}, Size{.width = widest, .height = height});
    }

    std::size_t ComboBox::visible_count() const {
        // an empty list has no visible row (§8.2)
        if (m_items.empty())
            return 0;
        // the inset is removed before the row count is divided, because the rows sit inside the popup padding (§8.2)
        const ControlMetrics& m = metrics();
        const float available = std::max(0.0f, popup_rect().height - m.text_field_padding * 2.0f);
        // a row height of zero cannot divide, so the whole list is treated as visible (§8.2)
        if (m.combo_box_item_height <= 0.0f)
            return m_items.size();
        // the ceiling bounds the visible rows, and the list bounds it again (§8.2)
        return std::min(m_items.size(), std::max<std::size_t>(1, static_cast<std::size_t>(available / m.combo_box_item_height)));
    }

    std::size_t ComboBox::first_visible() const {
        // a list that fits starts at its first option (§8.2)
        const std::size_t count = visible_count();
        if (m_items.size() <= count)
            return 0;
        // the open highlight drives the window, because the highlighted row must stay visible (§8.3)
        const int focus = m_expanded && m_hovered >= 0 ? m_hovered : m_selected;
        // a highlight past the window scrolls the window, which keeps the chosen row in view (§8.2)
        if (focus >= 0 && static_cast<std::size_t>(focus) >= count)
            return std::min(m_items.size() - count, static_cast<std::size_t>(focus) - count + 1);
        // no highlight keeps the window at the start (§8.2)
        return 0;
    }

    Rect ComboBox::option_rect(std::size_t index) const {
        // a row outside the visible window has no rectangle, because it is not drawn (§8.2)
        const std::size_t first = first_visible();
        const std::size_t count = visible_count();
        if (index < first || index >= first + count)
            return Rect{};
        // the inset frames the rows inside the popup (§8.2)
        const ControlMetrics& m = metrics();
        const Rect popup = popup_rect();
        // the row sits one item height below the previous one (§8.2)
        const float y = popup.y + m.text_field_padding + static_cast<float>(index - first) * m.combo_box_item_height;
        // the row spans the popup width minus its inset (§8.2)
        return Rect::from_origin_size(Point{.x = popup.x + m.text_field_padding, .y = y}, Size{.width = std::max(0.0f, popup.width - m.text_field_padding * 2.0f), .height = m.combo_box_item_height});
    }

    int ComboBox::option_at(Point point) const {
        // a closed popup has no option under any point (§8.3)
        if (!m_expanded)
            return -1;
        // the point must land in the popup before it can name a row (§8.3)
        const Rect popup = popup_rect();
        if (popup.empty() || point.x < popup.x || point.x >= popup.x + popup.width || point.y < popup.y || point.y >= popup.y + popup.height)
            return -1;
        // each visible row is tested in order, because the rows do not overlap (§8.3)
        const std::size_t first = first_visible();
        const std::size_t count = visible_count();
        for (std::size_t index = first; index < first + count && index < m_items.size(); index++) {
            // the row rectangle is the hit target, which is what a pointer selects (§8.3)
            const Rect row = option_rect(index);
            if (point.y >= row.y && point.y < row.y + row.height)
                return static_cast<int>(index);
        }
        // the point is inside the popup but not on a row, which names nothing (§8.3)
        return -1;
    }

    void ComboBox::paint_content(RenderContext& context) {
        // an unarranged control draws nothing, because there is no trigger plate (§6.2)
        if (m_bounds.empty())
            return;
        // the trigger content is clipped to the plate, because a long value must not bleed (§6.2)
        const ControlMetrics& m = metrics();
        context.save();
        context.clip_rect(m_bounds);
        // the selected value or the hint is drawn at the leading inset, which is where a field starts (§8.2)
        const TextLayout& value = selected_layout();
        // the value sits vertically centred, while the marker shares the same line (§9.1)
        const float value_y = m_bounds.y + (m_bounds.height - value.size.height) * 0.5f;
        // the value takes the value colour, which is the field text role (§6.1)
        const Color value_colour = theme() != nullptr ? theme()->palette().text_field_text[state_index(state())] : Color{0.92f, 0.94f, 0.96f, 1.0f};
        // a non empty run is recorded at the leading inset (§9.1)
        if (!value.empty())
            context.draw_text(value, Point{.x = m_bounds.x + m.text_field_padding, .y = value_y}, {.fill = value_colour});
        // the disclosure marker sits at the trailing edge, where the user reads the affordance (§9.1)
        if (!m_arrow_layout.empty()) {
            // the marker is centred on the trigger height, because it is one short run (§9.1)
            const float arrow_x = m_bounds.x + m_bounds.width - m.text_field_padding - m_arrow_layout.size.width;
            // the marker uses the value colour, so it reads with the value (§6.1)
            context.draw_text(m_arrow_layout, Point{.x = arrow_x, .y = m_bounds.y + (m_bounds.height - m_arrow_layout.size.height) * 0.5f}, {.fill = value_colour});
        }
        // the trigger clip comes off before the popup, because the popup hangs outside the plate (§6.2)
        context.restore();
        // a rootless control draws its own popup, because it never joined a layer (§8.3)
        if (m_expanded && m_popup == nullptr)
            paint_popup(context);
    }

    void ComboBox::paint_popup(RenderContext& context) {
        // a closed popup draws nothing, which is what a dismissed layer leaves behind (§8.2)
        if (!m_expanded)
            return;
        // the popup is clipped to its own rectangle, because a long list is bounded by the ceiling (§6.2)
        const ControlMetrics& m = metrics();
        const Rect popup = popup_rect();
        if (popup.empty())
            return;
        context.save();
        context.clip_rect(popup);
        // the popup face and hairline are the panel tokens, which is what an overlay surface reads through (§6.1)
        const Color face = theme() != nullptr ? theme()->palette().panel_background[state_index(ControlState::Normal)] : Color{0.13f, 0.15f, 0.19f, 1.0f};
        const Color line = theme() != nullptr ? theme()->palette().panel_border[state_index(ControlState::Normal)] : Color{0.27f, 0.30f, 0.36f, 1.0f};
        // the popup is one rounded card, which the rows sit inside (§6.2)
        context.draw_rounded_rect(popup, theme() != nullptr ? theme()->metrics().panel_radius : 6.0f, Paint{.fill = face, .stroke = line, .stroke_width = 1.0f});
        // the row text resolves through the field text role, while the highlight uses the selection role (§6.1)
        const Color row_colour = theme() != nullptr ? theme()->palette().text_field_text[state_index(ControlState::Normal)] : Color{0.92f, 0.94f, 0.96f, 1.0f};
        const Color row_selected = theme() != nullptr ? theme()->palette().text_field_selection[state_index(ControlState::Normal)] : Color{0.20f, 0.55f, 0.90f, 0.35f};
        // each visible row is drawn, which is the popup list the user chooses from (§8.3)
        const std::size_t first = first_visible();
        const std::size_t count = visible_count();
        for (std::size_t index = first; index < first + count && index < m_items.size(); index++) {
            // the row rectangle is the selection band and the text frame (§8.2)
            const Rect row = option_rect(index);
            if (row.empty())
                continue;
            // only the highlighted row carries the band, which keeps one row visibly current (§8.3)
            if (static_cast<int>(index) == m_hovered)
                context.draw_rounded_rect(row, 2.0f, Paint{.fill = row_selected});
            // a control with no text engine has no shaped run, but still paints the popup face (§9.1)
            if (index >= m_item_layouts.size())
                continue;
            // the option run is placed at the row's leading inset and vertical centre (§9.1)
            const TextLayout& layout = m_item_layouts[index];
            if (!layout.empty())
                context.draw_text(layout, Point{.x = row.x + m.text_field_padding, .y = row.y + (row.height - layout.size.height) * 0.5f}, {.fill = row_colour});
        }
        // the popup clip comes off, so a sibling draws at the state it found (§6.2)
        context.restore();
    }

    bool ComboBox::hit_test(Point logical_point) const {
        // a hidden or disabled control is not a hit, because it refuses input (§8.3)
        if (!visible() || disabled())
            return false;
        // the trigger is the whole plate, even while the popup is open (§8.3)
        const Rect trigger = trigger_rect();
        const bool in_trigger = !trigger.empty() && logical_point.x >= trigger.x && logical_point.x < trigger.x + trigger.width && logical_point.y >= trigger.y && logical_point.y < trigger.y + trigger.height;
        // a closed popup leaves only the trigger as a target (§8.3)
        if (!m_expanded)
            return in_trigger;
        // an open popup is a hit anywhere inside its rectangle, which lets it receive the close press (§8.3)
        const Rect popup = popup_rect();
        const bool in_popup = !popup.empty() && logical_point.x >= popup.x && logical_point.x < popup.x + popup.width && logical_point.y >= popup.y && logical_point.y < popup.y + popup.height;
        return in_trigger || in_popup;
    }

    void ComboBox::dispatch_event(Event& event) {
        // a hidden control takes no event at all, because it has no face (§8.3)
        if (!visible())
            return;
        // the trigger transitions run first, so the plate shows the pointer state (§8.3)
        handle_hover_transitions(event);
        // a disabled control refuses every input, which is what the state means (§8.3)
        if (disabled())
            return;
        // a move while the popup is open highlights the row under the pointer (§8.3)
        if (event.kind == EventKind::PointerMove && m_expanded) {
            // the row under the pointer becomes the hover row, which paints a highlight (§8.3)
            const int hovered = option_at(event.position);
            // an unchanged row redraws nothing, because the popup looks the same (§6.1)
            if (hovered != m_hovered) {
                // the new row is stored first, so the invalidation walks a popup that already carries it (§8.3)
                m_hovered = hovered;
                // a changed hover changes the popup paint (§6.1)
                invalidate();
            }
            // the move is not claimed, because the trigger may still need its hover transition (§8.3)
            return;
        }
        // a press chooses an option, toggles the trigger or closes an outside popup (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // an open popup wins the press before the trigger does, because it is the surface the user aimed at (§8.3)
            if (m_expanded) {
                // a press on a row chooses it, which is the one pointer selection path (§8.3)
                if (const int index = option_at(event.position); index >= 0) {
                    // the event is claimed, so no ancestor acts on the same press (§8.3)
                    event.handled = true;
                    // the choice is applied through the shared path (§8.3)
                    choose(index);
                    return;
                }
                // a press outside the popup and trigger closes it without changing the value (§8.3)
                if (!hit_test(event.position)) {
                    // the event is claimed, because an outside press closes the open surface (§8.3)
                    event.handled = true;
                    // the popup closes without a selection (§8.3)
                    close();
                    return;
                }
            }
            // a press on the trigger toggles the popup, which is what the disclosure marker promises (§8.3)
            const Rect trigger = trigger_rect();
            if (!trigger.empty() && event.position.x >= trigger.x && event.position.x < trigger.x + trigger.width && event.position.y >= trigger.y && event.position.y < trigger.y + trigger.height) {
                // the event is claimed, so the press does not reach a parent (§8.3)
                event.handled = true;
                // an open popup closes and a closed one opens (§8.3)
                if (m_expanded)
                    close();
                else
                    open();
            }
            return;
        }
        // a modified key belongs to the application, so the control only answers an unmodified key down (§8.3)
        if (event.kind != EventKind::KeyDown || !event.modifiers.none())
            return;
        // the escape key closes an open popup without changing the selection (§8.3)
        if (event.key == Key::Escape) {
            // an open popup is the only thing escape changes (§8.3)
            if (m_expanded) {
                // the event is claimed, so an enclosing dialog does not also close (§8.3)
                event.handled = true;
                // the selection stands while the popup leaves the screen (§8.3)
                close();
            }
            return;
        }
        // the arrows move the highlighted option and open the list when it is closed (§8.3)
        if (event.key == Key::ArrowDown || event.key == Key::ArrowUp) {
            // the key is claimed, because the control owns the list navigation (§8.3)
            event.handled = true;
            // the first arrow opens the list, which is the keyboard path to the choices (§8.3)
            if (!m_expanded)
                open();
            // the direction decides whether the row moves forward or back (§8.3)
            move_selection(event.key == Key::ArrowDown ? 1 : -1);
            return;
        }
        // home and end jump to the ends of the option list (§8.3)
        if (event.key == Key::Home || event.key == Key::End) {
            // the key is claimed, because the control owns the list navigation (§8.3)
            event.handled = true;
            // the list opens before a jump, because the chosen row must be visible (§8.3)
            if (!m_expanded)
                open();
            // an empty list has no row to jump to (§8.3)
            if (!m_items.empty())
                m_hovered = event.key == Key::Home ? 0 : static_cast<int>(m_items.size()) - 1;
            return;
        }
        // return and space choose the highlighted row, or open the list when it is closed (§8.3)
        if (event.key == Key::Return || event.key == Key::Space) {
            // the key is claimed, because the control owns the choice (§8.3)
            event.handled = true;
            // a closed list opens, while an open list commits the highlighted row (§8.3)
            if (!m_expanded)
                open();
            else if (m_hovered >= 0)
                choose(m_hovered);
            return;
        }
    }

    SemanticNode ComboBox::semantic_node() const {
        // the role is a combo box, because the control owns one choice and a popup list (§10.1)
        SemanticNode node{SemanticRole::ComboBox};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // a stable label names the control, while an authored id is the fallback a screen reader hears (§10.1, AD-11)
        const std::string& name = !m_label.empty() ? m_label : (!identity().explicit_id.empty() ? identity().explicit_id : m_placeholder);
        node.set_name(name);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled control reports the disabled state, which refuses every action (§10.1)
        node.set_disabled(disabled());
        // the expanded state is what a screen reader announces while the popup is open (§10.1)
        node.set_expanded(m_expanded);
        // a selected option is a chosen value rather than a toggle, which is what the state reports (§10.1)
        node.set_selected(m_selected >= 0);
        // a combo box is not checkable, because it chooses one value rather than toggling (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the open and close actions mirror the trigger, which automation can drive (§10.1)
        (void)node.add_action("open", [control = mutable_owner(this)] { control->open(); });
        (void)node.add_action("close", [control = mutable_owner(this)] { control->close(); });
        // the next and previous actions move the highlighted option (§10.1)
        (void)node.add_action("select_next", [control = mutable_owner(this)] {
            control->open();
            control->move_selection(1);
        });
        (void)node.add_action("select_previous", [control = mutable_owner(this)] {
            control->open();
            control->move_selection(-1);
        });
        // the activate action commits the highlighted option, which is what return does (§10.1)
        (void)node.add_action("activate", [control = mutable_owner(this)] {
            // a control with no highlighted row has nothing to commit (§10.1)
            if (control->m_hovered >= 0)
                control->choose(control->m_hovered);
        });
        return node;
    }

    std::size_t ComboBox::semantic_child_count() const {
        // one child announces each option, because the choices are the content a reader walks (§10.1)
        return m_items.size();
    }

    std::unique_ptr<SemanticProvider> ComboBox::semantic_child(std::size_t index) const {
        // an index past the options names nothing, which the lazy walk expects (§10.1)
        if (index >= m_items.size())
            return nullptr;
        // the item carries its option text and its chosen state, which is what a reader hears (§10.1)
        SemanticNode node{SemanticRole::ListItem};
        node.set_name(m_items[index]);
        node.set_selected(static_cast<int>(index) == m_selected);
        node.set_bounds(option_rect(index));
        node.set_check(CheckState::NotCheckable);
        return std::make_unique<SemanticNodeProvider>(std::move(node));
    }

    bool ComboBox::accepts_focus() const {
        // the control takes keys, so it is a keyboard stop (§8.3)
        return true;
    }

    Color ComboBox::background() const {
        // a themed control takes the field plate role, which is the surface a form entry reads through (§6.1)
        if (theme() != nullptr)
            return theme()->palette().text_field_plate[state_index(state())];
        // the unthemed fallback keeps the control visible without a theme (§6.1)
        return Color{0.13f, 0.15f, 0.19f, 1.0f};
    }

    float ComboBox::border_width() const {
        // a themed control takes the control hairline, which is the stroke a form entry reads through (§8.4)
        if (theme() != nullptr)
            return theme()->metrics().button_border_width;
        // the unthemed fallback keeps a hairline so the trigger reads as a control (§8.4)
        return UiNode::border_width();
    }

    Color ComboBox::border_color() const {
        // a themed control resolves the field border by state, which is the focus and disabled surface (§8.4)
        if (theme() != nullptr)
            return theme()->palette().text_field_border[state_index(state())];
        // the unthemed fallback keeps the colour the node stored (§8.4)
        return UiNode::border_color();
    }

    void ComboBox::set_items(std::vector<std::string> items) {
        // an unchanged list remeasures nothing, because every run is already current (§9.1)
        if (m_items == items)
            return;
        // the list is stored first, so the selection clamp reads the new size (§8.2)
        m_items = std::move(items);
        // a selection past the end is cleared, because the option it named is gone (§8.3)
        if (m_selected >= static_cast<int>(m_items.size()))
            m_selected = static_cast<int>(m_items.size()) - 1;
        // the option layouts are rebuilt from the new strings, which is what the paint reads (§9.1)
        remeasure();
        // a changed option list resizes the trigger and the popup, so both layout passes run (§8.2)
        invalidate_layout();
    }

    const std::vector<std::string>& ComboBox::items() const {
        // the list is handed back as stored, because a caller only reads it (§8)
        return m_items;
    }

    void ComboBox::set_selected(int index) {
        // a selection written before the options is kept, because a declaration may name it first (§8.3)
        const int requested = index < 0 ? -1 : index;
        // a selection past the current list is clamped only once a list exists (§8.3)
        const int clamped = requested < 0 || m_items.empty() ? requested : std::min(requested, static_cast<int>(m_items.size()) - 1);
        // an unchanged selection redraws nothing, because the trigger looks the same (§6.1)
        if (clamped == m_selected)
            return;
        // the index is stored first, so the invalidation walks a control that already carries it (§8.3)
        m_selected = clamped;
        // the trigger run follows the selected option, but a missing engine leaves no measured run to copy (§9.1)
        if (m_selected >= 0 && static_cast<std::size_t>(m_selected) < m_item_layouts.size())
            m_selected_layout = m_item_layouts[static_cast<std::size_t>(m_selected)];
        else
            m_selected_layout = TextLayout{};
        // an open popup highlights the newly chosen row, because the highlight is the pending choice (§8.3)
        if (m_expanded && m_selected >= 0)
            m_hovered = m_selected;
        // the trigger text changed, so the frame is requested (§6.1)
        invalidate();
    }

    int ComboBox::selected() const {
        // the selected index is negative when nothing is chosen, which is the model sentinel (§8.3)
        return m_selected;
    }

    void ComboBox::set_label(std::string label) {
        // an unchanged label republishes nothing, because the accessible name is stable (§10.1)
        if (m_label == label)
            return;
        // the label is stored first, so the next semantic walk reads it (§10.1)
        m_label = std::move(label);
        // the name changed, so the semantic tree is republished (§10.1)
        invalidate();
    }

    const std::string& ComboBox::label() const {
        // the label is the stable name a reader announces (§10.1)
        return m_label;
    }

    void ComboBox::set_placeholder(std::string placeholder) {
        // an unchanged hint redraws nothing, because the empty trigger looks the same (§6.1)
        if (m_placeholder == placeholder)
            return;
        // the hint is stored first, so a remeasure walks a control that already carries it (§9.1)
        m_placeholder = std::move(placeholder);
        // the hint run is remeasured, because its extent is part of the trigger (§9.1)
        remeasure();
        // a changed hint may resize the trigger, so the layout passes run again (§8.2)
        invalidate_layout();
    }

    const std::string& ComboBox::placeholder() const {
        // the hint is what an empty control shows (§8)
        return m_placeholder;
    }

    bool ComboBox::expanded() const {
        // the state is what the popup paint and the semantic node read (§8.3, §10.1)
        return m_expanded;
    }

    void ComboBox::open() {
        // a disabled or empty control cannot open a list, because there is nothing to choose (§8.3)
        if (m_disabled || m_items.empty() || m_expanded)
            return;
        // the popup opens before its highlight row is chosen (§8.3)
        m_expanded = true;
        // the committed row is the initial highlight, while an empty selection starts at the first row (§8.3)
        m_hovered = m_selected >= 0 ? m_selected : 0;
        // a widget that joined a tree adds its popup to the topmost layer, which draws above every sibling (§8.3)
        if (UiNode* root = popup_root(); root != nullptr && m_popup == nullptr) {
            // the layer is created before it is moved into the root, so the widget keeps a stable pointer (§8.3)
            auto popup = std::make_unique<ComboBoxPopup>(*this);
            // the pointer is recorded before ownership moves to the root (§8.3)
            m_popup = popup.get();
            // the root owns the layer and paints it after the ordinary tree (§8.3)
            root->add_overlay(std::move(popup));
        }
        // an existing layer is made visible again, because the root keeps it between openings (§8.3)
        if (m_popup != nullptr)
            m_popup->set_visible(true);
        // the popup appears, so the frame is requested (§6.1)
        invalidate();
    }

    void ComboBox::close() {
        // a closed popup has nothing to change (§8.3)
        if (!m_expanded)
            return;
        // the popup leaves the screen without changing the selection (§8.3)
        m_expanded = false;
        // the layer stays owned by the root, but it no longer paints or takes input (§8.3)
        if (m_popup != nullptr)
            m_popup->set_visible(false);
        // the hover row belongs to the open popup, so it is cleared with it (§8.3)
        m_hovered = -1;
        // the popup disappears, so the frame is requested (§6.1)
        invalidate();
    }

    void ComboBox::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the control looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the close below walks a control that already carries it (§8.3)
        m_disabled = disabled;
        // a disabled control cannot leave a popup open, because the popup would refuse every press (§8.3)
        if (m_disabled)
            close();
        // the trigger fades and the state changes, so the frame is requested (§6.1)
        invalidate();
    }

    bool ComboBox::disabled() const {
        // the flag is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void ComboBox::set_on_selection_changed(std::function<void(int)> handler) {
        // the handler is swapped in place, because a later choice must run the new work (§8.3)
        m_on_selection_changed = std::move(handler);
    }

    void ComboBox::move_selection(int delta) {
        // an empty list has no row to move onto (§8.3)
        if (m_items.empty())
            return;
        // the open popup starts from the current highlight, or from the committed choice (§8.3)
        const int base = m_hovered >= 0 ? m_hovered : (m_selected < 0 ? (delta > 0 ? -1 : 0) : m_selected);
        // the target is clamped into the list, because the list has two ends (§8.3)
        const int next = std::clamp(base + delta, 0, static_cast<int>(m_items.size()) - 1);
        // the highlight moves without committing, because return is the commit (§8.3)
        m_hovered = next;
        // the popup highlight changed, so the frame is requested (§6.1)
        invalidate();
    }

    void ComboBox::choose(int index) {
        // an index outside the list chooses nothing, which a stale action may name (§8.3)
        if (index < 0 || static_cast<std::size_t>(index) >= m_items.size())
            return;
        // the index is captured before the close, because the close clears the hover row (§8.3)
        const int chosen = index;
        // the chosen row is stored before the popup leaves, so a reader sees the committed value (§8.3)
        set_selected(chosen);
        // the popup closes after the choice, which is what a completed selection means (§8.3)
        close();
        // a commit is a user choice even when it picks the row already highlighted, so the signal fires (§8.3)
        if (m_on_selection_changed)
            m_on_selection_changed(chosen);
    }
} // namespace albor
