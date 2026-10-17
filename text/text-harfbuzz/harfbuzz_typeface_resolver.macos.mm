#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

#include <CoreText/CoreText.h>

#include <string>

namespace albor::harfbuzz
{
    namespace
    {
        // the resolver over core text, which is the platform font source this engine draws from (§9.1, AD-28)
        class CoreTextTypefaceResolver final : public HarfbuzzTypefaceResolver
        {
        public:
            // resolve the family through core text, which owns the font this resolver releases (§9.1)
            explicit CoreTextTypefaceResolver(const std::string& family_name) :
                HarfbuzzTypefaceResolver(family_name) {
                // an empty family asks for the system ui face, which is what a run that names none draws with (§9.1)
                if (family_name.empty()) {
                    m_font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 12.0, nullptr);
                    return;
                }
                // the name is converted once, because core text matches a family by its string (§9.1)
                CFStringRef name = CFStringCreateWithCString(nullptr, family_name.c_str(), kCFStringEncodingUTF8);
                // a name that did not convert leaves no font to resolve (§9.1)
                if (name == nullptr)
                    return;
                // the font is built over the name, because core text resolves a family to a face (§9.1)
                m_font = CTFontCreateWithName(name, 12.0, nullptr);
                // the family is read back, because core text substitutes a fallback for an absent name (§15)
                CFStringRef actual = m_font != nullptr ? CTFontCopyFamilyName(m_font) : nullptr;
                // a family that differs from the request is a fallback, which identity refuses (§15, AD-34)
                const bool matched = actual != nullptr && CFStringCompare(actual, name, kCFCompareCaseInsensitive) == kCFCompareEqualTo;
                // a copied name is released whatever the comparison found (§9.1)
                if (actual != nullptr)
                    CFRelease(actual);
                // the requested name is released after the comparison, because it was copied above (§9.1)
                CFRelease(name);
                // a fallback is released rather than drawn under the wrong family (§15, AD-34)
                if (!matched && m_font != nullptr) {
                    CFRelease(m_font);
                    m_font = nullptr;
                }
            }

            ~CoreTextTypefaceResolver() override {
                // the platform font is owned here, so it is released with the resolver (§9.1)
                if (m_font != nullptr)
                    CFRelease(m_font);
            }

            // override: hand out the platform font, which the renderer adapts to its own typeface (§9.1, AD-28)
            [[nodiscard]] const void* typeface() const override {
                // the handle is opaque here, and only the platform layer may name what it is (§3.5, AD-19)
                return m_font;
            }

        private:
            // the resolved platform font, which the engine keeps alive for the run's whole life (§9.1)
            CTFontRef m_font = nullptr;
        };
    } // namespace

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create(const std::string& family_name) {
        // an empty family name names no font, so the resolver is refused rather than built empty (§9.1)
        if (family_name.empty())
            return nullptr;
        // the platform resolver is returned as the base, so no platform type escapes this module (§3.5)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new CoreTextTypefaceResolver(family_name));
    }

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create_default() {
        // an empty family asks core text for its ui face, which a run that names none draws with (§9.1)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new CoreTextTypefaceResolver(std::string()));
    }
} // namespace albor::harfbuzz
