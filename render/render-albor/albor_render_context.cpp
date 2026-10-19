#include "render/render-albor/albor_render_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "render/draw_command.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/picture_resource.h"
#include "render/render-albor/albor_fill.h"
#include "render/render-albor/albor_geometry.h"
#include "render/render-albor/albor_glyph_path.h"

namespace albor::render
{
    namespace
    {
        // the miter limit a plain stroke joins with, which the default stroke style carries (§6.2)
        constexpr float STROKE_MITER_LIMIT = 4.0f;

        // the tessellation tolerance a flattened arc divides at, which a device pixel fourth resolves
        constexpr float TESS_TOLERANCE = 0.25f;

        // the determinant below which a transform collapses, which draws nothing (§6.2)
        constexpr float SINGULAR_LIMIT = 0.000001f;
    } // namespace

    AlborRenderContext::AlborRenderContext(FrameBatch& batch, const Rect& logical_extent, float device_scale) :
        m_batch(batch), m_extent(logical_extent), m_device_scale(device_scale > 0.0f ? device_scale : 1.0f), m_clip{.rect = logical_extent}, m_clip_device(device_transform()) {}

    void AlborRenderContext::save() {
        // the current transform, clip, clip transform and opacity are pushed, which a restore pops (§6.2)
        m_states.push_back(State{.transform = m_transform, .clip = m_clip, .clip_device = m_clip_device, .opacity = m_opacity});
    }

    void AlborRenderContext::restore() {
        // a restore with nothing pushed is a no operation rather than an error (§6.2)
        if (m_states.empty())
            return;
        // the saved state becomes the current one again (§6.2)
        const State state = m_states.back();
        m_states.pop_back();
        m_transform = state.transform;
        m_clip = state.clip;
        m_clip_device = state.clip_device;
        m_opacity = state.opacity;
    }

    void AlborRenderContext::set_transform(const Transform& transform) {
        // the scene transform is replaced outright, which every later draw lands under (§6.2)
        m_transform = transform;
    }

    const Transform& AlborRenderContext::transform() const {
        // the recorded transform is what a test asserts against (§6.2)
        return m_transform;
    }

    void AlborRenderContext::clip_rect(const Rect& rect) {
        // the clip freezes the transform it is pushed under, which a later transform must not move (§6.2)
        m_clip_device = device_transform();
        // a clip pushed under a collapsed transform spans no device area, so it cuts everything away (§6.2)
        const float determinant = m_clip_device.scale_x * m_clip_device.scale_y - m_clip_device.skew_x * m_clip_device.skew_y;
        if (std::abs(determinant) < SINGULAR_LIMIT) {
            m_clip = ClipState{};
            return;
        }
        // the recorded clip is the overlap of what was there and what was asked for, radius included (§6.2)
        clip_to_rect(m_clip, rect);
    }

    void AlborRenderContext::clip_rounded_rect(const Rect& rect, float radius) {
        // the clip freezes the transform it is pushed under, which a later transform must not move (§6.2)
        m_clip_device = device_transform();
        // a rounded clip narrows the area and keeps the radius only where it governs (§6.2)
        clip_to_rounded_rect(m_clip, rect, radius);
    }

    const Rect& AlborRenderContext::clip() const {
        // the recorded clip is what a test asserts against (§6.2)
        return m_clip.rect;
    }

