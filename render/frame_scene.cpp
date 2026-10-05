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
} // namespace albor
