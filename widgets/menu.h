#pragma once

#include <string>

#include "commands/shortcut.h"
#include "widgets/widget.h"

namespace albor
{
    // the shared action object a menu item binds, declared ahead of its definition (§18.1)
    class Command;

    // the text half of the framework, which a menu pass measures its rows through (§9.1)
    class TextEngine;

    // one row of a menu, which the menu adopts and lays out (§18.2)
    class MenuItem;

    // the thin rule that separates groups of menu items, which is not an action itself (§18.2)
    class MenuSeparator final : public Widget
    {
    public:
        // build a separator over the engine that settles its properties (§8)
        explicit MenuSeparator(BindingEngine* engine, std::string explicit_id = {});

        // override: a separator measures as one thin line, because it only separates (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: record the rule across the arranged width (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a separator is not a hit, because it carries no action (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: a separator is never a focus stop, because there is nothing to activate (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: a separator means nothing to a reader, so it announces no role (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;
    };

    // the ordered set of menu items and separators, which is the in-tree menu the shell hosts (§18.2)
    class Menu final : public Widget
    {
    public:
        // build an empty menu over the engines its rows and its text need (§8, §9.1)
        explicit Menu(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id = {});

        // adopt one command as a row, which is the shared action object the menu presents (§18.1)
        MenuItem& add_item(Command& command, const ShortcutTable* shortcuts = nullptr, Shortcut shortcut = {});

        // adopt a separator, which groups the rows around it (§18.2)
        MenuSeparator& add_separator();

        // override: measure the visible rows stacked inside the padding (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the visible rows top to bottom inside the padding (§8.2)
        void arrange(const Rect& bounds) override;

        // override: a menu announces itself as a group, because it frames its rows (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: the focus belongs to a row, because a menu is a container (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: arrow keys move between visible enabled rows, and escape drops the focus (§8.3)
        void dispatch_event(Event& event) override;

        // replace the inset between the menu edge and its rows (§8.2)
        void set_padding(float padding);

        // hand out the inset the menu applies (§8.2)
        [[nodiscard]] float padding() const;

        // replace the gap between two rows (§8.2)
        void set_spacing(float spacing);

        // hand out the gap between two rows (§8.2)
        [[nodiscard]] float spacing() const;

        // replace the menu face, which is the surface the rows sit on (§6.1)
        void set_background(Color background);

        // override: hand out the menu face (§6.1)
        [[nodiscard]] Color background() const override;

        // replace the corner radius, where a negative value keeps the hard edge (§6.2)
        void set_corner_radius(float radius);

        // override: hand out the radius the menu face draws (§6.2)
        [[nodiscard]] float corner_radius() const override;

    private:
        // move the focus one visible enabled row forward or backward, wrapping at the ends (§8.3)
        void focus_relative(int direction, Event& event);

        // the text engine a newly adopted row measures through (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the inset between the menu edge and its rows (§8.2)
        float m_padding = 4.0f;

        // the gap between two rows (§8.2)
        float m_spacing = 2.0f;

        // the menu surface, which the theme replaces by state when one is installed (§6.1)
        Color m_background{0.13f, 0.15f, 0.19f, 1.0f};

        // the radius the menu face draws, where a negative value is a hard rectangle (§6.2)
        float m_radius = 6.0f;
    };
} // namespace albor
