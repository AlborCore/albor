#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "core/json.h"
#include "language-server/lsp_server.h"
#include "language-server/lsp_transport.h"
#include "tools/albor-lsp/stdio_mode.h"

namespace
{
    // build the json-rpc notification the editor receives, because the server publishes only its parameters (§12.2)
    std::optional<std::string> notification_line(const albor::language_server::Notification& notification) {
        // the parameters are parsed, because the server hands them over as a json value (§12.2)
        std::optional<albor::core::Json> params = albor::core::Json::parse(notification.payload);
        // a payload the server cannot have produced is a transport failure rather than a malformed notification (§12.2)
        if (!params)
            return std::nullopt;
        // the notification is an object, because every protocol message is (§12.2)
        albor::core::Json line;
        // the version identifies the protocol, which the editor checks (§12.2)
        line.set("jsonrpc", albor::core::Json(std::string("2.0")));
        // the method is what the editor dispatches on, which comes from the server (§12.2)
        line.set("method", albor::core::Json(notification.method));
        // the parameters are the server's payload, which the editor reads after the method (§12.2)
        line.set("params", std::move(*params));
        // the complete notification is what the transport frames (§12.2)
        return line.to_string();
    }

    // write one exchange, which is the reply first and then the notifications the server raised (§12.2)
    bool write_exchange(const albor::language_server::Exchange& exchange) {
        // a reply is written first, because an editor matches it before it applies a notification (§12.2)
        if (exchange.reply && !albor::language_server::write_message(std::cout, *exchange.reply))
            return false;
        // every notification is written in the order the server raised it (§12.2)
        for (const albor::language_server::Notification& notification : exchange.notifications) {
            // the server payload is wrapped in the notification envelope the protocol defines (§12.2)
            const std::optional<std::string> line = notification_line(notification);
            // a payload that cannot be wrapped is a failure, because dropping it would hide diagnostics (§12.2)
            if (!line)
                return false;
            // the framed notification is what the editor reads next (§12.2)
            if (!albor::language_server::write_message(std::cout, *line))
                return false;
        }
        // every message in the exchange reached the channel (§12.2)
        return true;
    }
} // namespace

int main() {
    // the frame carries bytes, so windows text translation is disabled before the first read (§12.2)
    if (!albor::tools::set_binary_stdio()) {
        // a channel that cannot be made binary would corrupt every length, so the process refuses to start (§12.2)
        std::fprintf(stderr, "albor-lsp: cannot set binary stdio\n");
        return 1;
    }
    // the iostreams stay unsynchronised, because the transport already owns the byte stream (§12.2)
    std::ios::sync_with_stdio(false);
    // the server owns the editor state, which lives for the whole channel (§12.2)
    albor::language_server::LspServer server;
    // the loop reads frames until the editor closes the channel or sends exit (§12.2)
    for (;;) {
        // one frame is read with its content-length header, which is the transport boundary (§12.2)
        albor::language_server::ReadResult message = albor::language_server::read_message(std::cin);
        // a clean end is how an editor closes the channel, so the process exits successfully (§12.2)
        if (message.status == albor::language_server::ReadStatus::EndOfStream)
            return 0;
        // a malformed frame cannot be resynchronised, so the process reports and stops (§12.2)
        if (message.status == albor::language_server::ReadStatus::Error) {
            // the reason names the header or body that failed, which is what a developer needs (§12.2)
            std::fprintf(stderr, "albor-lsp: %s\n", message.error.c_str());
            return 1;
        }
        // the frame is handed to the server, which answers with a reply, notifications, or both (§12.2)
        const albor::language_server::Exchange exchange = server.handle_line(message.body);
        // a failed write leaves the editor waiting, so the process reports and stops (§12.2)
        if (!write_exchange(exchange)) {
            // the channel is no longer reliable, because a frame may be partial (§12.2)
            std::fprintf(stderr, "albor-lsp: cannot write a message\n");
            return 1;
        }
        // exit ends the loop after its exchange, because the protocol answers nothing to it (§12.2)
        if (server.exit_requested())
            return 0;
    }
}
