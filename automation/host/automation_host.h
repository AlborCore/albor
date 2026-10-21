#pragma once

#include <cstddef>
#include <memory>

#include "application/task_queue.h"
#include "automation/automation_registry.h"
#include "automation/automation_transport.h"
#include "automation/capture_commands.h"
#include "automation/host/host_paths.h"
#include "automation/host/host_state.h"
#include "semantics/semantic_provider.h"

namespace albor::automation
{
    // the process lifetime host that serves the standard MCP surface without application wiring (§10.3, AD-20)
    class AutomationHost
    {
    public:
        // hand out the one host this process owns, which the auto installer reaches at load (§10.3)
        [[nodiscard]] static AutomationHost& instance();

        // observe a window tree, which starts the server when this is the first binding (§10.3)
        void bind(const dev::WindowBinding& binding);

        // release a closing window tree, which stops the server when none remains (§10.3)
        void unbind(void* root);

        // start the MCP server, which reads the given tree or the active bound tree when null (§10.3)
        void start_mcp(SemanticTree* tree = nullptr);

        // serve one transport pass, which the repeating timer calls on the ui thread (§6.1, §10.3)
        void pump();

        // report whether a transport is listening, which the start and stop transitions own (§10.3)
        [[nodiscard]] bool running() const;

        // hand out how many windows the host observes, which the duplicate rule is tested against (§10.3)
        [[nodiscard]] std::size_t binding_count() const;

        // hand out the published paths, which the integration test reads after startup (§10.3)
        [[nodiscard]] const HostPaths& paths() const;

        // hand out the semantic tree the commands read, which is the supplied one or the active bound tree (§10.3)
        [[nodiscard]] SemanticTree* semantic_tree();

    private:
        // the host is built once by instance and never destroyed by a static destructor (§10.3)
        AutomationHost() = default;

        // build the registry and transport, publish discovery, and schedule the pump (§10.3)
        void start();

        // tear down the transport, registry, timer, and directory in the order each borrows the next (§10.3)
        void stop();

        // read the last frame the active window presented, which is the window most recently bound (§6.3, §10.3)
        [[nodiscard]] CapturedFrame capture_frame();

        // an app supplied tree the semantic commands read as-is, or null when the bound tree is used (§10.3)
        SemanticTree* m_external_tree = nullptr;

        // the window trees in activation order, whose back is active (§10.3)
        HostBindings m_bindings;

        // the commands the current server serves, which exists only while the transport does (§10.3)
        std::unique_ptr<AutomationRegistry> m_registry;

        // the local transport, which is built when MCP starts and stopped when none remains (§10.3)
        std::unique_ptr<AutomationTransport> m_transport;

        // the scratch tree semantic commands read when no external or bound tree exists (§10.3)
        SemanticTree m_tree;

        // the published paths, which are cleared when the endpoint is removed (§10.3)
        HostPaths m_paths;

        // the repeating pump, whose zero value means no timer is scheduled (§4.2)
        TimerHandle m_timer = 0;

        // whether a transport pass is running, which makes an unbind defer its stop (§10.3)
        bool m_serving = false;

        // whether an unbind arrived during a pass, which the pump acts on after the transport returns (§10.3)
        bool m_stop_pending = false;
    };
} // namespace albor::automation
