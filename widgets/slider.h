#pragma once

#include <functional>
#include <utility>

#include "commands/command_invocation.h"
#include "widgets/widget.h"

namespace albor
{
    // the widget that drags a thumb along a track, which is a continuous value control (§8)
    class Slider final : public Widget
    {
    public:
        // build a slider at zero over the default zero to one range (§8)
        explicit Slider(BindingEngine* engine, std::string explicit_id = {});

        // measure a wide and short control, because a slider is horizontal (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the slider, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the track and the thumb, which is what a slider looks like (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a slider is a hit only where it is visible and enabled (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: move the value on a pointer press inside the bounds, which is what dragging means (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the slider with its value and a set action (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the value, which is clamped into the range because a slider cannot leave it (§8)
        void set_value(float value);

        // hand out the value, which the render pass and the semantic node both read (§8)
        [[nodiscard]] float value() const;

        // replace the range, which re-clamps the value because a new range can exclude it (§8)
        void set_range(float min, float max);

        // hand out the bottom of the range (§8)
        [[nodiscard]] float minimum() const;

        // hand out the top of the range (§8)
        [[nodiscard]] float maximum() const;

        // replace the work a change performs, which receives the value the slider moved to (§8.3)
        void set_on_change(std::function<void(float)> handler);

        // move the value by a signed amount and report the change, which is what a key press on a focused slider does (§8.3)
        void step(float delta);

        // refuse or accept input, which redraws and changes what a press does (§8.3)
        void set_disabled(bool disabled);

        // report whether the slider refuses input (§8.3)
        [[nodiscard]] bool disabled() const override;

        // replace the track colour, which redraws without changing the extent (§6.1)
        void set_track_color(Color color);

        // replace the thumb colour, which redraws without changing the extent (§6.1)
        void set_thumb_color(Color color);

    protected:
        // override: a slider presents no label of its own, so it follows the enablement and the applicability (§18.7)
        void apply_command_state(const CommandState& state) override;

    private:
        // the track colours the slider resolves through, which are the installed palette when a theme is present (§6.1)
        [[nodiscard]] const Color* track_colors() const;

        // the thumb colours the slider resolves through, which are the installed palette when a theme is present (§6.1)
        [[nodiscard]] const Color* thumb_colors() const;

        // move the value to the position the pointer names, which a press and a drag share (§8.3)
        void jump_to(Point logical_position);

        // the work a change performs, which receives the new value (§8.3)
        std::function<void(float)> m_on_change;

        // the bottom of the range, which the value never falls below (§8)
        float m_minimum = 0.0f;

        // the top of the range, which the value never rises above (§8)
        float m_maximum = 1.0f;

        // the current value, which lives inside the range (§8)
        float m_value = 0.0f;

        // the colour of the track (§6.1)
        Color m_track_color{0.30f, 0.32f, 0.36f, 1.0f};

        // the colour of the thumb (§6.1)
        Color m_thumb_color{0.95f, 0.96f, 0.98f, 1.0f};

        // whether the slider refuses input (§8.3)
        bool m_disabled = false;

        // whether a pointer press is dragging the thumb, which the moves rely on (§8.3)
        bool m_dragging = false;
    };
} // namespace albor
