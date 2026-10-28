#include "language-server/lsp_transport.h"

#include <cctype>
#include <utility>

namespace albor::language_server
{
    namespace
    {
        // remove the optional spaces around a header name or value, which the protocol allows (§12.2)
        std::string_view trim(std::string_view text) {
            // the start walks over spaces and tabs, because a header may indent its value (§12.2)
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            // the end walks back over the same bytes, because a header may leave trailing space (§12.2)
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.remove_suffix(1);
            // the remaining slice is the meaningful part (§12.2)
            return text;
        }

        // fold a header name to lower case, because header names are case insensitive (§12.2)
        std::string lower(std::string_view text) {
            // the result accumulates, because the caller compares whole names (§12.2)
            std::string folded;
            // every byte is copied at its lower case spelling (§12.2)
            for (const char byte : text)
                folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(byte))));
            // the folded name is what the header comparison reads (§12.2)
            return folded;
        }

        // read a decimal content length, refusing anything that does not fit the transport limit (§12.2)
        bool parse_length(std::string_view text, std::size_t& length) {
            // an empty value cannot name a length, so the frame is malformed (§12.2)
            if (text.empty())
                return false;
            // the value accumulates one digit at a time, because the protocol writes it in decimal (§12.2)
            std::size_t value = 0;
            // every character must be a digit, because a sign or a unit is not part of the protocol (§12.2)
            for (const char byte : text) {
                // a byte outside the ten digits makes the header malformed (§12.2)
                if (byte < '0' || byte > '9')
                    return false;
                // the digit is the byte's distance from zero (§12.2)
                const std::size_t digit = static_cast<std::size_t>(byte - '0');
                // an overflow or a value past the limit is refused before the next multiply (§12.2)
                if (value > (MAX_MESSAGE_SIZE - digit) / 10)
                    return false;
                // the digit joins the value, which is the number the body must have (§12.2)
                value = value * 10 + digit;
            }
            // the parsed value is what the body read trusts (§12.2)
            length = value;
            return true;
        }

        // build a framing error, which carries the reason the process loop prints (§12.2)
        ReadResult error(std::string message) {
            // the result is an error, because the frame cannot be recovered (§12.2)
            ReadResult result;
            // the status tells the caller not to dispatch a body (§12.2)
            result.status = ReadStatus::Error;
            // the message names the header or body problem (§12.2)
            result.error = std::move(message);
            // the result is returned whole (§12.2)
            return result;
        }
    } // namespace

    ReadResult read_message(std::istream& input) {
        // the length is absent until a header names it, which is what the body read waits for (§12.2)
        std::size_t length = 0;
        // the flag keeps a header block from being mistaken for a leading blank line (§12.2)
        bool has_header = false;
        // the flag distinguishes an absent length from a declared length of zero (§12.2)
        bool has_length = false;
        // the line accumulates one header at a time, because the body follows the blank line (§12.2)
        std::string line;
        // the loop reads until the blank line that ends the header block (§12.2)
        while (std::getline(input, line)) {
            // a carriage return is part of the line ending on windows, not of the header (§12.2)
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            // a blank line ends the header block, which is where the body begins (§12.2)
            if (line.empty()) {
                // leading blank lines carry no header, so they are skipped rather than dispatched (§12.2)
                if (!has_header)
                    continue;
                // a frame with no length cannot be split, so it is refused before a body is allocated (§12.2)
                if (!has_length)
                    return error("the frame has no Content-Length header");
                // the body is sized exactly, because the header is the only length authority (§12.2)
                std::string body(length, '\0');
                // the body is read whole, because a partial frame cannot be parsed (§12.2)
                if (length > 0) {
                    // the declared bytes are requested, and the count below is what proves they arrived (§12.2)
                    input.read(body.data(), static_cast<std::streamsize>(length));
                    // a short read means the stream ended in the middle of the declared body (§12.2)
                    if (static_cast<std::size_t>(input.gcount()) != length)
                        return error("the frame ended before its Content-Length");
                }
                // the complete frame is handed to the json-rpc parser (§12.2)
                return ReadResult{.status = ReadStatus::Message, .body = std::move(body), .error = {}};
            }
            // a line with no colon is not a header, so the frame is malformed (§12.2)
            const std::size_t colon = line.find(':');
            // the problem points at the malformed header rather than guessing a value (§12.2)
            if (colon == std::string::npos)
                return error("a header line has no colon");
            // the name is folded, because the protocol treats header names case insensitively (§12.2)
            const std::string name = lower(trim(std::string_view(line).substr(0, colon)));
            // the value is trimmed, because the protocol allows spaces around it (§12.2)
            const std::string_view value = trim(std::string_view(line).substr(colon + 1));
            // only content-length changes the read, while every other header is ignored (§12.2)
            if (name == "content-length") {
                // a second length is ambiguous, so the frame is refused (§12.2)
                if (has_length)
                    return error("the frame has more than one Content-Length header");
                // the value must parse within the transport limit (§12.2)
                if (!parse_length(value, length))
                    return error("Content-Length is not a number or is too large");
                // the length is now declared, even when its value is zero (§12.2)
                has_length = true;
            }
            // the block now has a header, so a later blank line starts the body (§12.2)
            has_header = true;
        }
        // a stream that ends before any header is the editor closing the channel (§12.2)
        if (!has_header && input.eof())
            return ReadResult{};
        // a stream error before a header is a transport failure, which the process reports (§12.2)
        return error("the stream failed while reading a header");
    }

    bool write_message(std::ostream& output, std::string_view body) {
        // a body past the transport limit is refused, because this server could not read it back (§12.2)
        if (body.size() > MAX_MESSAGE_SIZE)
            return false;
        // the header names the exact body length, because that is how the reader splits frames (§12.2)
        output << "Content-Length: " << body.size() << "\r\n\r\n";
        // the body is written whole, because a partial message would desynchronise the reader (§12.2)
        if (!body.empty())
            output.write(body.data(), static_cast<std::streamsize>(body.size()));
        // the frame is flushed, because an editor waits for the whole message before dispatching it (§12.2)
        output.flush();
        // the write is successful only when the stream has no failure (§12.2)
        return output.good();
    }
} // namespace albor::language_server
