#include <cstddef>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "automation/automation_session.h"
#include "automation/semantic_commands.h"
#include "semantics/semantic_provider.h"

using namespace albor;
using namespace albor::automation;

namespace
{
    // a counter an action can move without capturing anything that dies, which is what a copied node needs (§10.1)
    using PressCount = std::shared_ptr<std::size_t>;

    // the button a scene publishes, which carries the id and the activate action (§10.1, AD-11)
    class ButtonNode final : public SemanticProvider
    {
    public:
        // the counter is shared, because the tree copies this node and the copy must not outlive its own object (§10.1)
        explicit ButtonNode(PressCount presses) :
            m_presses(std::move(presses)) {}

        SemanticNode semantic_node() const override {
            // the node is a button, which is what a press performs (§10.1)
            SemanticNode node{SemanticRole::Button};
            // the id is the only selector a client may use (§18.1, AD-11)
            node.set_explicit_id("save_button");
            // the name is the label, which is what a screen reader speaks (§10.1)
            node.set_name("Save");
            // the action moves the shared counter rather than capturing this, because the tree keeps a copy (§10.1)
            (void)node.add_action("activate", [presses = m_presses] { (*presses)++; });
            return node;
        }

        std::size_t semantic_child_count() const override { return 0; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t) const override { return nullptr; }

        // the counter this provider's action moves, which survives the copy the tree keeps (§10.1)
        PressCount m_presses;
    };

    // a node with no author id, which automation must not be able to select (AD-11, §18.1)
    class AnonymousNode final : public SemanticProvider
    {
    public:
        SemanticNode semantic_node() const override {
            // the node is a label, which carries no action and no id (§10.1)
            SemanticNode node{SemanticRole::Label};
            // the name is set but the id deliberately is not, because an unnamed node is unaddressable (AD-11)
            node.set_name("anonymous");
            return node;
        }

        std::size_t semantic_child_count() const override { return 0; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t) const override { return nullptr; }
    };

    // the viewport the scene publishes, which a client addresses by the id the author wrote (§10.1, AD-11)
    class ViewportNode final : public SemanticProvider
    {
    public:
        SemanticNode semantic_node() const override {
            // the role is the viewport, which is what a screen reader needs to offer scrolling (§10.1, AD-08)
            SemanticNode node{SemanticRole::ScrollView};
            // the id is the author written one, because that is the only selector automation offers (§18.1, AD-11)
            node.set_explicit_id("log_view");
            // the name is what a reader announces, so it carries the heading (§10.1)
            node.set_name("Log");
            return node;
        }

        std::size_t semantic_child_count() const override { return 0; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t) const override { return nullptr; }
    };

    // the provider a suite drives, which publishes a button and a label with author ids (§10.1, AD-11)
    class SceneProvider final : public SemanticProvider
    {
    public:
        // the counter is handed in, because the button it publishes must outlive its own provider (§10.1)
        explicit SceneProvider(PressCount shared) :
            presses(std::move(shared)) {}

        SemanticNode semantic_node() const override {
            // the root is a group, which is what a window publishes (§10.1)
            SemanticNode node{SemanticRole::Group};
            // the root carries an id too, because a client may target it (§18.1, AD-11)
            node.set_explicit_id("root");
            // the name is empty, because a window announces no label (§10.1)
            node.set_name("window");
            return node;
        }

        std::size_t semantic_child_count() const override { return 3; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // the first child is the identified button, which is what most commands target (§18.1, AD-11)
            if (index == 0)
                return std::make_unique<ButtonNode>(presses);
            // the second child is a node with no author id, which no selector may reach (AD-11)
            if (index == 1)
                return std::make_unique<AnonymousNode>();
            // the third child is the viewport, which a reader reaches by its own role (§10.1)
            if (index == 2)
                return std::make_unique<ViewportNode>();
            // an index past the end names no child (§10.1)
            return nullptr;
        }

    private:
        // the counter the published button's action moves, which must outlive the provider itself (§10.1)
        PressCount presses;
    };

    // the registry the commands are registered into, which a test drives through a session (§10.3)
    struct Fixture
    {
        // the scene every command reads, which a rebuild would replace (§10.3)
        SemanticTree tree;

