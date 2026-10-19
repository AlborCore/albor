#include "text/text-harfbuzz/harfbuzz_font_face.h"

#include <hb-ot.h>

#include "resources/embedded_font.h"

namespace albor::harfbuzz
{
    // the harfbuzz handles a face owns, which no header may name (§3.5, AD-19)
    struct HarfbuzzFontFace::State
    {
        // the bytes the blob borrows, which the face keeps alive for its whole life (§9.1, AD-34)
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;

        // the blob borrows those bytes, so the face never copies them (§9.1, AD-34)
        hb_blob_t* blob = nullptr;

        // the face reads the font tables out of that blob (§9.1)
        hb_face_t* face = nullptr;

        // the content hash of the bytes, which is the identity of the shape they carry (§15, AD-34)
        std::uint64_t hash = 0;

        // the handles are released in the order harfbuzz requires, the face before the blob it reads (§9.1)
        ~State() {
            // the face goes first, because it holds a reference to the blob below (§9.1)
            if (face != nullptr)
                hb_face_destroy(face);
            // the blob goes once nothing reads it any more (§9.1)
            if (blob != nullptr)
                hb_blob_destroy(blob);
        }
    };

    namespace
    {
        // the outline the draw callbacks write into, which keeps every contour apart rather than one point run (§6.4)
        struct OutlineSink
        {
            // the verbs and points walked so far, already in the logical pixels the caller asked for (§6.4, AD-27)
            GlyphOutlineData outline;

            // whether a contour is still open, so a move after one seals it before opening the next (§6.4)
            bool open = false;

            // how many segments one curve takes, fixed so a platform and an embedded run of one face agree (§15, AD-34)
            static constexpr int CURVE_SEGMENTS = 8;

            // append one verb and the point it consumes, which is the only place the stream grows (§6.4)
            void append(PathVerb verb, float x, float y) {
                // the verb joins the stream in drawing order, which the replay walks with a point cursor (§6.4)
                outline.verbs.push_back(verb);
                // the point the verb consumes follows in the same order, so the two lists stay paired (§6.4)
                outline.points.push_back(Point{.x = x, .y = y});
                // a move opens the contour it starts, while a line leaves it as it already was (§6.4)
                if (verb == PathVerb::MoveTo)
                    open = true;
            }

            // seal the contour under construction, which a second close leaves alone (§6.4)
            void seal() {
                // a stream with no open contour takes no close, because a close without a move names nothing (§6.4)
                if (!open)
                    return;
                // the close consumes no point and ends the contour where it began (§6.4)
                outline.verbs.push_back(PathVerb::Close);
                // the contour is sealed, so the next move opens a fresh one (§6.4)
                open = false;
            }

            // the move callback, which starts a contour at the pen position (§6.4)
            static void move_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back, because the callbacks carry no context of their own (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // a move after an unclosed contour seals it, so one ring cannot swallow the next (§6.4)
                sink->seal();
                // the move opens the next contour at the position hb-draw reports (§6.4)
                sink->append(PathVerb::MoveTo, to_x, to_y);
            }

            // the line callback, which continues the current contour (§6.4)
            static void line_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the point extends the contour by one straight segment (§6.4)
                sink->append(PathVerb::LineTo, to_x, to_y);
            }

            // the close callback, which ends the contour a move opened (§6.4)
            static void close_path(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the contour is sealed here rather than at the next move, which keeps its own ring (§6.4)
                sink->seal();
            }

