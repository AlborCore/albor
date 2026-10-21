#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "automation/automation_registry.h"
#include "automation/capture_commands.h"

using namespace albor::automation;

TEST(CaptureCommands, EveryNameItPublishesIsTheOneItRegisters) {
    // arrange a source that answers a one pixel frame, which is what a dispatch needs (§6.3)
    AutomationRegistry registry;
    register_capture_commands(registry, [] { return CapturedFrame{.width = 1, .height = 1, .pixels = {1, 2, 3, 4}}; });
    // act every published name is permitted and dispatched, which is the pairing a host depends on (§10.3, AD-10)
    const std::vector<std::string> names = capture_command_names();
    ASSERT_EQ(names.size(), 1U);
    EXPECT_EQ(names.front(), "capture.screenshot");
    registry.allow(names.front());
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = names.front()});
    // assert the published name really runs, because a rename on one side alone would leave it unreachable (§10.3)
    EXPECT_TRUE(result.ok);
}

TEST(CaptureCommands, ACaptureWithNoSourceIsRefused) {
    // arrange a registry whose host supplied no window source, which is a build with no binding (§10.3)
    AutomationRegistry registry;
    register_capture_commands(registry, CaptureSnapshot{});
    registry.allow("capture.screenshot");
    // act and assert the refusal names the reason rather than answering empty (§10.3)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot"});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.payload.empty());
    EXPECT_EQ(result.error, "the host supplied no capture source");
}

TEST(CaptureCommands, ACaptureWithoutPermissionNeverReachesTheSource) {
    // arrange a source that counts its reads, which is what a refused request must not touch (§10.3, AD-10)
    std::size_t reads = 0;
    AutomationRegistry registry;
    register_capture_commands(registry, [&reads] {
        reads++;
        return CapturedFrame{.width = 1, .height = 1, .pixels = {1, 2, 3, 4}};
    });
    // act the command is registered but not permitted, which is the state every host starts from (§10.3, AD-10)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot"});
    // assert the refusal happens before the read, because registration alone is not consent (§10.3, AD-10)
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "command not allowed: capture.screenshot");
    EXPECT_EQ(reads, 0U);
}

TEST(CaptureCommands, AWindowThatHasNotPresentedIsRefused) {
    // arrange a window whose read failed while its extent looked usable, which only the error branch can refuse (§6.3)
    AutomationRegistry registry;
    register_capture_commands(registry, [] { return CapturedFrame{.error = "the window has not presented a frame yet", .width = 2, .height = 2, .pixels = std::vector<std::uint8_t>(16, 0)}; });
    registry.allow("capture.screenshot");
    // act and assert the refusal carries the stated reason, which keeps a black frame from passing as a capture (§6.3)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot"});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.payload.empty());
    EXPECT_EQ(result.error, "the window has not presented a frame yet");
}

TEST(CaptureCommands, AFrameWhoseExtentDisagreesWithItsBytesIsRefused) {
    // arrange the three shapes a broken read can take, which no encoder turns into a picture (§6.3)
    const CapturedFrame frames[4] = {
        CapturedFrame{.width = 2, .height = 1},
        CapturedFrame{.width = 0, .height = 1, .pixels = {1, 2, 3, 4}},
        CapturedFrame{.width = 2, .height = 0, .pixels = {1, 2, 3, 4}},
        CapturedFrame{.width = 1, .height = 1, .pixels = {1, 2, 3, 4, 5, 6, 7, 8}},
    };
    for (const CapturedFrame& frame : frames) {
        AutomationRegistry registry;
        register_capture_commands(registry, [frame] { return frame; });
        registry.allow("capture.screenshot");
        // act and assert each shape is refused, because a header over the wrong bytes describes another picture (§6.3)
        const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot"});
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.payload.empty());
    }
}

TEST(CaptureCommands, ARawFrameCarriesItsHeaderAndBytes) {
    // arrange a three by two frame, whose stride and payload differ from its height and format (§6.3)
    AutomationRegistry registry;
    const std::vector<std::uint8_t> pixels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    register_capture_commands(registry, [pixels] { return CapturedFrame{.width = 3, .height = 2, .pixels = pixels}; });
    registry.allow("capture.screenshot");
    // act the byte exact format is asked for, which a pixel comparison reads (§6.3)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot", .arguments = {"raw"}});
    // assert the answer is tagged as bytes, because a client must not read a raw frame as text (§6.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.kind, ValueKind::Binary);
    EXPECT_EQ(encode_result(result).substr(0, 10), "!ok binary");
    ASSERT_EQ(result.payload.size(), 24U + pixels.size());
    EXPECT_EQ(result.payload.substr(0, 8), "ALBRSHOT");
    EXPECT_EQ(result.payload.substr(8, 4), std::string("\x03\0\0\0", 4));
    EXPECT_EQ(result.payload.substr(12, 4), std::string("\x02\0\0\0", 4));
    EXPECT_EQ(result.payload.substr(16, 4), std::string("\x0c\0\0\0", 4));
    EXPECT_EQ(result.payload.substr(20, 4), std::string("\x01\0\0\0", 4));
    // assert the bytes follow verbatim, because the raw format is the one a pixel may be compared on (§6.3)
    for (std::size_t index = 0; index < pixels.size(); index++)
        EXPECT_EQ(static_cast<std::uint8_t>(result.payload[24U + index]), pixels[index]);
}

TEST(CaptureCommands, TheDefaultFormatIsAPicture) {
    // arrange a one pixel frame, which either encoder can describe (§6.3)
    AutomationRegistry registry;
    register_capture_commands(registry, [] { return CapturedFrame{.width = 1, .height = 1, .pixels = {1, 2, 3, 4}}; });
    registry.allow("capture.screenshot");
    // act a client names no format, which takes the default this layer fixed (§6.3)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot"});
    // assert the answer is an image whose bytes are a png, which is what a person opens (§6.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.kind, ValueKind::Image);
    ASSERT_GT(result.payload.size(), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(result.payload[0]), 0x89U);
    EXPECT_EQ(result.payload.substr(1, 3), "PNG");
}

TEST(CaptureCommands, AnExplicitRawFormatIsAccepted) {
    // arrange a client that names the byte exact format rather than taking the default (§6.3)
    AutomationRegistry registry;
    register_capture_commands(registry, [] { return CapturedFrame{.width = 1, .height = 1, .pixels = {1, 2, 3, 4}}; });
    registry.allow("capture.screenshot");
    // act and assert the named form answers the same frame, because the default and the name agree (§6.3)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot", .arguments = {"raw"}});
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload.size(), 24U + 4U);
}

TEST(CaptureCommands, AFormatThisBuildDoesNotCarryIsRefused) {
    // arrange a client asking for a format the layer has no encoder for, beside a source that counts its reads (§6.3)
    std::size_t reads = 0;
    AutomationRegistry registry;
    register_capture_commands(registry, [&reads] {
        reads++;
        return CapturedFrame{.width = 1, .height = 1, .pixels = {1, 2, 3, 4}};
    });
    registry.allow("capture.screenshot");
    // act and assert the request is refused rather than answered with bytes of another format (§6.3)
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "capture.screenshot", .arguments = {"tiff"}});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.payload.empty());
    EXPECT_EQ(result.error, "the capture format is not available: tiff");
    // assert the refusal is decided before the window is read, which a bad request must not pay for (§10.3)
    EXPECT_EQ(reads, 0U);
}
