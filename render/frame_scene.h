#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "window/window_metrics.h"

namespace albor
{
    // a retained immutable resource the scene hands the render thread, which replays without rebuilding (§6.4, AD-06)
    class LineBatch;
    class PathResource;
    class PictureResource;
    class ImageResource;

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

    // one retained line batch with the transform and stroke it draws under, which stage two replays in order (§6.4, AD-06)
    struct LineBatchDraw
    {
        // the immutable segments the draw replays
        std::shared_ptr<const LineBatch> batch;

        // the transform from batch space into scene space
        Transform transform;

        // the stroke the segments draw with
        StrokeStyle stroke;
    };

    // one retained path with the transform and paint it draws under, which stage two replays in order (§6.4, AD-06)
    struct PathDraw
    {
        // the immutable contour the draw replays
        std::shared_ptr<const PathResource> path;

        // the transform from path space into scene space
        Transform transform;

        // the fill and stroke of the contour
        Paint paint;
    };

    // one retained picture with the transform it replays under, which stage two walks in order (§6.4, AD-06)
    struct PictureDraw
    {
        // the immutable command list the draw replays
        std::shared_ptr<const PictureResource> picture;

        // the transform from picture space into scene space
        Transform transform;
    };

    // one retained image with the destination it draws into, which stage two uploads once (§6.4, AD-06)
    struct ImageDraw
    {
        // the immutable pixels the draw uploads
        std::shared_ptr<const ImageResource> image;

        // the destination rectangle in scene space
        Rect destination;
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

        // hand out the fill colour of every recorded rectangle, parallel to the rectangles themselves (§6.1, AD-18)
        [[nodiscard]] const std::vector<Color>& rect_colors() const;

        // replace the fill colour of every recorded text run, which travels in the same order as the runs (§6.1, AD-18)
        void set_text_colors(std::vector<Color> colors);

        // hand out the fill colour of every recorded text run, parallel to the runs themselves (§6.1, AD-18)
        [[nodiscard]] const std::vector<Color>& text_colors() const;

        // add a retained line batch draw, which a ui node records during scene build (§6.4, AD-06)
        void add_line_batch(LineBatchDraw draw);

        // hand out the retained line batch draws, in recording order (§6.4, AD-06)
        [[nodiscard]] const std::vector<LineBatchDraw>& line_batches() const;

        // add a retained path draw, which a ui node records during scene build (§6.4, AD-06)
        void add_path(PathDraw draw);

        // hand out the retained path draws, in recording order (§6.4, AD-06)
        [[nodiscard]] const std::vector<PathDraw>& paths() const;

        // add a retained picture draw, which a ui node records during scene build (§6.4, AD-06)
        void add_picture(PictureDraw draw);

        // hand out the retained picture draws, in recording order (§6.4, AD-06)
        [[nodiscard]] const std::vector<PictureDraw>& pictures() const;

        // add a retained image draw, which a ui node records during scene build (§6.4, AD-06)
        void add_image(ImageDraw draw);

        // hand out the retained image draws, in recording order (§6.4, AD-06)
        [[nodiscard]] const std::vector<ImageDraw>& images() const;

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

        // the fill colour of every recorded text run, which stage two needs for the same reason (§6.1, AD-18)
        std::vector<Color> m_text_colors;

        // the retained line batch draws the scene replays (§6.4, AD-06)
        std::vector<LineBatchDraw> m_line_batches;

        // the retained path draws the scene replays (§6.4, AD-06)
        std::vector<PathDraw> m_paths;

        // the retained picture draws the scene replays (§6.4, AD-06)
        std::vector<PictureDraw> m_pictures;

        // the retained image draws the scene replays (§6.4, AD-06)
        std::vector<ImageDraw> m_images;
    };
} // namespace albor
