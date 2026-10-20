#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace albor::resources
{
    // the immutable font a build carries, identified by content hash because a name resolves per platform (§15, AD-34)
    struct EmbeddedFont
    {
        // the family name an application asks for, which is a label rather than a lookup key (§9.1)
        std::string family;

        // the hash of the bytes below, which is the font's real identity (§15, AD-34)
        std::uint64_t content_hash = 0;

        // the font file itself, which the engine reads the face from without touching a file system (§9.1)
        std::vector<std::uint8_t> bytes{};
    };

    // the registry generated code fills at startup, so the runtime never searches for a font (§11.3, AD-34)
    class FontRegistry
    {
    public:
        // register a font, which the generated code of a component calls once (§11.3, AD-34)
        static void register_font(EmbeddedFont font);

        // the font a family asks for, which is null when nothing carries it (§9.1)
        [[nodiscard]] static std::shared_ptr<const EmbeddedFont> find_family(std::string_view family);

        // the font a content hash names, which no reader can outlive because the registry shares it (§15, AD-18)
        [[nodiscard]] static std::shared_ptr<const EmbeddedFont> find_hash(std::uint64_t content_hash);

        // report how many fonts are registered, which a test asserts on after registering (§11.3)
        [[nodiscard]] static std::size_t count();

        // empty the registry, which a test does so that one registration does not leak into another (§2.2)
        static void clear();
    };

    // hash font bytes the way the registry identifies them, so that a generated font and a loaded one agree (§15, AD-34)
    [[nodiscard]] std::uint64_t hash_font_bytes(const std::uint8_t* data, std::size_t size);
} // namespace albor::resources
