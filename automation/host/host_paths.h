#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace albor::automation
{
    // what identifies one running host, which the discovery file publishes and the directory names (§10.3)
    struct HostIdentity
    {
        // the process id, which a client checks before it trusts a stale directory (§10.3)
        std::uint64_t pid = 0;

        // the user id, which the short path fallback carries so two users never share an endpoint (§10.3)
        std::uint64_t user = 0;

        // the process name, which a client shows in its connection list (§10.3)
        std::string process;

        // the temporary base the primary layout sits under, which the platform chooses (§10.3)
        std::filesystem::path base;
    };

    // the files one host publishes, which is the whole discovery contract (§10.3)
    struct HostPaths
    {
        // the per process directory, which is owned by the user and removed on stop (§10.3)
        std::filesystem::path directory;

        // the local socket a client connects to, which the transport binds (§10.3, AD-10)
        std::filesystem::path socket;

        // the token file the transport publishes, which the client reads (§10.3, AD-10)
        std::filesystem::path token;

        // the descriptor a client scans for, which names the other two paths and the protocol (§10.3)
        std::filesystem::path descriptor;
    };

    // build the identity of this process, which is platform specific (§10.3, AD-16)
    [[nodiscard]] HostIdentity host_identity();

    // build the primary or short fallback layout, which the transport can bind (§10.3)
    [[nodiscard]] HostPaths make_host_paths(const HostIdentity& identity);

    // create the owning directory with owner only permissions, reporting the reason on failure (§10.3)
    [[nodiscard]] bool ensure_host_directory(const HostPaths& paths, std::string& error);

    // render the discovery descriptor, which is what a client reads before it connects (§10.3)
    [[nodiscard]] std::string host_descriptor(const HostPaths& paths, const HostIdentity& identity, std::uint64_t started_ms, std::string_view protocol_version);

    // write the descriptor with owner only permissions, reporting the reason on failure (§10.3)
    [[nodiscard]] bool write_host_descriptor(const HostPaths& paths, const HostIdentity& identity, std::uint64_t started_ms, std::string_view protocol_version, std::string& error);

    // remove the host directory and everything the transport left in it (§10.3)
    void remove_host_directory(const HostPaths& paths);
} // namespace albor::automation
