#include "automation/automation_protocol.h"

#include <cstdint>
#include <sstream>
#include <utility>

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

        // append a payload with one line of framing intact, which escapes the three bytes that would break it (§10.3)
        void append_escaped(std::string& out, std::string_view payload) {
            for (const char byte : payload) {
                switch (byte) {
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                default:
                    out.push_back(byte);
                    break;
                }
            }
        }
    } // namespace

    AutomationResult AutomationResult::answered(std::string payload) {
        // the answer is built in place, because there is nothing else to report (§10.3)
        return AutomationResult{.ok = true, .kind = ValueKind::Text, .payload = std::move(payload), .error = {}};
    }

    AutomationResult AutomationResult::answered(ValueKind kind, std::string payload) {
        // the kind is carried beside the bytes, which is what tells a client how to read them (§10.3)
        return AutomationResult{.ok = true, .kind = kind, .payload = std::move(payload), .error = {}};
    }

    AutomationResult AutomationResult::refused(std::string error) {
        // the refusal carries no payload, because there is no answer to report (§10.3)
        return AutomationResult{.ok = false, .kind = ValueKind::Text, .payload = {}, .error = std::move(error)};
    }

    std::string_view value_kind_name(ValueKind kind) {
        switch (kind) {
        case ValueKind::Text:
            return "text";
        case ValueKind::Number:
            return "number";
        case ValueKind::Boolean:
            return "boolean";
        case ValueKind::Json:
            return "json";
        case ValueKind::Image:
            return "image";
        case ValueKind::Binary:
            return "binary";
        }
        // a kind outside the enumeration reads as text, which every client can still consume (§10.3)
        return "text";
    }

    std::string base64_encode(std::string_view bytes) {
        // the alphabet the wire uses, which is the one every client decodes (§10.3)
        constexpr std::string_view ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        // every three bytes become four characters, which is what the padding accounts for (§10.3)
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        // the whole triplets are encoded first, which is the bulk of any frame (§10.3)
        std::size_t index = 0;
        for (; index + 2 < bytes.size(); index += 3) {
            // the three bytes are packed into the twenty four bits the four characters carry (§10.3)
            const std::uint32_t group = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index + 1])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index + 2]));
            out.push_back(ALPHABET[(group >> 18) & 0x3Fu]);
            out.push_back(ALPHABET[(group >> 12) & 0x3Fu]);
            out.push_back(ALPHABET[(group >> 6) & 0x3Fu]);
            out.push_back(ALPHABET[group & 0x3Fu]);
        }
        // a tail of one byte leaves twelve bits, which two characters and two padding marks carry (§10.3)
        const std::size_t remaining = bytes.size() - index;
        if (remaining == 1) {
            const std::uint32_t group = static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index])) << 16;
            out.push_back(ALPHABET[(group >> 18) & 0x3Fu]);
            out.push_back(ALPHABET[(group >> 12) & 0x3Fu]);
            out.push_back('=');
            out.push_back('=');
        }
        // a tail of two bytes leaves eighteen bits, which three characters and one padding mark carry (§10.3)
        else if (remaining == 2) {
            const std::uint32_t group = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[index + 1])) << 8);
            out.push_back(ALPHABET[(group >> 18) & 0x3Fu]);
            out.push_back(ALPHABET[(group >> 12) & 0x3Fu]);
            out.push_back(ALPHABET[(group >> 6) & 0x3Fu]);
            out.push_back('=');
        }
        return out;
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
        // the answer opens with its kind, which is what a client switches on before it reads the payload (§10.3)
        std::string line = std::string(ANSWER) + std::string(value_kind_name(result.kind));
        // the separator keeps the tag apart from the payload, which may itself be empty (§10.3)
        line.push_back(SEPARATOR);
        // a byte string travels as base64, because a line cannot carry every byte a frame holds (§10.3)
        if (result.kind == ValueKind::Image || result.kind == ValueKind::Binary)
            line += base64_encode(result.payload);
        else
            append_escaped(line, result.payload);
        return line;
    }

    std::string encode_handshake(std::string_view token) {
        // the handshake is one field so no argument can be smuggled ahead of the token (§10.3, AD-10)
        return std::string("!auth ") + std::string(token);
    }
} // namespace albor::automation
