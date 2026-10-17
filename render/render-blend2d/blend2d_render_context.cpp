#include "render/render-blend2d/blend2d_render_context.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <blend2d.h>

#include "render/draw_command.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"
#include "render/text_layout.h"
#include "resources/embedded_font.h"

namespace albor::blend2d
{
    namespace
    {
        // the bytes and the face of one registered font, which the cache owns for the process (§9.2)
        struct CachedFontFace
        {
            // the font bytes the face was built from, owned here because blend2d borrows them (§9.2, AD-34)
            std::vector<std::uint8_t> bytes;

            // the font data over those bytes, which borrows them for the face below (§9.2)
            BLFontData data;

            // the face the fonts of a run are built from, which is immutable and independent of size (§9.2)
            BLFontFace face;
        };

        // the faces built from the registered font bytes, keyed by content hash and reused (§9.2)
        std::unordered_map<std::uint64_t, CachedFontFace>& font_face_cache() {
            // the cache outlives every frame, because a face is immutable and independent of size (§9.2)
            static std::unordered_map<std::uint64_t, CachedFontFace> cache;
            return cache;
        }

        std::mutex& font_face_cache_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        // blur an image in place with two box passes, the soft shadow edge blend2d has no primitive for (§6.2)
        void blur_box(BLImageData& data, int radius) {
            // a radius below one leaves the image untouched, because a zero window is no blur at all (§6.2)
            if (radius < 1)
                return;
            // the extent is read once, because both passes walk the same rows and then the same columns (§6.2)
            const int width = data.size.w;
            const int height = data.size.h;
            // an extent with no area has nothing to blur, so the pass is skipped (§6.2)
            if (width <= 0 || height <= 0)
                return;
            // the source is the premultiplied raster, whose palette is blue, green, red and alpha in memory (§6.3, AD-26)
            std::uint8_t* base = static_cast<std::uint8_t*>(data.pixelData);
            const std::intptr_t stride = data.stride;
            // the window is the count either side of the centre plus the centre itself (§6.2)
            const int window = radius * 2 + 1;
            // the horizontal result is held aside, because the vertical pass reads it back rather than the source (§6.2)
            std::vector<std::uint8_t> temporary(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            // every row is averaged across the window, which softens the edge horizontally (§6.2)
            for (int y = 0; y < height; y++) {
                const std::uint8_t* source_row = base + static_cast<std::intptr_t>(y) * stride;
                std::uint8_t* temporary_row = temporary.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
                for (int x = 0; x < width; x++) {
                    // the four channels are summed over the clamped window, so an edge repeats rather than darkens (§6.2)
                    int sum_blue = 0;
                    int sum_green = 0;
                    int sum_red = 0;
                    int sum_alpha = 0;
                    for (int offset = -radius; offset <= radius; offset++) {
                        // the sample is clamped to the row, because a blur has no pixel beyond the edge to read (§6.2)
                        const int sample = std::clamp(x + offset, 0, width - 1);
                        const std::uint8_t* pixel = source_row + static_cast<std::size_t>(sample) * 4;
                        sum_blue += pixel[0];
                        sum_green += pixel[1];
                        sum_red += pixel[2];
                        sum_alpha += pixel[3];
                    }
                    // the average is written in the same palette order the source used (§6.3)
                    std::uint8_t* destination = temporary_row + static_cast<std::size_t>(x) * 4;
                    destination[0] = static_cast<std::uint8_t>(sum_blue / window);
                    destination[1] = static_cast<std::uint8_t>(sum_green / window);
                    destination[2] = static_cast<std::uint8_t>(sum_red / window);
                    destination[3] = static_cast<std::uint8_t>(sum_alpha / window);
                }
            }
            // every column is averaged down the window, which softens the edge vertically (§6.2)
            for (int y = 0; y < height; y++) {
                std::uint8_t* destination_row = base + static_cast<std::intptr_t>(y) * stride;
                for (int x = 0; x < width; x++) {
                    int sum_blue = 0;
                    int sum_green = 0;
                    int sum_red = 0;
                    int sum_alpha = 0;
                    for (int offset = -radius; offset <= radius; offset++) {
                        // the sample is clamped to the column, because a blur has no pixel beyond the edge to read (§6.2)
                        const int sample = std::clamp(y + offset, 0, height - 1);
                        const std::uint8_t* pixel = temporary.data() + static_cast<std::size_t>(sample) * static_cast<std::size_t>(width) * 4 + static_cast<std::size_t>(x) * 4;
                        sum_blue += pixel[0];
                        sum_green += pixel[1];
                        sum_red += pixel[2];
                        sum_alpha += pixel[3];
                    }
                    std::uint8_t* pixel = destination_row + static_cast<std::size_t>(x) * 4;
                    pixel[0] = static_cast<std::uint8_t>(sum_blue / window);
                    pixel[1] = static_cast<std::uint8_t>(sum_green / window);
                    pixel[2] = static_cast<std::uint8_t>(sum_red / window);
                    pixel[3] = static_cast<std::uint8_t>(sum_alpha / window);
                }
            }
        }
    } // namespace

