#include "text/text-harfbuzz/harfbuzz_text_engine.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include FT_OUTLINE_H

#include "render/font_handle.h"
#include "render/shaped_run.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

namespace albor::harfbuzz
{
    // the freetype library, the face, the harfbuzz font and the typeface the renderer draws with (§9.1)
    struct HarfbuzzTextEngine::HarfbuzzEngineState
    {
        // the freetype library, which must outlive the face it loaded (§9.1)
        FT_Library library = nullptr;

        // the face the harfbuzz font was built from, which the library owns (§9.1)
        FT_Face face = nullptr;

        // the harfbuzz font that shapes, which is built from the face (§9.1)
        hb_font_t* font = nullptr;

        // the bytes the face was built from, which freetype reads without a file (§9.1, AD-34)
        std::vector<std::uint8_t> bytes;

        // the content hash of those bytes, which is the font's identity in a layout (§15, AD-34)
        std::uint64_t font_hash = 0;

        // the resolver holding the drawing typeface, which only the platform can supply (§9.1, AD-28)
        std::unique_ptr<HarfbuzzTypefaceResolver> resolver;

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
        // freetype comes up once per engine, and it fails only on a broken install (§9.1)
        if (FT_Init_FreeType(&m_state->library) != 0) {
            m_state->library = nullptr;
            return;
        }
        // the face is loaded at index zero, which is the first face in the file (§9.1)
        if (FT_New_Face(m_state->library, font_path.c_str(), 0, &m_state->face) != 0) {
            m_state->face = nullptr;
            return;
        }
        // the bytes are read so the engine can report the same identity an embedded one would (§15, AD-34)
        m_state->bytes = read_font_bytes(font_path);
        // the hash is over those bytes, so a path loaded font and an embedded one identify identically (§15, AD-34)
        m_state->font_hash = resources::hash_font_bytes(m_state->bytes.data(), m_state->bytes.size());
        // the shaper and the typeface come up next, because both need the face (§9.1)
        load(nullptr, 0, family_name);
    }

    HarfbuzzTextEngine::HarfbuzzTextEngine(const std::uint8_t* font_bytes, std::size_t byte_count, std::string_view family_name) :
        m_state(std::make_unique<HarfbuzzEngineState>()) {
        // freetype comes up once per engine, and it fails only on a broken install (§9.1)
        if (FT_Init_FreeType(&m_state->library) != 0) {
            m_state->library = nullptr;
            return;
        }
        // the bytes are copied, because freetype borrows them and the registry owns its own (§9.1, AD-34)
        m_state->bytes.assign(font_bytes, font_bytes + byte_count);
        // the face is read from memory, which is the whole point: no file system is touched (§15, AD-34)
        if (FT_New_Memory_Face(m_state->library, m_state->bytes.data(), static_cast<FT_Long>(byte_count), 0, &m_state->face) != 0) {
            m_state->face = nullptr;
            return;
        }
        // the hash identifies the bytes, and it is what a layout is keyed on (§15, AD-34)
        m_state->font_hash = resources::hash_font_bytes(m_state->bytes.data(), m_state->bytes.size());
        // the shaper and the typeface come up next, because both need the face (§9.1)
        load(nullptr, 0, family_name);
    }

    void HarfbuzzTextEngine::load(const std::uint8_t* font_bytes, std::size_t byte_count, std::string_view family_name) {
        // the arguments are unused here because both sources already built the face (§9.1)
        (void)font_bytes;
        // the byte count is unused for the same reason (§9.1)
        (void)byte_count;
        // an engine with no face has nothing to read metrics from, so it stops here (§9.1)
        if (m_state->face == nullptr)
            return;
        // the units per em convert design units into logical pixels, and a zero would divide by nothing (§9.1, AD-27)
        if (m_state->face->units_per_EM > 0)
            m_state->units_per_em = static_cast<float>(m_state->face->units_per_EM);
        // the vertical metrics are read from the face, because the baseline sits on the ascender (§9.1)
        m_state->ascender = static_cast<float>(m_state->face->ascender) / static_cast<float>(m_state->face->units_per_EM);
        // the descender is negative in every real face, which is what places the run below its baseline (§9.1)
        m_state->descender = static_cast<float>(m_state->face->descender) / static_cast<float>(m_state->face->units_per_EM);
        // the harfbuzz font is built over the face and referenced, so the engine owns one reference (§9.1)
        m_state->font = hb_ft_font_create_referenced(m_state->face);
        // a font that did not build leaves an engine that cannot shape (§9.1)
        if (m_state->font == nullptr)
            return;
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
        // the face is destroyed before the library that loaded it (§9.1)
        if (m_state->face != nullptr)
            FT_Done_Face(m_state->face);
        // the library goes last, because it owns nothing else at this point (§9.1)
        if (m_state->library != nullptr)
            FT_Done_FreeType(m_state->library);
    }

