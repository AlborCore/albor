#pragma once

#include "widgets/widget.h"

namespace albor
{
    // the widget that shows how far a task has advanced, which is a read only value control (§8)
    class ProgressBar final : public Widget
    {
    public:
        // build an empty bar over the engine that settles its properties (§8)
        explicit ProgressBar(BindingEngine* engine, std::string explicit_id = {});

        // measure a wide and short control, because progress reads as a horizontal bar (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the bar, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the track and the progress it carries, which is what a bar looks like (§6.1)
        void paint_content(RenderContext& context) override;

        // override: announce the bar with the fraction it has reached (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the fraction, which is clamped into the unit range because progress is normalized (§8)
        void set_value(float value);

        // hand out the fraction, which the render pass and the semantic node both read (§8)
        [[nodiscard]] float value() const;

        // refuse or accept the disabled paint, which is the only state a read only bar presents (§8.3)
        void set_disabled(bool disabled);

        // report whether the bar presents its disabled paint (§8.3)
        [[nodiscard]] bool disabled() const override;

        // replace the track colour, which redraws without changing the extent (§6.1)
        void set_track_color(Color color);

        // replace the fill colour, which redraws without changing the extent (§6.1)
        void set_fill_color(Color color);

    private:
        // the track colours the bar resolves through, which are the installed palette when a theme is present (§6.1)
        [[nodiscard]] const Color* track_colors() const;

        // the fill colours the bar resolves through, which are the installed palette when a theme is present (§6.1)
        [[nodiscard]] const Color* fill_colors() const;

        // the fraction of the track the fill covers, which stays inside the unit range (§8)
        float m_value = 0.0f;

        // the colour of the track (§6.1)
        Color m_track_color{0.24f, 0.27f, 0.32f, 1.0f};

        // the colour of the progress fill (§6.1)
        Color m_fill_color{0.20f, 0.55f, 0.90f, 1.0f};

        // whether the bar presents its disabled paint (§8.3)
        bool m_disabled = false;
    };
} // namespace albor
