#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "automation/host/host_paths.h"

namespace albor::automation
{
    HostIdentity host_identity() {
        // the identity is filled from the process itself, because the discovery contract is per process (§10.3)
        HostIdentity identity;
        // the process id names the directory and lets a client reject a stale entry (§10.3)
        identity.pid = static_cast<std::uint64_t>(::getpid());
        // the user id names the short fallback directory, so two users never share a socket (§10.3, AD-10)
        identity.user = static_cast<std::uint64_t>(::getuid());
        // the kernel exposes the process name without an argument list, which is what a client shows (§10.3)
        std::ifstream name("/proc/self/comm");
        // the fallback names the framework rather than an empty process when a container hides procfs (§10.3)
        if (!std::getline(name, identity.process) || identity.process.empty())
            identity.process = "albor";
        // the runtime directory is per user and already private, which is what the XDG contract means (§10.3, AD-10)
        if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime != nullptr && *runtime != '\0')
            identity.base = runtime;
        else
            identity.base = "/tmp";
        // the identity is returned whole, because the portable side owns the layout (§10.3)
        return identity;
    }
} // namespace albor::automation