        // the shared counter the published button's action moves, which the copy in the tree can still reach (§10.1)
        PressCount presses = std::make_shared<std::size_t>(0);

        // the registry under test, which is permitted wholesale because these tests are about the commands (§10.3, AD-10)
        AutomationRegistry registry;

        // the session, which authenticates the way a real client does (§10.3, AD-10)
        std::unique_ptr<AutomationSession> session;

        Fixture() {
            // the tree is built once, because a command reads a snapshot (§10.3)
            tree.build_from(SceneProvider(presses));
            // the commands are registered over the snapshot, which is what a host wires (§10.3, AD-20)
            register_semantic_commands(registry, [this] { return &tree; });
            // every command is permitted, because the allow list is tested in the automation suite (§10.3, AD-10)
            for (const std::string& name : semantic_command_names())
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

TEST(SemanticCommands, ATreeDumpNamesEveryNodeWithItsRoleAndId) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.tree");
    // assert the dump carries the root and both children, in depth first order (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("group root window"), std::string::npos);
    EXPECT_NE(result.payload.find("button save_button Save"), std::string::npos);
}

TEST(SemanticCommands, ANodeWithNoAuthorIdIsDumpedButNotAddressable) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.tree");
    // assert an unidentified node still reaches a screen reader, reported with a dash (§18.1, AD-11)
    EXPECT_NE(result.payload.find("label - anonymous"), std::string::npos);
}

TEST(SemanticCommands, FindingByAnAuthorIdAnswersTheRoleAndName) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.find save_button");
    // assert
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload, "button Save");
}

TEST(SemanticCommands, FindingAnAbsentIdIsRefusedWithTheSelector) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.find nothing_here");
    // assert the refusal names what failed, so a client can correct itself (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("nothing_here"), std::string::npos);
}

TEST(SemanticCommands, ANameIsNotASelector) {
    // arrange
    Fixture fixture;
    // act the accessible name is passed where an id belongs (§18.1, AD-11)
    const AutomationResult result = fixture.run("semantic.find Save");
    // assert the name does not resolve, because identity is the id and the name is presentation (§18.1, AD-11)
    EXPECT_FALSE(result.ok);
}

TEST(SemanticCommands, FindWithNoIdIsRefusedRatherThanScanning) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.find");
    // assert a lookup with no key would be a scan, which automation does not offer (§10.3, AD-11)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("needs an id"), std::string::npos);
}

TEST(SemanticCommands, ActionsAreListedByName) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.actions save_button");
    // assert a client reads the listing before it acts (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload, "activate");
}

TEST(SemanticCommands, InvokingAnActionRunsIt) {
    // arrange
    Fixture fixture;
    // act the command names the node and the action, which is the whole call (§10.3)
    const AutomationResult result = fixture.run("semantic.invoke save_button activate");
    // assert the command itself succeeded, which is what the client reads (§10.3)
    EXPECT_TRUE(result.ok);
    // assert the action actually ran, which is the only thing that makes an invocation mean anything (§10.3)
    EXPECT_EQ(*fixture.presses, 1U);
}

TEST(SemanticCommands, InvokingAnAbsentActionIsRefused) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.invoke save_button explode");
    // assert the refusal names the action, so a client knows which selector failed (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("explode"), std::string::npos);
}

TEST(SemanticCommands, InvokingWithNoActionIsRefusedRatherThanGuessed) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.invoke save_button");
    // assert a guess would press the wrong control (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("needs an action name"), std::string::npos);
}

TEST(SemanticCommands, FocusIsRecordedAndReadBack) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.set_focused save_button 1");
    // assert the change is answered, so a client knows it landed (§10.3)
    ASSERT_TRUE(result.ok);
    // assert the tree now reports the state, which is what assistive technology reads (§10.1)
    const SemanticNode* node = fixture.tree.at(1);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->focused());
}

TEST(SemanticCommands, FocusTakesOnlyZeroOrOne) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.set_focused save_button maybe");
    // assert anything else is a client mistake (§10.3)
    EXPECT_FALSE(result.ok);
}

