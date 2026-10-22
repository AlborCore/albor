#include "text/text-harfbuzz/harfbuzz_text_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hb-ot.h>
#include <hb.h>
#include <unicode/ubrk.h>
#include <unicode/utext.h>

#include "render/font_handle.h"
#include "render/shaped_run.h"
#include "resources/embedded_font.h"
#include "text/text-harfbuzz/embedded_glyph_provider.h"
#include "text/text-harfbuzz/harfbuzz_font_face.h"
#include "text/text-harfbuzz/harfbuzz_platform_font.h"
#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

namespace albor::harfbuzz
{
    namespace
    {
        // one resolved face, which is the blob, face, font and metrics every family resolves to (§9.1, AD-34)
        struct ResolvedFace
        {
            // the bytes the blob borrows, which the face reads without a file and a run keeps alive (§15, AD-34)
            std::shared_ptr<const std::vector<std::uint8_t>> bytes;

            // the font a run draws from, which outlives this engine because a shaped run holds it (§9.1, AD-34)
            std::shared_ptr<const FontFace> outliner;

            // the blob wrapping those bytes (§9.1)
            hb_blob_t* blob = nullptr;

            // the face built over the blob (§9.1)
            hb_face_t* face = nullptr;

            // the font that shapes and draws, built from the face (§9.1)
            hb_font_t* font = nullptr;

            // the content hash of the bytes, which is the face's identity in a layout (§15, AD-34)
            std::uint64_t font_hash = 0;

            // the design units per em, which converts a harfbuzz position into logical pixels (§9.1, AD-27)
            float units_per_em = 1000.0f;

            // the ascender of the face in design units, which places the first baseline (§9.1)
            float ascender = 800.0f;

            // the descender of the face in design units, which places the bottom of the run (§9.1)
            float descender = -200.0f;

            // a face owns its hb objects and its bytes, so it is only ever moved, never copied (§9.1, AD-34)
            ResolvedFace() = default;
            // a copy would share a borrowed blob, so copying is refused outright (§9.1, AD-34)
            ResolvedFace(const ResolvedFace&) = delete;
            // a copy assignment would share the same borrowed blob, so it is refused too (§9.1, AD-34)
            ResolvedFace& operator=(const ResolvedFace&) = delete;
            // a move transfers the owned hb objects and bytes, which is how a face enters the cache (§9.1)
            ResolvedFace(ResolvedFace&&) = default;
            // a move assignment transfers the same, which the default face assignment uses (§9.1)
            ResolvedFace& operator=(ResolvedFace&&) = default;
        };

