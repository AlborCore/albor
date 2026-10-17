#include "text/text-harfbuzz/harfbuzz_text_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <hb-ot.h>
#include <hb.h>
#include <unicode/ubrk.h>
#include <unicode/utext.h>

#include "render/font_handle.h"
#include "render/shaped_run.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

namespace albor::harfbuzz
{
    // the harfbuzz blob, face and font the engine shapes and draws through, all built over the font bytes (§9.1, AD-34)
    struct HarfbuzzTextEngine::HarfbuzzEngineState
    {
        // the blob wrapping the font bytes, which the face reads without a file system (§15, AD-34)
        hb_blob_t* blob = nullptr;

        // the face the font was built from, which the blob owns the bytes for (§9.1)
        hb_face_t* face = nullptr;

        // the harfbuzz font that shapes and draws, which is built from the face (§9.1)
        hb_font_t* font = nullptr;

        // the bytes the face was built from, which harfbuzz reads without a file (§9.1, AD-34)
        std::vector<std::uint8_t> bytes;

        // the content hash of those bytes, which is the font's identity in a layout (§15, AD-34)
        std::uint64_t font_hash = 0;

        // the resolver holding the drawing typeface, which only the platform can supply (§9.1, AD-28)
        std::unique_ptr<HarfbuzzTypefaceResolver> resolver;

        // whether the engine needs a platform typeface to be usable, which an embedded run does not (§15, AD-34)
        bool require_typeface = true;

        // the design units per em, which converts a harfbuzz position into logical pixels (§9.1, AD-27)
        float units_per_em = 1000.0f;

        // the ascender of the face in design units, which places the first baseline (§9.1)
        float ascender = 800.0f;

        // the descender of the face in design units, which places the bottom of the run (§9.1)
        float descender = -200.0f;
    };

    namespace
    {
        // read a font file into memory, because the engine identifies a font by its bytes and not by a path (§15, AD-34)
        std::vector<std::uint8_t> read_font_bytes(const std::string& path) {
            // the file is opened in binary, because a font read as text would be corrupted (§9.1)
            std::ifstream file(path, std::ios::binary);
            // a file that is not there carries no font, so the read is refused (§9.1)
            if (!file)
                return {};
            // the bytes are read whole, because a face is built from a complete font (§9.1)
            return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }
    } // namespace

    std::unique_ptr<HarfbuzzTextEngine> HarfbuzzTextEngine::create(const std::string& font_path, const std::string& family_name) {
        // an empty path names no font, so the engine is refused rather than built empty (§9.1)
        if (font_path.empty())
            return nullptr;
        // an empty family names nothing the renderer could draw, so the engine is refused (§9.1)
        if (family_name.empty())
            return nullptr;
        // the engine is built through its constructor, which loads the face and the typeface (§9.1)
        std::unique_ptr<HarfbuzzTextEngine> engine(new HarfbuzzTextEngine(font_path, family_name));
        // a face that did not load leaves an engine that is not ready, which a caller checks (§9.1)
        if (!engine->ready())
            return nullptr;
        // the engine is returned only when it can actually shape (§9.1)
        return engine;
    }

    std::unique_ptr<HarfbuzzTextEngine> HarfbuzzTextEngine::create_embedded(std::string_view family) {
        // an empty family names nothing the build carries, so the engine is refused (§9.1)
        if (family.empty())
            return nullptr;
        // the font comes from the registry the generated code filled, which is where AD-34's bytes live (§11.3, AD-34)
        const resources::EmbeddedFont* font = resources::FontRegistry::find_family(family);
        // a family the build does not carry has no font, and falling back would break identity (§15, AD-34)
        if (font == nullptr)
            return nullptr;
        // the engine is built over the bytes themselves, so no file system is consulted (§15, AD-34)
        std::unique_ptr<HarfbuzzTextEngine> engine(new HarfbuzzTextEngine(font->bytes.data(), font->bytes.size(), family));
        // an engine that could not shape the carried bytes is refused rather than half built (§9.1)
        if (!engine->ready())
            return nullptr;
        // the engine is returned only when it can actually shape (§9.1)
        return engine;
    }

