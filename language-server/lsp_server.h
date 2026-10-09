#pragma once

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

    private:
        // the documents the editor has open, which every request is answered from (§12.2)
        DocumentStore m_documents;

        // how many diagnostics the last compile produced, which an agreement test reads (§16)
        std::size_t m_last_diagnostics = 0;
    };
} // namespace albor::language_server
