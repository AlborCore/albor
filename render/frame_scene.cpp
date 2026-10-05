#include "render/frame_scene.h"

namespace albor
{
    bool TextLayout::empty() const {
        // a run with no glyphs costs nothing to draw (§6.2)
        return text.empty();
    }

    TextLayout TextLayout::from_text(std::string text, float font_size) {
        // the phase 1 measurement is a fixed advance per byte, which phase 3 replaces with shaping (§9.1)
        const float advance = font_size * 0.6f;
        TextLayout layout;
        layout.text = std::move(text);
        layout.size = Size{advance * static_cast<float>(layout.text.size()), font_size};
        // the baseline sits one fifth of the font size above the bottom of the run (§9.1)
        layout.ascent = font_size * 0.8f;
        layout.descent = font_size * 0.2f;
        return layout;
    }

    FrameScene::FrameScene(WindowMetrics metrics) :
        m_metrics(metrics) {}

    FrameScene FrameScene::from_primitives(WindowMetrics metrics, Color background, std::vector<Rect> rects) {
        // the scene is filled once and never touched again, because the render thread consumes it (§6.1)
        FrameScene scene(metrics);
        scene.m_background = background;
        scene.m_rects = std::move(rects);
        return scene;
    }

    const WindowMetrics& FrameScene::metrics() const {
        // the metrics travel with the scene, so the render thread knows what it was built for (§6.1)
        return m_metrics;
    }

    Color FrameScene::background() const {
        // the background covers the whole logical size of the surface (§6.1)
        return m_background;
    }

    const std::vector<Rect>& FrameScene::rects() const {
        // the rectangles are replayed in the order the ui thread built them (§6.1)
        return m_rects;
    }

    const std::vector<TextLayout>& FrameScene::texts() const {
        // the text runs are replayed in the order the ui thread built them (§6.1)
        return m_texts;
    }

    void FrameScene::add_text(TextLayout layout, Point origin) {
        // a run with no glyphs contributes nothing, so it is not recorded (§6.2)
        if (layout.empty())
            return;
        // the origin travels beside the run, because the render thread cannot ask the ui thread where it was (§6.1)
        m_text_origins.push_back(origin);
        m_texts.push_back(std::move(layout));
    }

    const std::vector<Point>& FrameScene::text_origins() const {
        // the origins are parallel to the runs, so index one lookup finds both (§6.1)
        return m_text_origins;
    }

    void FrameScene::set_rect_colors(std::vector<Color> colors) {
        // the colour list must stay parallel to the rectangle list, so a mismatched length is refused (§6.1)
        if (colors.size() != m_rects.size())
            return;
        // the colours travel with the rectangles, because the render thread cannot ask the ui thread (§6.1, AD-18)
        m_rect_colors = std::move(colors);
    }

    const std::vector<Color>& FrameScene::rect_colors() const {
        // the colours are parallel to the rectangles, so index one lookup finds both (§6.1)
        return m_rect_colors;
    }
} // namespace albor
