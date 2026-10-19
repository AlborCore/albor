#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwrite.h>
#include <windows.h>

#include <string>
#include <string_view>

#include "platform/windows/text_encoding.h"

namespace albor::harfbuzz
{
    namespace
    {
        // compare the resolved family with the requested one, insensitive to case like the reference fold (§15)
        bool family_is(const std::wstring& actual, const std::wstring& requested) {
            // the compare is the system's own insensitive fold, and the invariant locale keeps it fixed (§15)
            return CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, actual.c_str(), static_cast<int>(actual.size()), requested.c_str(), static_cast<int>(requested.size())) == CSTR_EQUAL;
        }

        // the shared dwrite factory the resolution walks through, which exists once per process (§9.1)
        IDWriteFactory* dwrite_factory() {
            // the factory is created once, because a shared one is what every consumer of this platform gets (§9.1)
            static IDWriteFactory* factory = [] {
                // a factory that did not come up is left null, which the callers refuse on (§9.1)
                IDWriteFactory* built = nullptr;
                (void)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&built));
                return built;
            }();
            return factory;
        }

        // the resolver over directwrite, which is the platform font source this engine draws from (§9.1, AD-28)
        class DirectWriteTypefaceResolver final : public HarfbuzzTypefaceResolver
        {
        public:
            // resolve the family through the system collection, which owns the face this resolver releases (§9.1)
            explicit DirectWriteTypefaceResolver(const std::string& family_name) :
                HarfbuzzTypefaceResolver(family_name) {
                // the factory the system collection comes from is up before any ask (§9.1)
                IDWriteFactory* factory = dwrite_factory();
                // a factory that did not come up leaves no face to resolve (§9.1)
                if (factory == nullptr)
                    return;
                // the system collection is the font source of this platform (§9.1)
                IDWriteFontCollection* collection = nullptr;
                if (factory->GetSystemFontCollection(&collection, FALSE) != S_OK || collection == nullptr)
                    return;
                // an empty family asks for the system ui face, which the message font names (§9.1)
                std::wstring requested;
                // the default ask is remembered, because identity only checks a named family (§15)
                const bool default_face = family_name.empty();
                if (default_face) {
                    // the message font is the face the system draws its own ui with (§9.1)
                    NONCLIENTMETRICSW metrics{};
                    metrics.cbSize = sizeof(metrics);
                    // metrics that did not answer leave the face unresolved, which the engine refuses on (§9.1)
                    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != 0 && metrics.lfMessageFont.lfFaceName[0] != L'\0')
                        requested = metrics.lfMessageFont.lfFaceName;
                }
                else {
                    // the name the run asked for is converted once, because the collection matches by its string (§9.1)
                    requested = windows::wide_from_utf8(family_name);
                }
                // a family with no name leaves the face unresolved, which is what the engine refuses on (§9.1)
                if (requested.empty()) {
                    collection->Release();
                    return;
                }
                // a non empty name is answered by the collection, where an absent one is a refusal (§15, AD-34)
                UINT32 index = 0;
                BOOL exists = FALSE;
                if (collection->FindFamilyName(requested.c_str(), &index, &exists) != S_OK || !exists) {
                    collection->Release();
                    return;
                }
                // the family is read back, because identity refuses a substitution under another face (§15, AD-34)
                IDWriteFontFamily* family = nullptr;
                if (collection->GetFontFamily(index, &family) != S_OK || family == nullptr) {
                    collection->Release();
                    return;
                }
                // the names of the family are walked, where a match in any of them is the family the run named (§15)
                bool matched = default_face;
                if (!default_face) {
                    // the localized names are read, and a refusal to read them is the refusal of the face (§15)
                    IDWriteLocalizedStrings* names = nullptr;
                    if (family->GetFamilyNames(&names) == S_OK && names != nullptr) {
                        // the count bounds the walk, because a family may carry a name per locale (§15)
                        const UINT32 count = names->GetCount();
                        // the buffer is sized per name, which the length query measures (§15)
                        for (UINT32 at = 0; at < count && !matched; at++) {
                            // the length is measured in units, which the terminator joins (§15)
                            UINT32 length = 0;
                            // a length that did not measure ends this name, because the buffer cannot be sized (§15)
                            if (names->GetStringLength(at, &length) != S_OK)
                                continue;
                            std::wstring name(static_cast<std::size_t>(length) + 1, L'\0');
                            // a name that did not copy ends the walk, because identity cannot be checked on it (§15)
                            if (names->GetString(at, name.data(), length + 1) != S_OK)
                                continue;
                            // the utf8 form is gone, because the compare runs over the wide names the family carries (§15)
                            matched = family_is(name, requested);
                        }
                        names->Release();
                    }
                }
                // a family that is not the one the run named is a refusal, which identity holds (§15, AD-34)
                if (!matched) {
                    family->Release();
                    collection->Release();
                    return;
                }
                // the face is the first matching font of the family, at the normal weight (§9.1)
                IDWriteFont* font = nullptr;
                if (family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font) != S_OK || font == nullptr) {
                    family->Release();
                    collection->Release();
                    return;
                }
                // the font face is what the renderer adapts to, which the font issues (§9.1, AD-28)
                if (font->CreateFontFace(&m_font_face) != S_OK || m_font_face == nullptr) {
                    font->Release();
                    family->Release();
                    collection->Release();
                    return;
                }
                // the locals go back in the reverse order, because the face owns the lifetime now (§9.1)
                font->Release();
                family->Release();
                collection->Release();
            }

            // take the font face down, which the com lifetime requires (§9.1)
            ~DirectWriteTypefaceResolver() override {
                // the face is released with the resolver, which the engine keeps alive for the run (§9.1, AD-28)
                if (m_font_face != nullptr) {
                    m_font_face->Release();
                    m_font_face = nullptr;
                }
            }

            // override: hand out the resolved typeface, which the renderer draws the glyphs with (§9.1, AD-28)
            [[nodiscard]] const void* typeface() const override {
                // a typeface that did not resolve means the platform has no such family (§9.1)
                if (m_font_face == nullptr)
                    return nullptr;
                // the pointer is opaque here, and only a renderer backend may name what it is (§3.5, AD-19)
                return m_font_face;
            }

        private:
            // the resolved font face, which is what the run carries as its font handle (§9.1, AD-28)
            IDWriteFontFace* m_font_face = nullptr;
        };
    } // namespace

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create(const std::string& family_name) {
        // an empty family name names no font, so the resolver is refused rather than built empty (§9.1)
        if (family_name.empty())
            return nullptr;
        // the platform resolver is returned as the base, so no platform type escapes this module (§3.5)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new DirectWriteTypefaceResolver(family_name));
    }

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create_default() {
        // an empty family asks the system for its ui face, which a run that names none draws with (§9.1)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new DirectWriteTypefaceResolver(std::string()));
    }
} // namespace albor::harfbuzz
