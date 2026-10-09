#include "language-server/lsp_document.h"

#include <utility>

namespace albor::language_server
{
    namespace
    {
        // index every line start in the text, so a line lookup does not rescan (§12.2)
        std::vector<std::size_t> index_lines(const std::string& text) {
            // the starts accumulate, with the first line always at zero (§12.2)
            std::vector<std::size_t> starts{0};
            // the text is walked, because a newline ends a line (§12.2)
            for (std::size_t i = 0; i < text.size(); i++) {
                // a newline starts the next line, which is what a line index records (§12.2)
                if (text[i] == '\n')
                    starts.push_back(i + 1);
            }
            // the starts are returned, which is the whole index (§12.2)
            return starts;
        }
    } // namespace

    Document::Document(std::string uri, std::string text, std::int64_t version) :
        m_uri(std::move(uri)), m_text(std::move(text)), m_version(version), m_line_starts(index_lines(m_text)) {}

    const std::string& Document::uri() const { return m_uri; }
    const std::string& Document::text() const { return m_text; }
    std::int64_t Document::version() const { return m_version; }

    bool Document::update(std::string text, std::int64_t version) {
        // a version that is not newer is stale, because editors may reorder their notifications (§12.2)
        if (version <= m_version)
            return false;
        // the text is replaced, because a newer version supersedes what was held (§12.2)
        m_text = std::move(text);
        // the version is recorded, so a later stale notification is rejected the same way (§12.2)
        m_version = version;
        // the line index is rebuilt, because the text moved underneath it (§12.2)
        m_line_starts = index_lines(m_text);
        // the update was accepted, which is what a caller needs to know (§12.2)
        return true;
    }

    std::string_view Document::line_at(std::size_t line) const {
        // a line past the end names nothing, which is what an empty range means (§12.2)
        if (line >= m_line_starts.size())
            return {};
        // the line starts at its recorded offset (§12.2)
        const std::size_t start = m_line_starts[line];
        // the line ends at the next start, or at the end of the text for the last line (§12.2)
        const std::size_t end = line + 1 < m_line_starts.size() ? m_line_starts[line + 1] : m_text.size();
        // the slice is trimmed of its newline, because a line's text does not carry one (§12.2)
        std::string_view slice{m_text.data() + start, end - start};
        // the newline is trimmed, because the protocol counts characters within a line (§12.2)
        while (!slice.empty() && (slice.back() == '\n' || slice.back() == '\r'))
            slice.remove_suffix(1);
        // the line is returned, which is what a hover or a completion reads (§12.2)
        return slice;
    }

    std::size_t Document::line_count() const { return m_line_starts.size(); }

    bool DocumentStore::open(std::string uri, std::string text, std::int64_t version) {
        // the uri is taken by value, because a document owns its own copy of it (§12.2)
        std::string key = uri;
        // a document already open is updated rather than replaced, because an editor may resend its text (§12.2)
        const auto found = m_documents.find(key);
        // an existing document takes the newer version, and rejects a stale one (§12.2)
        if (found != m_documents.end())
            return found->second.update(std::move(text), version);
        // a new document is stored, which is what didOpen means (§12.2)
        m_documents.insert_or_assign(std::move(key), Document(uri, std::move(text), version));
        // the open was accepted (§12.2)
        return true;
    }

    const Document* DocumentStore::find(std::string_view uri) const {
        // the map is searched, because a uri names exactly one document (§12.2)
        const auto found = m_documents.find(uri);
        // an unknown uri names nothing, which a caller reports rather than dereferences (§12.2)
        return found == m_documents.end() ? nullptr : &found->second;
    }

    std::size_t DocumentStore::size() const { return m_documents.size(); }

    void DocumentStore::close(std::string_view uri) {
        // the document is looked up first, because a transparent erase needs a key of the map's own type (§12.2)
        const auto found = m_documents.find(uri);
        // an unknown uri names nothing, which is not an error to a close (§12.2)
        if (found == m_documents.end())
            return;
        // the document is forgotten, which is what a close means (§12.2)
        m_documents.erase(found);
    }
} // namespace albor::language_server