    void AlborRenderContext::set_opacity(float opacity) {
        // the opacity is clamped into the unit range, because content computes it from data (§6.1)
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float AlborRenderContext::opacity() const {
        // the recorded opacity is what a test asserts against (§6.1)
        return m_opacity;
    }

    std::size_t AlborRenderContext::depth() const {
        // the depth is how many states are stacked, which a test asserts against (§6.2)
        return m_states.size();
    }

    Transform AlborRenderContext::device_transform() const {
        // the scene transform lands first and the device scale second, which maps logical onto physical (§6.3, AD-27)
        return m_transform.then(Transform::scaling(m_device_scale, m_device_scale));
    }

    float AlborRenderContext::to_device_length(float length) const {
        // the average scale of the device transform maps a logical length onto the physical raster (§6.3, AD-27)
        const Transform device = device_transform();
        // every caller is already past the drawable gate, so the transform cannot have collapsed here (§6.2)
        return length * std::sqrt(std::abs(device.scale_x * device.scale_y - device.skew_x * device.skew_y));
    }

    bool AlborRenderContext::drawable() const {
        // a clip without area cuts every draw away (§6.2)
        if (m_clip.rect.empty())
            return false;
        // a colour that puts no ink down draws nothing, which a zero opacity makes (§6.1)
        if (m_opacity <= 0.0f)
            return false;
        // a transform that collapses to a line or a point draws nothing (§6.2)
        const Transform device = device_transform();
        const float determinant = std::abs(device.scale_x * device.scale_y - device.skew_x * device.skew_y);
        return determinant >= SINGULAR_LIMIT;
    }

    BatchScissor AlborRenderContext::scissor() const {
        // an unclipped draw carries no scissor, which the shader skips (§6.2)
        BatchScissor result;
        // a rectangular clip that covers the whole extent leaves every draw uncut (§6.2)
        if (m_clip.rect == m_extent && m_clip.radius <= 0.0f)
            return result;
        // a clip without area cuts every draw away, so no scissor is carried at all (§6.2)
        if (m_clip.rect.empty())
            return result;
        // the frozen clip transform maps the clip onto the raster it was pushed for (§6.2)
        const Transform device = m_clip_device;
        result.enabled = true;
        // the scales turn the extent into a one device pixel ramp per axis (nanovg technique)
        result.scale_x = std::sqrt(device.scale_x * device.scale_x + device.skew_x * device.skew_x) / FRINGE;
        result.scale_y = std::sqrt(device.skew_y * device.skew_y + device.scale_y * device.scale_y) / FRINGE;
        // the corner radius rounds the mask, which zero leaves the clip rectangular (§6.2)
        result.radius = m_clip.radius;
        // the corner radius feathers the mask, which the shader ramps over (§6.2)
        result.feather = m_clip.radius > 0.0f ? FRINGE : 0.0f;
        // a square clip lands on whole device pixels, which is where the reference puts an axis aligned clip (§6.2, AD-26)
        const bool axis_aligned = std::abs(device.skew_x) < 0.0001f && std::abs(device.skew_y) < 0.0001f;
        if (m_clip.radius <= 0.0f && axis_aligned) {
            // the two corners map onto the raster, which the rounding snaps to the pixel grid (§6.2, AD-26)
            const Point origin = device.apply(Point{.x = m_clip.rect.x, .y = m_clip.rect.y});
            const Point corner = device.apply(Point{.x = m_clip.rect.x + m_clip.rect.width, .y = m_clip.rect.y + m_clip.rect.height});
            // the snapped box is the area the mask keeps, which the fragment compares against directly
            const float left = std::round(std::min(origin.x, corner.x));
            const float top = std::round(std::min(origin.y, corner.y));
            const float right = std::round(std::max(origin.x, corner.x));
            const float bottom = std::round(std::max(origin.y, corner.y));
            // an identity inverse compares the raster position against the box centre (§6.2)
            result.inverse = Transform{};
            result.extent_x = (right - left) * 0.5f;
            result.extent_y = (bottom - top) * 0.5f;
            // the box centre shifts the comparison, which the shader adds in (§6.2)
            result.inverse.translate_x = -(left + right) * 0.5f;
            result.inverse.translate_y = -(top + bottom) * 0.5f;
            return result;
        }
        // the clip centre anchors the space the shader compares against (§6.2)
        const float center_x = m_clip.rect.x + m_clip.rect.width * 0.5f;
        const float center_y = m_clip.rect.y + m_clip.rect.height * 0.5f;
        // the inverse maps a raster position onto the clip centre, which the extent compares against
        result.inverse = invert_transform(device).then(Transform::translation(-center_x, -center_y));
        // the extents are the half size of the clip, which a fragment outside crosses
        result.extent_x = m_clip.rect.width * 0.5f;
        result.extent_y = m_clip.rect.height * 0.5f;
        return result;
    }

    void AlborRenderContext::to_device(Contour& contour) const {
        // the device transform maps every contour point onto the physical raster (§6.3, AD-27)
        const Transform device = device_transform();
        for (ContourPoint& point : contour.points) {
            // the point lands where the transform places it
            point.position = device.apply(point.position);
        }
        // the directions and the lengths follow the mapped points, which the expansion reads
        stamp_directions(contour);
    }

    void AlborRenderContext::record(EmittedRange range, CoverageKind coverage, float ramp, const Color& color, std::uint64_t texture, bool convex) {
        // an expansion that appended no index records no draw (§6.2)
        if (range.index_count == 0)
            return;
        // the draw carries the range, the paint and the scissor of the moment it was recorded (§6.3)
        BatchDraw draw;
        draw.index_begin = range.index_begin;
        draw.index_count = range.index_count;
        draw.coverage = coverage;
        draw.ramp = ramp;
        draw.color = pack_color(color);
        draw.scissor = scissor();
        draw.texture = texture;
        draw.convex = convex;
        m_batch.draws.push_back(draw);
    }

    void AlborRenderContext::emit_fill(Contour contour, const Color& color, std::uint64_t texture) {
        // the fill measures against the device fringe, which a sub fringe width never reaches (§6.3)
        to_device(contour);
        // the fill fans the inset ring and straddles the boundary with its fringe (nanovg technique)
        const EmittedRange range = expand_fill(m_batch, contour);
        // the expansion resolved the convexity, which the draw record carries for the stencil decision
        const bool convex = contour.convex;
        // the fill draws with the lateral coverage as its alpha, dimmed by the node opacity (nanovg technique)
        record(range, CoverageKind::Plain, 1.0f, color.with_alpha(m_opacity), texture, convex);
    }

    void AlborRenderContext::emit_stroke(Contour contour, const Color& color, float width, Cap cap, Join join) {
        // the stroke expands in device space, where the fringe is one pixel (§6.3, AD-27)
        to_device(contour);
        // the strip folds its lateral coverage around the centre and ramps its ends (nanovg technique)
        const StrokeEmission emission = stroke_geometry(contour, width, cap, join);
        // the stroke draw carries the ramp the folded coverage folds by, dimmed by the opacity and the hairline alpha
        const Color dimmed = color.with_alpha(m_opacity).with_alpha(emission.alpha);
        record(emission.range, CoverageKind::Folded, emission.ramp, dimmed, 0, false);
    }

    StrokeEmission AlborRenderContext::stroke_geometry(Contour& contour, float width, Cap cap, Join join) {
        // a stroke with no width puts no ink down (§6.2)
        const float half_width = to_device_length(width) * 0.5f;
        if (half_width <= 0.0f)
            return StrokeEmission{.range = {0, 0}, .alpha = 0.0f};
        // the strip folds its lateral coverage around the centre and ramps its ends (nanovg technique)
        return expand_stroke(m_batch, contour, half_width, cap, join, STROKE_MITER_LIMIT, TESS_TOLERANCE);
    }

    void AlborRenderContext::emit_shape(Contour contour, const Paint& paint, Cap cap, Join join) {
        // the fill comes first, because the stroke overlaps it (§6.2)
        if (paint.fills())
            emit_fill(contour, paint.fill, 0);
        // the stroke follows over the same contour (§6.2)
        if (paint.strokes())
            emit_stroke(std::move(contour), paint.stroke, paint.stroke_width, cap, join);
    }

    void AlborRenderContext::emit_image(const Rect& destination, const Color& color, std::uint64_t texture) {
        // an image with no destination area draws nothing (§6.4)
        if (destination.empty() || texture == 0)
            return;
        // the destination corners map onto the raster under the device transform (§6.3, AD-27)
        const Transform device = device_transform();
        // the quad goes through the fringe, so a fractional destination antialiases like every other shape (§6.2)
        Contour contour = rect_contour(destination);
        to_device(contour);
        // the range the quad appends starts at the current counts
        const std::uint32_t index_begin = static_cast<std::uint32_t>(m_batch.indices.size());
        const std::size_t vertex_begin = m_batch.vertices.size();
        const EmittedRange range = expand_fill(m_batch, contour);
        // the inverse maps each vertex back to its logical position, which the uv corners read (§6.4)
        const Transform inverse = invert_transform(device);
        for (std::size_t index = vertex_begin; index < m_batch.vertices.size(); index++) {
            // the logical position places the vertex inside the destination it samples (§6.4)
            const Point logical = inverse.apply(Point{.x = m_batch.vertices[index].x, .y = m_batch.vertices[index].y});
            // the uv clamps at the edge, which a fringe vertex just outside the quad keeps sampling (§6.4)
            m_batch.vertices[index].u = std::clamp((logical.x - destination.x) / destination.width, 0.0f, 1.0f);
            m_batch.vertices[index].v = std::clamp((logical.y - destination.y) / destination.height, 0.0f, 1.0f);
        }
        // the image draw samples its texture across the range it appended, dimmed by the node opacity (§6.4)
        record(EmittedRange{.index_begin = index_begin, .index_count = range.index_count}, CoverageKind::Plain, 1.0f, color.with_alpha(m_opacity), texture, contour.convex);
    }

    void AlborRenderContext::draw_rect(const Rect& rect, const Paint& paint) {
        // a draw that cannot put ink down is refused before any geometry is built (§6.2)
        if (!drawable() || rect.empty())
            return;
        // a plain rectangle is the four cornered contour both halves share (§6.2)
        emit_shape(rect_contour(rect), paint, Cap::Butt, Join::Miter);
    }

    void AlborRenderContext::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // a draw that cannot put ink down is refused before any geometry is built (§6.2)
        if (!drawable() || rect.empty())
            return;
        // the cornered contour carries the radii the caller asked for, flattened to a device pixel (§6.2)
        const float scale = std::sqrt(std::abs(device_transform().scale_x * device_transform().scale_y - device_transform().skew_x * device_transform().skew_y));
        emit_shape(rounded_rect_contour(rect, radius, TESS_TOLERANCE / std::max(scale, 0.0001f)), paint, Cap::Butt, Join::Miter);
    }