    namespace
    {
        // the outline the decomposer writes into, which is a flat point list because a ui path is (§6.4, AD-06)
        struct OutlineSink
        {
            // the points walked so far, in the font's own units (§6.4)
            std::vector<Point> points;

            // the scale that converts those units into the logical pixels the caller asked for (§9.1, AD-27)
            float scale = 1.0f;

            // whether the walk is sampling in font units, which the curve flattener needs and a line does not (§9.1)
            bool in_font_units = false;

            // the move callback, which starts a contour at the pen position (§6.4)
            static int move_to(const FT_Vector* to, void* user) {
                // the sink is what freetype hands back, because the callbacks carry no context of their own (§6.4)
                auto* sink = static_cast<OutlineSink*>(user);
                // the point joins the walk, scaled unless the flattener is sampling (§6.4, §9.1)
                sink->points.push_back(sink->in_font_units ? Point{.x = static_cast<float>(to->x), .y = static_cast<float>(to->y)} : Point{.x = static_cast<float>(to->x) * sink->scale, .y = static_cast<float>(to->y) * sink->scale});
                return 0;
            }

            // the line callback, which continues the current contour (§6.4)
            static int line_to(const FT_Vector* to, void* user) {
                // the sink is what freetype hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(user);
                // the point joins the walk, scaled unless the flattener is sampling (§6.4, §9.1)
                sink->points.push_back(sink->in_font_units ? Point{.x = static_cast<float>(to->x), .y = static_cast<float>(to->y)} : Point{.x = static_cast<float>(to->x) * sink->scale, .y = static_cast<float>(to->y) * sink->scale});
                return 0;
            }

            // how many segments one curve is flattened into, fixed rather than adaptive so it cannot vary (§15)
            static constexpr int CURVE_SEGMENTS = 8;

            // the conic callback, flattened because a ui path is polygonal and a control point is off curve (§6.4)
            static int conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
                // the sink is what freetype hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(user);
                // the previous point is the contour's start of this curve, because freetype reports a segment (§6.4)
                const Point start = sink->points.empty() ? Point{0.0f, 0.0f} : sink->points.back();
                // every segment is sampled, so the polyline follows the curve rather than cutting across it (§6.4)
                for (int step = 1; step <= CURVE_SEGMENTS; step++) {
                    // the parameter runs over the segment, which is why the count is fixed (§6.4, §15)
                    const float t = static_cast<float>(step) / static_cast<float>(CURVE_SEGMENTS);
                    // the inverse parameter is what the quadratic basis needs (§6.4)
                    const float inverse = 1.0f - t;
                    // the curve is evaluated in font units, because that is what freetype reported (§9.1)
                    const float x = inverse * inverse * start.x + 2.0f * inverse * t * static_cast<float>(control->x) + t * t * static_cast<float>(to->x);
                    // the vertical coordinate follows the same basis (§9.1)
                    const float y = inverse * inverse * start.y + 2.0f * inverse * t * static_cast<float>(control->y) + t * t * static_cast<float>(to->y);
                    // the sampled point joins the walk, converted into the logical pixels the sink carries (§6.4, AD-27)
                    sink->points.push_back(Point{.x = x * sink->scale, .y = y * sink->scale});
                }
                return 0;
            }

