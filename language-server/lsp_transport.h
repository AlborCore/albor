#pragma once

#include <cstddef>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace albor::language_server
{
    // the largest frame the transport accepts, which bounds a malformed or hostile header (§12.2)
    constexpr std::size_t MAX_MESSAGE_SIZE = 64U * 1024U * 1024U;

    // what one read from the editor produced, which is a body, a clean end, or a framing error (§12.2)
    enum class ReadStatus
    {
        // a whole frame is in the body, because the header named a length and the bytes arrived (§12.2)
        Message,

        // the stream ended before a header, which is how an editor closes the channel (§12.2)
        EndOfStream,

        // the frame is malformed, so the body is empty and the error says why (§12.2)
        Error
    };

    // the result of reading one frame, which is what the process loop dispatches on (§12.2)
    struct ReadResult
    {
        // what the read produced, which decides whether the body or the error is meaningful (§12.2)
        ReadStatus status = ReadStatus::EndOfStream;

        // the json-rpc body, which is empty unless the status is Message (§12.2)
        std::string body;

        // the framing problem, which is empty unless the status is Error (§12.2)
        std::string error;
    };

    // read one content-length framed message, which is the whole of the stdio transport (§12.2)
    [[nodiscard]] ReadResult read_message(std::istream& input);

    // write one content-length framed message, which is how a reply or notification leaves the server (§12.2)
    [[nodiscard]] bool write_message(std::ostream& output, std::string_view body);
} // namespace albor::language_server