    std::uint64_t HarfbuzzTextEngine::font_hash() const {
        // the hash identifies the bytes this engine shaped with, and a layout is keyed on it (§15, AD-34)
        return m_state->font_hash;
    }

    HarfbuzzTextEngine::HarfbuzzTextEngine(const std::string& font_path, const std::string& family_name) :
        m_state(std::make_unique<HarfbuzzEngineState>()) {
        // the bytes are read first, because harfbuzz builds a face from them and from nothing else (§15, AD-34)
        m_state->bytes = read_font_bytes(font_path);
        // a path that read no byte names no font, so the engine stops with no face (§9.1)
        if (m_state->bytes.empty())
            return;
        // the blob borrows the bytes, which the state owns for the whole life of the engine (§9.1)
        m_state->blob = hb_blob_create_or_fail(reinterpret_cast<const char*>(m_state->bytes.data()), static_cast<unsigned int>(m_state->bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        // a blob that did not build leaves no face to read (§9.1)
        if (m_state->blob == nullptr)
            return;
        // the face is created at index zero, which is the first face in the file (§9.1)
        m_state->face = hb_face_create(m_state->blob, 0);
        // the hash is over the bytes, so a path loaded font and an embedded one identify identically (§15, AD-34)
        m_state->font_hash = resources::hash_font_bytes(m_state->bytes.data(), m_state->bytes.size());
        // the shaper comes up over the face, and the platform typeface because this engine draws one (§9.1)
        load_shaping();
        // the typeface is resolved last, because it is the part only the path engine needs (§9.1, AD-28)
        resolve_typeface(family_name);
    }

    HarfbuzzTextEngine::HarfbuzzTextEngine(const std::uint8_t* font_bytes, std::size_t byte_count, std::string_view family_name) :
        m_state(std::make_unique<HarfbuzzEngineState>()) {
        // an embedded engine draws as outlines, so it needs no platform typeface to be usable (§15, AD-34)
        m_state->require_typeface = false;
        // the bytes are copied, because the blob borrows them and the registry owns its own (§9.1, AD-34)
        m_state->bytes.assign(font_bytes, font_bytes + byte_count);
        // a build with no byte names no font, so the engine stops with no face (§9.1)
        if (m_state->bytes.empty())
            return;
        // the blob borrows the copied bytes, which the state owns for the whole life of the engine (§9.1)
        m_state->blob = hb_blob_create_or_fail(reinterpret_cast<const char*>(m_state->bytes.data()), static_cast<unsigned int>(m_state->bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        // a blob that did not build leaves no face to read (§9.1)
        if (m_state->blob == nullptr)
            return;
        // the face is read from memory, which is the whole point: no file system is touched (§15, AD-34)
        m_state->face = hb_face_create(m_state->blob, 0);
        // the hash identifies the bytes, and it is what a layout is keyed on (§15, AD-34)
        m_state->font_hash = resources::hash_font_bytes(m_state->bytes.data(), m_state->bytes.size());
        // only the shaper comes up, because an embedded engine draws outlines and needs no typeface (§15, AD-34)
        load_shaping();
    }

    void HarfbuzzTextEngine::load_shaping() {
        // a face is only usable with at least one glyph, so an empty face stops here (§9.1)
        if (m_state->face == nullptr || hb_face_get_glyph_count(m_state->face) == 0)
            return;
        // the units per em convert design units into logical pixels, and a zero would divide by nothing (§9.1, AD-27)
        if (hb_face_get_upem(m_state->face) > 0)
            m_state->units_per_em = static_cast<float>(hb_face_get_upem(m_state->face));
        // the harfbuzz font is built over the face, which owns no shaping of its own until the funcs are set (§9.1)
        m_state->font = hb_font_create(m_state->face);
        // a font that did not build leaves an engine that cannot shape (§9.1)
        if (m_state->font == nullptr)
            return;
        // the opentype funcs give the font its advances and its outlines, which is what hb-draw walks (§9.1)
        hb_ot_font_set_funcs(m_state->font);
        // the scale is set to the design units, so the extents below read in em units rather than in pixels (§9.1, AD-27)
        hb_font_set_scale(m_state->font, static_cast<int>(m_state->units_per_em), static_cast<int>(m_state->units_per_em));
        // the extents carry the ascender and the descender, which place the baseline and the bottom of the run (§9.1)
        hb_font_extents_t extents{};
        // the extents are read only when the font answers them, because a broken face reports nothing (§9.1)
        if (hb_font_get_h_extents(m_state->font, &extents)) {
            // the ascender is the height above the baseline, which is where the run hangs from (§9.1)
            m_state->ascender = static_cast<float>(extents.ascender) / m_state->units_per_em;
            // the descender is negative in every real face, which is what places the run below its baseline (§9.1)
            m_state->descender = static_cast<float>(extents.descender) / m_state->units_per_em;
        }
    }

    void HarfbuzzTextEngine::resolve_typeface(std::string_view family_name) {
        // the drawing typeface comes from the platform, because the renderer build offers no other source (§9.1, AD-28)
        m_state->resolver = HarfbuzzTypefaceResolver::create(std::string(family_name));
        // a resolver that did not build leaves runs that measure correctly and draw nothing (§9.1, AD-28)
        if (m_state->resolver == nullptr)
            return;
    }

    HarfbuzzTextEngine::~HarfbuzzTextEngine() {
        // the harfbuzz font is destroyed before the face it was built from (§9.1)
        if (m_state->font != nullptr)
            hb_font_destroy(m_state->font);
        // the face is destroyed before the blob that owns the bytes (§9.1)
        if (m_state->face != nullptr)
            hb_face_destroy(m_state->face);
        // the blob goes last, because it borrowed bytes the state still held until now (§9.1, AD-34)
        if (m_state->blob != nullptr)
            hb_blob_destroy(m_state->blob);
    }

    namespace
    {
        // the outline the draw callbacks write into, which is a flat point list because a ui path is (§6.4, AD-06)
        struct OutlineSink
        {
            // the points walked so far, already in the logical pixels the caller asked for (§6.4, AD-27)
            std::vector<Point> points;

            // how many segments one curve is flattened into, fixed rather than adaptive so it cannot vary (§15)
            static constexpr int CURVE_SEGMENTS = 8;

            // the move callback, which starts a contour at the pen position (§6.4)
            static void move_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back, because the callbacks carry no context of their own (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the point joins the walk at the pen position hb-draw reports (§6.4)
                sink->points.push_back(Point{.x = to_x, .y = to_y});
            }

            // the line callback, which continues the current contour (§6.4)
            static void line_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the point joins the walk, extending the contour one segment (§6.4)
                sink->points.push_back(Point{.x = to_x, .y = to_y});
            }

            // the conic callback, flattened because a ui path is polygonal and a control point is off curve (§6.4)
            static void quadratic_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float control_x, float control_y, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the previous point is the contour's start of this curve, because hb-draw reports a segment (§6.4)
                const Point start = sink->points.empty() ? Point{0.0f, 0.0f} : sink->points.back();
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
                    // the sampled point joins the walk as a drawable shape (§6.4, AD-27)
                    sink->points.push_back(Point{.x = x, .y = y});
                }
            }

            // the cubic callback, which is flattened by the same fixed count and for the same reason (§6.4, §15)
            static void cubic_to(hb_draw_funcs_t*, void* draw_data, hb_draw_state_t*, float control_one_x, float control_one_y, float control_two_x, float control_two_y, float to_x, float to_y, void*) {
                // the sink is what hb-draw hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(draw_data);
                // the previous point is the curve's start, because hb-draw reports a segment (§6.4)
                const Point start = sink->points.empty() ? Point{0.0f, 0.0f} : sink->points.back();
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
                    // the sampled point joins the walk as a drawable shape (§6.4, AD-27)
                    sink->points.push_back(Point{.x = x, .y = y});
                }
            }
        };
    } // namespace

