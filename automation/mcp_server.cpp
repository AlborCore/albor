#include "automation/mcp_server.h"

#include <optional>

#include "core/json.h"

namespace albor::automation
{
    namespace
    {
        // the protocol version this adapter answers with, which a client checks before anything else (§10.3)
        constexpr std::string_view PROTOCOL_VERSION = "2024-11-05";

        // the error code for a request this adapter cannot route, which json-rpc numbers (§10.3)
        constexpr int METHOD_NOT_FOUND = -32601;

        // the error code for a request whose parameters do not make sense, which json-rpc numbers (§10.3)
        constexpr int INVALID_PARAMS = -32602;

        // the error code for a call the automation layer refused, which json-rpc numbers (§10.3)
        constexpr int APPLICATION_ERROR = -32000;

        // build the reply envelope for an identifier, which every response carries (§10.3)
        core::Json envelope(const core::Json& id, core::Json result) {
            // the reply is an object, because json-rpc replies are (§10.3)
            core::Json reply;
            // the version is fixed, because that is what identifies the protocol (§10.3)
            reply.set("jsonrpc", core::Json(std::string("2.0")));
            // the identifier echoes the request, which is how a client matches the pair (§10.3)
            reply.set("id", id);
            // the result is what the call produced (§10.3)
            reply.set("result", std::move(result));
            // the reply is returned whole (§10.3)
            return reply;
        }

        // build the reply envelope for a failure, which carries no result (§10.3)
        core::Json failure(const core::Json& id, int code, std::string message) {
            // the reply is an object, because json-rpc replies are (§10.3)
            core::Json reply;
            // the version is fixed, because that is what identifies the protocol (§10.3)
            reply.set("jsonrpc", core::Json(std::string("2.0")));
            // the identifier echoes the request, which is how a client matches the pair (§10.3)
            reply.set("id", id);
            // the error carries the code a client branches on (§10.3)
            core::Json error;
            // the code is the json-rpc number, which is what a client matches (§10.3)
            error.set("code", core::Json(static_cast<double>(code)));
            // the message is the stated reason, which is what a human reads (§10.3)
            error.set("message", core::Json(std::move(message)));
            // the error object joins the reply, which is where json-rpc puts it (§10.3)
            reply.set("error", std::move(error));
            // the reply is returned whole (§10.3)
            return reply;
        }

        // the parameter names one command takes, in the order the layer wire carries them (§10.3)
        std::vector<std::string> parameter_names(const std::string& command) {
            // a client names its arguments while the wire is positional, so the order comes from here (§10.3)
            if (command == "semantic.find")
                return {"id"};
            // an invocation names both the node and the action it runs (§10.3)
            if (command == "semantic.actions")
                return {"id"};
            // an invocation names the node, the action, and nothing else (§10.3)
            if (command == "semantic.invoke")
                return {"id", "action"};
            // a state change names the node and the value it takes (§10.3)
            if (command == "semantic.set_focused")
                return {"id", "value"};
            // a dump takes nothing, because it reports the whole tree (§10.3)
            if (command == "semantic.tree")
                return {};
            // a lookup names the command it reads (§10.3, §18.1)
            if (command == "command.get")
                return {"id"};
            // an invocation names the command and its optional argument (§10.3, §18.3)
            if (command == "command.invoke")
                return {"id", "argument"};
            // a command this adapter does not know takes nothing by name (§10.3)
            return {};
        }

        // extract token from initialize metadata (§10.3, AD-10)
        std::optional<std::string> presented_token(const core::Json* params) {
            // a request with no parameters carries no metadata, so it presents no token (§10.3)
            if (params == nullptr || !params->is_object())
                return std::nullopt;
            // the metadata is an object, because that is where MCP carries extension values (§10.3)
            const core::Json* meta = params->member("_meta");
            // a request without metadata presents no token (§10.3, AD-10)
            if (meta == nullptr || !meta->is_object())
                return std::nullopt;
            // the token is named by the albor key, which no other protocol member occupies (§10.3)
            const core::Json* token = meta->member("albor/token");
            // a token that is absent or not a string cannot authenticate (§10.3, AD-10)
            if (token == nullptr || token->kind() != core::Json::Kind::String)
                return std::nullopt;
            // the token is copied, because the request outlives this call only until the route returns (§10.3)
            return token->as_string();
        }

