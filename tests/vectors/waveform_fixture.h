#pragma once

#include <memory>
#include <string>

#include "albor/core/paint.h"
#include "albor/semantics/semantic_node.h"
#include "waveform/waveform_view.h"

namespace albor
{
    // the data the snapshot carries, which the suite publishes to prove the binding reaches the native (§13.2)
    struct WaveformData
    {
        // the sampled peak, which the suite compares after a publish (§13.2)
        float peak = 0.0f;
    };

    // the concrete view behind the generated facade, which is what the factory returns (§11.2, AD-17)
    class WaveformViewFixture final : public WaveformView
    {
    public:
        // build the view over the engine, which the generated facade forwards to the widget base (§11.2)
        explicit WaveformViewFixture(BindingEngine* engine, std::string explicit_id = {});

        // destroy the view, which ends its lifetime in the tree (§11.2)
        ~WaveformViewFixture() override;

        // the data slot, which stores the immutable snapshot it is handed (§11.2, §13.2)
        void set_data(std::shared_ptr<const WaveformData> value) override;

        // the data accessor, which hands the stored snapshot out to a binding (§11.2, §13.2)
        [[nodiscard]] std::shared_ptr<const WaveformData> data() const override;

        // the colour slot, which stores the line colour it is handed (§11.2)
        void set_line_color(Color value) override;

        // the colour accessor, which hands the stored colour out to a binding (§11.2)
        [[nodiscard]] Color line_color() const override;

        // the zoom slot, which stores the scale it is handed (§11.2)
        void set_zoom(float value) override;

        // the zoom accessor, which hands the stored scale out to a binding (§11.2)
        [[nodiscard]] float zoom() const override;

        // hand out the semantic node, which names the fixture as a drawing surface (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // raise the selection signal, which the suite calls to prove the generated handler runs (§11.2)
        void raise_selection(float time_pos);

    private:
        // the sampled waveform, which the binding writes (§11.2, §13.2)
        std::shared_ptr<const WaveformData> m_data;

        // the line colour, which the empty view keeps at the declared default (§11.2)
        Color m_line_color = Color::rgb(0.0f, 1.0f, 0.0f);

        // the zoom, which the empty view keeps at the declared default (§11.2)
        float m_zoom = 1.0f;
    };

    // create the concrete view behind the facade, which a generated ui body calls (§11.2, AD-17)
    [[nodiscard]] std::unique_ptr<WaveformView> create_WaveformView(BindingEngine* engine);
} // namespace albor