TEST(SemanticCommands, ACommandIsRefusedWhenTheHostSuppliedNoSnapshot) {
    // arrange a registry whose host wired no snapshot at all (§10.3, AD-20)
    AutomationRegistry registry;
    register_semantic_commands(registry, nullptr);
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the command refuses rather than answering empty, which a client would read as an empty tree (§10.3)
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("no semantic snapshot"), std::string::npos);
}

TEST(SemanticCommands, EverySemanticCommandIsRefusedUntilItIsPermitted) {
    // arrange a registry that registered the commands but permitted none of them (§10.3, AD-10)
    AutomationRegistry registry;
    SemanticTree scratch;
    register_semantic_commands(registry, [&scratch] { return &scratch; });
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act and assert each command is refused, because registration is not consent (§10.3, AD-10)
    for (const std::string& name : semantic_command_names()) {
        // act
        const AutomationResult result = session.handle(name);
        // assert
        EXPECT_FALSE(result.ok) << name;
        EXPECT_NE(result.error.find("not allowed"), std::string::npos) << name;
    }
}

TEST(SemanticCommands, AViewportIsNamedByItsOwnRole) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.find log_view");
    // assert the answer names the viewport with the role a screen reader expects, not a generic group (§10.1, AD-08)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload, "scroll_view Log");
}

TEST(SemanticCommands, AViewportAppearsInTheTreeDumpWithItsRole) {
    // arrange
    Fixture fixture;
    // act
    const AutomationResult result = fixture.run("semantic.tree");
    // assert the dump reaches the viewport, because a reader walks the same tree (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("scroll_view log_view Log"), std::string::npos);
}

TEST(SemanticCommands, AnEditableNodeAppearsInTheDumpWithTheValueItPublishes) {
    // arrange a tree whose root is an editable field carrying text (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(0)->set_explicit_id("field");
    tree.mutable_at(0)->set_name("Notes");
    tree.mutable_at(0)->set_value("Albor");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the value reaches the client, because a text assertion must be possible without a tree walk (§10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("textfield field Notes value=Albor"), std::string::npos);
}

TEST(SemanticCommands, AnEditableNodeWithNoValueCarriesNoValueToken) {
    // arrange a tree whose root is an editable field that published nothing (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(0)->set_explicit_id("field");
    tree.mutable_at(0)->set_name("Notes");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert an emptied field announces no value rather than an empty one, because the token is the value (§10.1)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload.find("value="), std::string::npos);
    EXPECT_NE(result.payload.find("textfield field Notes"), std::string::npos);
}

TEST(SemanticCommands, TheEditableStatesAppearInTheDumpWhenTheyHold) {
    // arrange a tree whose field is read only and spans lines (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(0)->set_explicit_id("field");
    tree.mutable_at(0)->set_name("Notes");
    tree.mutable_at(0)->set_read_only(true);
    tree.mutable_at(0)->set_multiline(true);
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert both states reach the client, because the states are what a client must respect (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find(" read_only"), std::string::npos);
    EXPECT_NE(result.payload.find(" multiline"), std::string::npos);
}

TEST(SemanticCommands, TheEditableStatesCarryNoTokenWhenTheyDoNotHold) {
    // arrange a tree whose field is a plain editable single line one (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(0)->set_explicit_id("field");
    tree.mutable_at(0)->set_name("Notes");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the default states are silent, because a dump without them stays parseable as before (§10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload.find(" read_only"), std::string::npos);
    EXPECT_EQ(result.payload.find(" multiline"), std::string::npos);
}

TEST(SemanticCommands, AMultiLineValueDoesNotSplitTheDumpIntoMoreLines) {
    // arrange a multi line field whose value carries the breaks that make the tree multi line (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(0)->set_explicit_id("notes");
    tree.mutable_at(0)->set_name("Notes");
    tree.mutable_at(0)->set_value("first\nsecond");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the payload holds one line per node, which is what a client splits on (§10.3)
    ASSERT_TRUE(result.ok);
    const std::size_t lines = static_cast<std::size_t>(std::count(result.payload.begin(), result.payload.end(), '\n'));
    EXPECT_EQ(lines, 1u);
    // assert the break is spelled as an escape, so a client reads the value back whole (§10.3)
    EXPECT_NE(result.payload.find("value=first\\nsecond"), std::string::npos);
}

