#include "resources/embedded_font.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

namespace albor::resources
{
    namespace
    {
        // the fonts a build carries, which generated code fills at startup and a test empties (§11.3, §2.2)
        std::vector<std::shared_ptr<const EmbeddedFont>>& registry() {
            // the registry is one per process, because registration happens once per generated font (§11.3)
            static std::vector<std::shared_ptr<const EmbeddedFont>> fonts;
            // the fonts are handed back by reference, because a caller registers into the one list (§11.3)
            return fonts;
        }

        // the lock that guards the registry, because a backend reads it off the render thread (§10.5, AD-18)
        std::mutex& registry_mutex() {
            static std::mutex mutex;
            return mutex;
        }
    } // namespace

    std::uint64_t hash_font_bytes(const std::uint8_t* data, std::size_t size) {
        // a font that is not there hashes to zero, which is also the default an unregistered font carries (§15)
        if (data == nullptr)
            return 0;
        // the hash is fnv one a, which is stable across builds and platforms because it is pure arithmetic (§15, AD-34)
        constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ULL;
        // the prime is the fnv one a multiplier, which is a constant rather than a tuning knob (§15, AD-34)
        constexpr std::uint64_t PRIME = 1099511628211ULL;
        // the basis starts the running value, so two different fonts cannot start alike (§15)
        std::uint64_t hash = OFFSET_BASIS;
        // every byte is folded in, because a hash that skips bytes would collide on a partial font (§15)
        for (std::size_t index = 0; index < size; index++) {
            // the byte is spread across the word, because one byte into a sixty four bit word shifts one step (§15)
            hash ^= static_cast<std::uint64_t>(data[index]);
            // the running value is multiplied rather than rotated, which is what makes the hash avalanche (§15)
            hash *= PRIME;
        }
        // the finished hash identifies the bytes on every platform, which is the whole point (§15, AD-34)
        return hash;
    }

    void FontRegistry::register_font(EmbeddedFont font) {
        // the lock is held over the mutation, because a render thread may be resolving a hash (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the hash is computed here rather than trusted, because a caller could pass a wrong one (§15)
        font.content_hash = hash_font_bytes(font.bytes.data(), font.bytes.size());
        // a font with no bytes cannot shape anything, so it is refused rather than registered (§9.1)
        if (font.bytes.empty())
            return;
        // a family with no name cannot be asked for, so the font is refused (§9.1)
        if (font.family.empty())
            return;
        // an existing font of that family is dropped first, because the newest bytes are the truth (§15)
        registry().erase(std::remove_if(registry().begin(), registry().end(), [&font](const std::shared_ptr<const EmbeddedFont>& existing) { return existing->family == font.family; }), registry().end());
        // the font joins the registry as one shared object, so a reader keeps it alive past the lock (§11.3, AD-18)
        registry().push_back(std::make_shared<const EmbeddedFont>(std::move(font)));
    }

    std::shared_ptr<const EmbeddedFont> FontRegistry::find_family(std::string_view family) {
        // the lock is held over the search, because a registration may land mid walk (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the families are searched in registration order, because the newest of a family replaced the oldest (§15)
        for (const std::shared_ptr<const EmbeddedFont>& font : registry()) {
            // an exact match is the only one that counts, because a prefix would resolve differently per platform (§15)
            if (font->family == family)
                return font;
        }
        // no family of that name is registered, which is a refusal rather than a fallback (§9.1, AD-34)
        return nullptr;
    }

    std::shared_ptr<const EmbeddedFont> FontRegistry::find_hash(std::uint64_t content_hash) {
        // the lock is held over the search, because a registration may land mid walk (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the hashes are searched, because a layout is keyed on the bytes and not on a name (§15, AD-34)
        for (const std::shared_ptr<const EmbeddedFont>& font : registry()) {
            // a matching hash is the same font, whatever it was called (§15)
            if (font->content_hash == content_hash)
                return font;
        }
        // no registered font has those bytes, which is a refusal rather than a fallback (§9.1, AD-34)
        return nullptr;
    }

    std::size_t FontRegistry::count() {
        // the lock is held over the read, because a registration may land mid read (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the count is what a test asserts on, because registration is otherwise invisible (§11.3)
        return registry().size();
    }

    void FontRegistry::clear() {
        // the lock is held over the drop, because a backend may still be reading a font (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the fonts are dropped, so one test's registration cannot answer another's (§2.2)
        registry().clear();
    }
} // namespace albor::resources
