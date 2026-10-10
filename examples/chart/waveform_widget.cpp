#include "examples/chart/waveform_widget.h"

#include <memory>
#include <utility>

namespace albor
{
    WaveformWidget::WaveformWidget(BindingEngine* engine, std::string explicit_id) :
        WaveformView(engine, std::move(explicit_id)) {}

    WaveformWidget::~WaveformWidget() = default;

    void WaveformWidget::set_data(std::shared_ptr<const WaveformData> value) { m_data = std::move(value); }

    std::shared_ptr<const WaveformData> WaveformWidget::data() const { return m_data; }

    void WaveformWidget::set_line_color(Color value) { m_line_color = value; }

    Color WaveformWidget::line_color() const { return m_line_color; }

    void WaveformWidget::raise_selection(float time_pos) {
        // the generated raiser is protected, which is why the widget publishes its own entry point (§11.2)
        emit_selection_changed(time_pos);
    }

    SemanticNode WaveformWidget::semantic_node() const {
        // a chart is a drawing surface, which is the closest known role to the declared one (§10.1, §11.2)
        SemanticNode node(SemanticRole::Canvas);
        node.set_name("waveform chart");
        return node;
    }
} // namespace albor
