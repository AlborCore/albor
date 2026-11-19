#include "albor/text/text-harfbuzz/harfbuzz_typeface_resolver.h"

#include <CoreText/CoreText.h>

#include <string>

namespace albor::harfbuzz
{
    namespace
    {
        // the platform weight trait a face is selected at, on the normalized scale core text matches (§9.1)
        CGFloat weight_trait(FontWeight weight) {
            // the thin member is the lightest stroke a family may carry (§9.1)
            if (weight == FontWeight::Thin)
                return -0.6;
            // the light member follows thin (§9.1)
            if (weight == FontWeight::Light)
                return -0.4;
            // the regular member is the face a family resolves to when no weight is named (§9.1)
            if (weight == FontWeight::Regular)
                return 0.0;
            // the medium member follows regular (§9.1)
            if (weight == FontWeight::Medium)
                return 0.23;
            // the semibold member sits below the bold face (§9.1)
            if (weight == FontWeight::Semibold)
                return 0.3;
            // the bold member is the emphatic stroke a family usually carries (§9.1)
            if (weight == FontWeight::Bold)
                return 0.4;
            // the black member is the heaviest stroke a family may carry (§9.1)
            return 0.62;
        }

        // the traits dictionary carrying one weight, which the platform matches a face against (§9.1)
        CFDictionaryRef weight_traits(FontWeight weight) {
            // the trait is the platform's own normalized weight, which the matcher reads (§9.1)
            const CGFloat trait = weight_trait(weight);
            // the number is what the traits dictionary carries, which the matcher reads (§9.1)
            CFNumberRef number = CFNumberCreate(nullptr, kCFNumberCGFloatType, &trait);
            // a number that did not come up leaves no weight to ask for (§9.1)
            if (number == nullptr)
                return nullptr;
            // the weight is the one trait this selection carries, which the matcher resolves a face from (§9.1)
            const void* keys[] = {kCTFontWeightTrait};
            // the value pairs with the key above, which the dictionary retains from here (§9.1)
            const void* values[] = {number};
            // the dictionary is what the descriptor carries, and it owns the number from here (§9.1)
            CFDictionaryRef traits = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            // the local number reference goes back once the dictionary holds it (§9.1)
            CFRelease(number);
            return traits;
        }

        // the face a family selects at a weight, which the platform matches from the family's members (§9.1)
        CTFontRef family_face_at_weight(CFStringRef name, FontWeight weight) {
            // the traits carry the weight the family is asked at (§9.1)
            CFDictionaryRef traits = weight_traits(weight);
            // traits that did not come up leave no weight to match (§9.1)
            if (traits == nullptr)
                return nullptr;
            // the family and the weight are the two attributes the matcher needs (§9.1)
            const void* keys[] = {kCTFontFamilyNameAttribute, kCTFontTraitsAttribute};
            // the values pair with the keys above, in the same order (§9.1)
            const void* values[] = {name, traits};
            // the attributes dictionary is what the descriptor is built over (§9.1)
            CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            // the traits join the attributes, so the local reference goes back (§9.1)
            CFRelease(traits);
            // attributes that did not come up leave no face to create (§9.1)
            if (attributes == nullptr)
                return nullptr;
            // the descriptor names a family and a weight, which the matcher resolves to one member (§9.1)
            CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithAttributes(attributes);
            // the attributes go back once the descriptor holds them (§9.1)
            CFRelease(attributes);
            // a descriptor that did not come up leaves no face to create (§9.1)
            if (descriptor == nullptr)
                return nullptr;
            // the face is the member the matcher selected at the requested weight (§9.1)
            CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, 12.0, nullptr);
            // the descriptor goes back once the font owns the selection (§9.1)
            CFRelease(descriptor);
            return font;
        }

        // the resolver over core text, which is the platform font source this engine draws from (§9.1, AD-28)
        class CoreTextTypefaceResolver final : public HarfbuzzTypefaceResolver
        {
        public:
            // resolve the family through core text, which owns the font this resolver releases (§9.1)
            explicit CoreTextTypefaceResolver(const std::string& family_name, FontWeight weight) :
                HarfbuzzTypefaceResolver(family_name, weight) {
                // an empty family asks for the system ui face, which is what a run that names none draws with (§9.1)
                if (family_name.empty()) {
                    // the regular request keeps the ui face the platform draws its own interface with (§9.1)
                    if (weight == FontWeight::Regular) {
                        m_font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 12.0, nullptr);
                        return;
                    }
                    // the ui face is the base whose family the requested weight is selected from (§9.1)
                    CTFontRef base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 12.0, nullptr);
                    // a base that did not come up leaves no family to select from (§9.1)
                    if (base == nullptr)
                        return;
                    // the family the ui face carries is what a lighter or heavier selection matches (§9.1)
                    CFStringRef family = CTFontCopyFamilyName(base);
                    // the base goes back once its family is copied (§9.1)
                    CFRelease(base);
                    // a family that did not copy leaves no face to select (§9.1)
                    if (family == nullptr)
                        return;
                    // the weighted face keeps the ui family and selects its member at the weight (§9.1)
                    m_font = family_face_at_weight(family, weight);
                    // the family goes back once the selected face owns the choice (§9.1)
                    CFRelease(family);
                    return;
                }
                // the name is converted once, because core text matches a family by its string (§9.1)
                CFStringRef name = CFStringCreateWithCString(nullptr, family_name.c_str(), kCFStringEncodingUTF8);
                // a name that did not convert leaves no font to resolve (§9.1)
                if (name == nullptr)
                    return;
                // the regular weight is the family's own face, which the name alone resolves (§9.1)
                if (weight == FontWeight::Regular) {
                    // the face is the family's regular member, which the name resolves without a trait (§9.1)
                    m_font = CTFontCreateWithName(name, 12.0, nullptr);
                }
                else {
                    // a lighter or heavier request selects the family's member at that weight (§9.1)
                    m_font = family_face_at_weight(name, weight);
                }
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

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create(const std::string& family_name, FontWeight weight) {
        // an empty family name names no font, so the resolver is refused rather than built empty (§9.1)
        if (family_name.empty())
            return nullptr;
        // the platform resolver is returned as the base, so no platform type escapes this module (§3.5)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new CoreTextTypefaceResolver(family_name, weight));
    }

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create_default(FontWeight weight) {
        // an empty family asks core text for its ui face, which a run that names none draws with (§9.1)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new CoreTextTypefaceResolver(std::string(), weight));
    }
} // namespace albor::harfbuzz
