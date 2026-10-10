#pragma once

#include <functional>

#include "widgets/widget.h"

namespace albor
{
    // the widget that toggles a boolean on a press, which is a checkable control (§8)
    class CheckBox final : public Widget
    {
    public:
        // build an unchecked box over the engine that settles its properties (§8)
        explicit CheckBox(BindingEngine* engine, std::string explicit_id = {});

        // measure a fixed square, because a checkbox has no intrinsic text (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the box, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the outline, filled when the box is checked (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a box is a hit only where it is visible and enabled (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: flip the state on a pointer press inside the bounds, which is what toggles it (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the box with its check state and a toggle action (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // set or clear the checked state without a press, because a binding writes it (§8.3)
        void set_checked(bool checked);

        // report whether the box is checked (§8.3)
        [[nodiscard]] bool checked() const;

        // replace the work a toggle performs, which receives the state the box moved to (§8.3)
        void set_on_toggle(std::function<void(bool)> handler);

        // refuse or accept input, which redraws and changes what a press does (§8.3)
        void set_disabled(bool disabled);

        // report whether the box refuses input (§8.3)
        [[nodiscard]] bool disabled() const override;

        // replace the outline colour, which redraws without changing the extent (§6.1)
        void set_outline(Color outline);

        // hand out the outline colour (§6.1)
        [[nodiscard]] Color outline() const;

        // replace the fill a checked box carries, which redraws without changing the extent (§6.1)
        void set_check_fill(Color check_fill);

    private:
        // the outline width the box strokes with, which comes from the theme when one is installed (§6.1)
        [[nodiscard]] float outline_width() const;

        // the fill colours the box resolves through, which are the installed palette when a theme is present (§6.1)
        [[nodiscard]] const Color* fill_colors() const;

        // the outline colours the box resolves through, which are the installed palette when a theme is present (§6.1)
        [[nodiscard]] const Color* outline_colors() const;

        // the work a toggle performs, which receives the new state (§8.3)
        std::function<void(bool)> m_on_toggle;

        // the colour of the box outline (§6.1)
        Color m_outline{0.85f, 0.87f, 0.90f, 1.0f};

        // the colour a checked box fills with (§6.1)
        Color m_check_fill{0.20f, 0.55f, 0.90f, 1.0f};

        // whether the box is checked, which is what the semantic check state reports (§10.1)
        bool m_checked = false;

        // whether the box refuses input (§8.3)
        bool m_disabled = false;
    };
} // namespace albor