    void AlborRenderContext::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // a polyline with fewer than two vertices has no segment to stroke (§6.2)
        if (!drawable() || points.size() < 2)
            return;
        // the vertices join into one open contour, which the polyline stroke walks (§6.2)
        Contour contour;
        for (const Point& point : points) {
            // every vertex corners at, which the round join reads
            ContourPoint vertex;
            vertex.position = point;
            vertex.flags = Corner;
            contour.points.push_back(vertex);
        }
        // the ends and the elbows both close roundly, which is how a control glyph reads (§6.2)
        emit_stroke(contour, stroke.color, stroke.width, Cap::Round, Join::Round);
    }

    void AlborRenderContext::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a cast needs both a colour and a shape, because one without the other puts no ink down (§6.2)
        if (!drawable() || !shadow.casts() || rect.empty())
            return;
        // the caster moves by the shadow offset, which is where the cast falls (§6.2)
        const Rect caster = Rect::from_origin_size(Point{.x = rect.x + shadow.offset.x, .y = rect.y + shadow.offset.y}, Size{.width = rect.width, .height = rect.height});
        // the cast is described in device space, where the blur kernel measures its sigma (§6.3, AD-27)
        const Transform device = device_transform();
        const Point origin = device.apply(Point{.x = caster.x, .y = caster.y});
        const Point corner = device.apply(Point{.x = caster.x + caster.width, .y = caster.y + caster.height});
        // the radius and the blur scale with the device, which keeps the cast the same logical size (AD-27)
        const float scale = std::sqrt(std::abs(device.scale_x * device.scale_y - device.skew_x * device.skew_y));
        BatchShadow cast;
        // the two mapped corners normalise, so a mirrored transform cannot produce a negative extent (§6.2)
        const float left = std::min(origin.x, corner.x);
        const float top = std::min(origin.y, corner.y);
        cast.rect = Rect::from_origin_size(Point{.x = left, .y = top}, Size{.width = std::abs(corner.x - origin.x), .height = std::abs(corner.y - origin.y)});
        // the radius clamps to half the shorter side, which the silhouette it feeds also assumes (§6.2)
        cast.radius = std::min(radius * scale, std::min(cast.rect.width, cast.rect.height) * 0.5f);
        // the reference treats the blur radius as twice the sigma, which the kernel matches (§6.2, AD-26)
        cast.sigma = shadow.blur * 0.5f * scale;
        // the reach is the reference's three blur radii, which is six sigmas and bounds the offscreen layer (§6.2, AD-26)
        cast.reach = shadow.blur * 3.0f * scale;
        cast.color = pack_color(shadow.color.with_alpha(m_opacity));
        cast.scissor = scissor();
        // the shadow joins the frame in paint order, which the draw that casts it composites first (§6.2)
        const int index = static_cast<int>(m_batch.shadows.size());
        m_batch.shadows.push_back(cast);
        // the draw carries the cast, so a backend composites it under the shape it belongs to (§6.2)
        BatchDraw draw;
        draw.shadow = index;
        draw.scissor = cast.scissor;
        draw.color = cast.color;
        m_batch.draws.push_back(draw);
    }

    void AlborRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a run with no glyph or a context the frame does not draw through records nothing (§6.2)
        if (!drawable() || layout.empty())
            return;
        // the text takes the fill colour when the paint fills, and the stroke colour otherwise (§6.2)
        const Color color = paint.fills() ? paint.fill : paint.stroke;
        // the glyphs are replayed as shaped, which is what stage one measured (§9.1, AD-18)
        const ShapedRun& run = *layout.run;
        // every line carries its own typeface and its own baseline (§9.1)
        for (const ShapedRun::Line& line : run.lines()) {
            // a line this backend cannot resolve draws nothing, which is a refusal rather than a blank (§9.1)
            if (!line.font.valid())
                continue;
            // the baseline is the run origin under the line's own origin (§9.1)
            const Point baseline{.x = origin.x + line.origin.x, .y = origin.y + line.origin.y};
            // every glyph is its own outline, whose counters the group fill cuts (§6.2, §6.4)
            for (const ShapedGlyph& glyph : line.glyphs) {
                // the outline is walked from the platform typeface in logical pixels (§9.1, AD-28)
                const GlyphPath path = platform_glyph_path(line.font, glyph.id, run.font_size());
                // a glyph without a visible shape draws nothing, which a space is (§6.2)
                if (path.points.empty())
                    continue;
                // the glyph sits at its own offset from the baseline, which the shaped run placed (§9.1)
                const float glyph_x = baseline.x + glyph.position.x;
                const float glyph_y = baseline.y + glyph.position.y;
                // the outline shifts onto the glyph position before the device transform maps it (§6.3, AD-27)
                std::vector<Point> points;
                points.reserve(path.points.size());
                for (const Point& point : path.points)
                    points.push_back(Point{.x = point.x + glyph_x, .y = point.y + glyph_y});
                // the subpaths become contours, which one group fill sweeps so the counters stay open (§6.4)
                std::vector<Contour> contours = build_contours(path.verbs, points);
                for (Contour& contour : contours)
                    to_device(contour);
                const EmittedRange range = expand_fill_group(m_batch, contours);
                // the fill draws with the lateral coverage as its alpha, dimmed by the node opacity (§6.2)
                record(range, CoverageKind::Plain, 1.0f, color.with_alpha(m_opacity), 0, false);
            }
        }
    }

    void AlborRenderContext::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a batch with no segment draws nothing (§6.4, AD-06)
        if (!drawable() || !batch || batch->segment_count() == 0)
            return;
        // the retained transform lands under the current one, which places the segments where the scene put them (§6.4)
        const Transform outer = m_transform;
        m_transform = outer.then(transform);
        // the batch coalesces into one draw, which is what makes its replay constant time (§6.4, AD-06)
        const std::uint32_t index_begin = static_cast<std::uint32_t>(m_batch.indices.size());
        // every consecutive point pair is one open two point contour, which the stroke caps at both ends (§6.4)
        const std::span<const Point> points = batch->points();
        StrokeEmission last;
        for (std::size_t index = 0; index + 1 < points.size(); index += 2) {
            // the two endpoints carry the corner flag the join calculation reads
            Contour contour;
            ContourPoint start;
            start.position = points[index];
            start.flags = Corner;
            contour.points.push_back(start);
            ContourPoint end;
            end.position = points[index + 1];
            end.flags = Corner;
            contour.points.push_back(end);
            // the segments draw with the butt ends and the miter corners the retained style asks for (§6.4)
            to_device(contour);
            last = stroke_geometry(contour, stroke.width, Cap::Butt, Join::Miter);
        }
        // the one draw spans every segment the batch expanded, dimmed once by the opacity and the hairline alpha (§6.4)
        const Color dimmed = stroke.color.with_alpha(m_opacity).with_alpha(last.alpha);
        record(EmittedRange{.index_begin = index_begin, .index_count = static_cast<std::uint32_t>(m_batch.indices.size()) - index_begin}, CoverageKind::Folded, last.ramp, dimmed, 0, false);
        // the transform the draw was recorded under is restored (§6.4)
        m_transform = outer;
    }

    void AlborRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a path with no command draws nothing (§6.4, AD-06)
        if (!drawable() || !path || path->verbs().empty())
            return;
        // the retained transform lands under the current one, which places the contour where the scene put it (§6.4)
        const Transform outer = m_transform;
        m_transform = outer.then(transform);
        // every contour of the stream fills first, as one group, so an inner contour cuts a hole (§6.4)
        std::vector<Contour> contours = build_contours(path->verbs(), path->points());
        // the contours land in device space once, which the fill and the stroke both measure in (§6.3, AD-27)
        for (Contour& contour : contours)
            to_device(contour);
        if (paint.fills()) {
            // the group sweeps into one draw, which its non zero winding cuts the holes by (§6.2)
            const EmittedRange range = expand_fill_group(m_batch, contours);
            record(range, CoverageKind::Plain, 1.0f, paint.fill.with_alpha(m_opacity), 0, false);
        }
        // the strokes follow over the contours, which the recorded style asks for (§6.4)
        if (paint.strokes()) {
            for (Contour& contour : contours) {
                // the strip folds its lateral coverage around the centre and ramps its ends (nanovg technique)
                const StrokeEmission emission = stroke_geometry(contour, paint.stroke_width, Cap::Butt, Join::Miter);
                // the stroke draw carries the folded ramp, dimmed by the opacity and the hairline alpha
                record(emission.range, CoverageKind::Folded, emission.ramp, paint.stroke.with_alpha(m_opacity).with_alpha(emission.alpha), 0, false);
            }
        }
        // the transform the draw was recorded under is restored (§6.4)
        m_transform = outer;
    }

    void AlborRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture with no command draws nothing (§6.4, AD-06)
        if (!drawable() || !picture || picture->empty())
            return;
        // the picture nests under the transform the scene gave it, like any other retained draw (§6.4)
        const Transform outer = m_transform;
        m_transform = outer.then(transform);
        // the command list replays through this context, which is the one replay a frame and a picture share (§6.1, §6.4)
        replay_commands(picture->commands(), *this, m_extent);
        // the transform the draw was recorded under is restored (§6.4)
        m_transform = outer;
    }

    void AlborRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // an image with no pixels draws nothing (§6.4, AD-06)
        if (!drawable() || !image || image->pixels().empty() || destination.empty())
            return;
        // the paint dims the sampled texel through its alpha alone, which the reference does too (§6.4, AD-26)
        const float alpha = paint.fills() ? paint.fill.alpha : (paint.strokes() ? paint.stroke.alpha : 1.0f);
        const Color tint = Color::rgba(1.0f, 1.0f, 1.0f, alpha);
        // the pixels upload once per frame, which the backend then caches by the identifier it stamped (AD-06)
        const bool uploaded = std::any_of(m_batch.uploads.begin(), m_batch.uploads.end(), [&image](const BatchUpload& upload) { return upload.id == image->id(); });
        // an image the frame has not uploaded yet joins the upload list, which the backend copies before the draw
        if (!uploaded)
            m_batch.uploads.push_back(BatchUpload{.id = image->id(), .image = image});
        // the texture the backend caches is keyed on the resource identifier, which never repeats (AD-06)
        emit_image(destination, tint, image->id());
    }
} // namespace albor::render
