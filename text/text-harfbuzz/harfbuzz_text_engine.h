#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "core/geometry.h"
#include "text/text_engine.h"

namespace albor::harfbuzz
{
    // the provider that turns an embedded run into drawable outline commands, which the engine builds on demand (§6.4)
    class EmbeddedGlyphProvider;

    // the text engine that shapes with a vendored harfbuzz instead of a renderer bundled shaper (§9.1)
    class HarfbuzzTextEngine final : public TextEngine
    {
    public:
        // build over a font file and the family the renderer draws from, refusing when either is absent (§9.1)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTextEngine> create(const std::string& font_path, const std::string& family_name);

        // build over a font the build carries, which is how AD-34 removes font discovery (§15, AD-34)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTextEngine> create_embedded(std::string_view family);

        // build over the platform default face, which keeps text available before a build embeds a font (§9.1, AD-28)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTextEngine> create_platform(std::string_view family);

        // the content hash of the font this engine shapes with, which is its identity in a layout (§15, AD-34)
        [[nodiscard]] std::uint64_t font_hash() const;

        // how many shaping passes the engine has run (§9.1)
        [[nodiscard]] std::size_t shaped_run_count() const;

        // how many shaped runs the cache currently holds (§9.1)
        [[nodiscard]] std::size_t cache_size() const;

        // drop every cached run, which a memory pressure policy calls (§9.1)
        void clear_cache() const;

        // extract one glyph's outline as a polyline, which is how embedded text draws (§15, AD-34)
        [[nodiscard]] std::vector<Point> glyph_outline(std::uint32_t glyph_id, float font_size) const;

        ~HarfbuzzTextEngine() override;

        HarfbuzzTextEngine(const HarfbuzzTextEngine&) = delete;
        HarfbuzzTextEngine& operator=(const HarfbuzzTextEngine&) = delete;
        HarfbuzzTextEngine(HarfbuzzTextEngine&&) = delete;
        HarfbuzzTextEngine& operator=(HarfbuzzTextEngine&&) = delete;

        // a derived measure declaration would hide the base overloads, so the base set is brought in here (§9.1)
        using TextEngine::measure;

        // override: measure at the default font size (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override;

        // override: measure at an explicit font size (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override;

        // override: measure with an explicit font selection, which resolves the family through the registry (§9.1, AD-34)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size, const FontSpec& spec) const override;

        // override: measure in a declared reading direction, which sets the buffer direction (§9.2)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size, const FontSpec& spec, TextDirection direction) const override;

        // override: report whether a face was loaded, because an engine with no face shapes nothing (§9.1)
        [[nodiscard]] bool ready() const override;

        // report whether a run draws: the path engine needs a typeface, the embedded engine its outlines (§9.1, AD-34)
        [[nodiscard]] bool drawable() const;

        // override: the engine resolves a family, though a weight is still pending (#96) (§9.1)
        [[nodiscard]] bool resolves_font_selection() const override;

        // override: hand out the resolver embedded runs draw through, which walks the carried font (§6.4, AD-34)
        [[nodiscard]] GlyphOutlineResolver glyph_outline_resolver() const override;

    private:
        // load the face, the shaper and the drawing typeface, which is the whole of construction (§9.1)
        HarfbuzzTextEngine(const std::string& font_path, const std::string& family_name);

        // load the face from bytes the build carries, which is how the engine avoids a file system (§15, AD-34)
        HarfbuzzTextEngine(const std::uint8_t* font_bytes, std::size_t byte_count, std::string_view family_name);

        // resolve the platform typeface, which only the transitional path engine needs to draw (§9.1, AD-28)
        void resolve_typeface(std::string_view family_name);

        // the opaque harfbuzz blob, face and font state, so no shaping type leaves this header (§3.5, AD-19)
        struct HarfbuzzEngineState;

        // the loaded state, which the engine owns for its whole life (§9.1)
        std::unique_ptr<HarfbuzzEngineState> m_state;

        // the provider that resolves outlines by hash, built the first time a resolver is asked for (§6.4, AD-34)
        mutable std::unique_ptr<EmbeddedGlyphProvider> m_outline_provider;
    };

    // install the vendored engine as the default, which is what phase 3 of the text plan does (§9.1)
    void install_harfbuzz_text_engine();
} // namespace albor::harfbuzz