        // place a named argument object into the positional order the layer wire needs (§10.3)
        std::optional<std::vector<std::string>> place_arguments(const std::string& command, const core::Json& given) {
            // an array is already positional, so it is taken as written (§10.3)
            if (given.kind() == core::Json::Kind::Array) {
                // the elements are collected, which is the whole of the positional form (§10.3)
                std::vector<std::string> arguments;
                // each element must be a string, because the layer wire carries strings (§10.3)
                for (const core::Json& element : given.as_array()) {
                    // a non string element is refused, because the layer would misread it (§10.3)
                    if (element.kind() != core::Json::Kind::String)
                        return std::nullopt;
                    // the element joins the arguments, in the order it was written (§10.3)
                    arguments.push_back(element.as_string());
                }
                // the positional form is returned whole (§10.3)
                return arguments;
            }
            // only an object may be placed, because anything else names nothing (§10.3)
            if (given.kind() != core::Json::Kind::Object)
                return std::nullopt;
            // the order comes from the table, because a map has no order of its own (§10.3)
            const std::vector<std::string> order = parameter_names(command);
            // the arguments are placed by that order, which is what makes a named call match the wire (§10.3)
            std::vector<std::string> arguments;
            // each parameter is looked up by name, which is what a client wrote (§10.3)
            for (const std::string& parameter : order) {
                // the member is read, because a client names what it passed (§10.3)
                const core::Json* value = given.member(parameter);
                // a member that is absent is left empty, which the layer refuses as a missing argument (§10.3)
                if (value == nullptr)
                    continue;
                // only a string is sent, because the layer wire carries strings (§10.3)
                if (value->kind() != core::Json::Kind::String)
                    return std::nullopt;
                // the value joins the positional list (§10.3)
                arguments.push_back(value->as_string());
            }
            // the placed arguments are returned whole (§10.3)
            return arguments;
        }

        // render an automation command line, which is the wire format the layer speaks (§10.3)
        std::string command_line(const std::string& name, const std::vector<std::string>& arguments) {
            // the line starts with the command name, because that is what the session dispatches on (§10.3)
            std::string line = name;
            // each argument follows, separated, because that is the wire (§10.3)
            for (const std::string& argument : arguments) {
                // the separator is a space, which the protocol decodes back (§10.3)
                line.push_back(' ');
                // the argument joins the line, in the order the client wrote them (§10.3)
                line += argument;
            }
            // the line is returned whole, which is what the layer decodes (§10.3)
            return line;
        }
    } // namespace

    std::string_view mcp_protocol_version() {
        // the revision is returned from one place, because discovery and the handshake must not drift (§10.3)
        return PROTOCOL_VERSION;
    }

    McpServer::McpServer(McpRoute route) :
        m_route(std::move(route)) {}

