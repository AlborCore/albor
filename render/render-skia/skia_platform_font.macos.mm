#include "render/render-skia/skia_platform_font.h"

#include <CoreText/CoreText.h>

#include <mutex>
#include <unordered_map>

#include <skia/core/SkData.h>
#include <skia/core/SkFontMgr.h>
#include <skia/ports/SkFontMgr_mac_ct.h>
#include <skia/ports/SkTypeface_mac.h>

#include "resources/embedded_font.h"

namespace albor::skia
{
    namespace
    {
        // the lock that serializes the typeface cache, because a scene may be built from more than one thread (§10.5)
        std::mutex& typeface_mutex() {
            // the lock is one per process, because the cache it guards is one per process (§10.5)
            static std::mutex guard;
            return guard;
        }

        // the typefaces parsed from carried bytes, keyed by the content hash a run names (§15, AD-34)
        std::unordered_map<std::uint64_t, sk_sp<SkTypeface>>& typeface_cache() {
            // the cache is process wide, because a typeface is immutable and expensive to parse (§9.1)
            static std::unordered_map<std::uint64_t, sk_sp<SkTypeface>> cache;
            return cache;
        }
    } // namespace

    sk_sp<SkTypeface> typeface_from_platform_font(const void* font, std::uint64_t font_hash) {
        // a repeated hash answers from the cache, which is what makes a steady state frame cheap (§9.1)
        {
            const std::lock_guard<std::mutex> lock(typeface_mutex());
            const auto found = typeface_cache().find(font_hash);
            if (found != typeface_cache().end())
                return found->second;
        }
        // the carried bytes are the bytes the run shaped from, so the draw cannot disagree (§15, AD-34)
        const resources::EmbeddedFont* embedded = resources::FontRegistry::find_hash(font_hash);
        sk_sp<SkTypeface> typeface;
        // a run whose bytes the registry carries is parsed from them, which keeps layout and draw on one face (§15, AD-34)
        if (embedded != nullptr) {
            // the bytes are copied into skia data, because the parsed typeface owns its own copy (§15, AD-34)
            sk_sp<SkData> data = SkData::MakeWithCopy(embedded->bytes.data(), embedded->bytes.size());
            // the core text manager parses the carried sfnt, which is the same face the shaper read (§15, AD-34)
            typeface = SkFontMgr_New_CoreText(nullptr)->makeFromData(std::move(data));
        }
        // a run with no carried bytes falls back to the platform face it resolved (§9.1, AD-28)
        if (typeface == nullptr && font != nullptr)
            typeface = SkMakeTypefaceFromCTFont(static_cast<CTFontRef>(font));
        // a run with neither bytes nor a platform face draws nothing, which is a refusal (§9.1)
        if (typeface == nullptr)
            return nullptr;
        // the parsed face joins the cache, so the next frame reuses it (§9.1)
        const std::lock_guard<std::mutex> lock(typeface_mutex());
        typeface_cache().emplace(font_hash, typeface);
        // the face is handed back to the draw that asked for it (§9.1)
        return typeface;
    }
} // namespace albor::skia