    Blend2DRenderContext::Blend2DRenderContext(BLContext* context, float device_scale, Rect full_clip) :
        m_context(context), m_device_scale(device_scale), m_clip(full_clip) {
        // a context with no raster context draws nothing, so it keeps its own bookkeeping only (§6.2)
        if (m_context == nullptr)
            return;
        // the raster context spans the physical pixels, so the base scale maps the logical points onto them (§6.3, AD-27)
        m_context->scale(m_device_scale, m_device_scale);
        // the scale is folded into the meta matrix, so every later setTransform stays in logical points (§6.3, AD-27)
        m_context->userToMeta();
    }

    void Blend2DRenderContext::save() {
        // the albor state is pushed first, because the raster save is what the pop restores (§6.2)
        m_states.push_back(State{m_transform, m_clip, m_opacity});
        // a context with no raster context keeps its own stack only (§6.2)
        if (m_context == nullptr)
            return;
        m_context->save();
    }

    void Blend2DRenderContext::restore() {
        // a restore without a matching save is a programming error, so the stack is left alone (§6.2)
        if (m_states.empty())
            return;
        // the raster context is restored before the albor state, because it holds the real clip stack (§6.2)
        if (m_context != nullptr)
            m_context->restore();
        // the saved state becomes the current one again (§6.2)
        const State state = m_states.back();
        m_states.pop_back();
        m_transform = state.transform;
        m_clip = state.clip;
        m_opacity = state.opacity;
    }

    void Blend2DRenderContext::set_transform(const Transform& transform) {
        // a context with no raster context keeps its own transform bookkeeping only (§6.2)
        if (m_context == nullptr) {
            m_transform = transform;
            return;
        }
        // the matrix is rebuilt from the albor transform, so a save and a restore round trip (§6.2)
        m_context->setTransform(to_blend_matrix(transform));
        // the base scale sits in the meta matrix under the scene transform, so a logical point spans the raster (§6.3)
        m_transform = transform;
    }

    void Blend2DRenderContext::clip_rect(const Rect& rect) {
        // the recorded clip is the intersection of what was there and what was asked for (§6.2)
        const float left = std::max(m_clip.x, rect.x);
        const float top = std::max(m_clip.y, rect.y);
        const float right = std::min(m_clip.x + m_clip.width, rect.x + rect.width);
        const float bottom = std::min(m_clip.y + m_clip.height, rect.y + rect.height);
        m_clip = Rect::from_origin_size(Point{left, top}, Size{std::max(0.0f, right - left), std::max(0.0f, bottom - top)});
        // a context with no raster context keeps its clip bookkeeping only (§6.2)
        if (m_context == nullptr)
            return;
        m_context->clipToRect(BLRect(rect.x, rect.y, rect.width, rect.height));
    }

    void Blend2DRenderContext::clip_rounded_rect(const Rect& rect, float radius) {
        // blend2d clips to a rectangle only, so a rounded clip falls back to its bounding rectangle (§6.2, AD-26)
        (void)radius;
        clip_rect(rect);
    }

    void Blend2DRenderContext::set_opacity(float opacity) {
        // the opacity is clamped into the unit range, because content computes it from data (§6.1)
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float Blend2DRenderContext::opacity() const {
        // the opacity is read back by content that composes its own on top of it (§6.1)
        return m_opacity;
    }

    const Transform& Blend2DRenderContext::transform() const {
        // the recorded transform is what a test asserts against (§6.2)
        return m_transform;
    }

    const Rect& Blend2DRenderContext::clip() const {
        // the recorded clip is what a test asserts against (§6.2)
        return m_clip;
    }

    void Blend2DRenderContext::draw_rect(const Rect& rect, const Paint& paint) {
        // a plain and a rounded rectangle differ only in the shape they build (§6.2)
        paint_shape(paint, false, rect, 0.0f);
    }

    void Blend2DRenderContext::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // the corner radii are what distinguishes this shape from a plain rectangle (§6.2)
        paint_shape(paint, true, rect, radius);
    }