    std::vector<Point> HarfbuzzTextEngine::glyph_outline(std::uint32_t glyph_id, float font_size) const {
        // an engine with no font has no outlines, which is a refusal rather than an empty glyph (§9.1)
        if (m_state->font == nullptr)
            return {};
        // a size of no pixels cannot scale an outline to, so there is nothing to return (§9.1, AD-27)
        if (font_size <= 0.0f)
            return {};
        // the scale is set in sixty fourths, so a fractional size keeps its subpixel position (§15, AD-34)
        const int scale = static_cast<int>(font_size * 64.0f);
        // the draw callbacks report at that scale, which the sink then reduces to logical pixels (§9.1, AD-27)
        hb_font_set_scale(m_state->font, scale, scale);
        // the sink carries the points, because the callbacks carry no context of their own (§6.4)
        OutlineSink sink;
        // the draw funcs are built fresh per call, because the callbacks are stateless and cheap to attach (§6.4)
        hb_draw_funcs_t* funcs = hb_draw_funcs_create();
        // funcs that did not build walk no outline, so the glyph draws nothing (§6.4)
        if (funcs == nullptr)
            return {};
        // the contour, line, conic and cubic callbacks are the four hb-draw reports (§6.4)
        hb_draw_funcs_set_move_to_func(funcs, &OutlineSink::move_to, nullptr, nullptr);
        // the line callback continues a contour (§6.4)
        hb_draw_funcs_set_line_to_func(funcs, &OutlineSink::line_to, nullptr, nullptr);
        // the conic callback flattens a quadratic (§6.4)
        hb_draw_funcs_set_quadratic_to_func(funcs, &OutlineSink::quadratic_to, nullptr, nullptr);
        // the cubic callback flattens a cubic (§6.4)
        hb_draw_funcs_set_cubic_to_func(funcs, &OutlineSink::cubic_to, nullptr, nullptr);
        // the walk fills the sink over the font bytes, unhinted because hinting depends on a rasterizer (§15, AD-34)
        hb_font_draw_glyph(m_state->font, glyph_id, funcs, &sink);
        // the funcs are dropped once the walk is done, because the sink owns the points (§6.4)
        hb_draw_funcs_destroy(funcs);
        // every point is reported in sixty fourths, so each drops to logical pixels (§9.1, AD-27)
        for (Point& point : sink.points) {
            // the horizontal coordinate drops the sixty fourth scale to land in logical pixels (§6.4, AD-27)
            point.x /= 64.0f;
            // the vertical coordinate drops the same scale for the same reason (§6.4, AD-27)
            point.y /= 64.0f;
        }
        // the points are the outline the renderer draws (§6.4, AD-06)
        return sink.points;
    }

