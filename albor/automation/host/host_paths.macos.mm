#include <unistd.h>

#include <filesystem>
#include <optional>
#include <string>

#include "albor/automation/host/host_paths.h"
#include "albor/core/env.h"

namespace albor::automation
{
    HostIdentity host_identity() {
        // the identity is filled from the process itself, because the discovery contract is per process (§10.3)
        HostIdentity identity;
        // the process id names the directory and lets a client reject a stale entry (§10.3)
        identity.pid = static_cast<std::uint64_t>(::getpid());
        // the user id names the short fallback directory, so two users never share a socket (§10.3, AD-10)
        identity.user = static_cast<std::uint64_t>(::getuid());
        // the process name is what a client shows, and every process has one on macos (§10.3)
        identity.process = ::getprogname() != nullptr ? ::getprogname() : "albor";
        // the temporary root is what the platform gives this session, which is already user private (§10.3, AD-10)
        if (const std::optional<std::string> temporary = albor::core::get_environment_variable("TMPDIR"); temporary.has_value() && !temporary->empty())
            identity.base = *temporary;
        else
            identity.base = "/tmp";
        // the identity is returned whole, because the portable side owns the layout (§10.3)
        return identity;
    }

    std::filesystem::path host_endpoint_path(const HostIdentity&, const std::filesystem::path& directory) {
        // the unix domain socket sits beside the token, which is the endpoint a client connects to (§10.3, AD-10)
        return directory / "mcp.sock";
    }
} // namespace albor::automation
