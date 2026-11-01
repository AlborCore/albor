#include "automation/automation_protocol.h"

#include <cstdint>
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

        // the value one hexadecimal digit stands for, or -1 when the byte is not a digit (§10.3)
        int hex_value(char digit) {
            // a decimal digit is its own value, which is the first half of every escape (§10.3)
            if (digit >= '0' && digit <= '9')
                return digit - '0';
            // an upper case letter continues after nine, which the encoder writes (§10.3)
            if (digit >= 'A' && digit <= 'F')
                return digit - 'A' + 10;
            // a lower case letter is accepted too, because a hand written client may spell it (§10.3)
            if (digit >= 'a' && digit <= 'f')
                return digit - 'a' + 10;
            // anything else is not a hex digit, so it is copied as itself (§10.3)
            return -1;
        }

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

    std::string encode_argument(std::string_view argument) {
        // the escaped form is built once and only grows when a byte needs it (§10.3)
        std::string escaped;
        escaped.reserve(argument.size());
        // every byte is walked, because an ordinary path byte passes through as itself (§10.3)
        for (const char byte : argument) {
            // a percent starts an escape, so it is escaped first (§10.3)
            if (byte == '%') {
                escaped += "%25";
                continue;
            }
            // a separator would split the argument, which is the whole reason for the escape (§10.3)
            if (byte == ' ') {
                escaped += "%20";
                continue;
            }
            // a tab, a carriage return and a newline would frame the line wrongly, so each is escaped (§10.3)
            if (byte == '\t') {
                escaped += "%09";
                continue;
            }
            if (byte == '\n') {
                escaped += "%0A";
                continue;
            }
            if (byte == '\r') {
                escaped += "%0D";
                continue;
            }
            // every other byte is the byte itself, so plain text stays byte identical (§10.3)
            escaped += byte;
        }
        return escaped;
    }

    std::string decode_argument(std::string_view argument) {
        // the recovered form is built once, because a plain argument costs one copy (§10.3)
        std::string decoded;
        decoded.reserve(argument.size());
        // the scan walks the argument and consumes one escape at a time (§10.3)
        for (std::size_t index = 0; index < argument.size(); index++) {
            // a percent that does not open a two digit escape is the byte itself (§10.3)
            if (argument[index] != '%' || index + 2 >= argument.size()) {
                decoded += argument[index];
                continue;
            }
            // both digits must be hexadecimal, or the percent was literal text (§10.3)
            const int high = hex_value(argument[index + 1]);
            const int low = hex_value(argument[index + 2]);
            if (high < 0 || low < 0) {
                decoded += argument[index];
                continue;
            }
            // the two digits spell one byte, which is what the encoder wrote (§10.3)
            decoded += static_cast<char>((high << 4) | low);
            index += 2;
        }
        return decoded;
    }

    AutomationRequest decode_request(std::string_view line) {
        // the request is assembled as it is read, because one malformed field costs one field (§10.3)
        AutomationRequest request;
        // every separator ends one field, and a trailing separator names an empty one (§10.3)
        std::size_t start = 0;
        // the command is the first field, because it is what the session dispatches on (§10.3)
        bool command_read = false;
        for (;;) {
            // the next separator is where this field ends, or the line ends the field (§10.3)
            const std::size_t separator = line.find(SEPARATOR, start);
            // the field is what stands between the previous separator and this one (§10.3)
            const std::string_view field = line.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start);
            // the first field names the command, which the session dispatches by (§10.3)
            if (!command_read) {
                request.command = std::string(field);
                command_read = true;
            }
            // every later field is an argument, which the escape restores (§10.3)
            else {
                request.arguments.push_back(decode_argument(field));
            }
            // a field the line ended is the last one, whether it is empty or not (§10.3)
            if (separator == std::string_view::npos)
                break;
            // the scan resumes past the separator, which keeps a trailing empty field (§10.3)
            start = separator + 1;
        }
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
