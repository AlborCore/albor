#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace albor::automation
{
    // the shape of an answered value, which the wire tag and the mcp block both read (§10.3, AD-20)
    enum class ValueKind
    {
        // a plain string, which every command answered before the typed tag existed (§10.3)
        Text,

        // a number, which the payload spells in decimal (§10.3)
        Number,

        // a boolean, which the payload spells as yes or no (§10.3)
        Boolean,

        // a structured value, which the payload spells as json (§10.3)
        Json,

        // an image a client decodes as one, which the wire carries as base64 (§6.3, §10.3)
        Image,

        // an opaque byte string, which the wire carries as base64 because a line cannot carry raw bytes (§10.3)
        Binary,
    };

    // one request an out of process client sends, which is a command name and its arguments (§10.3, AD-10)
    struct AutomationRequest
    {
        // the command name, which must appear in the session's allow list before it runs (§10.3, AD-10)
        std::string command;

        // the arguments, which are already split because the transport decoded them (§10.3)
        std::vector<std::string> arguments{};
    };

    // one response the server sends back, which is either an answer or a stated refusal (§10.3, AD-10)
    struct AutomationResult
    {
        // whether the command ran, which a refusal reports as false rather than as an empty answer (§10.3)
        bool ok = false;

        // the shape of the answer, which a client reads before it decodes the payload (§10.3, AD-20)
        ValueKind kind = ValueKind::Text;

        // the answer, which is empty on a refusal because there is nothing to report (§10.3)
        std::string payload;

        // the stated reason a command was refused, which is what a client reads to learn why (§10.3, AD-10)
        std::string error;

        // an answered request, which is the only shape that carries a payload (§10.3)
        static AutomationResult answered(std::string payload);

        // an answered request whose payload has a shape, which is what a typed client switches on (§10.3)
        static AutomationResult answered(ValueKind kind, std::string payload);

        // a refused request, which carries the reason and never a payload (§10.3, AD-10)
        static AutomationResult refused(std::string error);
    };

    // the response to a handshake, which is what a client proves before any command runs (§10.3, AD-10)
    struct HandshakeResult
    {
        // whether the presented token was the one this session issued (§10.3, AD-10)
        bool authenticated = false;

        // the stated reason a handshake was refused, which never discloses the expected token (§10.3)
        std::string error;
    };

    // the tag a value kind carries on the wire, which is what a client switches on (§10.3)
    [[nodiscard]] std::string_view value_kind_name(ValueKind kind);

    // encode bytes as base64, which is the only form a line or a json string can carry (§10.3)
    [[nodiscard]] std::string base64_encode(std::string_view bytes);

    // decode one wire line into a request, which the transport hands the session (§10.3)
    [[nodiscard]] AutomationRequest decode_request(std::string_view line);

    // encode one result into a wire line, which the transport writes back (§10.3)
    [[nodiscard]] std::string encode_result(const AutomationResult& result);

    // the handshake line a client sends first, which carries the session token (§10.3, AD-10)
    [[nodiscard]] std::string encode_handshake(std::string_view token);
} // namespace albor::automation
