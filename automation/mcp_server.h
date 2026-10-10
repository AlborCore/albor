#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "automation/automation_protocol.h"

namespace albor::automation
{
    // how the adapter reaches the automation layer, which is the only path it may take (AD-20, §10.3)
    struct McpRoute
    {
        // the commands the allow list currently permits, which becomes the tool list a client sees (§10.3, AD-20)
        std::function<std::vector<std::string>()> permitted_commands;

        // run one command line through the automation layer, which is where every request ends (§10.3, AD-20)
        std::function<AutomationResult(std::string_view)> run;
    };

    // the protocol adapter, which reaches nothing the automation layer would refuse (§10.3, AD-20)
    class McpServer
    {
    public:
        // build an adapter over a route, which is the only thing it can reach (§10.3, AD-20)
        explicit McpServer(McpRoute route);

        // handle one json-rpc request line, returning nothing for a notification, which takes no reply (§12.2)
        [[nodiscard]] std::optional<std::string> handle_line(std::string_view line);

        // how many calls reached the automation layer, which a test asserts against (§10.3, AD-20)
        [[nodiscard]] std::size_t layer_calls() const;

    private:
        // answer one request object, which is the whole of the protocol (§10.3)
        [[nodiscard]] std::string answer(const class Json& request);

        // the route every call takes, which is the only path into the application (§10.3, AD-20)
        McpRoute m_route;

        // how many calls reached the layer, which a test asserts against (§10.3, AD-20)
        std::size_t m_layer_calls = 0;
    };
} // namespace albor::automation
