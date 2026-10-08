#include "examples/reactive-counter/counter_panel.h"

#include <algorithm>
#include <string>

namespace albor
{
    CounterPanel::CounterPanel(BindingEngine* engine, TextEngine* text_engine) :
        Component(engine, "counter_panel"), m_text_engine(text_engine) {
        // the three properties are registered first, so they all share this component base (§7.2, AD-04b)
        register_property(count);
        register_property(label);
        register_property(accent);
        // the label is bound to the count, so a count change re-evaluates it eagerly (§7.3, AD-04c)
        label.bind([this] { return "count " + std::to_string(count.get()); }, {count.id()});
        // the accent is bound to the count too, so the colour follows the same change (§7.3, AD-04c)
        accent.bind(
            [this] {
                // the accent cycles through three hues as the count grows, which makes a change visible (§7.1)
                const int phase = count.get() % 3;
                if (phase == 1)
                    return Color::rgb(0.20f, 0.75f, 0.45f);
                if (phase == 2)
                    return Color::rgb(0.90f, 0.55f, 0.20f);
                return Color::rgb(0.30f, 0.55f, 0.95f);
            },
            {count.id()});
    }

    void CounterPanel::measure(const SizeConstraints& constraints, Size& out_size) {
        // the panel fills whatever it is offered, because a root component covers the window (§8.2)
        UiNode::measure(constraints, out_size);
        // the preferred size is the measured one, which the parent has already clamped (§8.2)
        m_preferred_size = out_size;
    }

    void CounterPanel::arrange(const Rect& bounds) {
        // the assigned bounds are recorded before the children are arranged (§8.2)
        UiNode::arrange(bounds);
    }

    Rect CounterPanel::bar_rect() const {
        // the bar grows with the count but never past the panel width (§6.1)
        const float available = std::max(0.0f, bounds().width - m_padding * 2.0f);
        const float filled = available * std::min(1.0f, static_cast<float>(count.get()) / 10.0f);
        return Rect::from_origin_size(Point{bounds().x + m_padding, bounds().y + m_padding}, Size{filled, m_bar_height});
    }

    Point CounterPanel::label_origin() const {
        // the label box sits under the bar, so the two never overlap (§6.1)
        return Point{bounds().x + m_padding, bounds().y + m_padding + m_bar_height + 24.0f};
    }

    void CounterPanel::render(RenderContext& context) {
        // the panel background covers the whole component (§6.1)
        Paint background_paint;
        background_paint.fill = background();
        context.draw_rect(bounds(), background_paint);
        // the bar carries the bound accent, which is what makes a count change visible (§6.1, §7.3)
        Paint bar_paint;
        bar_paint.fill = accent.get();
        context.draw_rect(bar_rect(), bar_paint);
        // the label carries the bound text, which re-evaluates with the count (§6.1, §7.3)
        Paint text_paint;
        text_paint.fill = Color::rgb(0.92f, 0.94f, 0.96f);
        // the label is measured where it is drawn, because its binding changes on its own (§7.3, §9.1)
        if (m_text_engine != nullptr) {
            // the measurement is a cached lookup once the run has been shaped, so a repeat frame pays no shaping (§9.1)
            const TextLayout label_layout = m_text_engine->measure(label.get(), 0.0f, 20.0f);
            // an unmeasured run draws nothing, because it carries no glyph (§6.2)
            if (!label_layout.empty())
                context.draw_text(label_layout, label_origin(), text_paint);
        }
        // the children draw over the panel, which a composed tree needs (§6.1)
        UiNode::render(context);
    }

    void CounterPanel::dispatch_event(Event& event) {
        // a pointer press inside the panel increments the count, which is the interaction of the example (§8.3)
        if (event.kind == EventKind::PointerDown && hit_test(event.position)) {
            increment();
            event.handled = true;
            return;
        }
        // a key press resets the count, so the example offers a second way to drive it (§8.3)
        if (event.kind == EventKind::KeyDown) {
            reset();
            event.handled = true;
            return;
        }
        // anything else belongs to the children, which the dispatcher already offered before the panel (§8.3)
    }

    Color CounterPanel::background() const {
        // the panel is dark so the accent and the label both read against it (§6.1)
        return Color::rgb(0.08f, 0.09f, 0.11f);
    }

    void CounterPanel::increment() {
        // the imperative write breaks the binding on this property, which has none (§7.3, AD-24)
        count.set(count.get() + 1);
        // the change reaches the screen through the invalidation the pipeline already installed (§6.1)
        invalidate();
    }

    void CounterPanel::reset() {
        // the imperative write returns the count to its initial value (§7.3, AD-24)
        count.set(0);
        // the change reaches the screen through the same invalidation path (§6.1)
        invalidate();
    }
} // namespace albor
