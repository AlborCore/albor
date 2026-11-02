#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace albor::automation
{
    // one authenticated conversation over a transport, which is where a protocol adapter lives (§10.3, AD-20)
    class AutomationConnection
    {
    public:
        virtual ~AutomationConnection() = default;

        // handle one request line and hand back the reply, or nothing when a notification takes none (§10.3)
        [[nodiscard]] virtual std::optional<std::string> handle(std::string_view line) = 0;
    };

    // build one connection for a client with the token the transport published, which is per conversation (§10.3, AD-10)
    using ConnectionFactory = std::function<std::unique_ptr<AutomationConnection>(std::string_view token)>;
} // namespace albor::automation