        // build a face over font bytes, which every family the engine resolves goes through (§9.1, AD-34)
        ResolvedFace build_face(std::vector<std::uint8_t> bytes) {
            // the result fills as much as the bytes allow, so a bad font leaves a face with no font (§9.1)
            ResolvedFace resolved;
            // the bytes are owned first, because the blob borrows them and a run keeps them alive (§9.1, AD-34)
            resolved.bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
            // a font with no byte cannot be read, so the face is left empty (§9.1)
            if (resolved.bytes->empty())
                return resolved;
            // the content hash is over the bytes, which is the identity a layout keys on (§15, AD-34)
            resolved.font_hash = resources::hash_font_bytes(resolved.bytes->data(), resolved.bytes->size());
            // the blob borrows the bytes, which the face owns for its whole life (§9.1)
            resolved.blob = hb_blob_create(reinterpret_cast<const char*>(resolved.bytes->data()), static_cast<unsigned int>(resolved.bytes->size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
            // the face is read at index zero, which is the first face in the file (§9.1)
            resolved.face = hb_face_create(resolved.blob, 0);
            // a face with no glyph is unusable, so the font is never built over it (§9.1)
            if (resolved.face == nullptr || hb_face_get_glyph_count(resolved.face) == 0)
                return resolved;
            // the outliner is built over the same bytes, so a run outlives the engine that resolved it (§9.1, AD-34)
            resolved.outliner = HarfbuzzFontFace::create(resolved.bytes);
            // the units per em convert design units into logical pixels, and a zero would divide by nothing (§9.1, AD-27)
            if (hb_face_get_upem(resolved.face) > 0)
                resolved.units_per_em = static_cast<float>(hb_face_get_upem(resolved.face));
            // the harfbuzz font is built over the face, which owns no shaping until the funcs are set (§9.1)
            resolved.font = hb_font_create(resolved.face);
            // the opentype funcs give the font its advances and its outlines, which is what hb-draw walks (§9.1)
            hb_ot_font_set_funcs(resolved.font);
            // the scale is set to the design units, so the extents below read in em units rather than pixels (§9.1, AD-27)
            hb_font_set_scale(resolved.font, static_cast<int>(resolved.units_per_em), static_cast<int>(resolved.units_per_em));
            // the extents carry the ascender and the descender, which place the baseline and the run bottom (§9.1)
            hb_font_extents_t extents{};
            // the extents are read only when the font answers them, because a broken face reports nothing (§9.1)
            if (hb_font_get_h_extents(resolved.font, &extents)) {
                // the ascender is the height above the baseline, which is where the run hangs from (§9.1)
                resolved.ascender = static_cast<float>(extents.ascender) / resolved.units_per_em;
                // the descender is negative in every real face, which places the run below its baseline (§9.1)
                resolved.descender = static_cast<float>(extents.descender) / resolved.units_per_em;
            }
            return resolved;
        }

        // release the hb objects a resolved face owns, in the order harfbuzz requires (§9.1)
        void destroy_face(ResolvedFace& resolved) {
            // the font is destroyed before the face it was built from (§9.1)
            if (resolved.font != nullptr) {
                // the font is released, so a second teardown cannot free it twice (§9.1)
                hb_font_destroy(resolved.font);
                // the pointer is cleared, because the font no longer exists (§9.1)
                resolved.font = nullptr;
            }
            // the face is destroyed before the blob that owns the bytes (§9.1)
            if (resolved.face != nullptr) {
                // the face is released before the blob that backs it (§9.1)
                hb_face_destroy(resolved.face);
                // the pointer is cleared for the same reason as the font (§9.1)
                resolved.face = nullptr;
            }
            // the blob goes last, because it borrowed bytes the face still held until now (§9.1, AD-34)
            if (resolved.blob != nullptr) {
                // the blob is released once nothing reads its bytes (§9.1)
                hb_blob_destroy(resolved.blob);
                // the pointer is cleared, so the teardown is idempotent (§9.1)
                resolved.blob = nullptr;
            }
        }

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
        // how many shaped runs the cache holds before it drops them (§9.1)
        constexpr std::size_t MAXIMUM_CACHED_RUNS = 512;

        // what a shaped run is cached under, which owns the text because the cache has to keep it alive (§9.1)
        struct CacheKey
        {
            // the text the run was shaped from (§9.1)
            std::string text;
            // the ceiling the run was wrapped at (§8.2)
            float max_width = 0.0f;
            // the size every line was shaped at (§9.1)
            float font_size = 0.0f;
            // the content hash of the face the run was shaped with (§15, AD-34)
            std::uint64_t font_hash = 0;
            // the reading direction the run was shaped in (§9.2)
            TextDirection direction = TextDirection::Inherit;
        };

        // what a lookup carries, which views the text so that finding a run allocates nothing (§9.1)
        struct CacheLookup
        {
            // the text the caller measured, which the cache never copies (§9.1)
            std::string_view text;
            // the ceiling the run was wrapped at (§8.2)
            float max_width = 0.0f;
            // the size the run is shaped at (§9.1)
            float font_size = 0.0f;
            // the content hash of the face the run is shaped with (§15, AD-34)
            std::uint64_t font_hash = 0;
            // the reading direction the run is shaped in (§9.2)
            TextDirection direction = TextDirection::Inherit;
        };

        // compare keys, in either direction, so a lookup can test against an owned key (§9.1)
        struct CacheEqual
        {
            // the transparent marker is what tells the map a lookup may carry a different key type (§9.1)
            using is_transparent = void;
            // two owned keys are the same run when every field agrees (§9.1)
            bool operator()(const CacheKey& lhs, const CacheKey& rhs) const { return lhs.text == rhs.text && lhs.max_width == rhs.max_width && lhs.font_size == rhs.font_size && lhs.font_hash == rhs.font_hash && lhs.direction == rhs.direction; }
            // a lookup and an owned key are the same run when the view matches the owned text (§9.1)
            bool operator()(const CacheKey& owned, const CacheLookup& lookup) const { return owned.max_width == lookup.max_width && owned.font_size == lookup.font_size && owned.font_hash == lookup.font_hash && owned.direction == lookup.direction && owned.text == lookup.text; }
            // the map calls the comparison lookup first, so the reverse order forwards to the one above (§9.1)
            bool operator()(const CacheLookup& lookup, const CacheKey& owned) const { return (*this)(owned, lookup); }
        };

        // hash a key into a bucket, which is what the cache looks the run up by (§9.1)
        struct CacheHasher
        {
            // the transparent marker is what tells the map a lookup may carry a different key type (§9.1)
            using is_transparent = void;
            // fold every field into one hash, because a run shaped for other inputs must not share an entry (§9.1)
            static std::size_t fold(std::string_view text, float max_width, float font_size, std::uint64_t font_hash, TextDirection direction) {
                // the text seeds the hash, because the text is what distinguishes one run from another (§9.1)
                std::size_t seed = std::hash<std::string_view>{}(text);
                // the ceiling is folded in, because the same text wrapped differently is another run (§9.1)
                seed ^= std::hash<float>{}(max_width) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
                // the font size is folded in, because the same text shaped at another size is another run (§9.1)
                seed ^= std::hash<float>{}(font_size) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
                // the face is folded in, because the same text in another face is another run (§15, AD-34)
                seed ^= std::hash<std::uint64_t>{}(font_hash) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
                // the direction is folded in, because a run shaped right to left is another run (§9.2)
                seed ^= std::hash<int>{}(static_cast<int>(direction)) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
                return seed;
            }
            // an owned key hashes by the text it owns (§9.1)
            std::size_t operator()(const CacheKey& key) const { return fold(key.text, key.max_width, key.font_size, key.font_hash, key.direction); }
            // a lookup hashes by the text it views, so both shapes agree on a bucket (§9.1)
            std::size_t operator()(const CacheLookup& lookup) const { return fold(lookup.text, lookup.max_width, lookup.font_size, lookup.font_hash, lookup.direction); }
        };
    } // namespace

    // the harfbuzz state the engine shapes and draws through, which owns one face and a cache of families (§9.1, AD-34)
    struct HarfbuzzTextEngine::HarfbuzzEngineState
    {
        // the face the engine was built over, which is the one a run that names no family uses (§9.1)
        ResolvedFace face;

        // the additional families resolved on demand, keyed by the family a run names (§15, AD-34)
        std::unordered_map<std::string, ResolvedFace> families;

        // the family the default face carries, which decides whether a named family needs resolving (§9.1)
        std::string family;

        // the resolver holding the drawing typeface, which only the platform can supply (§9.1, AD-28)
        std::unique_ptr<HarfbuzzTypefaceResolver> resolver;

        // whether the engine needs a platform typeface to be usable, which an embedded run does not (§15, AD-34)
        bool require_typeface = true;

        // the shaped runs measured so far, which stage one replays instead of shaping again (§9.1, §6.1)
        mutable std::unordered_map<CacheKey, TextLayout, CacheHasher, CacheEqual> cache;

        // how many shaping passes ran, which a test reads to prove the cache absorbs a repeated measure (§9.1)
        mutable std::size_t shaped_runs = 0;
    };

    namespace
    {
        // register the bytes a face was resolved from, so a backend draws the run by content (§15, AD-34)
        void register_platform_bytes(const ResolvedFace& face, std::string_view family_name) {
            // a face with no identity, or one the registry already holds, needs no second registration (§15)
            if (face.font_hash == 0 || face.bytes == nullptr || resources::FontRegistry::find_hash(face.font_hash) != nullptr)
                return;
            // the label is unique per hash, because two faces of one family must both survive (§15)
            const std::string base = family_name.empty() ? std::string("Platform") : std::string(family_name);
            const std::string label = base + "-" + std::to_string(face.font_hash);
            // the bytes travel into the registry, which is what makes the run drawable everywhere (§11.3, AD-34)
            resources::FontRegistry::register_font(resources::EmbeddedFont{.family = label, .content_hash = face.font_hash, .bytes = *face.bytes});
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
        // the bytes join the registry, so a backend resolves the run by content without the platform family (§15, AD-34)
        register_platform_bytes(engine->m_state->face, family_name);
        // the engine is returned only when it can actually shape (§9.1)
        return engine;
    }

    std::unique_ptr<HarfbuzzTextEngine> HarfbuzzTextEngine::create_embedded(std::string_view family) {
        // an empty family names nothing the build carries, so the engine is refused (§9.1)
        if (family.empty())
            return nullptr;
        // the font comes from the registry the generated code filled, which is where AD-34's bytes live (§11.3, AD-34)
        const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_family(family);
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

    std::unique_ptr<HarfbuzzTextEngine> HarfbuzzTextEngine::create_platform(std::string_view family) {
        // the resolver names the platform face, which is the only source before a build embeds a font (§9.1, AD-28)
        std::unique_ptr<HarfbuzzTypefaceResolver> resolver = family.empty() ? HarfbuzzTypefaceResolver::create_default() : HarfbuzzTypefaceResolver::create(std::string(family));
        // a platform with no face has nothing to shape or draw, so the engine is refused (§9.1)
        if (resolver == nullptr)
            return nullptr;
        // the bytes the typeface fronts are what the shaper reads, so the platform font becomes a face (§15, AD-34)
        std::vector<std::uint8_t> bytes = platform_font_bytes(resolver->typeface());
        // a face the platform hands no bytes for cannot be shaped, so the engine is refused (§15)
        if (bytes.empty())
            return nullptr;
        // the engine is built over those bytes, which is the same path an embedded font takes (§15, AD-34)
        std::unique_ptr<HarfbuzzTextEngine> engine(new HarfbuzzTextEngine(bytes.data(), bytes.size(), family));
        // a face that did not load leaves an engine that cannot shape (§9.1)
        if (!engine->ready())
            return nullptr;
        // the renderer draws the platform typeface, because this engine resolved one (§9.1, AD-28)
        engine->m_state->require_typeface = true;
        // the resolved typeface is what the run carries for the renderer (§9.1, AD-28)
        engine->m_state->resolver = std::move(resolver);
        // the bytes join the registry, so a backend draws the run by content without a platform typeface (§15, AD-34)
        register_platform_bytes(engine->m_state->face, family);
        // the engine is returned only when it can actually shape (§9.1)
        return engine;
    }

    std::uint64_t HarfbuzzTextEngine::font_hash() const {
        // the hash identifies the bytes the default face shaped with, which a layout is keyed on (§15, AD-34)
        return m_state->face.font_hash;
    }

    HarfbuzzTextEngine::HarfbuzzTextEngine(const std::string& font_path, const std::string& family_name) :
        m_state(std::make_unique<HarfbuzzEngineState>()) {
        // the family is stored, because a named run compares against it to skip resolving (§9.1)
        m_state->family = family_name;
        // the face is built over the file's bytes, which is the whole of the shaper's input (§15, AD-34)
        m_state->face = build_face(read_font_bytes(font_path));
        // the bytes are enough to draw, so a platform typeface is resolved only when the platform offers one (§9.1, AD-34)
        m_state->require_typeface = false;
        // a resolved typeface still marks the run as one a backend resolves by content (§9.1, AD-34)
        resolve_typeface(family_name);
    }

    HarfbuzzTextEngine::HarfbuzzTextEngine(const std::uint8_t* font_bytes, std::size_t byte_count, std::string_view family_name) :
        m_state(std::make_unique<HarfbuzzEngineState>()) {
        // an embedded engine draws as outlines, so it needs no platform typeface to be usable (§15, AD-34)
        m_state->require_typeface = false;
        // the family is stored, because a named run compares against it to skip resolving (§9.1)
        m_state->family = std::string(family_name);
        // the face is built over the carried bytes, so no file system is consulted (§15, AD-34)
        m_state->face = build_face(std::vector<std::uint8_t>(font_bytes, font_bytes + byte_count));
    }

    void HarfbuzzTextEngine::resolve_typeface(std::string_view family_name) {
        // the typeface is optional now, because a run draws from its bytes when the platform names no face (§9.1, AD-34)
        m_state->resolver = HarfbuzzTypefaceResolver::create(std::string(family_name));
        // a platform with no such face leaves the run drawing through its outlines (§9.1, AD-34)
        if (m_state->resolver == nullptr)
            return;
    }

    HarfbuzzTextEngine::~HarfbuzzTextEngine() {
        // the default face is destroyed through the shared routine, so no hb object outlives the engine (§9.1)
        destroy_face(m_state->face);
        // every resolved family is destroyed the same way, because each owns its own hb objects (§9.1)
        for (auto& entry : m_state->families)
            destroy_face(entry.second);
    }

    GlyphOutlineData HarfbuzzTextEngine::glyph_outline(std::uint32_t glyph_id, float font_size) const {
        // the walk belongs to the face, which a render thread reaches without this engine (§9.1, AD-34)
        const std::shared_ptr<const FontFace>& outliner = m_state->face.outliner;
        // a face with no outliner carries no glyph, which is a refusal rather than a crash (§9.1)
        return outliner != nullptr ? outliner->outline(glyph_id, font_size) : GlyphOutlineData{};
    }

    bool HarfbuzzTextEngine::ready() const {
        // an embedded engine draws outlines and is usable whenever it can shape, with no typeface (§15, AD-34)
        if (!m_state->require_typeface)
            return m_state->face.font != nullptr;
        // the engine is usable exactly when it can shape and the renderer can draw what it shaped (§9.1, AD-28)
        return m_state->face.font != nullptr && m_state->resolver != nullptr && m_state->resolver->typeface() != nullptr;
    }

    bool HarfbuzzTextEngine::drawable() const {
        // an embedded run draws through outlines, so it is drawable whenever it can shape (§15, AD-34)
        if (!m_state->require_typeface)
            return m_state->face.font != nullptr;
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
            // the view reads the utf8 bytes once the breaker is up, and stays null when the breaker failed (§9.1)
            UText* view = (U_SUCCESS(status) && breaker != nullptr) ? utext_openUTF8(nullptr, text.data(), static_cast<int64_t>(text.size()), &status) : nullptr;
            // the walk runs only when both came up, because a failure leaves nothing to walk (§9.1)
            if (U_SUCCESS(status) && view != nullptr) {
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
            }
            // the view is closed when it opened, so a failed open leaks nothing (§9.1)
            if (view != nullptr)
                utext_close(view);
            // the breaker is closed when it opened, which the ordinary path always reaches (§9.1)
            if (breaker != nullptr)
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

    namespace
    {
        ShapedRun::Line shape_range(const ResolvedFace& face, bool draws_platform, std::string_view text, std::size_t start, std::size_t end, float font_size, TextDirection direction) {
            // the row carries the metrics of its line even when it holds no glyph, which a blank row needs (§9.1)
            ShapedRun::Line line;
            // an embedded run carries no font, because the renderer draws its glyphs as outlines (§15, AD-34)
            line.font = draws_platform ? FontHandle::from_face(face.outliner) : FontHandle{};
            // the baseline sits at the ascender, which is where every glyph of the line hangs from (§9.1)
            line.origin = Point{.x = 0.0f, .y = face.ascender * font_size};
            // the ascent of the line is the scaled ascender, which places the row's top (§9.1)
            line.ascent = face.ascender * font_size;
            // the descent is the scaled descender, reported positive because it is a distance (§9.1)
            line.descent = -face.descender * font_size;
            // the start byte places the row in the text, which a caret resolves against (§9.1)
            line.start_byte = start;
            // the end byte is where the row's glyphs stop, which is the next row's start (§9.1)
            line.end_byte = end;
            // a range that holds no byte shapes no glyph, so the empty row is returned with its metrics (§9.1)
            if (end <= start)
                return line;
            // the buffer carries the row and its properties, and is destroyed by the unique pointer (§9.1)
            std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), &hb_buffer_destroy);
            // the row's bytes are added as utf8, and the script and direction are guessed from them (§9.1)
            hb_buffer_add_utf8(buffer.get(), text.data() + start, static_cast<int>(end - start), 0, -1);
            // the language is pinned to the fixed locale, because an ambient language would break identity (§15, AD-34)
            hb_buffer_set_language(buffer.get(), hb_language_from_string(LINE_BREAK_LOCALE, -1));
            // the guess fills in script, language and direction from the buffer's contents (§9.1)
            hb_buffer_guess_segment_properties(buffer.get());
            // a declared direction overrides the guess, while an inherited one leaves the content-derived direction (§9.2)
            if (direction != TextDirection::Inherit)
                hb_buffer_set_direction(buffer.get(), direction == TextDirection::Rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            // the size is set in points because the font scales its own positions to it (§9.1, AD-27)
            hb_font_set_scale(face.font, static_cast<int>(font_size * 64.0f), static_cast<int>(font_size * 64.0f));
            // the shaping pass itself, which produces the glyph ids and their positions (§9.1)
            hb_shape(face.font, buffer.get(), nullptr, 0);
            // the shaped output, which carries one entry per glyph (§9.1)
            unsigned glyph_count = 0;
            hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
            // the positions are read beside the glyphs, because one position describes one glyph (§9.1)
            hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);
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
    } // namespace

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width) const {
        // an engine with no face measures nothing, because there is nothing to shape with (§9.1)
        if (!ready())
            return TextLayout{};
        // the default size is what a call site that does not name one gets (§9.1)
        return measure(text, max_width, DEFAULT_FONT_SIZE);
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width, float font_size) const {
        // the empty spec is built once, because a temporary would pay a proxy per call on the debug runtimes (§9.1)
        static const FontSpec default_selection;
        // the size only overload measures with the engine's default face, which is the family it was built over (§9.1)
        return measure(text, max_width, font_size, default_selection);
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width, float font_size, const FontSpec& spec) const {
        // the family and size overload lets the text pick its direction, which a call site that names none gets (§9.2)
        return measure(text, max_width, font_size, spec, TextDirection::Inherit);
    }

    TextLayout HarfbuzzTextEngine::measure(std::string_view text, float max_width, float font_size, const FontSpec& spec, TextDirection direction) const {
        // an engine with no face measures nothing, because there is nothing to shape with (§9.1)
        if (!ready())
            return TextLayout{};
        // an empty string shapes no line, which is what an empty layout means (§6.2)
        if (text.empty())
            return TextLayout{};
        // a size of no pixels cannot be shaped at, so the run measures nothing (§9.1)
        if (font_size <= 0.0f)
            return TextLayout{};
        // the face a selection resolves to, which is the engine's own when no family is named (§9.1, AD-34)
        auto resolve_face = [this](const FontSpec& selection) -> const ResolvedFace* {
            // a run that names no family, or the engine's own family, uses the default face (§9.1)
            if (selection.family.empty() || selection.family == m_state->family)
                return &m_state->face;
            // the families to try in order are the named family and then the fallback chain (§9.1, AD-34)
            std::vector<std::string_view> candidates;
            // the named family is tried first, because it is what the author asked for (§9.1)
            candidates.push_back(selection.family);
            // each fallback follows, because a named family may be absent on a platform (§15, AD-34)
            for (const std::string& fallback : selection.fallback)
                candidates.push_back(fallback);
            for (std::string_view candidate : candidates) {
                // a family already resolved answers from the cache, so the bytes are parsed once (§9.1)
                const auto found = m_state->families.find(std::string(candidate));
                if (found != m_state->families.end()) {
                    // a cached face that shapes answers, and one that failed falls through to the next family (§9.1)
                    if (found->second.font != nullptr)
                        return &found->second;
                    // the failed family is skipped, because a later family may still shape (§15, AD-34)
                    continue;
                }
                // the registry is consulted, because a carried font is what a family resolves to (§15, AD-34)
                const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_family(candidate);
                if (font == nullptr)
                    continue;
                // the face is built over the carried bytes and cached under the family it resolved (§11.3, AD-34)
                const auto inserted = m_state->families.emplace(std::string(candidate), build_face(font->bytes));
                // a face that shapes answers, and one a corrupt font left empty falls through (§9.1)
                if (inserted.first->second.font != nullptr)
                    return &inserted.first->second;
                // the family is skipped, because the next one may carry a usable face (§15, AD-34)
                continue;
            }
            // no family in the chain is carried, so the run is refused rather than drawn with the wrong face (§15, AD-34)
            return nullptr;
        };
        // the resolved face is what the run shapes and is measured with, and its absence refuses the run (§15, AD-34)
        const ResolvedFace* face = resolve_face(spec);
        // a selection no carried font answers measures nothing, which is a refusal rather than a silent default (§15)
        if (face == nullptr)
            return TextLayout{};
        // the lookup views the caller's text, because copying it would allocate on every measure (§9.1)
        const CacheLookup lookup{.text = text, .max_width = max_width, .font_size = font_size, .font_hash = face->font_hash, .direction = direction};
        // a cached run is handed back as it was measured, so a steady state frame pays no shaping (§9.1)
        const auto cached = m_state->cache.find(lookup);
        if (cached != m_state->cache.end())
            return cached->second;
        // the typeface is the engine's own only for its face, because a foreign face draws as outlines (§9.1, AD-34)
        const void* typeface = face == &m_state->face && m_state->resolver != nullptr ? m_state->resolver->typeface() : nullptr;
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
                    const ShapedRun::Line probe = shape_range(*face, typeface != nullptr, text, line_start, probe_end, font_size, direction);
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
            ShapedRun::Line row = shape_range(*face, typeface != nullptr, text, line_start, content_end, font_size, direction);
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
        // the run replays its rows with the face's hash, drawing through a typeface or as outlines (§9.1, AD-18, AD-34)
        layout.run = ShapedRun::make(std::move(lines), font_size, face->font_hash, typeface != nullptr ? ShapeSource::Platform : ShapeSource::Embedded);
        // the size is the widest row by the stacked height of every row (§8.2)
        layout.size = Size{.width = widest, .height = total_height};
        // the ascent is the first row's, which a caller places content by (§9.1)
        layout.ascent = first_ascent;
        // the descent is the first row's, reported positive because it is a distance (§9.1)
        layout.descent = first_descent;
        // the shaping pass is counted, so a test can tell a cache hit from a fresh shaping (§9.1)
        m_state->shaped_runs++;
        // a full cache drops its runs, because an unbounded cache is a leak an application cannot see (§9.1)
        if (m_state->cache.size() >= MAXIMUM_CACHED_RUNS)
            m_state->cache.clear();
        // the run is cached under a key that owns the text, which only a miss ever builds (§9.1)
        m_state->cache.emplace(CacheKey{.text = std::string(text), .max_width = max_width, .font_size = font_size, .font_hash = face->font_hash, .direction = direction}, layout);
        // exit
        return layout;
    }

    std::size_t HarfbuzzTextEngine::shaped_run_count() const {
        // the count is what the engine has shaped, which excludes every run the cache served (§9.1)
        return m_state->shaped_runs;
    }

    std::size_t HarfbuzzTextEngine::cache_size() const {
        // the size is what a memory pressure policy reads before it decides to drop (§9.1)
        return m_state->cache.size();
    }

    void HarfbuzzTextEngine::clear_cache() const {
        // the cache holds only runs the ui thread can rebuild, so dropping them costs nothing but the next shaping (§9.1)
        m_state->cache.clear();
    }

    bool HarfbuzzTextEngine::resolves_font_selection() const {
        // the engine resolves a family through the registry, so a selection beyond its default face is honored (§9.1)
        return true;
    }

    GlyphOutlineResolver HarfbuzzTextEngine::glyph_outline_resolver() const {
        // the provider is built once, because it caches an engine for every font a run carries (§6.4, AD-34)
        if (m_outline_provider == nullptr)
            m_outline_provider = std::make_unique<EmbeddedGlyphProvider>();
        // the resolver is handed out, and it borrows the provider the engine owns for its whole life (§6.4)
        return m_outline_provider->resolver();
    }

    void install_harfbuzz_text_engine() {
        // the factory prefers the family the build carries, because only those bytes are identical everywhere (§15, AD-34)
        set_text_engine_factory([]() -> std::unique_ptr<TextEngine> {
            // the family the composition root named is the carried font AD-34 makes identical across platforms (§11.3)
            const std::string family = installed_font_family();
            // a build that carries the family shapes through its bytes, which is the target of AD-34 (§15, AD-34)
            if (std::unique_ptr<HarfbuzzTextEngine> embedded = HarfbuzzTextEngine::create_embedded(family))
                return std::unique_ptr<TextEngine>(std::move(embedded));
            // a build that carries no font still shapes through the platform face until phase 4 embeds one (§9.1, AD-28)
            return std::unique_ptr<TextEngine>(HarfbuzzTextEngine::create_platform(""));
        });
    }
} // namespace albor::harfbuzz
