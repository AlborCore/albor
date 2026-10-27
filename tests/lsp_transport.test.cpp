#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "language-server/lsp_transport.h"

using namespace albor;
using namespace albor::language_server;

TEST(LspTransport, ReadsOneContentLengthFrame) {
    // arrange a frame whose header and body are separated by the protocol blank line (§12.2)
    std::istringstream input("Content-Length: 7\r\n\r\n{\"a\":1}");
    // act one message is read, which is one complete exchange (§12.2)
    const ReadResult result = read_message(input);
    // assert the body is exactly the declared bytes (§12.2)
    ASSERT_EQ(result.status, ReadStatus::Message);
    EXPECT_EQ(result.body, R"({"a":1})");
    EXPECT_TRUE(result.error.empty());
}

TEST(LspTransport, TreatsHeaderNamesCaseInsensitively) {
    // arrange a lowercase length and an extra content type, which editors may either send (§12.2)
    std::istringstream input("content-length: 2\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n{}");
    // act the frame is read (§12.2)
    const ReadResult result = read_message(input);
    // assert the length is found and the unrelated header is ignored (§12.2)
    ASSERT_EQ(result.status, ReadStatus::Message);
    EXPECT_EQ(result.body, "{}");
}

TEST(LspTransport, ReadsTwoFramesFromOneStream) {
    // arrange two frames back to back, which is how a request and its notification arrive (§12.2)
    std::istringstream input("Content-Length: 2\r\n\r\n{}Content-Length: 4\r\n\r\nnull");
    // act both frames are read in order (§12.2)
    const ReadResult first = read_message(input);
    const ReadResult second = read_message(input);
    // assert the first body does not consume the second frame (§12.2)
    ASSERT_EQ(first.status, ReadStatus::Message);
    EXPECT_EQ(first.body, "{}");
    ASSERT_EQ(second.status, ReadStatus::Message);
    EXPECT_EQ(second.body, "null");
}

TEST(LspTransport, ReadsAnEmptyBody) {
    // arrange a zero length frame, which is a valid frame with no body (§12.2)
    std::istringstream input("Content-Length: 0\r\n\r\n");
    // act the frame is read (§12.2)
    const ReadResult result = read_message(input);
    // assert the zero length is a message rather than a missing header (§12.2)
    ASSERT_EQ(result.status, ReadStatus::Message);
    EXPECT_TRUE(result.body.empty());
}

TEST(LspTransport, ReportsACleanEndOfStream) {
    // arrange an empty stream, which is an editor closing the channel (§12.2)
    std::istringstream input;
    // act the transport reads from it (§12.2)
    const ReadResult result = read_message(input);
    // assert a clean end is not a framing error (§12.2)
    EXPECT_EQ(result.status, ReadStatus::EndOfStream);
    EXPECT_TRUE(result.body.empty());
    EXPECT_TRUE(result.error.empty());
}

TEST(LspTransport, RejectsAFrameWithoutAContentLength) {
    // arrange a header block that names only a content type (§12.2)
    std::istringstream input("Content-Type: application/vscode-jsonrpc\r\n\r\n{}");
    // act the frame is read (§12.2)
    const ReadResult result = read_message(input);
    // assert the missing length is a framing error (§12.2)
    EXPECT_EQ(result.status, ReadStatus::Error);
    EXPECT_FALSE(result.error.empty());
}

TEST(LspTransport, RejectsATruncatedBody) {
    // arrange a length larger than the bytes that follow (§12.2)
    std::istringstream input("Content-Length: 5\r\n\r\n{}");
    // act the frame is read (§12.2)
    const ReadResult result = read_message(input);
    // assert a partial body is not dispatched (§12.2)
    EXPECT_EQ(result.status, ReadStatus::Error);
    EXPECT_FALSE(result.error.empty());
}

TEST(LspTransport, RejectsADuplicateContentLength) {
    // arrange two lengths, which make the frame ambiguous (§12.2)
    std::istringstream input("Content-Length: 0\r\nContent-Length: 0\r\n\r\n");
    // act the frame is read (§12.2)
    const ReadResult result = read_message(input);
    // assert the ambiguity is reported (§12.2)
    EXPECT_EQ(result.status, ReadStatus::Error);
    EXPECT_FALSE(result.error.empty());
}

TEST(LspTransport, RejectsAnOversizedFrame) {
    // arrange a length past the transport bound, which no editor message should reach (§12.2)
    std::istringstream input("Content-Length: " + std::to_string(MAX_MESSAGE_SIZE + 1) + "\r\n\r\n");
    // act the header is read before any body is allocated (§12.2)
    const ReadResult result = read_message(input);
    // assert the oversized frame is refused (§12.2)
    EXPECT_EQ(result.status, ReadStatus::Error);
    EXPECT_FALSE(result.error.empty());
}

TEST(LspTransport, WritesAContentLengthFrame) {
    // arrange an output stream that captures the bytes sent to the editor (§12.2)
    std::ostringstream output;
    // act one body is framed (§12.2)
    const bool written = write_message(output, R"({"a":1})");
    // assert the header names the exact body length and the frame is complete (§12.2)
    EXPECT_TRUE(written);
    EXPECT_EQ(output.str(), "Content-Length: 7\r\n\r\n{\"a\":1}");
}

TEST(LspTransport, WritesAnEmptyFrame) {
    // arrange an output stream and an empty body, which the zero length must still frame (§12.2)
    std::ostringstream output;
    // act the empty body is framed (§12.2)
    const bool written = write_message(output, "");
    // assert the header is present even though there is no body (§12.2)
    EXPECT_TRUE(written);
    EXPECT_EQ(output.str(), "Content-Length: 0\r\n\r\n");
}
