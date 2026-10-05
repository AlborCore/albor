#include "render/renderer_backend.h"

namespace albor
{
    namespace
    {
        // the installed backend factory, which is empty until a backend installs one (§6.2)
        RendererBackendFactory g_backend_factory;
    } // namespace

    void set_renderer_backend_factory(RendererBackendFactory factory) {
        // the factory replaces the previous one, because the framework draws through exactly one backend (§6.2)
        g_backend_factory = std::move(factory);
    }

    const RendererBackendFactory& renderer_backend_factory() {
        // an uninstalled backend is reported as an empty factory rather than a crash (§6.2)
        return g_backend_factory;
    }

    std::unique_ptr<RendererBackend> create_renderer_backend() {
        // without an installed backend the framework has no way to draw (§6.2)
        if (!g_backend_factory)
            return nullptr;
        return g_backend_factory();
    }
} // namespace albor
