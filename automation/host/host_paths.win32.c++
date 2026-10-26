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
        char* local = nullptr;
        // the buffer length is written by the read, which the release below does not need (§10.3)
        std::size_t local_size = 0;
        // a private endpoint prefers the per user directory, which windows publishes in the environment (§10.3, AD-10)
        const bool found = ::_dupenv_s(&local, &local_size, "LOCALAPPDATA") == 0 && local != nullptr && *local != '\0';
        // the value is copied before the allocation is released, because the identity owns its path (§10.3)
        if (found)
            identity.base = local;
        else
            identity.base = std::filesystem::temp_directory_path();
        // the environment block is released on both paths, because the copy no longer needs it (§10.3)
        std::free(local);
        // the identity is returned whole, because the portable side owns the layout (§10.3)
        return identity;
    }

    std::filesystem::path host_endpoint_path(const HostIdentity& identity, const std::filesystem::path&) {
        // the named pipe is a kernel object, so it is addressed by name rather than by a directory entry (§10.3, AD-10)
        DWORD session = 0;
        // the session id keeps two processes that reuse a pid in different sessions apart (§10.3)
        (void)::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
        // the pipe name carries the session and process, which is unique while the host is alive (§10.3)
        return std::filesystem::path(L"\\\\.\\pipe\\albor-mcp-" + std::to_wstring(session) + L"-" + std::to_wstring(identity.pid));
    }
} // namespace albor::automation
