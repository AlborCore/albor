#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace albor::language_server
{
    // one open document the server holds, which is the text an editor is showing (§12.2)
    class Document
    {
    public:
        // build a document over a uri and its text, which is what an open notification carries (§12.2)
        Document(std::string uri, std::string text, std::int64_t version);

        // hand out the uri the editor knows this document by (§12.2)
        [[nodiscard]] const std::string& uri() const;

        // hand out the text as last received, which is what a compile reads (§12.2)
        [[nodiscard]] const std::string& text() const;

        // hand out the version the editor gave it, which a stale notification is rejected by (§12.2)
        [[nodiscard]] std::int64_t version() const;

        // replace the text when the version is newer, reporting whether it was accepted (§12.2)
        bool update(std::string text, std::int64_t version);

        // hand out the line the editor's line index names, which is zero based as the protocol counts (§12.2)
        [[nodiscard]] std::string_view line_at(std::size_t line) const;

        // hand out how many lines the text holds, which a position check needs (§12.2)
        [[nodiscard]] std::size_t line_count() const;

    private:
        // the uri the editor knows this document by (§12.2)
        std::string m_uri;

        // the text as last received, which is what a compile reads (§12.2)
        std::string m_text;

        // the version the editor gave it, which a stale notification is rejected by (§12.2)
        std::int64_t m_version;

        // where each line starts, so a line index does not rescan the text (§12.2)
        std::vector<std::size_t> m_line_starts;
    };

    // the documents one editor has open, which is what the server answers from (§12.2)
    class DocumentStore
    {
    public:
        // open or replace a document, which is what didOpen and didChange both do (§12.2)
        bool open(std::string uri, std::string text, std::int64_t version);

        // hand out a document by uri, which is what every request reads (§12.2)
        [[nodiscard]] const Document* find(std::string_view uri) const;

        // hand out how many documents are open, which a test asserts on (§12.2)
        [[nodiscard]] std::size_t size() const;

        // forget a document, which is what didClose means (§12.2)
        void close(std::string_view uri);

    private:
        // the open documents by uri, which is what a request is dispatched through (§12.2)
        std::map<std::string, Document, std::less<>> m_documents;
    };
} // namespace albor::language_server
