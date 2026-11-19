#include "albor/text/text-harfbuzz/harfbuzz_typeface_resolver.h"

#if defined(ALBOR_HAVE_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#endif

#include <memory>
#include <string>

namespace albor::harfbuzz
{
#if defined(ALBOR_HAVE_FONTCONFIG)
    namespace
    {
        // map a written weight to the fontconfig scale, which the pattern matches against (§9.1)
        int fc_weight(FontWeight weight) {
            // the thin member is the lightest stroke a family may carry (§9.1)
            if (weight == FontWeight::Thin)
                return FC_WEIGHT_THIN;
            // the light member follows thin (§9.1)
            if (weight == FontWeight::Light)
                return FC_WEIGHT_LIGHT;
            // the regular member is the default the pattern asked for before a weight was carried (§9.1)
            if (weight == FontWeight::Regular)
                return FC_WEIGHT_REGULAR;
            // the medium member follows regular (§9.1)
            if (weight == FontWeight::Medium)
                return FC_WEIGHT_MEDIUM;
            // the semibold member sits below the bold face (§9.1)
            if (weight == FontWeight::Semibold)
                return FC_WEIGHT_DEMIBOLD;
            // the bold member is the emphatic stroke a family usually carries (§9.1)
            if (weight == FontWeight::Bold)
                return FC_WEIGHT_BOLD;
            // the black member is the heaviest stroke a family may carry (§9.1)
            return FC_WEIGHT_BLACK;
        }

        // the resolver over fontconfig, which is the platform font source this engine draws from (§9.1, AD-28)
        class FontconfigTypefaceResolver final : public HarfbuzzTypefaceResolver
        {
        public:
            // resolve the family through fontconfig, which owns the pattern this resolver releases (§9.1)
            explicit FontconfigTypefaceResolver(const std::string& family_name, FontWeight weight) :
                HarfbuzzTypefaceResolver(family_name, weight) {
                // the pattern carries the family a platform face is resolved at (§9.1)
                FcPattern* pattern = FcPatternCreate();
                // a pattern that did not come up leaves no face to resolve (§9.1)
                if (pattern == nullptr)
                    return;
                // an empty family asks for the system ui face, which fontconfig names sans-serif (§9.1)
                const std::string requested = family_name.empty() ? std::string("sans-serif") : family_name;
                // the family the run names joins the pattern, because fontconfig matches by family (§9.1)
                FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(requested.c_str()));
                // the weight the run selects joins the pattern, because the configuration matches by it (§9.1)
                FcPatternAddInteger(pattern, FC_WEIGHT, fc_weight(weight));
                // the roman slant is the face a factory with no slant input resolves (§9.1)
                FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
                // the current configuration is applied, because fontconfig aliases and defaults decide the match (§9.1)
                FcConfig* config = FcConfigGetCurrent();
                // a configuration that did not come up leaves the family unresolved (§9.1)
                if (config == nullptr || !FcConfigSubstitute(config, pattern, FcMatchPattern)) {
                    FcPatternDestroy(pattern);
                    return;
                }
                // the default substitution fills the pattern a user did not name, which fontconfig requires (§9.1)
                FcDefaultSubstitute(pattern);
                // the match is the face the configuration resolves for the pattern (§9.1)
                FcResult result = FcResultNoMatch;
                FcPattern* match = FcFontMatch(config, pattern, &result);
                // the request pattern is no longer needed once the match carries the result (§9.1)
                FcPatternDestroy(pattern);
                // a pattern that matched nothing leaves no face to resolve (§9.1)
                if (match == nullptr || result != FcResultMatch) {
                    // a match that exists despite the result is released, because only a full match is kept (§9.1)
                    if (match != nullptr)
                        FcPatternDestroy(match);
                    return;
                }
                // the family is read back, because fontconfig substitutes a fallback for an absent name (§15)
                FcChar8* actual = nullptr;
                // a match with no family is unusable, because identity cannot be checked on it (§15, AD-34)
                if (FcPatternGetString(match, FC_FAMILY, 0, &actual) != FcResultMatch || actual == nullptr) {
                    FcPatternDestroy(match);
                    return;
                }
                // the default face accepts the family the configuration names, which is the system ui face (§9.1)
                const bool matched = family_name.empty() || FcStrCmpIgnoreCase(actual, reinterpret_cast<const FcChar8*>(family_name.c_str())) == 0;
                // a family that differs from the request is a fallback, which identity refuses (§15, AD-34)
                if (!matched) {
                    FcPatternDestroy(match);
                    return;
                }
                // the pattern is kept as the opaque typeface, which the renderer adapts to its own handle (§9.1, AD-28)
                m_pattern = match;
            }

            ~FontconfigTypefaceResolver() override {
                // the platform pattern is owned here, so it is released with the resolver (§9.1)
                if (m_pattern != nullptr)
                    FcPatternDestroy(m_pattern);
            }

            // override: hand out the platform pattern, which the renderer adapts to its own face (§9.1, AD-28)
            [[nodiscard]] const void* typeface() const override {
                // the handle is opaque here, and only the platform layer may name what it is (§3.5, AD-19)
                return m_pattern;
            }

        private:
            // the resolved platform pattern, which the engine keeps alive for the run's whole life (§9.1)
            FcPattern* m_pattern = nullptr;
        };
    } // namespace
#endif

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create(const std::string& family_name, FontWeight weight) {
        // an empty family name names no font, so the resolver is refused rather than built empty (§9.1)
        if (family_name.empty())
            return nullptr;
#if defined(ALBOR_HAVE_FONTCONFIG)
        // the platform resolver is returned as the base, so no platform type escapes this module (§3.5)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new FontconfigTypefaceResolver(family_name, weight));
#else
        // a build without fontconfig resolves no platform face, so embedded text is the only path (§9.1, AD-34)
        return nullptr;
#endif
    }

    std::unique_ptr<HarfbuzzTypefaceResolver> HarfbuzzTypefaceResolver::create_default(FontWeight weight) {
#if defined(ALBOR_HAVE_FONTCONFIG)
        // an empty family asks fontconfig for its system ui face, which a run that names none draws with (§9.1)
        return std::unique_ptr<HarfbuzzTypefaceResolver>(new FontconfigTypefaceResolver(std::string(), weight));
#else
        // a build without fontconfig resolves no system face, so embedded text is the only path (§9.1, AD-34)
        return nullptr;
#endif
    }
} // namespace albor::harfbuzz
