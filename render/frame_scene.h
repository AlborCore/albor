#pragma once

#include <string>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "window/window_metrics.h"

namespace albor
{
    // one laid out run of text with its own origin, which the text engine produces (§9.1)
    struct TextLayout
    {
        // the utf8 text of the run
        std::string text;

        // the size of the run in logical pixels
        Size size;

        // the ascent and descent of the run, which a baseline draw needs
        float ascent = 0.0f;

        // the descent of the run, which is positive downwards
        float descent = 0.0f;

        // report whether the run carries no glyphs
        [[nodiscard]] bool empty() const;

        // build a run from text measured at a font size, which the text engine refines in phase 3 (§9.1)
        [[nodiscard]] static TextLayout from_text(std::string text, float font_size);

        // compare two runs field by field
        friend bool operator==(const TextLayout& lhs, const TextLayout& rhs) = default;
    };

    // the immutable scene a UI thread builds and a render thread consumes (§6.1, AD-18)
    class FrameScene
    {
    public:
        // build an empty scene for the metrics the surface presents
        explicit FrameScene(WindowMetrics metrics);

        // build a scene from a background colour and a list of primitives, which is the phase 1 shape (§6.1)
        [[nodiscard]] static FrameScene from_primitives(WindowMetrics metrics, Color background, std::vector<Rect> rects);

        // hand out the metrics the scene was built for
        [[nodiscard]] const WindowMetrics& metrics() const;

        // hand out the background colour of the scene
        [[nodiscard]] Color background() const;

        // hand out the solid rectangles the scene draws
        [[nodiscard]] const std::vector<Rect>& rects() const;

        // hand out the text runs the scene draws
        [[nodiscard]] const std::vector<TextLayout>& texts() const;

        // add a text run at a baseline origin, which a ui node records during scene build (§6.1)
        void add_text(TextLayout layout, Point origin);

        // hand out the baseline origin of every text run, parallel to the runs themselves (§6.1)
        [[nodiscard]] const std::vector<Point>& text_origins() const;

        // replace the fill colour of every recorded rectangle, which travels in the same order as the rectangles (§6.1, AD-18)
        void set_rect_colors(std::vector<Color> colors);

        // hand out the fill colour of every recorded rectangle, parallel to the rectangles themselves (§6.1)
        [[nodiscard]] const std::vector<Color>& rect_colors() const;

    private:
        // the metrics the scene was built for, which the render thread presents into
        WindowMetrics m_metrics;

        // the background colour the surface is cleared with
        Color m_background;

        // the solid rectangles the scene draws
        std::vector<Rect> m_rects;

        // the text runs the scene draws
        std::vector<TextLayout> m_texts;

        // the baseline origin of every text run, which the thread hand off must carry as well (§6.1)
        std::vector<Point> m_text_origins;

        // the fill colour of every recorded rectangle, which stage two needs because it owns the canvas (§6.1, AD-18)
        std::vector<Color> m_rect_colors;
    };
} // namespace albor