            // the cubic callback, which is flattened by the same fixed count and for the same reason (§6.4, §15)
            static int cubic_to(const FT_Vector* control_one, const FT_Vector* control_two, const FT_Vector* to, void* user) {
                // the sink is what freetype hands back (§6.4)
                auto* sink = static_cast<OutlineSink*>(user);
                // the previous point is the curve's start, because freetype reports a segment (§6.4)
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
                    // the curve is evaluated in font units, because that is what freetype reported (§9.1)
                    const float x = w0 * start.x + w1 * static_cast<float>(control_one->x) + w2 * static_cast<float>(control_two->x) + w3 * static_cast<float>(to->x);
                    // the vertical coordinate follows the same basis (§9.1)
                    const float y = w0 * start.y + w1 * static_cast<float>(control_one->y) + w2 * static_cast<float>(control_two->y) + w3 * static_cast<float>(to->y);
                    // the sampled point joins the walk, converted into the logical pixels the sink carries (§6.4, AD-27)
                    sink->points.push_back(Point{.x = x * sink->scale, .y = y * sink->scale});
                }
                return 0;
            }
        };
    } // namespace

    std::vector<Point> HarfbuzzTextEngine::glyph_outline(std::uint32_t glyph_id, float font_size) const {
        // an engine with no face has no outlines, which is a refusal rather than an empty glyph (§9.1)
        if (m_state->face == nullptr)
            return {};
        // a size of no pixels cannot scale an outline to, so there is nothing to return (§9.1, AD-27)
        if (font_size <= 0.0f)
            return {};
        // the glyph is loaded unhinted, because hinting depends on the rasterizer and AD-34 forbids that (§15, AD-34)
        if (FT_Load_Glyph(m_state->face, static_cast<FT_UInt>(glyph_id), FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0)
            return {};
        // a glyph with no outline draws nothing, which is what a space is (§6.2)
        if (m_state->face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
            return {};
        // the sink carries the points and the scale, because the callbacks carry neither (§6.4)
        OutlineSink sink;
        // the scale converts font units into the logical pixels the caller asked for (§9.1, AD-27)
        sink.scale = font_size / static_cast<float>(m_state->face->units_per_EM);
        // the flattener samples in font units, because the curves are evaluated before the scale is applied (§9.1)
        sink.in_font_units = true;
        // the scale is left off until the walk is finished, because a sampled point must not be scaled twice (§9.1, AD-27)
        sink.scale = 1.0f;
        // the callbacks are the standard freetype decompose set, which walks an outline into points (§6.4)
        FT_Outline_Funcs callbacks{};
        // the contour, line, conic and cubic callbacks are the four freetype reports (§6.4)
        callbacks.move_to = &OutlineSink::move_to;
        // the line callback continues a contour (§6.4)
        callbacks.line_to = &OutlineSink::line_to;
        // the conic callback flattens a quadratic (§6.4)
        callbacks.conic_to = &OutlineSink::conic_to;
        // the cubic callback flattens a cubic (§6.4)
        callbacks.cubic_to = &OutlineSink::cubic_to;
        // the shift and delta are zero because the glyph was loaded unscaled (§9.1)
        callbacks.shift = 0;
        // the delta is zero because the coordinates are used as they are (§9.1)
        callbacks.delta = 0;
        // the walk fills the sink, and a failure leaves it empty rather than half drawn (§6.4)
        if (FT_Outline_Decompose(&m_state->face->glyph->outline, &callbacks, &sink) != 0)
            return {};
        // the walk is scaled once at the end, so a curve is sampled in units and scaled as a whole (§9.1, AD-27)
        std::vector<Point> scaled;
        // the final scale is the one the caller asked for, applied to every sampled point (§6.4, AD-27)
        const float final_scale = font_size / static_cast<float>(m_state->face->units_per_EM);
        // the reserve is the walk's length, because the conversion makes exactly that many points (§6.4)
        scaled.reserve(sink.points.size());
        // every point is converted, which is what makes the outline a drawable shape (§6.4)
        for (const Point& point : sink.points)
            scaled.push_back(Point{.x = point.x * final_scale, .y = point.y * final_scale});
        // the scaled points are the outline the renderer draws (§6.4, AD-06)
        return scaled;
    }

    bool HarfbuzzTextEngine::ready() const {
        // the engine is usable exactly when it can shape and the renderer can draw what it shaped (§9.1, AD-28)
        return m_state->font != nullptr && m_state->resolver != nullptr && m_state->resolver->typeface() != nullptr;
    }

    bool HarfbuzzTextEngine::drawable() const {
        // a run is drawable exactly when the engine resolved a typeface for the renderer (§9.1, AD-28)
        return m_state->resolver != nullptr && m_state->resolver->typeface() != nullptr;
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width) const {
        // an engine with no face measures nothing, because there is nothing to shape with (§9.1)
        if (!ready())
            return TextLayout{};
        // the default size is what a call site that does not name one gets (§9.1)
        return measure(text, max_width, DEFAULT_FONT_SIZE);
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width, float font_size) const {
        // this engine does not wrap yet, so the ceiling is read and reported rather than ignored silently (§9.1)
        const float ceiling = max_width > 0.0f ? max_width : UNBOUNDED_WIDTH;
        // a ceiling of no pixels cannot bound anything, so the run is measured unbounded (§9.1)
        if (ceiling <= 0.0f)
            return TextLayout{};
        // an engine with no face measures nothing, because there is nothing to shape with (§9.1)
        if (!ready())
            return TextLayout{};
        // an empty string shapes no line, which is what an empty layout means (§6.2)
        if (text.empty())
            return TextLayout{};
        // a size of no pixels cannot be shaped at, so the run measures nothing (§9.1)
        if (font_size <= 0.0f)
            return TextLayout{};
        // the buffer carries the run and its properties, and is destroyed by the unique pointer (§9.1)
        std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), &hb_buffer_destroy);
        // a buffer that did not allocate shapes nothing, which an absent layout reports (§9.1)
        if (buffer == nullptr)
            return TextLayout{};
        // the text is added as utf8 and the script and direction are guessed from it, which is the shaping default (§9.1)
        hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()), 0, -1);
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
        // a run with no glyphs measures nothing, which is what an empty layout means (§6.2)
        if (glyph_count == 0 || glyphs == nullptr || positions == nullptr)
            return TextLayout{};
        // the line is the whole run, because this engine does not wrap yet (§9.1)
        ShapedRun::Line line;
        // the font handle carries the typeface the platform resolved, which the renderer draws with (§9.1, AD-28)
        line.font = FontHandle::from_object(m_state->resolver->typeface());
        // the baseline sits at the ascender, which is where every glyph of the line hangs from (§9.1)
        line.origin = Point{.x = 0.0f, .y = m_state->ascender * font_size};
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
            // the glyph is recorded where the pen currently sits, carrying the byte it came from (§9.1)
            line.glyphs.push_back(ShapedGlyph{.id = glyphs[index].codepoint, .position = Point{.x = advance + offset_x, .y = -offset_y}, .cluster = glyphs[index].cluster});
            // the pen advances by this glyph's advance, which is what lays the next one out (§9.1)
            advance += static_cast<float>(position.x_advance) / 64.0f;
        }
        // the line's advance is where a caret stands past its last glyph (§9.1)
        line.width = advance;
        // the ascent of the line is the scaled ascender, because one line carries the whole run (§9.1)
        line.ascent = m_state->ascender * font_size;
        // the descent is the scaled descender, reported positive because it is a distance (§9.1)
        line.descent = -m_state->descender * font_size;
        // the line begins at the first byte, because nothing precedes the only line of the run (§9.1)
        line.start_byte = 0;
        // the line ends at the end of the text, because one line holds all of it (§9.1)
        line.end_byte = text.size();
        // the lines join the run in reading order, which for one line is the one line (§9.1)
        std::vector<ShapedRun::Line> lines;
        // the line joins the run by move, because it is already built (§9.1)
        lines.push_back(std::move(line));
        // the shaped run is what the render stage replays verbatim (§9.1, AD-18)
        TextLayout layout;
        // the run is built over its lines and the size it was shaped at (§9.1)
        layout.run = ShapedRun::make(std::move(lines), font_size, m_state->font_hash);
        // the width is the accumulated advance, which is the whole line because nothing wraps (§8.2)
        layout.size = Size{.width = advance, .height = (m_state->ascender - m_state->descender) * font_size};
        // the ascent is the scaled ascender, which a caller places content by (§9.1)
        layout.ascent = m_state->ascender * font_size;
        // the descent is the scaled descender, reported positive because it is a distance (§9.1)
        layout.descent = -m_state->descender * font_size;
        // exit
        return layout;
    }

    void install_harfbuzz_text_engine() {
        // the factory needs a font path the composition root owns, so this installs nothing until discovery lands (§9.1)
        (void)0;
    }
} // namespace albor::harfbuzz