TEST(SemanticCommands, AMultiByteValueSurvivesTheDumpUnchanged) {
    // arrange a field whose value is not ascii, which an escape must leave byte identical (§10.3)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(0)->set_explicit_id("notes");
    tree.mutable_at(0)->set_name("Notes");
    tree.mutable_at(0)->set_value("été");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the two byte characters passed through, because an escape is about control characters only (§10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("value=été"), std::string::npos);
}

TEST(SemanticCommands, ANameCarryingALineBreakDoesNotSplitTheDumpIntoMoreLines) {
    // arrange a node whose accessible name carries a break, which the dump must escape as well (§10.1, §10.3)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(0)->set_explicit_id("caption");
    tree.mutable_at(0)->set_name("two\nlines");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the payload is still one line, because a name is text rather than a protocol delimiter (§10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(std::count(result.payload.begin(), result.payload.end(), '\n'), 1u);
    EXPECT_NE(result.payload.find("two\\nlines"), std::string::npos);
}

TEST(SemanticCommands, AListAppearsInTheDumpWithItsOwnRole) {
    // arrange a tree whose root is a list, which is not a generic group (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::List});
    tree.mutable_at(0)->set_explicit_id("inbox");
    tree.mutable_at(0)->set_name("Inbox");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the dump names the list with the role a reader navigates rows in, not a generic group (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find("list inbox Inbox"), std::string::npos);
}

TEST(SemanticCommands, FindingAListAnswersTheListRole) {
    // arrange a tree whose root is an addressed list (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::List});
    tree.mutable_at(0)->set_explicit_id("inbox");
    tree.mutable_at(0)->set_name("Inbox");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.find inbox");
    // assert the answer names the list role, so a client targets the container it expects (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload, "list Inbox");
}

TEST(SemanticCommands, AFocusedNodeSaysSoInTheDump) {
    // arrange a tree whose list node holds the focus (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::List});
    tree.mutable_at(0)->set_explicit_id("inbox");
    tree.mutable_at(0)->set_name("Inbox");
    tree.mutable_at(0)->set_focused(true);
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the focused state reaches the client, because a client reads the focus back (§10.1, §10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find(" focused"), std::string::npos);
}

TEST(SemanticCommands, ADisabledSelectedNodeSaysSoInTheDump) {
    // arrange a tree whose row is disabled and selected, which is what a list client reads back (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::ListItem});
    tree.mutable_at(0)->set_explicit_id("row");
    tree.mutable_at(0)->set_name("Row");
    tree.mutable_at(0)->set_disabled(true);
    tree.mutable_at(0)->set_selected(true);
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert both states reach the client, because a client must read what it may drive (§10.1)
    ASSERT_TRUE(result.ok);
    EXPECT_NE(result.payload.find(" disabled"), std::string::npos);
    EXPECT_NE(result.payload.find(" selected"), std::string::npos);
}

TEST(SemanticCommands, TheSelectionStatesCarryNoTokenWhenTheyDoNotHold) {
    // arrange a tree whose node is neither disabled, selected nor focused (§10.1)
    AutomationRegistry registry;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::ListItem});
    tree.mutable_at(0)->set_explicit_id("row");
    tree.mutable_at(0)->set_name("Row");
    register_semantic_commands(registry, [&tree] { return &tree; });
    // every command is permitted, because these tests are about the answering, not the allow list (§10.3, AD-10)
    for (const std::string& name : semantic_command_names())
        registry.allow(name);
    AutomationSession session(registry);
    (void)session.handle(encode_handshake(session.token()));
    // act
    const AutomationResult result = session.handle("semantic.tree");
    // assert the default states are silent, so a dump without them stays parseable as before (§10.3)
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.payload.find(" disabled"), std::string::npos);
    EXPECT_EQ(result.payload.find(" selected"), std::string::npos);
    EXPECT_EQ(result.payload.find(" focused"), std::string::npos);
}
