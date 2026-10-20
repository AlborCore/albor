#include <windows.h>

#include <process.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "automation/host/host_paths.h"

namespace albor::automation
{
    HostIdentity host_identity() {
        // the identity is filled from the process itself, because the discovery contract is per process (§10.3)
        HostIdentity identity;
        // the process id names the directory and lets a client reject a stale entry (§10.3)
        identity.pid = static_cast<std::uint64_t>(::_getpid());
        // windows has no unix user id, so the short fallback carries zero (§10.3)
        identity.user = 0;
        // the module name is the process a client sees, which the executable itself carries (§10.3)
        char module[MAX_PATH]{};
        // the length decides whether the name is usable, because a truncated path names no process (§10.3)
        const DWORD length = ::GetModuleFileNameA(nullptr, module, MAX_PATH);
        // a usable name is reduced to its stem, which is what a client shows (§10.3)
        if (length > 0 && length < MAX_PATH)
            identity.process = std::filesystem::path(module).stem().string();
        else
            identity.process = "albor";
        // the local application data is per user, which is what a private endpoint needs (§10.3, AD-10)
        if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0')
            identity.base = local;
        else
            identity.base = std::filesystem::temp_directory_path();
        // the identity is returned whole, because the portable side owns the layout (§10.3)
        return identity;
    }
} // namespace albor::automation
