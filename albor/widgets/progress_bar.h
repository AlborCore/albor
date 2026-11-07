#pragma once

#include "albor/animation/animation_engine.h"
#include "albor/widgets/widget.h"

namespace albor
{
    // the widget that shows how far a task has advanced, which is a read only value control (§8)
    class ProgressBar final : public Widget
    {
    public:
        // build an empty bar over the engine that settles its properties (§8)
        explicit ProgressBar(BindingEngine* engine, std::string explicit_id = {});

        // end the sweep before the bar dies, because the engine would keep calling into it (§7.3)
        ~ProgressBar() override;

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

        // show work whose remaining length is unknown with a repeating sweep (§7.3)
        void set_indeterminate(bool indeterminate);

        // report whether the bar shows the unknown-length sweep (§7.3)
        [[nodiscard]] bool indeterminate() const;

        // override: a read-only bar is not a keyboard stop, because the user does not drive it (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: a hidden bar ends its sweep, and a shown one starts it again (§7.3)
        void set_visible(bool visible) override;

    protected:
        // override: retire the sweep on the old engine and restart it on the new one (§7.3)
        void on_animation_engine_changed(AnimationEngine* engine) override;

    public:
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

        // start the repeating sweep when an engine is available, or leave it for the paint pass (§7.3)
        void ensure_animation();

        // stop the sweep and release the handle the engine holds (§7.3)
        void stop_animation();

        // the fraction of the track the fill covers, which stays inside the unit range (§8)
        float m_value = 0.0f;

        // whether the bar shows the unknown-length sweep instead of a fraction (§7.3)
        bool m_indeterminate = false;

        // where the sweep sits in its pass, which the animation writes and the paint reads (§7.3)
        float m_phase = 0.5f;

        // the repeating animation the bar scheduled, where zero names none (§7.3)
        AnimationHandle m_animation = 0;

        // the engine that issued the handle, which is what cancels it even after the tree drops it (§7.3)
        AnimationEngine* m_animation_engine = nullptr;

        // the colour of the track (§6.1)
        Color m_track_color{0.24f, 0.27f, 0.32f, 1.0f};

        // whether the author wrote the track, because an explicit value wins over the theme role (§8.4)
        bool m_track_color_set = false;

        // the colour of the progress fill (§6.1)
        Color m_fill_color{0.20f, 0.55f, 0.90f, 1.0f};

        // whether the author wrote the fill, because an explicit value wins over the theme role (§8.4)
        bool m_fill_color_set = false;

        // whether the bar presents its disabled paint (§8.3)
        bool m_disabled = false;
    };
} // namespace albor
