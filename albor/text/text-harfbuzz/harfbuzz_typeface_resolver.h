#pragma once

#include <memory>
#include <string>

#include "albor/core/font_weight.h"

namespace albor::harfbuzz
{
    // resolve the platform font whose bytes a run shapes and draws from, which each platform supplies (§9.1, AD-34)
    class HarfbuzzTypefaceResolver
    {
    public:
        // resolve the named family at a weight, whose typeface is null when the platform has no such font (§9.1)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTypefaceResolver> create(const std::string& family_name, FontWeight weight = FontWeight::Regular);

        // resolve the platform default face, which an engine with no named family draws with (§9.1, AD-28)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTypefaceResolver> create_default(FontWeight weight = FontWeight::Regular);

        virtual ~HarfbuzzTypefaceResolver() = default;

        HarfbuzzTypefaceResolver(const HarfbuzzTypefaceResolver&) = delete;
        HarfbuzzTypefaceResolver& operator=(const HarfbuzzTypefaceResolver&) = delete;
        HarfbuzzTypefaceResolver(HarfbuzzTypefaceResolver&&) = delete;
        HarfbuzzTypefaceResolver& operator=(HarfbuzzTypefaceResolver&&) = delete;

        // hand out the resolved platform font, whose bytes the engine reads into the run (§9.1, AD-34)
        [[nodiscard]] virtual const void* typeface() const = 0;

    protected:
        // build the resolver over a family and the weight it selects a face at, which each platform implements (§9.1)
        explicit HarfbuzzTypefaceResolver(const std::string& family_name, FontWeight weight) :
            m_family_name(family_name), m_weight(weight) {}

        // the family this resolver looks up, which the platform implementation matches (§9.1)
        const std::string m_family_name;

        // the weight the platform selects a face at, which a run's selection carries (§9.1)
        const FontWeight m_weight;
    };
} // namespace albor::harfbuzz
