#include "automation/host/automation_host.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "application/application.h"
#include "automation/command_commands.h"
#include "automation/mcp_connection.h"
#include "automation/mcp_server.h"
#include "automation/semantic_commands.h"
#include "ui/semantic_tree_builder.h"
#include "ui/ui_node.h"

namespace albor::automation
{
    namespace
    {
        // how often the host serves the transport, which is one interactive pass per display frame (§6.1, §10.3)
        constexpr std::chrono::milliseconds PUMP_INTERVAL{8};

        // report a host failure without reaching the application, because automation never breaks a window (§10.3)
        void report(std::string_view message) {
            // the reason goes to stderr, which is where a developer reads a failed host (§10.3)
            std::fprintf(stderr, "albor automation: %.*s\n", static_cast<int>(message.size()), message.data());
        }
    } // namespace

    AutomationHost& AutomationHost::instance() {
        // the host is allocated once and intentionally never destroyed, because process exit is the final stop (§10.3)
        static AutomationHost* host = new AutomationHost();
        // the singleton is handed out by reference, because every auto installed observer reaches this one (§10.3)
        return *host;
    }

    void AutomationHost::bind(void* root, void* engine, void* text) {
        // a null tree names no window, so it cannot become the active semantic root (§10.3)
        if (root == nullptr)
            return;
        // the tree is appended or moved to the front of the stack, which makes it active (§10.3)
        m_bindings.bind(root, engine, text);
        // pending stop stays alive if tree remains (§10.3)
        m_stop_pending = false;
        // the server starts on the first tree, which is what makes the host transparent to the application (§10.3)
        if (!m_transport)
            start_mcp();
    }

    void AutomationHost::unbind(void* root) {
        // the closing tree is removed, and an unknown root leaves every other window untouched (§10.3)
        m_bindings.unbind(root);
        // a server with another tree stays up, because that tree is now the active one (§10.3)
        if (!m_bindings.empty())
            return;
        // a stop during a command is deferred, because the transport is still writing that command's reply (§10.3)
        if (m_serving) {
            // the pump completes the stop after the active serve returns (§10.3)
            m_stop_pending = true;
            return;
        }
        // no tree remains, so the endpoint and its registry are released now (§10.3)
        stop();
    }

    void AutomationHost::pump() {
        // a host with no transport has nothing to serve, which a failed start leaves behind (§10.3)
        if (!m_transport)
            return;
        // a pump already in progress must not reenter the transport, because one thread owns the conversation (§10.3)
        if (m_serving)
            return;
        // the serving flag is set before the transport runs, so an unbind from a command can defer its stop (§10.3)
        m_serving = true;
        // the transport serves at most one client and never waits, which keeps the ui thread responsive (§6.1, §10.3)
        const bool served = m_transport->serve_one();
        // the flag is cleared before a deferred stop, because that stop owns the transport from here (§10.3)
        m_serving = false;
        // a listener level failure is logged and retried on the next timer tick (§10.3)
        if (!served)
            report(m_transport->last_error());
        // a command that closed the last window is completed after its reply was written (§10.3)
        if (m_stop_pending)
            stop();
    }

    bool AutomationHost::running() const {
        // a live transport is the only state that can serve, because a failed start leaves it null (§10.3)
        return m_transport != nullptr;
    }

    std::size_t AutomationHost::binding_count() const {
        // the count is the number of observed windows, which a duplicate safeguard test reads (§10.3)
        return m_bindings.count();
    }

    const HostPaths& AutomationHost::paths() const {
        // the paths are the published ones, or empty before a successful start (§10.3)
        return m_paths;
    }

    void AutomationHost::start() {
        // a start is attempted once per transition from no transport to a live one (§10.3)
        // start_mcp() owns the guard, which keeps the single transition rule in one place (§10.3)
        start_mcp();
    }

