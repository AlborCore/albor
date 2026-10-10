#pragma once

#include <map>
#include <memory>
#include <string>

#include "render/glyph_outline.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"

namespace albor::harfbuzz
{
    // the outlines of the fonts a build carries, which turns an embedded run into drawable commands (§15, AD-34)
    class EmbeddedGlyphProvider
    {
    public:
        // the resolver to hand the scene recorder, which is how draw_text reaches an embedded run (§6.4)
        [[nodiscard]] GlyphOutlineResolver resolver();

        // how many engines the provider has built, which a test asserts on (§9.1)
        [[nodiscard]] std::size_t engine_count() const;

        // empty the provider, which a test does so that one test's engines cannot answer another's (§2.2)
        void clear();

    private:
        // the engine for a font hash, built on first use from the registry the build filled (§11.3, AD-34)
        [[nodiscard]] std::shared_ptr<HarfbuzzTextEngine> engine_for(std::uint64_t font_hash);

        // the engines by font identity, because a name would resolve per platform (§15)
        std::map<std::uint64_t, std::shared_ptr<HarfbuzzTextEngine>> m_engines;
    };
} // namespace albor::harfbuzz
