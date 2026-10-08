#pragma once

#include <functional>
#include <string>
#include <utility>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // the widget that performs an action when it is pressed, which is a push control (§8)
    class Button final : public Widget
    {
    public:
        // build a button over the engines it reads, with a default plate and an empty label (§8, AD-14)
        explicit Button(BindingEngine* engine, TextEngine* text_engine, std::string label = {}, std::string explicit_id = {});

        // measure the label through the text engine and give the button a default extent, because a push control must be grabbable (§8.2, AD-14)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the button, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the filled plate and the centred label, which is what a push control looks like (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a button is a hit only where it is visible and enabled (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: run the click handler on a pointer press inside the bounds, which is what activates the button (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the button with its label and an activate action (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the drawn label, which is also the accessible name (§10.1)
        void set_label(std::string label);

        // hand out the drawn label (§10.1)
        [[nodiscard]] const std::string& label() const;

        // replace the work a press performs, which is what the activate action and the click path both run (§8.3, §10.1)
        void set_on_click(std::function<void()> handler);

        // refuse or accept input, which redraws and changes what a press does (§8.3)
        void set_disabled(bool disabled);

        // report whether the button refuses input (§8.3)
        [[nodiscard]] bool disabled() const override;

        // replace the plate fill, which redraws without changing the extent (§6.1)
        void set_background(Color background);

        // override: hand out the plate fill, which the base records before the label (§6.2)
        [[nodiscard]] Color background() const override;

    private:
        // the text engine that measures the label, which the application owns (§9.1, AD-14)
        TextEngine* m_text_engine = nullptr;

        // the run the label measures to, because the plate is as large as the text it carries (§9.1)
        TextLayout m_label_layout;

        // the drawn label, which is also the accessible name (§10.1)
        std::string m_label;

        // the work a press performs (§8.3)
        std::function<void()> m_on_click;

        // the fill colour of the plate (§6.1)
        Color m_background{0.20f, 0.55f, 0.90f, 1.0f};

        // whether the button refuses input (§8.3)
        bool m_disabled = false;
    };
} // namespace albor
