#include "render/renderer_backend.h"

namespace albor
{
    namespace
    {
        // the installed state, which a null pointer leaves empty without running any initializer (§6.2)
        RendererBackendFactoryState* g_state = nullptr;

        // hand out the state, building it the first time it is asked for, which is the only writer (§6.2)
        [[nodiscard]] RendererBackendFactoryState& state() {
            // the first caller creates the state, and every later one reads the same object (§6.2)
            if (g_state == nullptr)
                g_state = new RendererBackendFactoryState();
            return *g_state;
        }
    } // namespace

    void set_renderer_backend_factory(RendererBackendFactory factory) {
        // the factory replaces the previous one, because the framework draws through exactly one backend (§6.2)
        state().factory = std::move(factory);
    }

    const RendererBackendFactory& renderer_backend_factory() {
        // a never installed factory is the empty one, reported rather than crashed on (§6.2)
        static const RendererBackendFactory empty;
        return g_state ? g_state->factory : empty;
    }

    std::unique_ptr<RendererBackend> create_renderer_backend() {
        // without an installed backend the framework has no way to draw (§6.2)
        const RendererBackendFactory& factory = renderer_backend_factory();
        if (!factory)
            return nullptr;
        return factory();
    }
} // namespace albor
