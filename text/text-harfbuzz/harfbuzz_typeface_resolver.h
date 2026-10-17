#pragma once

#include <memory>
#include <string>

namespace albor::harfbuzz
{
    // resolve the platform font a run draws with, which every platform supplies by family name (§9.1, AD-28)
    class HarfbuzzTypefaceResolver
    {
    public:
        // resolve the named family to a drawable typeface, returning null when the platform has no such font (§9.1)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTypefaceResolver> create(const std::string& family_name);

        // resolve the platform default face, which an engine with no named family draws with (§9.1, AD-28)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTypefaceResolver> create_default();

        virtual ~HarfbuzzTypefaceResolver() = default;

        HarfbuzzTypefaceResolver(const HarfbuzzTypefaceResolver&) = delete;
        HarfbuzzTypefaceResolver& operator=(const HarfbuzzTypefaceResolver&) = delete;
        HarfbuzzTypefaceResolver(HarfbuzzTypefaceResolver&&) = delete;
        HarfbuzzTypefaceResolver& operator=(HarfbuzzTypefaceResolver&&) = delete;

        // hand out the resolved platform font, which the renderer adapts to its own typeface (§9.1, AD-28)
        [[nodiscard]] virtual const void* typeface() const = 0;

    protected:
        // build the resolver over a family name, which each platform implements (§9.1)
        explicit HarfbuzzTypefaceResolver(const std::string& family_name) :
            m_family_name(family_name) {}

        // the family this resolver looks up, which the platform implementation matches (§9.1)
        const std::string m_family_name;
    };
} // namespace albor::harfbuzz