    void AutomationHost::start_mcp(SemanticTree* tree) {
        // a start is attempted once per transition from no transport to a live one (§10.3)
        if (m_transport)
            return;
        // an app supplied tree is read as-is, so the commands need no bound window (§10.3)
        m_external_tree = tree;
        // the registry is fresh per run, because the allow list and command set belong to this endpoint (§10.3, AD-10)
        m_registry = std::make_unique<AutomationRegistry>();
        // the semantic commands read the active tree (or external, or empty) on demand (§10.3)
        register_semantic_commands(*m_registry, [this] { return semantic_tree(); });
        // commands read the process-wide registry (§10.3, §18.1)
        register_command_commands(*m_registry, [] { return &Application::instance().commands(); });
        // the standard surface is permitted explicitly, because registration alone is not consent (§10.3, AD-10)
        for (const std::string& name : semantic_command_names()) {
            // a name this build did not register is skipped, because the allow list may only name a live command (§10.3)
            if (m_registry->knows(name))
                m_registry->allow(name);
        }
        // the command half of the standard surface is permitted the same way (§10.3, AD-10)
        for (const std::string& name : command_command_names()) {
            // a name this build did not register is skipped, because the allow list may only name a live command (§10.3)
            if (m_registry->knows(name))
                m_registry->allow(name);
        }
        // the platform transport is created only when this process has one, which fails soft on windows (§10.3, AD-10)
        m_transport = create_transport(*m_registry);
        // a platform without a transport leaves the application running and serves nothing (§10.3)
        if (!m_transport) {
            // the absence is reported once, because the host cannot serve on this platform yet (§10.3)
            report("no local transport is available on this platform");
            // the registry is released, because no endpoint will use it (§10.3)
            m_registry.reset();
            return;
        }
        // the client protocol is MCP, which reaches the application only through the registry and session (§10.3, AD-20)
        AutomationRegistry* registry = m_registry.get();
        // factory installed before first client (§10.3)
        m_transport->set_connection_factory([registry](std::string_view token) { return std::make_unique<McpConnection>(*registry, std::string(token)); });
        // the identity is read from the process, because discovery is per process and never per window (§10.3)
        const HostIdentity identity = host_identity();
        // the layout is built before anything is written, because a failed directory leaves no endpoint (§10.3)
        m_paths = make_host_paths(identity);
        // the directory is created with owner only permissions, which the endpoint contract requires (§10.3, AD-10)
        std::string error;
        // a directory failure is logged and cleaned up, because the application continues normally (§10.3)
        if (!ensure_host_directory(m_paths, error)) {
            // the reason names the path a user can inspect (§10.3)
            report(error);
            // the partial endpoint is released before the next bind retries (§10.3)
            stop();
            return;
        }
        // the transport binds the socket and publishes the token the client will read (§10.3, AD-10)
        const AutomationEndpoint endpoint{.socket_path = m_paths.socket.string(), .token_path = m_paths.token.string()};
        // a listen failure leaves no endpoint, and the next bind may retry after the cause is removed (§10.3)
        if (!m_transport->listen(endpoint)) {
            // the transport states why it refused, which is what a developer needs (§10.3)
            report(m_transport->last_error());
            // the partial directory and transport are released together (§10.3)
            stop();
            return;
        }
        // the start time orders two hosts that reused a process id (§10.3)
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        // milliseconds are what the descriptor publishes, because a client compares an instant (§10.3)
        const std::uint64_t started_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        // the descriptor is written after the socket exists, so a client never finds a path that cannot connect (§10.3)
        if (!write_host_descriptor(m_paths, identity, started_ms, mcp_protocol_version(), error)) {
            // the reason names the file a user can inspect (§10.3)
            report(error);
            // the endpoint is removed, because an undiscoverable host is not a host (§10.3)
            stop();
            return;
        }
        // the pump is scheduled last, because every earlier failure must leave no timer behind (§10.3)
        m_timer = Application::instance().timers().schedule_repeating([this] { pump(); }, PUMP_INTERVAL);
        // the endpoint is reported once, which is how a developer connects without reading the temporary tree (§10.3)
        report("serving " + m_paths.descriptor.string());
    }

    void AutomationHost::stop() {
        // a scheduled timer is cancelled first, because its callback reaches this host (§4.2, §10.3)
        if (m_timer != 0) {
            // the timer is removed from the application scheduler, which owns the callback (§4.2)
            (void)Application::instance().timers().cancel(m_timer);
            // the handle is cleared, so a second stop does not cancel an unrelated timer (§4.2)
            m_timer = 0;
        }
        // the transport stops before the registry, because every connection borrows that registry (§10.3, AD-20)
        if (m_transport) {
            // the listener and client are released, which is what makes the socket unreachable (§10.3)
            m_transport->stop();
            // the transport is destroyed before the registry it borrowed (§10.3, AD-20)
            m_transport.reset();
        }
        // the registry is destroyed after every connection that could reach it (§10.3, AD-20)
        m_registry.reset();
        // the published directory is removed, which takes the descriptor, token, and socket together (§10.3)
        remove_host_directory(m_paths);
        // the paths are cleared, so a later start builds a fresh layout (§10.3)
        m_paths = HostPaths{};
        // a pending stop that reached this call has been completed (§10.3)
        m_stop_pending = false;
    }

    SemanticTree* AutomationHost::semantic_tree() {
        // an app supplied tree is read as-is, because the app owns its content (§10.3)
        if (m_external_tree != nullptr)
            return m_external_tree;
        // the active tree is the most recently bound window, which is what a client drives (§10.3)
        const HostBinding* active = m_bindings.active();
        // a host with no active tree reports an empty one, because a command may race the last unbind (§10.3)
        if (active == nullptr) {
            // the scratch tree is cleared, so no stale node survives its window (§10.3)
            m_tree.clear();
            // the empty tree is still a valid snapshot, which a command answers from (§10.3)
            return &m_tree;
        }
        // tree rebuilt from ui root on demand (§10.3)
        m_tree = build_semantic_tree(*static_cast<const UiNode*>(active->root));
        // the rebuilt tree is what every semantic command reads and writes for this line (§10.3)
        return &m_tree;
    }
} // namespace albor::automation