    std::optional<std::string> McpServer::handle_line(std::string_view line) {
        // the line is parsed, because a request is a json document (§10.3)
        const std::optional<core::Json> parsed = core::Json::parse(line);
        // a line that is not json is answered with a parse error, because silence would hang the client (§10.3)
        if (!parsed)
            return failure(core::Json(), INVALID_PARAMS, "the request is not json").to_string();
        // the request is an object, because json-rpc requests are (§10.3)
        if (!parsed->is_object())
            return failure(core::Json(), INVALID_PARAMS, "the request is not an object").to_string();
        // the identifier is read, because every reply echoes it (§10.3)
        const core::Json* id = parsed->member("id");
        // a request with no identifier is a notification, which takes no reply (§12.2)
        if (id == nullptr)
            return std::nullopt;
        // a notification that carries an identifier of null is still a notification by json-rpc (§12.2)
        if (id->kind() == core::Json::Kind::Null)
            return std::nullopt;
        // the method is read, because it is what this adapter routes (§10.3)
        const core::Json* method = parsed->member("method");
        // a request with no method cannot be routed, which is a stated failure (§10.3)
        if (method == nullptr || method->kind() != core::Json::Kind::String)
            return failure(*id, INVALID_PARAMS, "the request carries no method").to_string();
        // the parameters are read when present, because every call but initialize carries them (§12.2)
        const core::Json* params = parsed->member("params");

        // the initialize handshake is answered from the protocol itself, because it reaches no application (§12.2)
        if (method->as_string() == "initialize") {
            // auth requires host token from metadata (§10.3, AD-10)
            if (m_route.authenticate) {
                // the token is extracted before the session sees it, because the comparison belongs to the session (§10.3)
                const std::optional<std::string> token = presented_token(params);
                // a missing, malformed, or wrong token is refused with one generic reason (§10.3, AD-10)
                if (!token || !m_route.authenticate(*token))
                    return failure(*id, APPLICATION_ERROR, "authentication failed").to_string();
            }
            // the result advertises the protocol and this adapter's one capability (§10.3)
            core::Json result;
            // the version is the one this adapter speaks (§10.3)
            result.set("protocolVersion", core::Json(std::string(PROTOCOL_VERSION)));
            // the capabilities advertise tools only, which is all this adapter routes (§10.3, AD-20)
            core::Json capabilities;
            // the tools capability is what a client looks for (§10.3)
            core::Json tools;
            // the list changed notification is not sent, because this adapter has no server to notify (§10.3)
            tools.set("listChanged", core::Json(false));
            // the tools capability joins the capabilities (§10.3)
            capabilities.set("tools", std::move(tools));
            // the capabilities join the result (§10.3)
            result.set("capabilities", std::move(capabilities));
            // the server names itself, which is what a client shows (§10.3)
            result.set("serverInfo", core::Json(std::map<std::string, core::Json, std::less<>>{{"name", core::Json(std::string("albor"))}, {"version", core::Json(std::string("1"))}}));
            // the handshake answers without reaching the layer, because it concerns no application (§12.2, AD-20)
            return envelope(*id, std::move(result)).to_string();
        }

        // every request after the handshake requires the conversation to have proved its token (§10.3, AD-10)
        if (m_route.authenticated && !m_route.authenticated())
            return failure(*id, APPLICATION_ERROR, "authentication failed").to_string();

        // the tool listing is answered from the allow list, so a client sees only what the layer permits (§10.3, AD-20)
        if (method->as_string() == "tools/list") {
            // the permitted names come from the layer, which is the only source of what may be called (§10.3, AD-20)
            const std::vector<std::string> permitted = m_route.permitted_commands ? m_route.permitted_commands() : std::vector<std::string>();
            // each permitted command becomes one tool, which is what a client may call (§10.3, AD-20)
            std::vector<core::Json> tools;
            // the listing walks the permitted names in order, because that is the order the layer reports (§10.3)
            for (const std::string& name : permitted) {
                // the tool is an object, because that is what the protocol describes (§10.3)
                core::Json tool;
                // the name is the command, which is what the call sends back (§10.3, AD-20)
                tool.set("name", core::Json(name));
                // the description states what reaching the layer means (§10.3, AD-20)
                tool.set("description", core::Json("run " + name + " through the albor automation layer"));
                // the schema declares the names the adapter places, so a client fills them by name (§10.3)
                core::Json schema;
                // the schema is the object type, which is what a call carries (§10.3)
                schema.set("type", core::Json(std::string("object")));
                // the properties come from the table, because that is what the adapter can place (§10.3)
                std::map<std::string, core::Json, std::less<>> properties;
                // each parameter is declared as a string, because the layer wire carries strings (§10.3)
                for (const std::string& parameter : parameter_names(name)) {
                    // the property describes the string the client sends (§10.3)
                    core::Json property;
                    // the type is a string, which is the only shape the wire carries (§10.3)
                    property.set("type", core::Json(std::string("string")));
                    // the description states what the parameter means for this command (§10.3)
                    property.set("description", core::Json("the " + parameter + " this command takes"));
                    // the property joins the schema (§10.3)
                    properties.insert_or_assign(parameter, std::move(property));
                }
                // the properties join the schema (§10.3)
                schema.set("properties", core::Json(std::move(properties)));
                // the tool carries its schema, which is what a client validates against (§10.3)
                tool.set("inputSchema", std::move(schema));
                // the tool joins the listing (§10.3)
                tools.push_back(std::move(tool));
            }
            // the listing is wrapped, because the protocol reads a tools member (§10.3)
            core::Json result;
            // the tools array joins the result (§10.3)
            result.set("tools", core::Json(std::move(tools)));
            // the listing answers without reaching the layer, because the allow list was the whole query (§10.3, AD-20)
            return envelope(*id, std::move(result)).to_string();
        }

        // a tool call is the only request that reaches the application (§10.3, AD-20)
        if (method->as_string() == "tools/call") {
            // a call without parameters names no tool, which is a stated failure (§10.3)
            if (params == nullptr || !params->is_object())
                return failure(*id, INVALID_PARAMS, "a tool call needs a name").to_string();
            // the tool name is read, because it is the command the layer will see (§10.3, AD-20)
            const core::Json* name = params->member("name");
            // a call without a name cannot be routed (§10.3)
            if (name == nullptr || name->kind() != core::Json::Kind::String)
                return failure(*id, INVALID_PARAMS, "a tool call needs a tool name").to_string();
            // the arguments are read when present, because a tool may take none (§10.3)
            std::vector<std::string> arguments;
            // a named argument object is placed into the order the layer wire needs (§10.3)
            if (const core::Json* given = params->member("arguments"); given != nullptr && given->kind() != core::Json::Kind::Null) {
                // the placement is attempted, because a client may send names or positions (§10.3)
                const std::optional<std::vector<std::string>> placed = place_arguments(name->as_string(), *given);
                // an argument form the adapter cannot place is stated, because guessing would call the wrong thing (§10.3)
                if (!placed)
                    return failure(*id, INVALID_PARAMS, "the arguments are not strings this tool accepts").to_string();
                // the placed arguments are what the call sends (§10.3)
                arguments = *placed;
            }
            // a route with no runner can do nothing, which is stated rather than silently empty (§10.3, AD-20)
            if (!m_route.run)
                return failure(*id, APPLICATION_ERROR, "the automation layer is not reachable").to_string();
            // the line is rendered, which is the wire the layer speaks (§10.3)
            const std::string wire = command_line(name->as_string(), arguments);
            // the call is counted, because the layer boundary is what AD-20 is about (§10.3, AD-20)
            m_layer_calls++;
            // the line runs through the layer, which is the only path into the application (§10.3, AD-20)
            const AutomationResult result = m_route.run(wire);
            // a refusal becomes an error carrying the layer's own reason, which keeps the two aligned (AD-20)
            if (!result.ok)
                return failure(*id, APPLICATION_ERROR, result.error).to_string();
            // an answer is wrapped as text, because the layer returns one payload and the protocol wants content (§10.3)
            core::Json content;
            // the text block is what the protocol describes for a string result (§10.3)
            content.set("type", core::Json(std::string("text")));
            // the payload is the layer's answer, unchanged (§10.3, AD-20)
            content.set("text", core::Json(result.payload));
            // the tool result is the protocol's shape around it (§10.3)
            core::Json tool_result;
            // the content joins the result (§10.3)
            tool_result.set("content", core::Json(std::vector<core::Json>{std::move(content)}));
            // an answered call reports itself as an error flag false, which is how a client tells refusal apart (§10.3)
            tool_result.set("isError", core::Json(false));
            // the call answers with the wrapped result (§10.3)
            return envelope(*id, std::move(tool_result)).to_string();
        }

        // a method this adapter does not route is stated, because silence would hang the client (§10.3)
        return failure(*id, METHOD_NOT_FOUND, "no such method: " + method->as_string()).to_string();
    }

    std::size_t McpServer::layer_calls() const { return m_layer_calls; }
} // namespace albor::automation
