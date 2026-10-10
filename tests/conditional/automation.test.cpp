#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include "automation/automation_protocol.h"
#include "automation/automation_registry.h"
#include "automation/automation_session.h"

using namespace albor::automation;

namespace
{
    // a registry holding one command that records that it ran, which is how a test proves it did (§10.3)
    AutomationRegistry registry_with_probe(std::size_t* runs) {
        // the registry is local because the session holds a reference to it (§10.3)
        static AutomationRegistry registry;
        // the command is registered but not permitted, because a shipped command is not reachable (§10.3, AD-10)
        registry.register_command("probe", [runs](const AutomationRequest&) {
            // the run is counted, which is the only effect a test needs to observe (§10.3)
            (*runs)++;
            // the answer is short, which is what the wire encodes (§10.3)
            return AutomationResult::answered("ok");
        });
        // the registry is handed back so a test can permit or not (§10.3, AD-10)
        return registry;
    }
} // namespace

TEST(AutomationProtocol, ARequestDecodesIntoItsCommandAndArguments) {
    // act
    const AutomationRequest request = decode_request("probe alpha beta");
    // assert the command is the first field and the arguments keep their order (§10.3)
    EXPECT_EQ(request.command, "probe");
    ASSERT_EQ(request.arguments.size(), 2U);
    EXPECT_EQ(request.arguments.at(0), "alpha");
    EXPECT_EQ(request.arguments.at(1), "beta");
}

TEST(AutomationProtocol, ALineWithNoArgumentsDecodesToACommandAlone) {
    // act
    const AutomationRequest request = decode_request("probe");
    // assert
    EXPECT_EQ(request.command, "probe");
    EXPECT_TRUE(request.arguments.empty());
}

TEST(AutomationProtocol, AnAnswerAndARefusalEncodeDifferently) {
    // assert the prefixes differ, because a client must never read a refusal as an answer (§10.3, AD-10)
    EXPECT_EQ(encode_result(AutomationResult::answered("yes")), "!ok yes");
    EXPECT_EQ(encode_result(AutomationResult::refused("no")), "!refused no");
}

TEST(AutomationRegistry, ARegisteredCommandIsNotYetPermitted) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    // assert registration is not permission, because silence is not consent (§10.3, AD-10)
    EXPECT_TRUE(registry.knows("probe"));
    EXPECT_FALSE(registry.allows("probe"));
}

TEST(AutomationRegistry, ACommandThatIsNotPermittedNeverRuns) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    // act
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "probe"});
    // assert the refusal names the command, which is what a client reads to learn why (§10.3, AD-10)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("not allowed"), std::string::npos);
    // assert the handler never ran, which is the whole point of the allow list (§10.3, AD-10)
    EXPECT_EQ(runs, 0U);
}

TEST(AutomationRegistry, APermittedCommandRuns) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    registry.allow("probe");
    // act
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "probe"});
    // assert
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(runs, 1U);
}

TEST(AutomationRegistry, AnUnknownCommandIsRefusedRatherThanRun) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    registry.allow("probe");
    // act
    const AutomationResult result = registry.dispatch(AutomationRequest{.command = "absent"});
    // assert
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("no such command"), std::string::npos);
}

TEST(AutomationRegistry, TheAllowedListReadsBackInAStableOrder) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    registry.allow("probe");
    registry.allow("another");
    // act
    const std::vector<std::string> allowed = registry.allowed_commands();
    // assert the order is the map's, so a client sees a stable reply (§10.3, AD-10)
    ASSERT_EQ(allowed.size(), 2U);
    EXPECT_EQ(allowed.at(0), "another");
    EXPECT_EQ(allowed.at(1), "probe");
}

TEST(AutomationSession, TwoSessionsHoldDifferentTokens) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    // act
    AutomationSession first(registry);
    AutomationSession second(registry);
    // assert the token is per session, because a shared token would authenticate any client (§10.3, AD-10)
    EXPECT_NE(first.token(), second.token());
}

TEST(AutomationSession, ATokenCarriesNoSeparator) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    AutomationSession session(registry);
    // act
    const std::string& token = session.token();
    // assert a separator would let the token be smuggled as two fields (§10.3, AD-10)
    EXPECT_EQ(token.find(' '), std::string::npos);
}

TEST(AutomationSession, ACommandBeforeTheHandshakeIsRefused) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    registry.allow("probe");
    AutomationSession session(registry);
    // act
    const AutomationResult result = session.handle("probe");
    // assert the conversation proved nothing, so no command runs (§10.3, AD-10)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("authentication required"), std::string::npos);
    EXPECT_EQ(runs, 0U);
}

TEST(AutomationSession, TheCorrectTokenAuthenticates) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    AutomationSession session(registry);
    // act
    const AutomationResult result = session.handle(encode_handshake(session.token()));
    // assert
    EXPECT_TRUE(session.authenticated());
    EXPECT_TRUE(result.ok);
}

TEST(AutomationSession, AWrongTokenIsRefusedWithoutDisclosingTheExpectedOne) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    AutomationSession session(registry);
    // act
    const AutomationResult result = session.handle(encode_handshake("not-the-token"));
    // assert the conversation stays closed (§10.3, AD-10)
    EXPECT_FALSE(session.authenticated());
    EXPECT_FALSE(result.ok);
    // assert the refusal never echoes what was expected, which would hand it to whoever asked (§10.3, AD-10)
    EXPECT_EQ(result.error.find(session.token()), std::string::npos);
}

TEST(AutomationSession, ATokensWrongLengthIsRefused) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    AutomationSession session(registry);
    // act
    const AutomationResult result = session.handle(encode_handshake("short"));
    // assert a length mismatch must cost the same comparison as a wrong byte (§10.3, AD-10)
    EXPECT_FALSE(session.authenticated());
    EXPECT_FALSE(result.ok);
}

TEST(AutomationSession, AnAuthenticatedSessionRunsAPermittedCommand) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    registry.allow("probe");
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("probe");
    // assert the whole path works, which is what the reference automation test exercises (§16)
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(runs, 1U);
}

TEST(AutomationSession, AnAuthenticatedSessionStillRefusesAnUnpermittedCommand) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act the command is registered but never permitted (§10.3, AD-10)
    const AutomationResult result = session.handle("probe");
    // assert authentication is not permission, which is the boundary AD-10 draws (§10.3, AD-10)
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(runs, 0U);
}

TEST(AutomationSession, AHandshakeCannotBeReplayedToChangeAClosedSession) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    AutomationSession session(registry);
    // act a second handshake follows a successful one (§10.3, AD-10)
    (void)session.handle(encode_handshake(session.token()));
    (void)session.handle(encode_handshake("wrong-after-auth"));
    // assert the session stays authenticated, because a later frame cannot unseat an earlier proof (§10.3, AD-10)
    EXPECT_TRUE(session.authenticated());
}