    void Blend2DRenderContext::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // a context with no raster context draws nothing (§6.2)
        if (m_context == nullptr)
            return;
        // a polyline with fewer than two vertices has no segment to stroke (§6.2)
        if (points.size() < 2 || stroke.width <= 0.0f)
            return;
        // the vertices are joined into one contour, which is what makes the elbows solid rather than notched (§6.2)
        BLPath path;
        path.moveTo(points.front().x, points.front().y);
        // every vertex after the first is a line, which is what a polyline is (§6.2)
        for (std::size_t index = 1; index < points.size(); index++)
            path.lineTo(points[index].x, points[index].y);
        // the stroke carries the colour with the opacity folded in, like every stroke of a frame (§6.1)
        m_context->setStrokeStyle(to_blend_color(stroke.color));
        m_context->setStrokeWidth(stroke.width);
        // the ends and the elbows both close roundly, which is how a control glyph reads (§6.2)
        m_context->setStrokeCaps(BL_STROKE_CAP_ROUND);
        m_context->setStrokeJoin(BL_STROKE_JOIN_ROUND);
        // the polyline draws at the current transform, which is where the scene placed it (§6.2)
        m_context->strokePath(path);
    }

    void Blend2DRenderContext::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a context with no raster context casts nothing (§6.2)
        if (m_context == nullptr)
            return;
        // a cast needs both a colour and a shape, because one without the other puts no ink down (§6.2)
        if (!shadow.casts() || rect.empty())
            return;
        // the cast is the caster moved by the shadow offset, which is what lifts it off the surface (§6.2)
        const Rect cast{.x = rect.x + shadow.offset.x, .y = rect.y + shadow.offset.y, .width = rect.width, .height = rect.height};
        // a blur of zero is a hard copy of the caster, which needs no offscreen pass (§6.2)
        if (shadow.blur <= 0.0f) {
            paint_shape(Paint{.fill = shadow.color}, radius > 0.0f, cast, radius);
            return;
        }
        // three sigmas is the reach of the blur, so the offscreen covers the whole soft edge (§6.2)
        const float reach = shadow.blur * 3.0f;
        // offscreen is rendered at device resolution, so the blur is not softened twice by the meta scale (§6.2)
        const int width = std::max(1, static_cast<int>(std::ceil((rect.width + reach * 2.0f) * m_device_scale)));
        const int height = std::max(1, static_cast<int>(std::ceil((rect.height + reach * 2.0f) * m_device_scale)));
        // the offscreen is premultiplied, matching the raster the frame draws into (§6.3, AD-26)
        BLImage offscreen;
        if (offscreen.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS)
            return;
        {
            // the pass renders the silhouette alone, so the blur sees nothing but the casting shape (§6.2)
            BLContext pass;
            if (pass.begin(offscreen) != BL_SUCCESS)
                return;
            // the pass clears to transparent, because only the silhouette puts ink down (§6.2)
            pass.clearAll();
            // the pass draws at device resolution, which places the caster at the reach inside the offscreen (§6.3, AD-27)
            pass.setTransform(BLMatrix2D(m_device_scale, 0.0, 0.0, m_device_scale, m_device_scale * (reach - rect.x), m_device_scale * (reach - rect.y)));
            pass.setFillStyle(to_blend_color(shadow.color));
            // a rounded caster keeps its radii in the cast, clamped like every rounded shape (§6.2)
            if (radius > 0.0f)
                pass.fillRoundRect(rect.x, rect.y, rect.width, rect.height, std::min(radius, std::min(rect.width, rect.height) * 0.5f));
            else
                pass.fillRect(rect.x, rect.y, rect.width, rect.height);
            // the pass flushes before the silhouette is blurred in place (§6.2)
            pass.end();
        }
        // the silhouette is blurred in place, which is the soft edge blend2d has no primitive for (§6.2, AD-26)
        BLImageData data;
        if (offscreen.makeMutable(&data) != BL_SUCCESS)
            return;
        blur_box(data, static_cast<int>(std::lround(shadow.blur * m_device_scale)));
        // the blurred cast is blitted one to one at device resolution, which the meta scale maps back (§6.2)
        m_context->blitImage(BLRect(cast.x - reach, cast.y - reach, static_cast<double>(width) / m_device_scale, static_cast<double>(height) / m_device_scale), offscreen);
    }

    void Blend2DRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a context with no raster context draws nothing (§6.2)
        if (m_context == nullptr)
            return;
        // an empty run has no glyphs to draw, so it costs nothing (§6.2)
        if (layout.empty())
            return;
        // the text takes the fill colour when the paint fills, and the stroke colour otherwise (§6.2)
        m_context->setFillStyle(to_blend_color(paint.fills() ? paint.fill : paint.stroke));
        // the run is the shaped output stage one measured, so the glyphs are replayed as positioned (§9.1, AD-18)
        const ShapedRun& run = *layout.run;
        // every line carries its own typeface and its own baseline (§9.1)
        for (const ShapedRun::Line& line : run.lines()) {
            // a line with no glyph draws nothing, which a space is (§6.2)
            if (line.glyphs.empty())
                continue;
            // the font is resolved from the registered bytes, because blend2d loads a face rather than borrowing (§9.2)
            BLFont font = resolve_font(run.font_hash(), run.font_size());
            // a line this backend cannot resolve is skipped rather than guessed at (§3.5, §9.1)
            if (font.empty())
                continue;
            // the glyph indices and their positions are packed into the two arrays a glyph run reads (§9.1)
            std::vector<std::uint32_t> glyph_ids;
            glyph_ids.reserve(line.glyphs.size());
            std::vector<BLPoint> placements;
            placements.reserve(line.glyphs.size());
            // every shaped glyph contributes its index and its position relative to the line origin (§9.1)
            for (const ShapedGlyph& glyph : line.glyphs) {
                glyph_ids.push_back(glyph.id);
                placements.push_back(BLPoint(glyph.position.x, glyph.position.y));
            }
            // the run carries absolute user space placements, which the glyph outline pass honours (§9.1)
            BLGlyphRun glyph_run;
            glyph_run.glyphData = glyph_ids.data();
            glyph_run.placementData = placements.data();
            glyph_run.size = glyph_ids.size();
            glyph_run.reserved = 0;
            glyph_run.placementType = BL_GLYPH_PLACEMENT_TYPE_USER_UNITS;
            glyph_run.glyphAdvance = static_cast<std::int8_t>(sizeof(std::uint32_t));
            glyph_run.placementAdvance = static_cast<std::int8_t>(sizeof(BLPoint));
            glyph_run.flags = BL_GLYPH_RUN_NO_FLAGS;
            // the line origin is the baseline, added to the run origin the caller placed the run at (§9.1)
            m_context->fillGlyphRun(BLPoint(origin.x + line.origin.x, origin.y + line.origin.y), font, glyph_run);
        }
    }

    std::size_t Blend2DRenderContext::depth() const {
        // the depth is what a test asserts the saves and restores produced (§6.2)
        return m_states.size();
    }

    BLRgba32 Blend2DRenderContext::to_blend_color(const Color& color) const {
        // with_alpha already scales the colour's own alpha, so passing the opacity alone scales it once (§6.1)
        const Color dimmed = color.with_alpha(m_opacity);
        // the albor colour is in the zero to one range, while blend2d packs one byte per channel (§6.3)
        const auto channel = [](float value) -> std::uint8_t {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(clamped * 255.0f);
        };
        // the channels are given in the red, green, blue and alpha order blend2d stores them in (§6.3)
        return BLRgba32(channel(dimmed.red), channel(dimmed.green), channel(dimmed.blue), channel(dimmed.alpha));
    }

    void Blend2DRenderContext::paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius) {
        // a context with no raster context draws nothing (§6.2)
        if (m_context == nullptr)
            return;
        // a shape with no area contributes nothing, so it is skipped rather than drawn degenerate (§6.2)
        if (rect.empty())
            return;
        // a rounded shape clamps its radius, because a radius larger than half the extent cannot be drawn (§6.2)
        const double effective_radius = rounded ? static_cast<double>(std::min(radius, std::min(rect.width, rect.height) * 0.5f)) : 0.0;
        // a transparent fill draws nothing, which is how a paint carries only a stroke (§6.2)
        if (paint.fills()) {
            m_context->setFillStyle(to_blend_color(paint.fill));
            // the fill of a plain and of a rounded rectangle differ only in the shape itself (§6.2)
            if (rounded)
                m_context->fillRoundRect(rect.x, rect.y, rect.width, rect.height, effective_radius);
            else
                m_context->fillRect(rect.x, rect.y, rect.width, rect.height);
        }
        // a transparent or a zero width stroke draws nothing (§6.2)
        if (paint.strokes()) {
            m_context->setStrokeStyle(to_blend_color(paint.stroke));
            m_context->setStrokeWidth(paint.stroke_width);
            // the default butt cap and miter join are restored here, because a polyline leaves round ones behind (§6.2)
            m_context->setStrokeCaps(BL_STROKE_CAP_BUTT);
            m_context->setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
            // the stroke of a plain and of a rounded rectangle differ only in the shape itself (§6.2)
            if (rounded)
                m_context->strokeRoundRect(rect.x, rect.y, rect.width, rect.height, effective_radius);
            else
                m_context->strokeRect(rect.x, rect.y, rect.width, rect.height);
        }
    }

    BLMatrix2D Blend2DRenderContext::to_blend_matrix(const Transform& transform) {
        // the matrix mirrors the albor transform with the same column order the scale, skew and translate follow (§6.3)
        return BLMatrix2D(transform.scale_x, transform.skew_y, transform.skew_x, transform.scale_y, transform.translate_x, transform.translate_y);
    }

    BLPath Blend2DRenderContext::build_path(std::span<const PathVerb> verbs, std::span<const Point> points) {
        // the path starts empty, because a malformed stream is refused before it reaches here (§6.4, AD-06)
        BLPath path;
        // the point index advances with every consuming verb (§6.4)
        std::size_t point_index = 0;
        for (const PathVerb verb : verbs) {
            // a move starts a new contour at the next point (§6.4)
            if (verb == PathVerb::MoveTo) {
                path.moveTo(points[point_index].x, points[point_index].y);
                point_index++;
            }
            // a line draws to the next point (§6.4)
            else if (verb == PathVerb::LineTo) {
                path.lineTo(points[point_index].x, points[point_index].y);
                point_index++;
            }
            // a close runs the contour back to its start and consumes no point (§6.4)
            else if (verb == PathVerb::Close) {
                path.close();
            }
        }
        return path;
    }

    BLFont Blend2DRenderContext::resolve_font(std::uint64_t font_hash, float font_size) {
        // the registered font is the only byte source here, because a handle borrows a foreign typeface (§9.2)
        const resources::EmbeddedFont* embedded = resources::FontRegistry::find_hash(font_hash);
        // a run whose font is not registered carries no bytes this backend can load, so it is skipped (§9.2, AD-34)
        if (embedded == nullptr)
            return BLFont();
        // the lookup and the cache share one lock, because a second render thread races otherwise (§9.2, AD-18)
        const std::lock_guard<std::mutex> lock(font_face_cache_mutex());
        // the cache is keyed by content hash, because a face is immutable and independent of size (§9.2, §15)
        std::unordered_map<std::uint64_t, CachedFontFace>& faces = font_face_cache();
        auto found = faces.find(font_hash);
        if (found == faces.end()) {
            // the entry owns a copy of the bytes, because the registry may clear while a face still draws (§9.2)
            CachedFontFace entry;
            entry.bytes.assign(embedded->bytes.begin(), embedded->bytes.end());
            // the data borrows the owned bytes, which outlive it inside the cache entry (§9.2, AD-34)
            if (entry.data.createFromData(entry.bytes.data(), entry.bytes.size()) != BL_SUCCESS)
                return BLFont();
            // the face is built from the data, which borrows the bytes the entry keeps (§9.2)
            if (entry.face.createFromData(entry.data, 0) != BL_SUCCESS)
                return BLFont();
            found = faces.emplace(font_hash, std::move(entry)).first;
        }
        // the font is built at the size the run was shaped at, which maps the outlines to logical pixels (§9.1)
        BLFont font;
        if (font.createFromFace(found->second.face, font_size) != BL_SUCCESS)
            return BLFont();
        return font;
    }

    void Blend2DRenderContext::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a context with no raster context draws nothing (§6.4)
        if (m_context == nullptr)
            return;
        // a batch with no segment draws nothing (§6.4, AD-06)
        if (!batch || batch->segment_count() == 0)
            return;
        // every consecutive point pair lifts to one segment, which the batched replay walks (§6.4)
        const std::span<const Point> points = batch->points();
        BLPath path;
        for (std::size_t index = 0; index < points.size(); index += 2) {
            path.moveTo(points[index].x, points[index].y);
            path.lineTo(points[index + 1].x, points[index + 1].y);
        }
        // the retained transform concats under the current one, so the segments land where the scene placed them (§6.4)
        m_context->save();
        m_context->applyTransform(to_blend_matrix(transform));
        m_context->setStrokeStyle(to_blend_color(stroke.color));
        m_context->setStrokeWidth(stroke.width);
        // the butt cap and miter join match the skia reference, which sets neither for a batched stroke (§6.2, AD-26)
        m_context->setStrokeCaps(BL_STROKE_CAP_BUTT);
        m_context->setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
        m_context->strokePath(path);
        m_context->restore();
    }

    void Blend2DRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a context with no raster context draws nothing (§6.4)
        if (m_context == nullptr)
            return;
        // a path with no command draws nothing (§6.4, AD-06)
        if (!path || path->verbs().empty())
            return;
        // the contour is rebuilt in one pass, because the resource validates the command stream on creation (§6.4, AD-06)
        const BLPath contour = build_path(path->verbs(), path->points());
        // the retained transform concats under the current one, so the contour lands where the scene placed it (§6.4)
        m_context->save();
        m_context->applyTransform(to_blend_matrix(transform));
        // a transparent fill draws nothing, which is how the paint carries only a stroke (§6.4)
        if (paint.fills()) {
            m_context->setFillStyle(to_blend_color(paint.fill));
            m_context->fillPath(contour);
        }
        // a transparent or a zero width stroke draws nothing (§6.4)
        if (paint.strokes()) {
            m_context->setStrokeStyle(to_blend_color(paint.stroke));
            m_context->setStrokeWidth(paint.stroke_width);
            // the default butt cap and miter join are what a contour strokes with, like the skia reference (§6.2)
            m_context->setStrokeCaps(BL_STROKE_CAP_BUTT);
            m_context->setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
            m_context->strokePath(contour);
        }
        m_context->restore();
    }

    void Blend2DRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture with no command draws nothing (§6.4, AD-06)
        if (!picture || picture->empty())
            return;
        // the list nests under the transform the scene gave it, like any other retained draw (§6.4)
        if (m_context != nullptr) {
            m_context->save();
            m_context->applyTransform(to_blend_matrix(transform));
        }
        // the recorded clip applies to the list, and a narrower one opens a switch of its own (§6.2)
        replay_commands(picture->commands(), *this, m_clip);
        // the list is closed, because a picture draws inside the scope it was given (§6.2)
        if (m_context != nullptr)
            m_context->restore();
    }

    void Blend2DRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        if (m_context == nullptr)
            return;
        if (!image || image->pixels().empty())
            return;
        if (destination.empty())
            return;
        // the extent is the pixel grid the raster and the drawable share, which a zero size cannot host (§6.4)
        const int width = static_cast<int>(image->width());
        const int height = static_cast<int>(image->height());
        if (width <= 0 || height <= 0)
            return;
        // the reference treats image bytes as premultiplied rgba8, a fill paints opaque to the alpha (§6.4, AD-26, §6.1)
        float paint_alpha = 1.0f;
        if (paint.fills())
            paint_alpha = paint.fill.alpha;
        else if (paint.strokes())
            paint_alpha = paint.stroke.alpha;
        paint_alpha = std::clamp(paint_alpha * m_opacity, 0.0f, 1.0f);
        const int modulation = static_cast<int>(std::lround(paint_alpha * 255.0f));
        BLImage raster;
        if (raster.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS)
            return;
        BLImageData data;
        if (raster.makeMutable(&data) != BL_SUCCESS)
            return;
        // resource bytes are premultiplied rgba8, while raster is premultiplied bgra (§6.4, AD-26)
        const std::span<const std::uint8_t> source_pixels = image->pixels();
        for (int y = 0; y < height; y++) {
            const std::uint8_t* source = source_pixels.data() + static_cast<std::size_t>(y) * image->row_bytes();
            std::uint8_t* target = static_cast<std::uint8_t*>(data.pixelData) + static_cast<std::intptr_t>(y) * data.stride;
            for (int x = 0; x < width; x++) {
                // red and blue swap into bgra order, and the shared alpha modulation is applied (§6.4, AD-26)
                target[0] = static_cast<std::uint8_t>(source[2] * modulation / 255);
                target[1] = static_cast<std::uint8_t>(source[1] * modulation / 255);
                target[2] = static_cast<std::uint8_t>(source[0] * modulation / 255);
                target[3] = static_cast<std::uint8_t>(source[3] * modulation / 255);
                source += 4;
                target += 4;
            }
        }
        m_context->blitImage(BLRect(destination.x, destination.y, destination.width, destination.height), raster);
    }
} // namespace albor::blend2d
