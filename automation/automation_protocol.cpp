#include "automation/automation_protocol.h"

#include <sstream>

namespace albor::automation
{
    namespace
    {
        // the field separator on the wire, which a client cannot smuggle into a name (§10.3)
        constexpr char SEPARATOR = ' ';

        // the prefix a refusal carries, because a client reads it rather than guessing (§10.3)
        constexpr std::string_view REFUSAL = "!refused ";

        // the prefix an answer carries, because a client reads it rather than guessing (§10.3)
        constexpr std::string_view ANSWER = "!ok ";
    } // namespace

    AutomationResult AutomationResult::answered(std::string payload) {
        // the answer is built in place, because there is nothing else to report (§10.3)
        return AutomationResult{.ok = true, .payload = std::move(payload), .error = {}};
    }

    AutomationResult AutomationResult::refused(std::string error) {
        // the refusal carries no payload, because there is no answer to report (§10.3)
        return AutomationResult{.ok = false, .payload = {}, .error = std::move(error)};
    }

    AutomationRequest decode_request(std::string_view line) {
        // the request is assembled as it is read, because one malformed field costs one field (§10.3)
        AutomationRequest request;
        // the stream splits on the separator, which is what the wire format is (§10.3)
        std::istringstream stream{std::string(line)};
        // the command comes first, because it is what the session dispatches on (§10.3)
        std::string field;
        // the command is read first, because everything else qualifies it (§10.3)
        if (std::getline(stream, field, SEPARATOR))
            request.command = field;
        // the arguments follow in order, because the order is the call's shape (§10.3)
        while (std::getline(stream, field, SEPARATOR))
            request.arguments.push_back(field);
        // the request is returned whole, because the session decides what it means (§10.3)
        return request;
    }

    std::string encode_result(const AutomationResult& result) {
        // a refusal is written first, because a client must not read a refusal as an answer (§10.3, AD-10)
        if (!result.ok)
            return std::string(REFUSAL) + result.error;
        // an answer is written with its payload, which is the whole reply (§10.3)
        return std::string(ANSWER) + result.payload;
    }

    std::string encode_handshake(std::string_view token) {
        // the handshake is one field so no argument can be smuggled ahead of the token (§10.3, AD-10)
        return std::string("!auth ") + std::string(token);
    }
} // namespace albor::automation
