#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "chart/waveform_view.h"
#include "core/geometry.h"
#include "core/paint.h"
#include "examples/chart/waveform_data.h"
#include "render/line_batch.h"
#include "render/resource_factory.h"

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

        // override: replay the retained trace under the transform the current plot implies (§6.4, AD-06)
        void paint_content(RenderContext& context) override;

        // hand out the semantic node, which maps the declared chart role onto a known one (§10.1, §11.2)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // report that a selection landed at the given time position, which raises the declared signal (§11.2)
        void raise_selection(float time_pos);

        // hand out the retained trace, which a caller reads to see what the frames replay (§6.4, AD-06)
        [[nodiscard]] std::shared_ptr<const LineBatch> trace() const;

        // hand out how many times the trace was rebuilt, which stays at one while the data holds (§6.4, AD-06)
        [[nodiscard]] std::size_t trace_build_count() const;

        // hand out how many frames replayed the retained batch, which grows with the frame count (§6.4, AD-06)
        [[nodiscard]] std::size_t replays() const;

    private:
        // build the retained trace from the current snapshot, which is the one step retention moves off the frame (§6.4)
        void rebuild_trace();

        // the rectangle the trace occupies inside the widget, which leaves a margin for the frame (§6.1)
        [[nodiscard]] Rect plot_rect() const;

        // the transform that maps the normalized trace onto the plot, which a resize changes without a rebuild (§6.4)
        [[nodiscard]] Transform trace_transform() const;

        // the sampled waveform, which a painted line and the bindings read (§11.2, §13.2)
        std::shared_ptr<const WaveformData> m_data;

        // the line colour the empty widget keeps at the declared default (§11.2)
        Color m_line_color = Color::rgb(0.0f, 1.0f, 0.0f);

        // the factory that stamps every retained resource the widget builds (§6.4, AD-06)
        ResourceFactory m_factory;

        // the retained trace, which the frames replay instead of rebuilding (§6.4, AD-06)
        std::shared_ptr<const LineBatch> m_trace;

        // how many times the trace was built, which is what proves a frame replayed rather than rebuilt (§6.4)
        std::size_t m_trace_builds = 0;

        // how many frames replayed the retained batch, which is the count that grows while the data holds (§6.4, AD-06)
        std::size_t m_replays = 0;
    };
} // namespace albor
