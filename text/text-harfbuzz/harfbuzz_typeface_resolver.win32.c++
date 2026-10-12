#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

#include <skia/core/SkFontMgr.h>
#include <skia/core/SkTypeface.h>
#include <skia/ports/SkTypeface_win.h>

namespace albor::harfbuzz
{
    namespace
    {
        // the resolver over the directwrite font manager, which is the only manager this platform offers (§9.1)
        class DirectWriteTypefaceResolver final : public HarfbuzzTypefaceResolver
        {
        public:
            // resolve the family through the platform manager, which owns the typeface the engine hands out (§9.1)
            explicit DirectWriteTypefaceResolver(const std::string& family_name) :
                HarfbuzzTypefaceResolver(family_name) {
                // the manager is created once and owned here, because the typeface borrows from it (§9.1)
                m_manager = SkFontMgr_New_DirectWrite();
                // a manager that did not come up leaves no typeface to resolve (§9.1)
                if (m_manager == nullptr)
                    return;
                // the family is matched at the normal weight, which is what a default label uses (§9.1)
                m_typeface = m_manager->matchFamilyStyle(family_name.c_str(), SkFontStyle());
            }

            // override: hand out the resolved typeface, which the renderer draws the glyphs with (§9.1, AD-28)
            [[nodiscard]] const void* typeface() const override {
                // a typeface that did not resolve means the platform has no such family (§9.1)
                if (m_typeface == nullptr)
                    return nullptr;
                // the pointer is opaque here, and only a renderer backend may name what it is (§3.5, AD-19)
                return m_typeface.get();
            }

        private:
            // the platform font manager, which owns the resolved typeface (§9.1, AD-28)
            sk_sp<SkFontMgr> m_manager;

            // the resolved typeface, which is what the run carries as its font handle (§9.1, AD-28)
            sk_sp<SkTypeface> m_typeface;
        };
    } // namespace

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create(const std::string& family_name) {
        // an empty family name names no font, so the resolver is refused rather than built empty (§9.1)
        if (family_name.empty())
            return nullptr;
        // the platform resolver is returned as the base, so no platform type escapes this module (§3.5)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new DirectWriteTypefaceResolver(family_name));
    }
} // namespace albor::harfbuzz
