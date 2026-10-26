#pragma once

#include <functional>
#include <string>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // one choice in a radio group, which carries a circle and the label beside it (§8)
    class RadioButton final : public Widget
    {
    public:
        // build an unchecked choice over the engines it reads (§8)
        RadioButton(BindingEngine* engine, TextEngine* text_engine, int value = 0, std::string label = {}, std::string explicit_id = {});

        // measure the circle and the label, because a choice is one row of content (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the choice, which the group does (§8.2)
        void arrange(const Rect& bounds) override;

        // record the circle, its dot and the label (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a choice is a hit over its whole row, because the label is part of the target (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: claim a press or a space, which selects this choice (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the choice with its single-selection state (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the value the group matches against (§8)
        void set_value(int value);

        // hand out the value the group matches against (§8)
        [[nodiscard]] int value() const;

        // replace the label the circle carries, which is also the accessible name (§8, §10.1)
        void set_label(std::string label);

        // hand out the label the circle carries (§10.1)
        [[nodiscard]] const std::string& label() const;

        // set or clear the chosen state, which only the group writes (§8)
        void set_checked(bool checked);

        // report whether this choice is the chosen one (§8)
        [[nodiscard]] bool checked() const;

        // refuse or accept input, which is what a disabled choice does (§8.3)
        void set_disabled(bool disabled);

        // report whether the choice refuses input (§8.3)
        [[nodiscard]] bool disabled() const override;

        // install the work a selection performs, which the group uses to own the choice (§8.3)
        void set_on_select(std::function<void()> handler);

    private:
        // select the choice, which a press and the semantic action share (§8.3, §10.1)
        void activate();

        // hand out the size the label is measured at (§9.1)
        [[nodiscard]] float label_font_size() const;

        // hand out the circle the control aims at (§8.2)
        [[nodiscard]] Rect circle_rect() const;

        // hand out the outline colours, which the checkbox palette carries for a choice (§6.1)
        [[nodiscard]] const Color* outline_colors() const;

        // hand out the fill colours of a chosen circle (§6.1)
        [[nodiscard]] const Color* fill_colors() const;

        // hand out the dot colour, which reads over the accent fill (§8.4)
        [[nodiscard]] Color dot_color() const;

        // the text engine that measures the label, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the label measured to, because the row is as wide as the text it carries (§9.1)
        TextLayout m_label_layout;

        // the label the circle carries, which is also the accessible name (§10.1)
        std::string m_label;

        // the value the group matches this choice against (§8)
        int m_value = 0;

        // whether this choice is the chosen one (§8)
        bool m_checked = false;

        // whether the choice refuses input (§8.3)
        bool m_disabled = false;

        // the work a selection performs, which the group installs (§8.3)
        std::function<void()> m_on_select;
    };
} // namespace albor
