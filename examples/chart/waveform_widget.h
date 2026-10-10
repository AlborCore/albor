#pragma once

#include <memory>
#include <string>

#include "chart/waveform_view.h"
#include "core/paint.h"
#include "examples/chart/waveform_data.h"

namespace albor
{
    // the hand written implementation of the chart facade, which supplies the storage behind the accessors (§11.2, AD-17)
    class WaveformWidget final : public WaveformView
    {
    public:
        // build the widget over the engine, which the generated facade forwards to the widget base (§11.2, §8.1)
        explicit WaveformWidget(BindingEngine* engine, std::string explicit_id = {});

        // destroy the widget, which ends its lifetime in the tree (§11.2)
        ~WaveformWidget() override;

        // the data slot, which stores the immutable snapshot it is handed (§11.2, §13.2)
        void set_data(std::shared_ptr<const WaveformData> value) override;

        // the data accessor, which hands the stored snapshot out to a binding (§11.2, §13.2)
        [[nodiscard]] std::shared_ptr<const WaveformData> data() const override;

        // the colour slot, which stores the line colour it is handed (§11.2)
        void set_line_color(Color value) override;

        // the colour accessor, which hands the stored colour out to a binding (§11.2)
        [[nodiscard]] Color line_color() const override;

        // hand out the semantic node of the widget, which maps the declared chart role onto a known one (§10.1, §11.2)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // report that a selection landed at the given time position, which raises the declared signal (§11.2)
        void raise_selection(float time_pos);

    private:
        // the sampled waveform, which a painted line and the bindings read (§11.2, §13.2)
        std::shared_ptr<const WaveformData> m_data;

        // the line colour, which the empty widget keeps at the declared default (§11.2)
        Color m_line_color = Color::rgb(0.0f, 1.0f, 0.0f);
    };
} // namespace albor