            // the conic callback, flattened because a ui path is polygonal and a control point is off curve (§6.4)
            static void quadratic_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float control_x, float control_y, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the previous point is the contour's start of this curve, because hb-draw reports a segment (§6.4)
                const Point start = sink->outline.points.empty() ? Point{0.0f, 0.0f} : sink->outline.points.back();
                // every segment is sampled, so the polyline follows the curve rather than cutting across it (§6.4)
                for (int step = 1; step <= CURVE_SEGMENTS; step++) {
                    // the parameter runs over the segment, which is why the count is fixed (§6.4, §15)
                    const float t = static_cast<float>(step) / static_cast<float>(CURVE_SEGMENTS);
                    // the inverse parameter is what the quadratic basis needs (§6.4)
                    const float inverse = 1.0f - t;
                    // the curve is evaluated in the scaled units hb-draw reported (§9.1)
                    const float x = inverse * inverse * start.x + 2.0f * inverse * t * control_x + t * t * to_x;
                    // the vertical coordinate follows the same basis (§9.1)
                    const float y = inverse * inverse * start.y + 2.0f * inverse * t * control_y + t * t * to_y;
                    // the sampled point joins the walk as a straight segment of the tessellation (§6.4, AD-27)
                    sink->append(PathVerb::LineTo, x, y);
                }
            }

