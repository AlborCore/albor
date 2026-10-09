#include "text/text-harfbuzz/harfbuzz_text_engine.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include "render/font_handle.h"
#include "render/shaped_run.h"

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

        // the design units per em, which converts a harfbuzz position into logical pixels (§9.1, AD-27)
        float units_per_em = 1000.0f;

        // the ascender of the face in design units, which places the first baseline (§9.1)
        float ascender = 800.0f;

        // the descender of the face in design units, which places the bottom of the run (§9.1)
        float descender = -200.0f;
    };

    std::unique_ptr<HarfbuzzTextEngine> HarfbuzzTextEngine::create(const std::string& font_path) {
        // an empty path names no font, so the engine is refused rather than built empty (§9.1)
        if (font_path.empty())
            return nullptr;
        // the engine is built through its private constructor, which loads the face (§9.1)
        std::unique_ptr<HarfbuzzTextEngine> engine(new HarfbuzzTextEngine(font_path));
        // a face that did not load leaves an engine that is not ready, which a caller checks (§9.1)
        if (!engine->ready())
            return nullptr;
        // the engine is returned only when it can actually shape (§9.1)
        return engine;
    }

    HarfbuzzTextEngine::HarfbuzzTextEngine(const std::string& font_path) :
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

    bool HarfbuzzTextEngine::ready() const {
        // the engine shapes exactly when a harfbuzz font was built over the face (§9.1)
        return m_state->font != nullptr;
    }

    bool HarfbuzzTextEngine::drawable() const {
        // this engine issues no typeface, so every run carries an empty font handle; see DEFECT-TEXT-VENDOR-TYPEFACE
        return false;
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
        // the font handle stays empty because this engine cannot issue a drawable typeface; see the defect (§9.1)
        line.font = FontHandle{};
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
            // the glyph is recorded where the pen currently sits (§9.1)
            line.glyphs.push_back(ShapedGlyph{.id = glyphs[index].codepoint, .position = Point{.x = advance + offset_x, .y = -offset_y}});
            // the pen advances by this glyph's advance, which is what lays the next one out (§9.1)
            advance += static_cast<float>(position.x_advance) / 64.0f;
        }

        // the lines join the run in reading order, which for one line is the one line (§9.1)
        std::vector<ShapedRun::Line> lines;
        // the line joins the run by move, because it is already built (§9.1)
        lines.push_back(std::move(line));

        // the shaped run is what the render stage replays verbatim (§9.1, AD-18)
        TextLayout layout;
        // the run is built over its lines and the size it was shaped at (§9.1)
        layout.run = ShapedRun::make(std::move(lines), font_size);
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
