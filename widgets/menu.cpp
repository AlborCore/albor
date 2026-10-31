#include "widgets/menu.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "commands/command.h"
#include "widgets/menu_item.h"

namespace albor
{
    MenuSeparator::MenuSeparator(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void MenuSeparator::measure(const SizeConstraints& constraints, Size& out_size) {
        // the rule is one pixel tall and asks for no width, because the menu stretches it to the full row (§8.2)
        const Size content{.width = 0.0f, .height = 1.0f};
        // the offered constraints bind the row like any other child (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so the menu can query it (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void MenuSeparator::paint_content(RenderContext& context) {
        // an unarranged separator draws nothing, because there is no line to record (§6.2)
        if (bounds().empty())
            return;
        // the rule is a thin horizontal line at the middle of the row, which is what separates groups (§6.1)
        context.draw_rect(Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y + (bounds().height - 1.0f) * 0.5f}, Size{.width = bounds().width, .height = 1.0f}), Paint{.fill = Color::rgba(0.55f, 0.58f, 0.64f, 0.45f)});
    }

    bool MenuSeparator::hit_test(Point) const {
        // a separator carries no action, so no point belongs to it (§8.3)
        return false;
    }

    bool MenuSeparator::accepts_focus() const {
        // a separator is not a stop, because there is nothing to activate (§8.3)
        return false;
    }

    SemanticNode MenuSeparator::semantic_node() const {
        // the role is none, because a rule carries no meaning to a reader (§10.1)
        SemanticNode node{SemanticRole::None};
        // no name is announced, because the separator has no caption (§10.1)
        node.set_name({});
        // the node stays out of the semantic walk, which a role of none tells the tree (§10.1)
        return node;
    }

    Menu::Menu(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine) {}

    MenuItem& Menu::add_item(Command& command, const ShortcutTable* shortcuts, Shortcut shortcut) {
        // the row is built over the engines the menu already carries, so the caller does not repeat them (§9.1)
        auto item = std::make_unique<MenuItem>(engine(), m_text_engine);
        // the command is bound before adoption, so the first publication reaches the row at once (§18.1)
        item->set_command(&command);
        // the accelerator is spelled by the table when one is installed, which is the shared display rule (§18.5)
        if (shortcuts != nullptr)
            item->set_shortcut(*shortcuts, shortcut);
        // the raw pointer is kept for the caller, because the tree owns the object from here (§8.1)
        MenuItem* const adopted = item.get();
        // the row joins the menu, which is what lays it out and routes its events (§8.1)
        add_child(std::move(item));
        return *adopted;
    }

    MenuSeparator& Menu::add_separator() {
        // the rule is built over the engine, because it still participates in layout and paint (§8)
        auto separator = std::make_unique<MenuSeparator>(engine());
        // the raw pointer is kept for the caller, because the tree owns the object from here (§8.1)
        MenuSeparator* const adopted = separator.get();
        // the rule joins the menu between the rows it separates (§18.2)
        add_child(std::move(separator));
        return *adopted;
    }

    void Menu::measure(const SizeConstraints& constraints, Size& out_size) {
        // the rows see only the space the padding leaves, which keeps the menu from overflowing (§8.2)
        const SizeConstraints inner = SizeConstraints::at_most(Size{.width = std::max(0.0f, constraints.maximum.width - m_padding * 2.0f), .height = std::max(0.0f, constraints.maximum.height - m_padding * 2.0f)});
        // the content accumulates the widest visible row and the stacked height (§8.2)
        Size content{};
        // a gap joins two visible rows, while the first one starts at the top (§8.2)
        bool first = true;
        for (const auto& child : children()) {
            // a hidden row takes no space, because the menu does not show it (§18.2)
            if (!child->visible())
                continue;
            // each row measures inside the inset space, because that is what the menu gives it (§8.2)
            Size child_size{};
            child->measure(inner, child_size);
            // the widest row decides the content width (§8.2)
            content.width = std::max(content.width, child_size.width);
            // a gap separates this row from the previous visible one (§8.2)
            if (!first)
                content.height += m_spacing;
            // the row height joins the stack, which is the column contract (§8.2)
            content.height += child_size.height;
            // the first visible row is recorded, so the second one takes a gap (§8.2)
            first = false;
        }
        // the insets are added back, because the menu surrounds the rows it frames (§8.2)
        const Size total{.width = content.width + m_padding * 2.0f, .height = content.height + m_padding * 2.0f};
        // the offered constraints are final, because the parent has the last word (§8.2)
        m_measured_size = constraints.constrain(total);
        // the preferred size follows the measured one, so a parent can query it (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Menu::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, which the rows sit inside (§8.2)
        m_bounds = bounds;
        // the insets are capped at the extent, so a wide padding still keeps rows inside (§8.2)
        const float left = std::min(m_padding, bounds.width);
        const float top = std::min(m_padding, bounds.height);
        const float right = std::min(m_padding, std::max(0.0f, bounds.width - left));
        const float bottom = std::min(m_padding, std::max(0.0f, bounds.height - top));
        // the rows are placed top to bottom inside the inset frame (§8.2)
        float y = bounds.y + top;
        // the width is what both insets leave (§8.2)
        const float width = std::max(0.0f, bounds.width - left - right);
        // the bottom inset bounds the stack, which keeps a row from painting past the menu (§8.2)
        const float bottom_edge = bounds.y + bounds.height - bottom;
        // the first visible row takes the top edge, while a gap precedes every later one (§8.2)
        bool first = true;
        for (const auto& child : children()) {
            // a hidden row is not placed, because the menu draws nothing for it (§18.2)
            if (!child->visible())
                continue;
            // a gap separates this row from the previous visible one (§8.2)
            if (!first)
                y += m_spacing;
            // the row keeps its measured height, because a menu does not stretch its rows (§8.2)
            const float height = std::min(child->measured_size().height, std::max(0.0f, bottom_edge - y));
            // the row takes the full inner width, because a menu row is a full-width target (§8.2)
            arrange_if_changed(*child, Rect::from_origin_size(Point{.x = bounds.x + left, .y = y}, Size{.width = width, .height = height}));
            // the next row starts below this one (§8.2)
            y += height;
            // the first visible row is recorded, so the next one takes a gap (§8.2)
            first = false;
        }
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    SemanticNode Menu::semantic_node() const {
        // the role is a group, because a menu frames the rows it owns (§10.1)
        SemanticNode node{SemanticRole::Group};
        // the author id travels with the node, because automation selects by it (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the id is the name, because a menu carries no caption of its own (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement at the menu (§10.1)
        node.set_bounds(bounds());
        // a container is not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        return node;
    }

    bool Menu::accepts_focus() const {
        // a menu is a container, so the focus belongs to one of its rows (§8.3)
        return false;
    }

    void Menu::dispatch_event(Event& event) {
        // a menu only handles unmodified keys, because a modified key is an application shortcut (§18.5)
        if (event.kind != EventKind::KeyDown || !event.modifiers.none())
            return;
        // the down arrow moves to the next row, and the up arrow to the previous one (§8.3)
        if (event.key == Key::ArrowDown) {
            focus_relative(1, event);
            return;
        }
        if (event.key == Key::ArrowUp) {
            focus_relative(-1, event);
            return;
        }
        // escape drops the row focus, which is the dismissal a menu offers without a popup surface (§8.3)
        if (event.key != Key::Escape)
            return;
        // the focused row is cleared, because escape ends the traversal (§8.3)
        for (const auto& child : children()) {
            if (auto* item = dynamic_cast<MenuItem*>(child.get()); item != nullptr && item->focused()) {
                // the ring goes away with the focus, which repaints the row (§8.3)
                item->set_focused(false);
                // the key is consumed, so no ancestor also dismisses (§8.3)
                event.handled = true;
                return;
            }
        }
    }

    void Menu::focus_relative(int direction, Event& event) {
        // the visible enabled rows are the only stops, because a disabled row is skipped (§18.2)
        std::vector<MenuItem*> stops;
        for (const auto& child : children()) {
            auto* item = dynamic_cast<MenuItem*>(child.get());
            if (item != nullptr && item->visible() && !item->disabled())
                stops.push_back(item);
        }
        // a menu with no stop has nothing to move, so the key belongs to the application (§8.3)
        if (stops.empty())
            return;
        // the current row is found, because the arrow moves relative to what the reader is on (§8.3)
        const auto current = std::find_if(stops.begin(), stops.end(), [](const MenuItem* item) { return item->focused(); });
        // a menu with no focused row starts at the first or last stop, which is the traversal entry (§8.3)
        std::size_t next = direction > 0 ? 0 : stops.size() - 1;
        // a focused row moves one stop in the direction, wrapping at the ends (§8.3)
        if (current != stops.end()) {
            // the distance from the start names the current index, because the list is contiguous (§8.3)
            const std::size_t index = static_cast<std::size_t>(current - stops.begin());
            // the direction moves forward or backward with a wrap, which is what a menu does (§8.3)
            next = (index + (direction > 0 ? 1 : stops.size() - 1)) % stops.size();
        }
        // the previous row drops its ring even when it became disabled or hidden, which is the stale focus case (§8.3)
        for (const auto& child : children())
            if (auto* item = dynamic_cast<MenuItem*>(child.get()); item != nullptr)
                item->set_focused(false);
        // the chosen row takes the focus, which is the one stop the reader is on (§8.3)
        stops.at(next)->set_focused(true);
        // the key is consumed, so no ancestor also moves the focus (§8.3)
        event.handled = true;
    }

    void Menu::set_padding(float padding) {
        // the inset is clamped at zero, because a negative one would pull rows outside the menu (§8.2)
        const float clamped = std::max(0.0f, padding);
        // an unchanged inset remeasures nothing, because the layout did not change (§8.2)
        if (m_padding == clamped)
            return;
        // the inset is stored first, so the invalidation walks a menu that already carries it (§8.2)
        m_padding = clamped;
        // a new inset changes the space the rows receive, so both layout passes run (§8.2)
        invalidate_layout();
    }

    float Menu::padding() const { return m_padding; }

    void Menu::set_spacing(float spacing) {
        // the gap is clamped at zero, because a negative one would overlap rows (§8.2)
        const float clamped = std::max(0.0f, spacing);
        // an unchanged gap remeasures nothing, because the stack did not change (§8.2)
        if (m_spacing == clamped)
            return;
        // the gap is stored first, so the invalidation walks a menu that already carries it (§8.2)
        m_spacing = clamped;
        // a new gap changes the stacked extent, so both layout passes run (§8.2)
        invalidate_layout();
    }

    float Menu::spacing() const { return m_spacing; }

    void Menu::set_background(Color background) {
        // an unchanged face redraws nothing, because the menu looks the same (§6.1)
        if (m_background == background)
            return;
        // the face is stored first, so the invalidation walks a menu that already paints it (§6.1)
        m_background = background;
        // a face change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color Menu::background() const {
        // a themed menu takes the panel face, which is the surface a group sits on (§6.1)
        if (theme() != nullptr)
            return resolve(theme()->palette().panel_background, m_background);
        // an unthemed menu keeps its own face, which is the surface it always had (§6.1)
        return m_background;
    }

    void Menu::set_corner_radius(float radius) {
        // the radius is clamped at zero, because a negative one would mean nothing (§6.2)
        const float clamped = std::max(0.0f, radius);
        // an unchanged radius redraws nothing, because the face looks the same (§6.2)
        if (m_radius == clamped)
            return;
        // the radius is stored first, so the invalidation walks a menu that already draws it (§6.2)
        m_radius = clamped;
        // a radius change is visual only, so the layout passes are left alone (§6.2)
        invalidate();
    }

    float Menu::corner_radius() const { return m_radius; }
} // namespace albor
