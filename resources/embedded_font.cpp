#include "resources/embedded_font.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

#include "core/content_hash.h"

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
        // the shared content hash identifies the bytes on every platform, which is the whole point (§15, AD-34)
        return hash_bytes(std::span<const std::uint8_t>{data, size});
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
        // an existing face of that family and weight is dropped first, because the newest bytes are the truth (§15)
        registry().erase(std::remove_if(registry().begin(), registry().end(), [&font](const std::shared_ptr<const EmbeddedFont>& existing) { return existing->family == font.family && existing->weight == font.weight; }), registry().end());
        // the font joins the registry as one shared object, so a reader keeps it alive past the lock (§11.3, AD-18)
        registry().push_back(std::make_shared<const EmbeddedFont>(std::move(font)));
    }

    std::shared_ptr<const EmbeddedFont> FontRegistry::find_family(std::string_view family) {
        // the lock is held over the search, because a registration may land mid walk (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the regular face is the default a family name resolves to, which a bare selection expects (§9.1)
        for (const std::shared_ptr<const EmbeddedFont>& font : registry()) {
            // an exact family and regular weight answers before any other face of the family (§9.1, AD-34)
            if (font->family == family && font->weight == FontWeight::Regular)
                return font;
        }
        // a family with no regular face has no default, which is a refusal rather than another weight (§9.1, AD-34)
        return nullptr;
    }

    std::shared_ptr<const EmbeddedFont> FontRegistry::find_face(std::string_view family, FontWeight weight) {
        // the lock is held over the search, because a registration may land mid walk (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry_mutex());
        // the pairing is searched exactly, because substituting a weight would draw another face (§9.1)
        for (const std::shared_ptr<const EmbeddedFont>& font : registry()) {
            // the family and weight both decide, which is what makes a selection exact (§9.1, AD-34)
            if (font->family == family && font->weight == weight)
                return font;
        }
        // the pairing is not carried, which is a refusal rather than a nearest weight (§9.1, AD-34)
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
