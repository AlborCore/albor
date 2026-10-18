#include "render/render-albor/albor_glyph_path.macos.h"

#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>

namespace albor::render
{
    namespace
    {
        // the samples one curve divides into, which keeps its chord inside a fifth of a logical pixel (§9.1)
        int curve_samples(float bend) {
            // the deviation a flattened curve stays inside, which a device pixel fifth resolves (§9.1)
            constexpr float CHORD_TOLERANCE = 0.2f;
            // the chord error falls with the square of the sample count, which the bend is divided by
            const float samples = std::sqrt(bend / (8.0f * CHORD_TOLERANCE));
            // a curve takes at least four samples, which keeps a small glyph round
            return std::clamp(static_cast<int>(std::ceil(samples)), 4, 64);
        }

        // the bend a quadratic carries, which the distance of its control point from the chord measures (§9.1)
        float quadratic_bend(Point from, Point control, Point to) {
            // the second difference is twice the distance the curve bends from its chord
            const float dx = from.x - 2.0f * control.x + to.x;
            const float dy = from.y - 2.0f * control.y + to.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // the bend a cubic carries, which the larger of its two second differences measures (§9.1)
        float cubic_bend(Point from, Point control0, Point control1, Point to) {
            // the two second differences bound the curvature the curve carries
            const float dx0 = from.x - 2.0f * control0.x + control1.x;
            const float dy0 = from.y - 2.0f * control0.y + control1.y;
            const float dx1 = control0.x - 2.0f * control1.x + to.x;
            const float dy1 = control0.y - 2.0f * control1.y + to.y;
            return std::max(std::sqrt(dx0 * dx0 + dy0 * dy0), std::sqrt(dx1 * dx1 + dy1 * dy1));
        }

        // map a core graphics point onto the logical space, which flips the y axis the font reports in (§9.1, AD-27)
        Point to_logical(CGPoint point) {
            // the vertical coordinate is negated, because the font grows upwards and the device grows downwards (§6.3)
            return Point{.x = static_cast<float>(point.x), .y = -static_cast<float>(point.y)};
        }

        // append the flattened samples of a quadratic curve, which the control point bends (§9.1)
        void append_quadratic(GlyphPath& path, Point from, Point control, Point to) {
            // the samples this curve needs hold its chord at the tolerance (§9.1)
            const int segments = curve_samples(quadratic_bend(from, control, to));
            for (int step = 1; step <= segments; step++) {
                // the parameter walks the curve from the start to its end (§6.4)
                const float t = static_cast<float>(step) / static_cast<float>(segments);
                const float u = 1.0f - t;
                // the quadratic evaluates the blended control points (§6.4)
                path.verbs.push_back(PathVerb::LineTo);
                path.points.push_back(Point{.x = u * u * from.x + 2.0f * u * t * control.x + t * t * to.x, .y = u * u * from.y + 2.0f * u * t * control.y + t * t * to.y});
            }
        }

        // append the flattened samples of a cubic curve, which the two control points bend (§9.1)
        void append_cubic(GlyphPath& path, Point from, Point control0, Point control1, Point to) {
            // the samples this curve needs hold its chord at the tolerance (§9.1)
            const int segments = curve_samples(cubic_bend(from, control0, control1, to));
            for (int step = 1; step <= segments; step++) {
                // the parameter walks the curve from the start to its end (§6.4)
                const float t = static_cast<float>(step) / static_cast<float>(segments);
                const float u = 1.0f - t;
                // the cubic evaluates the three blended control points, which the coefficients weight (§6.4)
                const float a = u * u * u;
                const float b = 3.0f * u * u * t;
                const float c = 3.0f * u * t * t;
                const float d = t * t * t;
                path.verbs.push_back(PathVerb::LineTo);
                path.points.push_back(Point{.x = a * from.x + b * control0.x + c * control1.x + d * to.x, .y = a * from.y + b * control0.y + c * control1.y + d * to.y});
            }
        }

        // the state one outline walk carries, which remembers the point the next segment leaves from (§6.4)
        struct PathWalk
        {
            // the path the walk appends its verbs and points to
            GlyphPath* path = nullptr;

            // the point the segment under construction starts from
            Point current;
        };

        // append the quadratic one path element reports, whose end point the walk then leaves from (§9.1)
        void apply_quadratic(PathWalk& walk, const CGPathElement* element) {
            // the end point the curve reaches, which the next segment leaves from
            const Point end = to_logical(element->points[1]);
            append_quadratic(*walk.path, walk.current, to_logical(element->points[0]), end);
            walk.current = end;
        }

        // append the cubic one path element reports, whose end point the walk then leaves from (§9.1)
        void apply_cubic(PathWalk& walk, const CGPathElement* element) {
            // the end point the curve reaches, which the next segment leaves from
            const Point end = to_logical(element->points[2]);
            append_cubic(*walk.path, walk.current, to_logical(element->points[0]), to_logical(element->points[1]), end);
            walk.current = end;
        }

        // turn one core graphics path element into the verbs a contour is built from (§6.4)
        void apply_element(void* info, const CGPathElement* element) {
            PathWalk& walk = *static_cast<PathWalk*>(info);
            switch (element->type) {
            // a move opens a fresh contour, which every subpath of a glyph starts with (§6.4)
            case kCGPathElementMoveToPoint:
                walk.current = to_logical(element->points[0]);
                walk.path->verbs.push_back(PathVerb::MoveTo);
                walk.path->points.push_back(walk.current);
                return;
            // a line reaches the next point directly, which the walk remembers (§6.4)
            case kCGPathElementAddLineToPoint:
                walk.current = to_logical(element->points[0]);
                walk.path->verbs.push_back(PathVerb::LineTo);
                walk.path->points.push_back(walk.current);
                return;
            // a quadratic bends towards its control point, which the flattening samples (§9.1)
            case kCGPathElementAddQuadCurveToPoint:
                apply_quadratic(walk, element);
                return;
            // a cubic bends towards its two control points, which the flattening samples (§9.1)
            case kCGPathElementAddCurveToPoint:
                apply_cubic(walk, element);
                return;
            // a close seals the contour, which the outline joins back to its start (§6.4)
            case kCGPathElementCloseSubpath:
                walk.path->verbs.push_back(PathVerb::Close);
                return;
            }
        }
    } // namespace

