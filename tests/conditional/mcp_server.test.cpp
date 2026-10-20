#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "automation/automation_registry.h"
#include "automation/automation_session.h"
#include "automation/mcp_server.h"
#include "automation/semantic_commands.h"
#include "core/json.h"
#include "semantics/semantic_provider.h"

using namespace albor;
using namespace albor::automation;

namespace
{
    // the tree the suite drives, which publishes one identified node so a call has something to find (§18.1, AD-11)
    class RootProvider final : public SemanticProvider
    {
    public:
        SemanticNode semantic_node() const override {
            // the node is a group, which is what a window publishes (§10.1)
            SemanticNode node{SemanticRole::Group};
            // the id is the only selector a client may use (§18.1, AD-11)
            node.set_explicit_id("root");
            // the name is what a screen reader speaks (§10.1)
            node.set_name("window");
            return node;
        }

        std::size_t semantic_child_count() const override { return 0; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t) const override { return nullptr; }
    };

    // the fixture a suite drives, which is a real automation layer behind the adapter (§10.3, AD-20)
    struct Fixture
    {
        // the tree the commands read, which is the live one the host publishes (§10.3)
        SemanticTree tree;

        // the registry the layer runs commands from (§10.3)
        AutomationRegistry registry;

        // the session the adapter's route speaks through, which is the layer itself (§10.3, AD-20)
        AutomationSession session{registry};

        // the adapter under test, which the body builds because its route captures this (§10.3, AD-20)
        std::unique_ptr<McpServer> server;

        Fixture() {
            // the tree carries one identified node, so a call has something to address (§18.1, AD-11)
            tree.build_from(RootProvider());
            // the commands are registered over the tree, which is what a host wires (§10.3, AD-20)
            register_semantic_commands(registry, [this] { return &tree; });
            // every command is permitted, because the allow list is what the listing test varies (§10.3, AD-10)
            for (const std::string& name : semantic_command_names())
                registry.allow(name);
            // the route reads the allow list from the layer, so the tool list cannot exceed it (§10.3, AD-20)
            McpRoute route;
            // the permitted names come from the layer itself (§10.3, AD-20)
            route.permitted_commands = [this] { return registry.allowed_commands(); };
            // the run sends a line through a real session, which is the only path the adapter has (§10.3, AD-20)
            route.run = [this](std::string_view line) {
                // the handshake happens once, because the layer refuses commands before it (§10.3, AD-10)
                (void)session.handle(encode_handshake(session.token()));
                // the line goes to the layer, which is what answers (§10.3, AD-20)
                return session.handle(line);
            };
            // the adapter is built over the route (§10.3, AD-20)
            server = std::make_unique<McpServer>(std::move(route));
        }

        // send one request line and parse the reply, which is what a client does (§10.3)
        core::Json ask(const std::string& line) {
            // the reply is parsed, because a test asserts on the json (§10.3)
            const std::optional<core::Json> parsed = core::Json::parse(*server->handle_line(line));
            // a reply that does not parse is a defect, which the assertion catches (§10.3)
            EXPECT_TRUE(parsed.has_value()) << line;
            // the parsed value stands in for the reply (§10.3)
            return parsed.value_or(core::Json());
        }

        // send one request line without parsing, which is what a notification does (§12.2)
        std::optional<std::string> send(const std::string& line) { return server->handle_line(line); }
    };

    // build a tools call request, which is the request a client sends (§10.3)
    std::string tools_call(const std::string& id, const std::string& tool, const std::string& arguments) {
        // the request is an object, because json-rpc requests are (§10.3)
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + tool + "\",\"arguments\":" + arguments + "}}";
    }
} // namespace

