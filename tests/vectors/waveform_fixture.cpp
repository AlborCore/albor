#include "waveform_fixture.h"

#include <memory>
#include <utility>

namespace albor
{
    WaveformViewFixture::WaveformViewFixture(BindingEngine* engine, std::string explicit_id) :
        WaveformView(engine, std::move(explicit_id)) {}

    WaveformViewFixture::~WaveformViewFixture() = default;

    void WaveformViewFixture::set_data(std::shared_ptr<const WaveformData> value) { m_data = std::move(value); }

    std::shared_ptr<const WaveformData> WaveformViewFixture::data() const { return m_data; }

    void WaveformViewFixture::set_line_color(Color value) { m_line_color = value; }

    Color WaveformViewFixture::line_color() const { return m_line_color; }

    void WaveformViewFixture::set_zoom(float value) { m_zoom = value; }

    float WaveformViewFixture::zoom() const { return m_zoom; }

    void WaveformViewFixture::raise_selection(float time_pos) {
        // the generated raiser is protected, which is why the fixture publishes its own entry point (§11.2)
        emit_selection_changed(time_pos);
    }

    SemanticNode WaveformViewFixture::semantic_node() const {
        // a chart is a drawing surface, which is the closest known role to the declared one (§10.1, §11.2)
        return SemanticNode(SemanticRole::Canvas);
    }

    std::unique_ptr<WaveformView> create_WaveformView(BindingEngine* engine) {
        // the application decides the concrete type, which is what keeps the facade opaque (§11.2, AD-17)
        return std::make_unique<WaveformViewFixture>(engine);
    }
} // namespace albor
