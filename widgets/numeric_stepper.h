#pragma once

#include <functional>
#include <string>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // the bounded number a user steps with a minus and a plus, which is a spinner (§8)
    class NumericStepper final : public Widget
    {
    public:
        // build a stepper over the engines it reads, with the default unit step (§8)
        NumericStepper(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id = {});

        // measure the value run and the two step zones, because the plate has to hold both (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the plate, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the plate, the value and the two step marks (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a stepper is a hit over its whole plate (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: claim a press, a wheel notch or a stepping key (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the value and the two stepping actions (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the value, which is clamped into the minimum and maximum (§8)
        void set_value(float value);

        // hand out the value the plate shows (§8)
        [[nodiscard]] float value() const;

        // replace the lower bound, which clamps the value and the upper bound (§8)
        void set_minimum(float minimum);

        // hand out the lower bound (§8)
        [[nodiscard]] float minimum() const;

        // replace the upper bound, which clamps the value and the lower bound (§8)
        void set_maximum(float maximum);

        // hand out the upper bound (§8)
        [[nodiscard]] float maximum() const;

        // replace the amount one step moves, which is clamped above zero (§8)
        void set_step(float step);

        // hand out the amount one step moves (§8)
        [[nodiscard]] float step() const;

        // refuse or accept input, which is what a disabled stepper does (§8.3)
        void set_disabled(bool disabled);

        // report whether the stepper refuses input (§8.3)
        [[nodiscard]] bool disabled() const override;

        // install the work told about every value change, which is the signal (§8.3)
        void set_on_change(std::function<void(float)> handler);

    private:
        // move the value by a number of steps, which a press, a wheel and a key share (§8.3)
        void step_by(float steps);

        // apply a value that was already clamped, which the range setters use without replacing the request (§8)
        void apply_clamped_value(float value);

        // rebuild the text the plate draws, which the value formatting owns (§9.1)
        void update_value_text();

        // format the value as the shortest run that still names it (§9.1)
        [[nodiscard]] std::string format_value() const;

        // hand out the zone a point lands on: minus, plus or neither (§8.3)
        [[nodiscard]] int zone_at(Point logical_point) const;

        // hand out the width one step zone owns, which shrinks with the plate so the zones never overlap (§8.2)
        [[nodiscard]] float step_zone_width() const;

        // hand out the size the value is measured at (§9.1)
        [[nodiscard]] float label_font_size() const;

        // hand out the plate and label colours for the current state (§6.1)
        [[nodiscard]] Color plate_color() const;
        [[nodiscard]] Color label_color() const;

        // the text engine that measures the value, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the value measured to, because the plate has to hold it (§9.1)
        TextLayout m_value_layout;

        // the formatted value, which the plate draws and the semantic node announces (§9.1, §10.1)
        std::string m_value_text;

        // the value the last writer requested, which survives a temporary clamp so a wider range can recover it (§8)
        float m_requested_value = 0.0f;

        // the current value, which is clamped into the bounds (§8)
        float m_value = 0.0f;

        // the lower bound the value may take (§8)
        float m_minimum = 0.0f;

        // the upper bound the value may take (§8)
        float m_maximum = 100.0f;

        // the amount one step moves (§8)
        float m_step = 1.0f;

        // whether the stepper refuses input (§8.3)
        bool m_disabled = false;

        // the work told about every value change (§8.3)
        std::function<void(float)> m_on_change;
    };
} // namespace albor
