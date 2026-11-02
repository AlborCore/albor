#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace albor::i18n
{
    // the reading direction a locale writes in, which is what layout rearranges for (§9.2)
    enum class ReadingDirection
    {
        // the locale reads left to right, which is most of them (§9.2)
        LeftToRight,

        // the locale reads right to left, which is what layout mirrors (§9.2)
        RightToLeft
    };

    // one locale's catalogue, keyed by the source string rather than an identifier to keep in step (§9.2, AD-11)
    class Catalog
    {
    public:
        // build an empty catalogue, which a generator fills before startup (§9.2)
        Catalog();

        // add one entry, which a generated table contributes (§9.2)
        void add(std::string source, std::string translated);

        // look up one entry, returning nothing when the catalogue does not carry it (§9.2)
        [[nodiscard]] std::optional<std::string> find(std::string_view source) const;

        // hand out how many entries the catalogue holds, which a coverage check reads (§9.2)
        [[nodiscard]] std::size_t size() const;

    private:
        // the entries by source text, which is what the compiler extracted (§9.2)
        std::map<std::string, std::string, std::less<>> m_entries;
    };

    // the catalogue of the current locale, which every translation resolves against (§9.2)
    [[nodiscard]] Catalog& current();

    // replace the catalogue of the current locale, which an application does once from a generated table (§9.2)
    void set_current(Catalog catalog);

    // translate one source string, falling back to the source so a reader is never shown an empty label (§9.2)
    [[nodiscard]] std::string translate(std::string_view source);

    // report whether the catalogue carries a source string, which a build check asserts on (§9.2)
    [[nodiscard]] bool translated(std::string_view source);

    // how many source strings the current catalogue is missing, which a coverage check reads (§9.2)
    [[nodiscard]] std::size_t missing_count(const std::vector<std::string>& sources);
} // namespace albor::i18n