            // the cubic callback, which is flattened by the same fixed count and for the same reason (§6.4, §15)
            static void cubic_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float control_one_x, float control_one_y, float control_two_x, float control_two_y, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the previous point is the curve's start, because hb-draw reports a segment (§6.4)
                const Point start = sink->outline.points.empty() ? Point{0.0f, 0.0f} : sink->outline.points.back();
                // every segment is sampled, so the polyline follows the curve (§6.4)
                for (int step = 1; step <= CURVE_SEGMENTS; step++) {
                    // the parameter runs over the segment, which is why the count is fixed (§6.4, §15)
                    const float t = static_cast<float>(step) / static_cast<float>(CURVE_SEGMENTS);
                    // the inverse parameter is what the cubic basis needs (§6.4)
                    const float inverse = 1.0f - t;
                    // the three weights of a cubic at this parameter, which are the basis functions (§6.4)
                    const float w0 = inverse * inverse * inverse;
                    // the middle weight multiplies the first control point (§6.4)
                    const float w1 = 3.0f * inverse * inverse * t;
                    // the second middle weight multiplies the second control point (§6.4)
                    const float w2 = 3.0f * inverse * t * t;
                    // the last weight multiplies the curve's end (§6.4)
                    const float w3 = t * t * t;
                    // the curve is evaluated in the scaled units hb-draw reported (§9.1)
                    const float x = w0 * start.x + w1 * control_one_x + w2 * control_two_x + w3 * to_x;
                    // the vertical coordinate follows the same basis (§9.1)
                    const float y = w0 * start.y + w1 * control_one_y + w2 * control_two_y + w3 * to_y;
                    // the sampled point joins the walk as a straight segment of the tessellation (§6.4, AD-27)
                    sink->append(PathVerb::LineTo, x, y);
                }
            }
        };
    } // namespace

    std::shared_ptr<const HarfbuzzFontFace> HarfbuzzFontFace::create(std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
        // a font with no byte cannot be read, so the face is refused rather than built empty (§9.1)
        if (bytes == nullptr || bytes->empty())
            return nullptr;
        // the state owns the bytes before the blob borrows them (§9.1, AD-34)
        auto state = std::make_unique<State>();
        state->bytes = std::move(bytes);
        // the hash is over the bytes, which is the identity a layout and a cache key on (§15, AD-34)
        state->hash = resources::hash_font_bytes(state->bytes->data(), state->bytes->size());
        // the blob borrows the bytes the state owns for the whole life of the face (§9.1)
        state->blob = hb_blob_create(reinterpret_cast<const char*>(state->bytes->data()), static_cast<unsigned int>(state->bytes->size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        // the face is read at index zero, which is the first face in the file (§9.1)
        state->face = hb_face_create(state->blob, 0);
        // a face with no glyph cannot outline anything, so it is refused rather than handed out (§9.1)
        if (state->face == nullptr || hb_face_get_glyph_count(state->face) == 0)
            return nullptr;
        // the face is immutable from here, so a render thread may read it while a ui thread shapes (§10.5, AD-18)
        return std::shared_ptr<const HarfbuzzFontFace>(new HarfbuzzFontFace(std::move(state)));
    }

    HarfbuzzFontFace::HarfbuzzFontFace(std::unique_ptr<State> state) :
        m_state(std::move(state)) {}

    HarfbuzzFontFace::~HarfbuzzFontFace() = default;

    std::uint64_t HarfbuzzFontFace::hash() const {
        // the content hash is fixed at construction, because the bytes never change (§15, AD-34)
        return m_state->hash;
    }

    std::span<const std::uint8_t> HarfbuzzFontFace::bytes() const {
        // the bytes are handed out read only, because they belong to the face (§15, AD-34)
        return *m_state->bytes;
    }

    GlyphOutlineData HarfbuzzFontFace::outline(std::uint32_t glyph_id, float font_size) const {
        // a size of no pixels cannot scale an outline to, so there is nothing to return (§9.1, AD-27)
        if (font_size <= 0.0f)
            return {};
        // the scale is set in sixty fourths, so a fractional size keeps its subpixel position (§15, AD-34)
        const int scale = static_cast<int>(font_size * 64.0f);
        // a font of its own keeps the walk out of every other thread's state, which the render thread needs (§10.5, AD-18)
        hb_font_t* font = hb_font_create(m_state->face);
        // the opentype funcs give the font its outlines, which is what hb-draw walks (§9.1)
        hb_ot_font_set_funcs(font);
        // the scale turns design units into the pixels the caller asked for (§9.1, AD-27)
        hb_font_set_scale(font, scale, scale);
        // the sink carries the verbs and points, because the callbacks carry no context of their own (§6.4)
        OutlineSink sink;
        // the draw funcs are built fresh per call, because the callbacks are stateless and cheap to attach (§6.4)
        hb_draw_funcs_t* funcs = hb_draw_funcs_create();
        // the move, line, close, conic and cubic callbacks are the five hb-draw reports (§6.4)
        hb_draw_funcs_set_move_to_func(funcs, &OutlineSink::move_to, nullptr, nullptr);
        // the line callback continues a contour (§6.4)
        hb_draw_funcs_set_line_to_func(funcs, &OutlineSink::line_to, nullptr, nullptr);
        // the conic callback flattens a quadratic (§6.4)
        hb_draw_funcs_set_quadratic_to_func(funcs, &OutlineSink::quadratic_to, nullptr, nullptr);
        // the cubic callback flattens a cubic (§6.4)
        hb_draw_funcs_set_cubic_to_func(funcs, &OutlineSink::cubic_to, nullptr, nullptr);
        // the close callback ends a contour, which is what keeps a counter a ring of its own (§6.4)
        hb_draw_funcs_set_close_path_func(funcs, &OutlineSink::close_path, nullptr, nullptr);
        // the walk fills the sink over the font bytes, unhinted because hinting depends on a rasterizer (§15, AD-34)
        hb_font_draw_glyph(font, glyph_id, funcs, &sink);
        // the funcs are dropped once the walk is done, because the sink owns the outlines (§6.4)
        hb_draw_funcs_destroy(funcs);
        // the per call font goes back, because the sink owns what it produced (§9.1)
        hb_font_destroy(font);
        // a contour the font left open is sealed here, so the stream always ends a ring it started (§6.4)
        sink.seal();
        // every point is reported in sixty fourths, so each drops to logical pixels (§9.1, AD-27)
        for (Point& point : sink.outline.points) {
            // the horizontal coordinate drops the sixty fourth scale to land in logical pixels (§6.4, AD-27)
            point.x /= 64.0f;
            // the vertical coordinate flips the font's y up axis to the y down one a surface draws in (§9.1, AD-27)
            point.y = -point.y / 64.0f;
        }
        // the contours are the outline the renderer draws (§6.4, AD-06)
        return sink.outline;
    }
} // namespace albor::harfbuzz
