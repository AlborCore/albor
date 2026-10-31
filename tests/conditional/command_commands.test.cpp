#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "automation/automation_session.h"
#include "automation/command_commands.h"
#include "commands/command_registry.h"
#include "commands/undo_stack.h"

using namespace albor;
using namespace albor::automation;

namespace
{
    // the host a command suite drives, which declares actions into a registry and exposes them for automation (§10.3)
    struct Fixture
    {
        // the shared action objects, which is what the command commands read and invoke (§18.1)
        CommandRegistry commands;

        // the registry the allow list governs, which is what a session dispatches through (§10.3, AD-10)
        AutomationRegistry registry;

        // the session, which authenticates the way a real client does (§10.3, AD-10)
        std::unique_ptr<AutomationSession> session;

        Fixture() {
            // the commands are registered over the live registry, which is what a host wires (§10.3, AD-20)
            register_command_commands(registry, [this] { return &commands; });
            // every command is permitted, because the allow list is tested separately below (§10.3, AD-10)
            for (const std::string& name : command_command_names())
                registry.allow(name);
            // the session is built, which mints the token this test presents (§10.3, AD-10)
            session = std::make_unique<AutomationSession>(registry);
        }

        // run one line as an authenticated client, which is how every assertion below reaches a command (§10.3)
        AutomationResult run(const std::string& line) {
            // the handshake happens once, because a command before it would be refused (§10.3, AD-10)
            (void)session->handle(encode_handshake(session->token()));
            // the line is sent, which is the whole of the client's part (§10.3)
            return session->handle(line);
        }
    };
} // namespace

TEST(CommandCommands, ADumpNamesEveryCommandWithItsTwoPublishedStates) {
    // arrange
    Fixture fixture;
    auto save_owned = fixture.commands.declare("file.save", "Save");
    Command& save = *save_owned;
    auto erase_owned = fixture.commands.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    save.set_enabled(false);
    erase.set_visible(false);
    // act
    const AutomationResult result = fixture.run("command.list");
    // assert a client sees both states as words, which is what it asserts enablement against (§18.2)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("file.save disabled visible Save"), std::string::npos);
    EXPECT_NE(result.payload.find("row.erase enabled hidden Delete Rows"), std::string::npos);
}

TEST(CommandCommands, AnUnnamedCommandIsDumpedButNotSelectable) {
    // arrange
    Fixture fixture;
    auto anonymous_owned = fixture.commands.declare("", "Anonymous");
    // act
    const AutomationResult dumped = fixture.run("command.list");
    const AutomationResult invoked = fixture.run("command.invoke Anonymous");
    // assert a command the author never named is visible to a reader but unreachable by a selector (AD-11)
    EXPECT_NE(dumped.payload.find("- enabled visible Anonymous"), std::string::npos);
    EXPECT_FALSE(invoked.ok);
}

TEST(CommandCommands, TheNamedHistoryCommandIsDumpedUnderItsAuthorId) {
    // arrange
    Fixture fixture;
    // the declaration joins the registry, which is what the dump below reads (§18.1, §18.4)
    const UndoCommand& undo = fixture.commands.declare_undo("edit.undo", "Undo");
    (void)undo;
    // act
    const AutomationResult result = fixture.run("command.list");
    // assert the history command joins the same dump under the id its author assigned (§18.1, §18.4)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("edit.undo disabled visible Undo"), std::string::npos);
}

TEST(CommandCommands, OneCommandReportsItsOwnState) {
    // arrange
    Fixture fixture;
    auto save_owned = fixture.commands.declare("file.save", "Save");
    Command& save = *save_owned;
    save.set_enabled(false);
    // act
    const AutomationResult result = fixture.run("command.get file.save");
    // assert the single command answer carries both published states and the label (§18.2)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload, "disabled visible Save");
}

TEST(CommandCommands, AnAbsentIdIsRefusedRatherThanGuessed) {
    // arrange
    Fixture fixture;
    auto absent_owned = fixture.commands.declare("file.save", "Save");
    // act and assert a selector that matches nothing is refused with the reason (AD-11)
    EXPECT_FALSE(fixture.run("command.get file").ok);
    EXPECT_FALSE(fixture.run("command.invoke file").ok);
    EXPECT_FALSE(fixture.run("command.get").ok);
}

TEST(CommandCommands, AnAllowedClientInvokesTheSameActionAControlRuns) {
    // arrange
    Fixture fixture;
    auto save_owned = fixture.commands.declare("file.save", "Save");
    Command& save = *save_owned;
    std::size_t runs = 0;
    InvocationSource route = InvocationSource::Pointer;
    save.set_action([&runs, &route](const CommandInvocation& invocation) {
        runs++;
        route = invocation.source;
    });
    // act
    const AutomationResult result = fixture.run("command.invoke file.save");
    // assert the client drove the one shared object rather than a parallel path (§18.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(runs, std::size_t{1});
    EXPECT_EQ(route, InvocationSource::Automation);
}