    bool HarfbuzzTextEngine::ready() const {
        // an embedded engine draws outlines and is usable whenever it can shape, with no typeface (§15, AD-34)
        if (!m_state->require_typeface)
            return m_state->font != nullptr;
        // the engine is usable exactly when it can shape and the renderer can draw what it shaped (§9.1, AD-28)
        return m_state->font != nullptr && m_state->resolver != nullptr && m_state->resolver->typeface() != nullptr;
    }

    bool HarfbuzzTextEngine::drawable() const {
        // an embedded run draws through outlines, so it is drawable whenever it can shape (§15, AD-34)
        if (!m_state->require_typeface)
            return m_state->font != nullptr;
        // a run is drawable exactly when the engine resolved a typeface for the renderer (§9.1, AD-28)
        return m_state->resolver != nullptr && m_state->resolver->typeface() != nullptr;
    }

    namespace
    {
        // the fixed locale the line breaker uses, because a per platform locale would break identity (§15, AD-34)
        constexpr char LINE_BREAK_LOCALE[] = "en";

        // the byte offsets a line may break at and whether each is mandatory, which ICU answers over the utf8 text (§9.1)
        std::vector<std::pair<std::size_t, bool>> line_break_offsets(std::string_view text) {
            // the offsets gathered so far, which always include the end of the text (§9.1)
            std::vector<std::pair<std::size_t, bool>> offsets;
            // the status every ICU call writes and the next call reads (§9.1)
            UErrorCode status = U_ZERO_ERROR;
            // the line breaker opens over no text yet, because the view is attached next (§9.1)
            UBreakIterator* breaker = ubrk_open(UBRK_LINE, LINE_BREAK_LOCALE, nullptr, 0, &status);
            // a breaker that did not open yields no offset, which the caller reads as one line (§9.1)
            if (U_FAILURE(status) || breaker == nullptr)
                return offsets;
            // the view exposes the utf8 bytes with utf8 native indices, so a break is already a byte offset (§9.1)
            UText* view = utext_openUTF8(nullptr, text.data(), static_cast<int64_t>(text.size()), &status);
            // a view that did not open leaves the breaker unused, so both are released (§9.1)
            if (U_FAILURE(status) || view == nullptr) {
                // the breaker is closed because no view will walk it (§9.1)
                ubrk_close(breaker);
                return offsets;
            }
            // the breaker walks the view's native indices, which is what makes every break a byte offset (§9.1)
            ubrk_setUText(breaker, view, &status);
            // the walk collects every offset only when the view attached cleanly (§9.1)
            if (U_SUCCESS(status)) {
                for (int32_t position = ubrk_first(breaker); position != UBRK_DONE; position = ubrk_next(breaker)) {
                    // a hard break is mandatory, which ends a row even when no ceiling forces one (§9.1)
                    const bool hard = ubrk_getRuleStatus(breaker) == UBRK_LINE_HARD;
                    // the offset and its mandatory flag join the walk (§9.1)
                    offsets.emplace_back(static_cast<std::size_t>(position), hard);
                }
            }
            // the view is dropped first, because the breaker holds a pointer to it (§9.1)
            utext_close(view);
            // the breaker is dropped once its walk is done (§9.1)
            ubrk_close(breaker);
            return offsets;
        }

