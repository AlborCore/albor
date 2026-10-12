#include "examples/chart/waveform_widget.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace albor
{
    namespace
    {
        // a trace of one sample describes no segment, so the plot stays empty until a second arrives (§6.4)
        constexpr std::size_t MINIMUM_SAMPLES = 2;
    } // namespace

    WaveformWidget::WaveformWidget(BindingEngine* engine, std::string explicit_id) :
        WaveformView(engine, std::move(explicit_id)) {}

    WaveformWidget::~WaveformWidget() = default;

    void WaveformWidget::set_data(std::shared_ptr<const WaveformData> value) {
        m_data = std::move(value);
        // the retained geometry is rebuilt here, off the record pass, because the data is what changed (§6.4, AD-06)
        rebuild_trace();
        // a new trace is a new visual, so the tree asks for the frame that draws it (§6.1)
        invalidate();
    }

    std::shared_ptr<const WaveformData> WaveformWidget::data() const { return m_data; }

    void WaveformWidget::set_line_color(Color value) {
        // an unchanged colour redraws nothing, which is what the visual dirty flag is for (§6.1)
        if (m_line_color == value)
            return;
        m_line_color = value;
        // the trace strokes in this colour, so a change of it changes what the frame carries (§6.4)
        invalidate();
    }

    Color WaveformWidget::line_color() const { return m_line_color; }

    void WaveformWidget::raise_selection(float time_pos) {
        // the generated raiser is protected, which is why the widget publishes its own entry point (§11.2)
        emit_selection_changed(time_pos);
    }

    std::shared_ptr<const LineBatch> WaveformWidget::trace() const { return m_trace; }

    std::size_t WaveformWidget::trace_build_count() const { return m_trace_builds; }

    std::size_t WaveformWidget::replays() const { return m_replays; }

    SemanticNode WaveformWidget::semantic_node() const {
        // a chart is a drawing surface, which is the closest known role to the declared one (§10.1, §11.2)
        SemanticNode node(SemanticRole::Canvas);
        node.set_name("waveform chart");
        return node;
    }

    void WaveformWidget::rebuild_trace() {
        m_trace_builds++;
        // a snapshot with too few samples describes no segment, so the factory would refuse it (§6.4, AD-06)
        if (m_data == nullptr || m_data->samples.size() < MINIMUM_SAMPLES) {
            // an empty plot draws its frame and nothing else, which is what a widget with no data shows (§6.1)
            m_trace.reset();
            return;
        }
        // the samples become segment endpoints, which is the pair a batch replays as one line (§6.4)
        std::vector<Point> points;
        // every consecutive pair of samples is one segment, so the list holds two points per sample but one (§6.4)
        points.reserve((m_data->samples.size() - 1) * 2);
        // the last sample index divides the run, which spreads the samples across the full width (§6.4)
        const float last = static_cast<float>(m_data->samples.size() - 1);
        for (std::size_t index = 0; index < m_data->samples.size() - 1; index++) {
            // the horizontal coordinate is the sample's share of the run, which the transform maps onto the plot (§6.4)
            const float first_x = static_cast<float>(index) / last;
            // the next sample continues the run one step further right (§6.4)
            const float second_x = static_cast<float>(index + 1) / last;
            // the vertical coordinate is the amplitude itself, because the transform carries the sign (§6.4)
            points.push_back(Point{.x = first_x, .y = m_data->samples[index]});
            // the pair closes the segment, which is what the factory validates the point count against (§6.4, AD-06)
            points.push_back(Point{.x = second_x, .y = m_data->samples[index + 1]});
        }
        // the factory stamps the batch, so the backend keys its cache on an identifier nothing else repeats (§6.4, AD-06)
        m_trace = m_factory.create_line_batch(points);
    }

    Rect WaveformWidget::plot_rect() const {
        // the plot is inset from the widget, so the frame has somewhere to sit (§6.1)
        constexpr float MARGIN = 24.0f;
        // the inset width is the widget less both margins, which a small widget clamps to nothing (§8.2)
        const float width = std::max(0.0f, bounds().width - MARGIN * 2.0f);
        // the inset height follows the same margin, because a plot is square inset (§8.2)
        const float height = std::max(0.0f, bounds().height - MARGIN * 2.0f);
        return Rect::from_origin_size(Point{.x = bounds().x + MARGIN, .y = bounds().y + MARGIN}, Size{.width = width, .height = height});
    }

    Transform WaveformWidget::trace_transform() const {
        // the plot is where the trace lands, so the transform maps the normalized run onto it (§6.4)
        const Rect plot = plot_rect();
        // the run stretches across the plot width, which is the whole horizontal mapping (§6.4)
        const float scale_x = plot.width;
        // the amplitude stretches across half the plot height, because the samples are signed around zero (§6.4)
        const float scale_y = -plot.height * 0.5f;
        // the origin is the plot's left edge, so the run starts where the axis does (§6.4)
        const float translate_x = plot.x;
        // the origin is the plot's vertical middle, because that is where a zero amplitude lands (§6.4)
        const float translate_y = plot.y + plot.height * 0.5f;
        // the scale applies first and the translation second, so a normalized point lands inside the plot (§6.2, §6.4)
        return Transform::scaling(scale_x, scale_y).then(Transform::translation(translate_x, translate_y));
    }

    void WaveformWidget::paint_content(RenderContext& context) {
        // the replay is counted, which is what a caller reads to see the frame reused the resource (§6.4, AD-06)
        m_replays++;
        // the plot is what this content draws into, which the arrange phase decided (§6.1)
        const Rect plot = plot_rect();
        // a widget the layout gave no area draws nothing, because it occupies no pixel (§6.1)
        if (plot.empty())
            return;
        // the plot plate fills behind the trace, which is the surface the data reads against (§6.2)
        context.draw_rect(plot, {.fill = Color::rgba(0.09f, 0.10f, 0.13f, 1.0f)});
        // the border is a stroked plate with no fill, which is what draws only its edge (§6.2)
        context.draw_rect(plot, {.stroke = Color::rgba(0.22f, 0.24f, 0.30f, 1.0f), .stroke_width = 1.0f});
        // the zero line is where a flat trace lands, which is what makes the amplitude readable (§6.1)
        context.draw_rect(Rect::from_origin_size(Point{.x = plot.x, .y = plot.y + plot.height * 0.5f}, Size{.width = plot.width, .height = 1.0f}), {.fill = Color::rgba(0.18f, 0.20f, 0.26f, 1.0f)});
        // a trace that was never built draws nothing, which is what an empty widget shows (§6.4)
        if (m_trace == nullptr)
            return;
        // the retained batch replays under the plot transform, which is the whole point of retaining it (§6.4, AD-06)
        context.draw_line_batch(m_trace, trace_transform(), {.color = m_line_color, .width = 1.6f});
    }
} // namespace albor
