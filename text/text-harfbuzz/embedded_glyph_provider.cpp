#include "text/text-harfbuzz/embedded_glyph_provider.h"

#include "resources/embedded_font.h"

namespace albor::harfbuzz
{
    std::shared_ptr<HarfbuzzTextEngine> EmbeddedGlyphProvider::engine_for(std::uint64_t font_hash) {
        // an engine that was built before answers, because a font does not change under a build (§11.3)
        const auto found = m_engines.find(font_hash);
        if (found != m_engines.end())
            return found->second;
        // a hash the build does not carry has no font, and falling back would break identity (§15, AD-34)
        const std::shared_ptr<const resources::EmbeddedFont> font = resources::FontRegistry::find_hash(font_hash);
        if (font == nullptr)
            return nullptr;
        // the engine is built over the carried bytes, so nothing on the file system is consulted (§15, AD-34)
        std::shared_ptr<HarfbuzzTextEngine> engine = HarfbuzzTextEngine::create_embedded(font->family);
        // an engine that could not shape the bytes is not cached, because a later call may succeed (§9.1)
        if (engine == nullptr)
            return nullptr;
        // the engine is kept under the hash that identifies it, not under the name (§15)
        m_engines.emplace(font_hash, engine);
        // the engine is handed out, and the caller shares it (§11.3)
        return engine;
    }

    GlyphOutlineResolver EmbeddedGlyphProvider::resolver() {
        // the resolver is a bound call so the recorder holds no provider type, keeping ui free of text (§3.5)
        return [this](const GlyphOutlineKey& key) -> std::shared_ptr<const GlyphOutline> {
            // a key with no font hash names no font, so nothing is built (§6.4)
            if (key.font_hash == 0)
                return nullptr;
            // the engine for that font is built or found, which is what makes one outline (§6.4)
            const std::shared_ptr<HarfbuzzTextEngine> engine = engine_for(key.font_hash);
            // a font the build does not carry has no outline, which is a refusal rather than a blank (§6.4)
            if (engine == nullptr)
                return nullptr;
            // the outline is walked from the font's own bytes, unhinted and at a fixed tessellation (§15, AD-34)
            return std::make_shared<const GlyphOutline>(key.font_hash, key.glyph_id, key.font_size, engine->glyph_outline(key.glyph_id, key.font_size));
        };
    }

    std::size_t EmbeddedGlyphProvider::engine_count() const {
        // the count is what a test asserts on, because a provider is otherwise invisible (§9.1)
        return m_engines.size();
    }

    void EmbeddedGlyphProvider::clear() {
        // the engines are dropped, so one test's provider cannot answer another's (§2.2)
        m_engines.clear();
    }
} // namespace albor::harfbuzz
