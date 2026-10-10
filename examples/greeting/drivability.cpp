#include "drivability.h"

#include <cstdio>

#include "accessibility/accessibility_adapter.h"
#include "automation/automation_registry.h"
#include "automation/automation_transport.h"
#include "automation/semantic_commands.h"
#include "ui/semantic_tree_builder.h"

namespace albor::example
{
    namespace
    {
        // the endpoint a client connects over, which is local and readable by this user only (§10.3, AD-10)
        const std::string ENDPOINT = "/tmp/albor-greeting.sock";

        // the file the token is published to, which is what a client reads to authenticate (§10.3, AD-10)
        const std::string TOKEN = "/tmp/albor-greeting.token";
    } // namespace

    // the implementation owns the bridge, the registry and the transport, which the header only forward declares
    class DrivableImpl
    {
    public:
        // the tree this reads, which refresh rebuilds every frame (§10.1)
        UiNode* root = nullptr;

        // the bridge publishes to assistive technology, and the transport serves automation clients (§10.2, §10.3)
        std::unique_ptr<accessibility::AccessibilityAdapter> bridge;

        // the commands a client may run, which are permitted one by one rather than all at once (§10.3, AD-10)
        automation::AutomationRegistry commands;

        // the transport clients connect over, which is a local socket and never a network port (§10.3, AD-10)
        std::unique_ptr<automation::AutomationTransport> transport;
    };

    Drivable::Drivable(UiNode& root, const void* native_window) {
        // the implementation is allocated, because the header names no platform or automation type (§3.5, AD-05)
        auto impl = std::make_unique<DrivableImpl>();
        // the tree root is remembered, because refresh rebuilds from it every frame (§10.1)
        impl->root = &root;
        // the bridge publishes the semantic tree to the platform, which is the macos side of §10.2
        impl->bridge = accessibility::create_adapter();
        // a bridge exists on this platform, and a missing one is stated rather than silently skipped (§10.2)
        if (!impl->bridge)
            std::fprintf(stderr, "greeting: no accessibility bridge on this platform\n");
        // the semantic commands read the live tree, which is what makes a state change land (§10.3, AD-20)
        automation::register_semantic_commands(impl->commands, [this] { return &m_semantics; });
        // only reading and pressing are permitted, so nothing else is reachable (§10.3, AD-10)
        for (const char* name : {"semantic.tree", "semantic.find", "semantic.actions", "semantic.invoke"})
            impl->commands.allow(name);
        // the transport is built over the registry, which is what the commands run through (§10.3)
        impl->transport = automation::create_transport(impl->commands);
        // listening fails loudly, because an unauthenticated endpoint is worse than none (§10.3, AD-10)
        if (impl->transport && !impl->transport->listen({.socket_path = ENDPOINT, .token_path = TOKEN}))
            std::fprintf(stderr, "greeting: the automation endpoint did not open: %s\n", impl->transport->last_error().c_str());
        // the bridge attaches to the window, so assistive technology reaches it through that window (§10.2)
        if (impl->bridge) {
            // the native handle is wrapped, which is how a platform pointer crosses a portable boundary (§5.3, AD-05)
            albor::NativeWindowHandle handle = albor::NativeWindowHandle::from_object(native_window);
            // the attachment is the whole of the window side (§10.2)
            impl->bridge->attach_window(handle);
        }
        // the tree is built once, so a frame has something to publish before the first refresh (§10.1)
        m_semantics = build_semantic_tree(root);
        // the implementation is kept by the header slot, which is typed no further than this file (§3.5)
        m_impl = std::move(impl);
    }

    Drivable::~Drivable() {
        // a construction that failed leaves nothing to destroy (§10.3)
        if (m_impl == nullptr)
            return;
        // the transport stops first, because it may be blocked reading from a client (§4.1, §10.3)
        if (m_impl->transport)
            m_impl->transport->stop();
        // the bridge detaches, because the window is going away (§5.2, §10.2)
        if (m_impl->bridge)
            m_impl->bridge->detach_window();
    }

    void Drivable::refresh() {
        // a construction that failed leaves nothing to refresh (§10.3)
        if (m_impl == nullptr || m_impl->root == nullptr)
            return;
        // the tree is rebuilt from the live ui tree every frame, because a tree from an earlier frame is a lie (§10.1)
        m_semantics = build_semantic_tree(*m_impl->root);
        // one waiting client is served, which is where a command takes effect (§10.3)
        if (m_impl->transport)
            (void)m_impl->transport->serve_one();
        // the bridge republishes what was just built, which is what assistive technology sees (§10.2)
        if (m_impl->bridge)
            m_impl->bridge->publish(m_semantics);
    }

    const std::string& Drivable::endpoint() const { return ENDPOINT; }
} // namespace albor::example