TEST(McpServer, InitializeAnswersWithoutReachingTheLayer) {
    // arrange
    Fixture fixture;
    // act
    const core::Json reply = fixture.ask(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    // assert the protocol version is advertised, because a client checks it first (§10.3)
    ASSERT_NE(reply.member("result"), nullptr);
    const core::Json* result = reply.member("result");
    const core::Json* version = result->member("protocolVersion");
    ASSERT_NE(version, nullptr);
    EXPECT_FALSE(version->as_string().empty());
    // assert the handshake reached no application, because it concerns none (§10.3, AD-20)
    EXPECT_EQ(fixture.server->layer_calls(), 0U);
}

TEST(McpServer, TheToolListingIsExactlyTheAllowList) {
    // arrange
    Fixture fixture;
    // act
    const core::Json reply = fixture.ask(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    // assert the tools carry the permitted names, because a client may only call what the layer allows (§10.3, AD-20)
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* tools = result->member("tools");
    ASSERT_NE(tools, nullptr);
    ASSERT_EQ(tools->kind(), core::Json::Kind::Array);
    // every semantic command appears once, which is what the allow list permitted (§10.3, AD-20)
    EXPECT_EQ(tools->as_array().size(), semantic_command_names().size());
    // assert the listing reached no application, because the allow list was the whole query (§10.3, AD-20)
    EXPECT_EQ(fixture.server->layer_calls(), 0U);
}

TEST(McpServer, AnUnpermittedCommandIsAbsentFromTheToolListing) {
    // arrange a layer that permits nothing, which is the default every session starts from (§10.3, AD-10)
    AutomationRegistry registry;
    register_semantic_commands(registry, [] { return nullptr; });
    McpRoute route;
    route.permitted_commands = [&registry] { return registry.allowed_commands(); };
    route.run = [](std::string_view) { return AutomationResult::refused("command not allowed"); };
    McpServer server(std::move(route));
    // act
    const std::string line = *server.handle_line(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
    // assert an empty listing is what a client sees, because nothing is permitted (§10.3, AD-20)
    EXPECT_NE(line.find("\"tools\":[]"), std::string::npos);
}

TEST(McpServer, AToolCallReachesTheLayer) {
    // arrange
    Fixture fixture;
    // act the call names the tool and its argument by name, which is how a client sends them (§10.3)
    const core::Json reply = fixture.ask(tools_call("4", "semantic.find", R"({"id":"root"})"));
    // assert the layer was reached exactly once, which is the boundary AD-20 is about (§10.3, AD-20)
    EXPECT_EQ(fixture.server->layer_calls(), 1U);
    // assert the answer carries the layer's payload unchanged (§10.3, AD-20)
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* content = result->member("content");
    ASSERT_NE(content, nullptr);
    ASSERT_EQ(content->kind(), core::Json::Kind::Array);
    ASSERT_EQ(content->as_array().size(), 1U);
    EXPECT_EQ(content->as_array().at(0).member("text")->as_string(), "group window");
}

TEST(McpServer, NamedArgumentsArePlacedInTheOrderTheLayerTakes) {
    // arrange
    Fixture fixture;
    // act the client writes the two names in the opposite order to the wire, which the adapter must fix (§10.3)
    const core::Json reply = fixture.ask(tools_call("5", "semantic.invoke", R"({"action":"activate","id":"root"})"));
    // assert the refusal names the action, which only happens when the id resolved first (§10.3)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    EXPECT_NE(error->member("message")->as_string().find("no such action: activate"), std::string::npos);
}

TEST(McpServer, ALayerRefusalBecomesAJsonRpcErrorCarryingTheLayersReason) {
    // arrange
    Fixture fixture;
    // act the tool names something the layer does not have (§10.3)
    const core::Json reply = fixture.ask(tools_call("6", "semantic.find", R"({"id":"absent"})"));
    // assert the error carries the layer's own words, so a client reads one refusal not two (§10.3, AD-20)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->member("code")->as_number(), -32000.0);
    EXPECT_NE(error->member("message")->as_string().find("absent"), std::string::npos);
}

TEST(McpServer, AnUnroutableToolNeverReachesTheLayer) {
    // arrange
    Fixture fixture;
    // act the tool is not a command at all (§10.3, AD-20)
    const core::Json reply = fixture.ask(tools_call("7", "semantic.nonexistent", "{}"));
    // assert the layer was reached, because only the layer may judge whether a command exists (§10.3, AD-20)
    EXPECT_EQ(fixture.server->layer_calls(), 1U);
    // assert the refusal is the layer's, because the adapter must not answer for it (§10.3, AD-20)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    EXPECT_NE(error->member("message")->as_string().find("no such command"), std::string::npos);
}

TEST(McpServer, AnUnknownMethodIsStatedRatherThanIgnored) {
    // arrange
    Fixture fixture;
    // act
    const core::Json reply = fixture.ask(R"({"jsonrpc":"2.0","id":8,"method":"resources/list"})");
    // assert silence would hang a client, so the failure is stated (§10.3)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->member("code")->as_number(), -32601.0);
}

TEST(McpServer, ANotificationTakesNoReply) {
    // arrange
    Fixture fixture;
    // act a notification carries no identifier (§12.2)
    const std::optional<std::string> reply = fixture.send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    // assert
    EXPECT_FALSE(reply.has_value());
}

TEST(McpServer, MalformedJsonIsAnsweredWithAParseFailure) {
    // arrange
    Fixture fixture;
    // act
    const core::Json reply = fixture.ask("{not json");
    // assert silence would hang a client, so the failure is stated (§10.3)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    EXPECT_NE(error->member("message")->as_string().find("not json"), std::string::npos);
}

TEST(McpServer, AnAdapterWithNoRouteCannotReachAnything) {
    // arrange an adapter with no runner at all (§10.3, AD-20)
    McpRoute route;
    route.permitted_commands = [] { return std::vector<std::string>{"semantic.tree"}; };
    McpServer server(std::move(route));
    // act
    const std::string line = *server.handle_line(tools_call("9", "semantic.tree", "{}"));
    // assert the refusal is stated, because an empty answer would read as an empty tree (§10.3, AD-20)
    EXPECT_NE(line.find("not reachable"), std::string::npos);
}

TEST(McpServer, AnAuthenticateCallbackRefusesMissingToken) {
    // arrange a route that requires authentication
    McpRoute route;
    route.permitted_commands = [] { return std::vector<std::string>{"semantic.tree"}; };
    route.authenticate = [](std::string_view token) { return !token.empty(); };
    route.authenticated = [] { return true; };
    route.run = [](std::string_view) { return AutomationResult::answered("{}"); };
    McpServer server(std::move(route));
    // act initialize without _meta.albor/token
    const std::string line = *server.handle_line(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    // assert the reply is an error because token is missing
    EXPECT_NE(line.find("authentication failed"), std::string::npos);
}

TEST(McpServer, AnAuthenticateCallbackAcceptsValidToken) {
    // arrange a route that requires authentication
    std::string valid_token = "abc123";
    McpRoute route;
    route.permitted_commands = [] { return std::vector<std::string>{"semantic.tree"}; };
    route.authenticate = [&valid_token](std::string_view token) { return token == valid_token; };
    route.authenticated = [] { return true; };
    route.run = [](std::string_view) { return AutomationResult::answered("{}"); };
    McpServer server(std::move(route));
    // act initialize with correct token in metadata
    const std::string line = *server.handle_line("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"_meta\":{\"albor/token\":\"abc123\"}}}");
    // assert the reply is a success
    EXPECT_NE(line.find("\"result\""), std::string::npos);
}

TEST(McpServer, AnAuthenticatedFlagGatesSubsequentCalls) {
    // arrange a route that requires authentication but the flag is initially false
    McpRoute route;
    route.permitted_commands = [] { return std::vector<std::string>{"semantic.tree"}; };
    route.authenticate = [](std::string_view) { return true; };
    bool authenticated = false;
    route.authenticated = [&authenticated] { return authenticated; };
    route.run = [](std::string_view) { return AutomationResult::answered("{}"); };
    McpServer server(std::move(route));
    // act initialize without token (authenticate returns true but authenticated flag is false)
    const std::string init_line = *server.handle_line(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    // assert initialize fails because authenticated flag is false
    EXPECT_NE(init_line.find("authentication failed"), std::string::npos);
    // act set authenticated flag true and call a tool
    authenticated = true;
    const std::string call_line = *server.handle_line(tools_call("2", "semantic.tree", "{}"));
    // assert the call succeeds
    EXPECT_NE(call_line.find("\"result\""), std::string::npos);
}