TEST(CommandCommands, ADisabledCommandIsRefusedRatherThanForced) {
    // arrange
    Fixture fixture;
    auto save_owned = fixture.commands.declare("file.save", "Save");
    Command& save = *save_owned;
    std::size_t runs = 0;
    save.set_action([&runs](const CommandInvocation&) { runs++; });
    save.set_enabled(false);
    // act
    const AutomationResult result = fixture.run("command.invoke file.save");
    // assert the client observes exactly the enablement a user would, with no test only path (AD-10, §18.3)
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(runs, std::size_t{0});
    EXPECT_NE(result.error.find("not permitted"), std::string::npos);
}

TEST(CommandCommands, AHiddenCommandIsRefusedRatherThanForced) {
    // arrange
    Fixture fixture;
    auto erase_owned = fixture.commands.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    erase.set_visible(false);
    // act
    const AutomationResult result = fixture.run("command.invoke row.erase");
    // assert a command that does not apply says so rather than running (§18.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("does not apply"), std::string::npos);
}

TEST(CommandCommands, ASelectionScopedInvocationReportsTheRangeItRanOver) {
    // arrange
    Fixture fixture;
    fixture.commands.set_selection_source([] { return SelectionSpan{.first = 2, .last = 7}; });
    auto erase_owned = fixture.commands.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    erase.set_selection_required(true);
    std::size_t runs = 0;
    erase.set_action([&runs](const CommandInvocation&) { runs++; });
    // act
    const AutomationResult result = fixture.run("command.invoke row.erase");
    // assert the answer carries the count, which is what a client asserts a selection against (§18.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload, "5");
    EXPECT_EQ(runs, std::size_t{1});
}

TEST(CommandCommands, ACommandThatTakesAnArgumentReceivesIt) {
    // arrange
    Fixture fixture;
    auto erase_owned = fixture.commands.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    std::string observed;
    erase.set_action([&observed](const CommandInvocation& invocation) { observed = std::get<std::string>(invocation.argument); });
    // act
    const AutomationResult result = fixture.run("command.invoke row.erase row-7");
    // assert the same command bound to many rows carries the identity the client named (§18.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(observed, "row-7");
}

TEST(CommandCommands, TheHistoryIsAnsweredThroughTheWire) {
    // arrange
    Fixture fixture;
    auto save_owned = fixture.commands.declare("file.save", "Save");
    Command& save = *save_owned;
    int undone = 0;
    save.set_action([](const CommandInvocation&) {});
    save.set_undo_entry(UndoEntry{.label = "Save", .undo = [&undone](const CommandInvocation&) { undone++; }, .redo = [](const CommandInvocation&) {}});
    // act
    const AutomationResult empty = fixture.run("command.can_undo");
    (void)fixture.run("command.invoke file.save");
    const AutomationResult filled = fixture.run("command.can_undo");
    const AutomationResult reversed = fixture.run("command.undo");
    // assert the client drives the one history the controls drive, and an empty direction refuses (AD-10, §18.4)
    EXPECT_EQ(empty.payload, "no");
    EXPECT_EQ(filled.payload, "yes");
    ASSERT_TRUE(reversed.ok);
    EXPECT_EQ(undone, 1);
}

TEST(CommandCommands, ReversingAnEmptyHistoryIsRefused) {
    // arrange
    Fixture fixture;
    // act and assert an empty direction is refused rather than reported as an empty success (§10.3)
    EXPECT_FALSE(fixture.run("command.undo").ok);
    EXPECT_FALSE(fixture.run("command.redo").ok);
}

TEST(CommandCommands, AnUnpermittedCommandNeverReachesTheApplication) {
    // arrange a host that registered the whole family but permits only part of it (§10.3, AD-10)
    CommandRegistry commands;
    auto save_owned = commands.declare("file.save", "Save");
    Command& save = *save_owned;
    std::size_t runs = 0;
    save.set_action([&runs](const CommandInvocation&) { runs++; });
    AutomationRegistry registry;
    register_command_commands(registry, [&commands] { return &commands; });
    // the invocation is the one command the allow list omits, which is a partial allow (§10.3, AD-10)
    for (const std::string& name : command_command_names()) {
        if (name == "command.invoke")
            continue;
        registry.allow(name);
    }
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("command.invoke file.save");
    // assert a registered command the allow list does not permit never reaches the action (AD-10)
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(runs, std::size_t{0});
}
