#pragma once

#include <cstdint>

namespace albor
{
    // the runtime identity of a command inside its registry, where zero names no command (§18.1)
    using CommandId = std::uint64_t;

    // the identity of one bound consumer, which is what unbind is given and where zero binds nothing (§18.1)
    using ObserverHandle = std::uint64_t;

    // the identifier a command carries before one is assigned, which never names a real command (§18.1)
    [[nodiscard]] constexpr CommandId invalid_command_id() {
        // the zero identity is the one a default constructed command carries (§18.1)
        return 0;
    }

    // report whether an identifier names a real command, which a lookup refuses on (§18.1)
    [[nodiscard]] constexpr bool is_valid(CommandId id) {
        // the zero identity is the sentinel, so anything else names a command (§18.1)
        return id != 0;
    }
} // namespace albor
