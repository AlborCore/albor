#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "language-server/lsp_document.h"

namespace albor::language_server
{
    // one notification the server sends unprompted, which is how diagnostics reach an editor (§12.2)
    struct Notification
    {
        // the method name, which is what an editor dispatches on (§12.2)
        std::string method;

        // the notification body, which is what the method's payload is (§12.2)
        std::string payload;
    };

    // what one request produced, which is a reply, a notification, or both (§12.2)
    struct Exchange
    {
        // the reply, which is absent for a notification (§12.2)
        std::optional<std::string> reply;

        // the notifications sent while handling it, which is how diagnostics are published (§12.2, §16)
        std::vector<Notification> notifications;
    };

    // the language server, which answers an editor using the same compiler the build runs (§12.2, §16)
    class LspServer
    {
    public:
        // handle one json-rpc line, which is the whole of the protocol (§12.2)
        [[nodiscard]] Exchange handle_line(std::string_view line);

        // hand out how many diagnostics the last compile produced, which an agreement test asserts on (§16)
        [[nodiscard]] std::size_t last_diagnostic_count() const;

        // hand out how many documents are open, which a test asserts on (§12.2)
        [[nodiscard]] std::size_t document_count() const;

        // report whether an exit notification arrived, which is what ends the stdio loop (§12.2)
        [[nodiscard]] bool exit_requested() const;

    private:
        // compile an open document as a root, publishing imports and clearing stale publications (§12.2, §16)
        [[nodiscard]] Exchange compile_root(std::string_view uri);

        // forget a closed root and clear the publications no other root still owns (§12.2)
        void close_root(std::string_view uri, std::vector<Notification>& notifications);

        // the documents the editor has open, which every request is answered from (§12.2)
        DocumentStore m_documents;

        // the uris each open root published, which is what a later compile or close clears (§12.2, §16)
        std::map<std::string, std::vector<std::string>, std::less<>> m_root_publications;

        // the files each open root read, which is what an imported change recompiles (§11.1, §12.2)
        std::map<std::string, std::vector<std::string>, std::less<>> m_root_dependencies;

        // how many diagnostics the last compile produced, which an agreement test reads (§16)
        std::size_t m_last_diagnostics = 0;

        // whether an exit notification arrived, which makes the stdio loop stop (§12.2)
        bool m_exit_requested = false;
    };
} // namespace albor::language_server
