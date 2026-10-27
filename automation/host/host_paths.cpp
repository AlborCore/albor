#include "automation/host/host_paths.h"

#include <fstream>
#include <map>
#include <system_error>
#include <utility>

#include "core/json.h"

namespace albor::automation
{
    namespace
    {
        // longest socket path portable side hands (§10.3)
        constexpr std::size_t MAX_SOCKET_PATH = 100;

        // the family the host directories live under, which keeps every process beside its siblings (§10.3)
        constexpr std::string_view HOST_FAMILY = "albor-mcp";
    } // namespace

    HostPaths make_host_paths(const HostIdentity& identity) {
        // the base is the platform temporary root, which every desktop supplies before a window exists (§10.3)
        const std::filesystem::path base = identity.base.empty() ? std::filesystem::temp_directory_path() : identity.base;
        // the primary layout is one directory per process, which is what a client scans (§10.3)
        HostPaths paths;
        // the directory is named by the process id, because two running applications must never share one (§10.3)
        paths.directory = base / std::string(HOST_FAMILY) / std::to_string(identity.pid);
        // the transport endpoint is the local socket or pipe a client connects to (§10.3, AD-10)
        paths.socket = host_endpoint_path(identity, paths.directory);
        // the token sits beside the socket, because the transport publishes both together (§10.3, AD-10)
        paths.token = paths.directory / "token";
        // the descriptor sits beside them, because that is what a client finds first (§10.3)
        paths.descriptor = paths.directory / "host.json";
        // over-limit socket replaced by short fallback (§10.3)
        if (paths.socket.string().size() >= MAX_SOCKET_PATH) {
            // the fallback keeps the user and process in its path, so an old run cannot answer for a new one (§10.3)
            paths.directory = std::filesystem::path("/tmp") / ("albor-" + std::to_string(identity.user)) / std::to_string(identity.pid);
            // the transport endpoint is rebuilt for the short layout (§10.3)
            paths.socket = host_endpoint_path(identity, paths.directory);
            // the token is rebuilt under the short directory (§10.3)
            paths.token = paths.directory / "token";
            // the descriptor is rebuilt under the short directory (§10.3)
            paths.descriptor = paths.directory / "host.json";
        }
        // the whole layout is returned, because the host publishes exactly these paths (§10.3)
        return paths;
    }

    bool ensure_host_directory(const HostPaths& paths, std::string& error) {
        // the directory is created with an error code, because a host failure must not throw into the application (§10.3)
        std::error_code code;
        // every missing parent is created, because the temporary root may not carry the family yet (§10.3)
        std::filesystem::create_directories(paths.directory, code);
        // a failure is reported with the path a user can inspect (§10.3)
        if (code) {
            // the reason is what the host logs (§10.3)
            error = "create " + paths.directory.string() + ": " + code.message();
            return false;
        }
        // a path that exists but is not a directory cannot hold the endpoint (§10.3)
        if (!std::filesystem::is_directory(paths.directory, code) || code) {
            // the reason names the path, because a stale file is the likely cause (§10.3)
            error = "not a directory: " + paths.directory.string();
            return false;
        }
        // the directory is restricted to its owner, because a local endpoint is not a shared one (§10.3, AD-10)
        std::filesystem::permissions(paths.directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, code);
        // a directory that cannot be restricted is refused rather than exposed (§10.3, AD-10)
        if (code) {
            // the reason is what the host logs (§10.3)
            error = "permissions " + paths.directory.string() + ": " + code.message();
            return false;
        }
        // the directory is ready for the token and descriptor (§10.3)
        return true;
    }

    std::string host_descriptor(const HostPaths& paths, const HostIdentity& identity, std::uint64_t started_ms, std::string_view protocol_version) {
        // the descriptor is an object, because every discovery file is keyed by name (§10.3)
        core::Json descriptor;
        // the process id lets a client ignore a stale directory whose owner is gone (§10.3)
        descriptor.set("pid", core::Json(static_cast<double>(identity.pid)));
        // the process name is what a client shows, which is not part of the endpoint (§10.3)
        descriptor.set("process", core::Json(identity.process));
        // the socket is the actual path, including the short fallback when it was used (§10.3)
        descriptor.set("socket", core::Json(paths.socket.string()));
        // the token is the actual path, which a client reads before it connects (§10.3)
        descriptor.set("token", core::Json(paths.token.string()));
        // the protocol names MCP, because the transparent host does not speak raw automation (§10.3, AD-20)
        descriptor.set("protocol", core::Json(std::string("mcp")));
        // the version lets a client reject a protocol it cannot speak before it sends a request (§10.3)
        descriptor.set("protocolVersion", core::Json(std::string(protocol_version)));
        // the start time lets a client order two hosts that reused a pid (§10.3)
        descriptor.set("started", core::Json(static_cast<double>(started_ms)));
        // the descriptor is rendered once, because the file is written as one atomic document (§10.3)
        return descriptor.to_string();
    }

    bool write_host_descriptor(const HostPaths& paths, const HostIdentity& identity, std::uint64_t started_ms, std::string_view protocol_version, std::string& error) {
        // the output is opened with truncation, because a previous run may have left a longer descriptor (§10.3)
        std::ofstream file(paths.descriptor, std::ios::out | std::ios::trunc | std::ios::binary);
        // a descriptor that cannot be opened leaves the endpoint undiscoverable (§10.3)
        if (!file) {
            // the reason names the path, which is what a user can inspect (§10.3)
            error = "open descriptor: " + paths.descriptor.string();
            return false;
        }
        // the document is rendered and written in one call, because a partial descriptor is worse than none (§10.3)
        file << host_descriptor(paths, identity, started_ms, protocol_version);
        // a write failure is reported before the file is trusted (§10.3)
        if (!file) {
            // the reason names the path, which is what a user can inspect (§10.3)
            error = "write descriptor: " + paths.descriptor.string();
            return false;
        }
        // the file is closed before its permissions are narrowed, because a reader must see a complete document (§10.3)
        file.close();
        // the close itself can fail, which means the descriptor is not trustworthy (§10.3)
        if (!file) {
            // the reason names the path, which is what a user can inspect (§10.3)
            error = "close descriptor: " + paths.descriptor.string();
            return false;
        }
        // the descriptor is readable only by its owner, because it names the token path (§10.3, AD-10)
        std::error_code code;
        // the permissions are replaced rather than masked, because the umask must not widen them (§10.3, AD-10)
        std::filesystem::permissions(paths.descriptor, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, code);
        // descriptor refusal, same-user token boundary (§10.3, AD-10)
        if (code) {
            // the reason is what the host logs (§10.3)
            error = "descriptor permissions " + paths.descriptor.string() + ": " + code.message();
            return false;
        }
        // the descriptor is published and ready for a client (§10.3)
        return true;
    }

    void remove_host_directory(const HostPaths& paths) {
        // an empty path names no directory, which is what a failed start leaves (§10.3)
        if (paths.directory.empty())
            return;
        // the removal uses an error code, because a host failure must not throw during teardown (§10.3)
        std::error_code code;
        // the whole directory is removed, which takes the socket, token, and descriptor together (§10.3)
        std::filesystem::remove_all(paths.directory, code);
        // a failed removal is ignored by the caller, because process exit will reap the temporary directory (§10.3)
    }
} // namespace albor::automation