    GlyphPath glyph_path_from_cgpath(CGPathRef path) {
        // the walk turns every element into the verbs a contour is built from (§6.4)
        GlyphPath result;
        PathWalk walk{.path = &result, .current = {}};
        CGPathApply(path, &walk, apply_element);
        return result;
    }

    GlyphPath platform_glyph_path(const FontHandle& font, std::uint32_t glyph_id, float font_size) {
        // a handle with no typeface and a size of no pixels carry no outline, which the walk refuses (§9.1)
        if (!font.valid() || font_size <= 0.0f)
            return {};
        // the handle is the core text face the text engine resolved, which only this half may name (§3.5, AD-28)
        const CTFontRef typeface = static_cast<CTFontRef>(font.object());
        // the face is copied at the run size, because an outline is in pixels and does not scale (§9.1, AD-27)
        CTFontRef scaled = CTFontCreateCopyWithAttributes(typeface, font_size, nullptr, nullptr);
        // a copy the platform refused carries no glyph, which is a refusal rather than a guess (§9.1)
        if (scaled == nullptr)
            return GlyphPath{};
        // the path is the glyph's own outline, which a glyph with no visible shape leaves null (§6.2)
        CGPathRef path = CTFontCreatePathForGlyph(scaled, static_cast<CGGlyph>(glyph_id), nullptr);
        // the scaled face goes back, because the path outlives it (§9.1)
        CFRelease(scaled);
        // a glyph the face carries no outline for draws nothing, which a space is (§6.2)
        if (path == nullptr)
            return {};
        // the walk copies every element into the result, which outlives the path (§6.4)
        const GlyphPath result = glyph_path_from_cgpath(path);
        // the path goes back, because its elements were copied into the result (§6.4)
        CFRelease(path);
        return result;
    }
} // namespace albor::render