        // the byte a row's glyphs stop at, which drops the break character so it is never shaped as a glyph (§9.1)
        std::size_t trim_row_end(std::string_view text, std::size_t start, std::size_t end, bool strip_spaces) {
            // the trimmed bound walks back from the break the row was ended at (§9.1)
            std::size_t trimmed = end;
            while (trimmed > start) {
                // the byte under the bound decides whether it is shaved off (§9.1)
                const char character = text[trimmed - 1];
                // a newline is always dropped, because it is a break and not a glyph (§9.1)
                const bool is_break = character == '\n' || character == '\r';
                // a space is dropped only when the caller asked, because a soft wrap ends on one (§9.1)
                const bool is_space = strip_spaces && (character == ' ' || character == '\t');
                // a byte that is neither keeps the row as it is (§9.1)
                if (!is_break && !is_space)
                    break;
                // the break or space byte is dropped from the glyph range (§9.1)
                trimmed--;
            }
            return trimmed;
        }
    } // namespace

    ShapedRun::Line HarfbuzzTextEngine::shape_line(std::string_view text, std::size_t start, std::size_t end, float font_size) const {
        // the row carries the metrics of its line even when it holds no glyph, which a blank row needs (§9.1)
        ShapedRun::Line line;
        // an embedded run carries no typeface, because the renderer draws its glyphs as outlines (§15, AD-34)
        line.font = m_state->resolver != nullptr ? FontHandle::from_object(m_state->resolver->typeface()) : FontHandle{};
        // the baseline sits at the ascender, which is where every glyph of the line hangs from (§9.1)
        line.origin = Point{.x = 0.0f, .y = m_state->ascender * font_size};
        // the ascent of the line is the scaled ascender, which places the row's top (§9.1)
        line.ascent = m_state->ascender * font_size;
        // the descent is the scaled descender, reported positive because it is a distance (§9.1)
        line.descent = -m_state->descender * font_size;
        // the start byte places the row in the text, which a caret resolves against (§9.1)
        line.start_byte = start;
        // the end byte is where the row's glyphs stop, which is the next row's start (§9.1)
        line.end_byte = end;
        // a range that holds no byte shapes no glyph, so the empty row is returned with its metrics (§9.1)
        if (end <= start)
            return line;
        // the buffer carries the row and its properties, and is destroyed by the unique pointer (§9.1)
        std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), &hb_buffer_destroy);
        // a buffer that did not allocate shapes nothing, so the row carries its metrics alone (§9.1)
        if (buffer == nullptr)
            return line;
        // the row's bytes are added as utf8, and the script and direction are guessed from them (§9.1)
        hb_buffer_add_utf8(buffer.get(), text.data() + start, static_cast<int>(end - start), 0, -1);
        // the guess fills in script, language and direction from the buffer's contents (§9.1)
        hb_buffer_guess_segment_properties(buffer.get());
        // the size is set in points because the font scales its own positions to it (§9.1, AD-27)
        hb_font_set_scale(m_state->font, static_cast<int>(font_size * 64.0f), static_cast<int>(font_size * 64.0f));
        // the shaping pass itself, which produces the glyph ids and their positions (§9.1)
        hb_shape(m_state->font, buffer.get(), nullptr, 0);
        // the shaped output, which carries one entry per glyph (§9.1)
        unsigned glyph_count = 0;
        hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
        // the positions are read beside the glyphs, because one position describes one glyph (§9.1)
        hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);
        // a row with no glyph keeps its metrics, which is what a blank row is (§9.1)
        if (glyph_count == 0 || glyphs == nullptr || positions == nullptr)
            return line;
        // the advance is accumulated in design units, because harfbuzz reports positions in them (§9.1)
        float advance = 0.0f;
        // the glyphs are reserved, because the shaped output reports how many there are (§9.1)
        line.glyphs.reserve(static_cast<std::size_t>(glyph_count));
        for (unsigned index = 0; index < glyph_count; index++) {
            // each glyph contributes its index and its offset from the accumulated pen (§9.1)
            const hb_glyph_position_t& position = positions[index];
            // the offset is scaled into logical pixels, because the run is measured in them (§9.1, AD-27)
            const float offset_x = static_cast<float>(position.x_offset) / 64.0f;
            // the vertical offset is the same scale, and is rare but a diacritic needs it (§9.1)
            const float offset_y = static_cast<float>(position.y_offset) / 64.0f;
            // the glyph carries the absolute byte it came from, which the row's start restores (§9.1)
            const std::uint32_t cluster = glyphs[index].cluster + static_cast<std::uint32_t>(start);
            // the glyph is recorded where the pen currently sits, carrying the byte it came from (§9.1)
            line.glyphs.push_back(ShapedGlyph{.id = glyphs[index].codepoint, .position = Point{.x = advance + offset_x, .y = -offset_y}, .cluster = cluster});
            // the pen advances by this glyph's advance, which is what lays the next one out (§9.1)
            advance += static_cast<float>(position.x_advance) / 64.0f;
        }
        // the line's advance is where a caret stands past its last glyph (§9.1)
        line.width = advance;
        return line;
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width) const {
        // an engine with no face measures nothing, because there is nothing to shape with (§9.1)
        if (!ready())
            return TextLayout{};
        // the default size is what a call site that does not name one gets (§9.1)
        return measure(text, max_width, DEFAULT_FONT_SIZE);
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width, float font_size) const {
        // an engine with no face measures nothing, because there is nothing to shape with (§9.1)
        if (!ready())
            return TextLayout{};
        // an empty string shapes no line, which is what an empty layout means (§6.2)
        if (text.empty())
            return TextLayout{};
        // a size of no pixels cannot be shaped at, so the run measures nothing (§9.1)
        if (font_size <= 0.0f)
            return TextLayout{};
        // the rows of the run, each of which the walk below stacks under the one before it (§8.2)
        std::vector<ShapedRun::Line> lines;
        // the ceiling bounds the run only when the caller asked for one below the unbounded sentinel (§8.2)
        const bool bounded = max_width > 0.0f && max_width < UNBOUNDED_WIDTH;
        // the byte offsets a line may break at and whether each is mandatory, which ICU answers over the text (§9.1)
        const std::vector<std::pair<std::size_t, bool>> offsets = line_break_offsets(text);
        // the offset from the run origin the next row's baseline sits at, which stacks the rows (§8.2)
        float pen_y = 0.0f;
        // the greedy walk starts at the first byte and lays one row at a time until the text ends (§8.2)
        std::size_t line_start = 0;
        while (line_start < text.size()) {
            // the furthest break that ends this row, which the walk below settles (§8.2)
            std::size_t line_end = line_start;
            if (!bounded) {
                // an unbounded run ends only at a mandatory break, because no ceiling forces a soft one (§9.1)
                for (const std::pair<std::size_t, bool>& offset : offsets) {
                    // a break at or before the row's start cannot end it, so it is skipped (§8.2)
                    if (offset.first <= line_start)
                        continue;
                    // the row ends at the break, and a mandatory one ends the search for this row (§9.1)
                    line_end = offset.first;
                    // a mandatory break ends the row even though nothing bounded it (§9.1)
                    if (offset.second)
                        break;
                }
            }
            else {
                // a bounded run ends at the furthest break whose glyphs still fit the ceiling (§8.2)
                for (const std::pair<std::size_t, bool>& offset : offsets) {
                    // a break at or before the row's start cannot end it, so it is skipped (§8.2)
                    if (offset.first <= line_start)
                        continue;
                    // the candidate is measured over the glyphs it would draw, which drop its break byte (§8.2)
                    const std::size_t probe_end = trim_row_end(text, line_start, offset.first, true);
                    // the probe is the row the candidate would produce (§8.2)
                    const ShapedRun::Line probe = shape_line(text, line_start, probe_end, font_size);
                    // a probe that fits is kept, and the first is always kept so the walk always advances (§8.2)
                    if (probe.width <= max_width || line_end == line_start) {
                        line_end = offset.first;
                        // a mandatory break ends the row here, even though the glyphs would fit further (§9.1)
                        if (offset.second || probe.width > max_width)
                            break;
                    }
                    else {
                        // the first probe that overflows ends the row at the last break that fit (§8.2)
                        break;
                    }
                }
            }
            // a row that still ends where it started falls back to the end, which cannot loop forever (§8.2)
            if (line_end == line_start)
                line_end = text.size();
            // the row draws the glyphs before its break, which keeps a newline out of the run (§9.1)
            const std::size_t content_end = trim_row_end(text, line_start, line_end, bounded);
            // the settled row is shaped over its glyph range, because the break byte is not a glyph (§9.1)
            ShapedRun::Line row = shape_line(text, line_start, content_end, font_size);
            // the span ends at the break, so the next row starts where this one broke (§9.1)
            row.end_byte = line_end;
            // the row's baseline sits under every row before it, which stacks the rows down the run (§8.2)
            row.origin.y += pen_y;
            // the next row starts below this one, which is the sum of the heights so far (§8.2)
            pen_y += row.ascent + row.descent;
            // the row joins the run in reading order (§9.1)
            lines.push_back(std::move(row));
            // the walk resumes at the byte after this row's break (§8.2)
            line_start = line_end;
        }
        // a run that produced no row measures nothing, which an absent layout reports (§6.2)
        if (lines.empty())
            return TextLayout{};
        // the widest row decides the run's width, which a caller reserves room for (§8.2)
        float widest = 0.0f;
        // the rows stack, so their heights sum into the run's height (§8.2)
        float total_height = 0.0f;
        for (const ShapedRun::Line& line : lines) {
            // the widest row is the one the run must reserve room for (§8.2)
            widest = std::max(widest, line.width);
            // each row adds its own ascent and descent to the run's height (§8.2)
            total_height += line.ascent + line.descent;
        }
        // the first row's ascent places the run's origin, which a caller positions against (§9.1)
        const float first_ascent = lines.front().ascent;
        // the first row's descent is reported for the same reason (§9.1)
        const float first_descent = lines.front().descent;
        // the shaped run is what the render stage replays verbatim (§9.1, AD-18)
        TextLayout layout;
        // the run is built over its rows and the size it was shaped at (§9.1)
        layout.run = ShapedRun::make(std::move(lines), font_size, m_state->font_hash, ShapeSource::Embedded);
        // the size is the widest row by the stacked height of every row (§8.2)
        layout.size = Size{.width = widest, .height = total_height};
        // the ascent is the first row's, which a caller places content by (§9.1)
        layout.ascent = first_ascent;
        // the descent is the first row's, reported positive because it is a distance (§9.1)
        layout.descent = first_descent;
        // exit
        return layout;
    }

    void install_harfbuzz_text_engine() {
        // the factory needs a font path the composition root owns, so this installs nothing until discovery lands (§9.1)
        (void)0;
    }
} // namespace albor::harfbuzz
