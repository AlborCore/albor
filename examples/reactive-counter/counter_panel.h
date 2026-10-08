#pragma once

#include <cstddef>
#include <string>

#include "core/constraints.h"
#include "core/geometry.h"
#include "core/paint.h"
#include "render/render_context.h"
#include "runtime/binding_engine.h"
#include "runtime/property.h"
#include "text/text_engine.h"
#include "ui/component.h"

namespace albor
{
    // the root of a counter component, which demonstrates the reactive pipeline end to end (§7, §14 phase 2)
    class CounterPanel final : public Component
    {
    public:
        // build the panel over the engine that settles its bindings and the text engine (§7.2, §9.1)
        CounterPanel(BindingEngine* engine, TextEngine* text_engine);

        // the reactive count a timer or a click increments (§7.1)
        Property<int> count;

        // the label bound to the count, which the panel draws (§7.1, §7.3)
        Property<std::string> label;

        // the accent bound to the count, which the panel paints (§7.1, §7.3)
        Property<Color> accent;

        void measure(const SizeConstraints& constraints, Size& out_size) override;

        void arrange(const Rect& bounds) override;

        void render(RenderContext& context) override;

        [[nodiscard]] Color background() const override;

        void dispatch_event(Event& event) override;

        // increment the count, which is what a click handler calls (§7.1)
        void increment();

        // reset the count to zero, which is what a reset handler calls (§7.1)
        void reset();

    private:
        // the rectangle the count bar occupies, which grows with the count (§6.1)
        [[nodiscard]] Rect bar_rect() const;

        // the top left of the label box, which sits under the bar (§6.1)
        [[nodiscard]] Point label_origin() const;

        // the engine that measures the bound label, which is null when the application installed none (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the height of the count bar in logical pixels (§6.1)
        float m_bar_height = 40.0f;

        // the distance between the panel edge and the bar (§6.1)
        float m_padding = 24.0f;
    };
} // namespace albor
