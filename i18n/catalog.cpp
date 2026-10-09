#include "i18n/catalog.h"

#include <mutex>
#include <utility>

namespace albor::i18n
{
    Catalog::Catalog() = default;

    void Catalog::add(std::string source, std::string translated) {
        // an empty source names nothing, so there is nothing a reader would see (§9.2)
        if (source.empty())
            return;
        // the entry is added, because a later table may legitimately refine an earlier one (§9.2)
        m_entries.insert_or_assign(std::move(source), std::move(translated));
    }

    std::optional<std::string> Catalog::find(std::string_view source) const {
        // the entry is searched by source, because that is the key the compiler extracted (§9.2)
        const auto found = m_entries.find(source);
        // an absent entry yields nothing, which the caller turns into the source itself (§9.2)
        if (found == m_entries.end())
            return std::nullopt;
        // the translation is returned whole (§9.2)
        return found->second;
    }

    std::size_t Catalog::size() const { return m_entries.size(); }

    namespace
    {
        // the catalogue in use, which startup replaces from a generated table (§9.2)
        Catalog g_catalog;

        // the guard, because a table is installed at startup beside readers on other threads (§9.2, §10.5)
        std::mutex g_mutex;
    } // namespace

    Catalog& current() {
        // the guard is taken, because two threads may read or install a catalogue (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // the catalogue is returned by reference, which is what a caller reads through (§9.2)
        return g_catalog;
    }

    void set_current(Catalog catalog) {
        // the guard is taken, because installation and reading must not overlap (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // the catalogue is replaced wholesale, because a locale is one table rather than a mixture (§9.2)
        g_catalog = std::move(catalog);
    }

    std::string translate(std::string_view source) {
        // the guard is taken, because a reader may be on another thread (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // the entry is looked up by source text (§9.2)
        const auto found = g_catalog.find(source);
        // an untranslated string reads as itself, which is the honest fallback (§9.2)
        if (!found)
            return std::string(source);
        // the translation is returned whole (§9.2)
        return *found;
    }

    bool translated(std::string_view source) {
        // the guard is taken, because a reader may be on another thread (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // presence is what a coverage check asks (§9.2)
        return g_catalog.find(source).has_value();
    }

    std::size_t missing_count(const std::vector<std::string>& sources) {
        // the guard is taken, because a coverage check may run beside a reader (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // the count is what the vector holds, which is what the compiler extracted (§9.2)
        std::size_t missing = 0;
        // each source is checked, because a catalogue is judged entry by entry (§9.2)
        for (const std::string& source : sources) {
            // a source with no entry is one a reader would see untranslated (§9.2)
            if (!g_catalog.find(source))
                missing++;
        }
        // the count is returned whole (§9.2)
        return missing;
    }
} // namespace albor::i18n
