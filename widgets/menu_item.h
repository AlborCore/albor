#pragma once

#include <functional>
#include <string>

#include "commands/command_invocation.h"
#include "commands/command_state.h"
#include "commands/shortcut.h"
#include "render/text_layout.h"
#include "widgets/widget.h"

namespace albor
{
    // the text half of the framework, which a menu item measures its label through (§9.1)
    class TextEngine;

    // one row of a menu, which presents the command it binds rather than holding state of its own (§18.1, §18.7)
    class MenuItem final : public Widget
    {
    public:
        // build an item over the engines it reads, with an optional authored label (§8, §18.1)
        explicit MenuItem(BindingEngine* engine, TextEngine* text_engine, std::string label = {}, std::string explicit_id = {});

        // measure the label and the accelerator text, which is the full row a menu puts down (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the row, which the owning menu does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the hover plate, the label and the accelerator text beside it (§6.1)
        void paint_content(RenderContext& context) override;

        // override: an item is a hit only while it is visible and enabled (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: a press invokes the bound command through the menu route (§8.3, §18.3)
        void dispatch_event(Event& event) override;

        // override: a menu item is a focus stop while it is enabled, which is what arrow traversal moves (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: announce the item as a menu item with the action a click runs (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the authored label, which stands in until a bound command publishes one (§18.7)
        void set_label(std::string label);

        // hand out the label the row currently presents (§18.7)
        [[nodiscard]] const std::string& label() const;

        // present the accelerator the table spells, which is the same text a shortcut route uses (§18.5)
        void set_shortcut(const ShortcutTable& table, Shortcut shortcut);

        // present an accelerator spelling supplied directly, which a host that has no table uses (§18.5)
        void set_shortcut_text(std::string text);

        // hand out the accelerator spelling the row draws (§18.5)
        [[nodiscard]] const std::string& shortcut_text() const;

        // replace the work a press performs when no command is bound (§8.3)
        void set_on_invoke(std::function<void()> handler);

        // refuse or accept input, which a bound command overrides with its own enablement (§18.7)
        void set_disabled(bool disabled);

        // override: report whether the row refuses activation (§18.7)
        [[nodiscard]] bool disabled() const override;

        // override: hand out the row plate, which is the hover fill over the menu surface (§6.1)
        [[nodiscard]] Color background() const override;

    protected:
        // override: a bound command publishes the label and both applicability states (§18.7)
        void apply_command_state(const CommandState& state) override;

    private:
        // invoke the bound command or the local handler, which is what both input and semantics run (§18.3)
        void activate(InvocationSource source);

        // the text engine that measures the label and the accelerator (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the label measures to, which decides the row height (§9.1)
        TextLayout m_label_layout;

        // the run the accelerator measures to, which sits at the trailing edge (§9.1)
        TextLayout m_shortcut_layout;

        // the label the row presents, which a bound command replaces (§18.7)
        std::string m_label;

        // the accelerator text the row presents, which is what the shortcut table spelled (§18.5)
        std::string m_shortcut;

        // the work a press performs when no command is bound (§8.3)
        std::function<void()> m_on_invoke;

        // whether the row refuses input while no command owns applicability (§8.3)
        bool m_disabled = false;

        // the size the row's two runs are measured at, which follows the theme typography (§8.4, §9.1)
        [[nodiscard]] float label_font_size() const;
    };
} // namespace albor
