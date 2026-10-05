#include "window/platform_window_factory.h"

namespace albor
{
    namespace
    {
        // the installed factory, which is empty until a platform backend installs one (§5.3)
        PlatformWindowFactory g_factory;
    } // namespace

    void set_platform_window_factory(PlatformWindowFactory factory) {
        // the factory replaces the previous one, because a process is served by exactly one backend (§5.3)
        g_factory = std::move(factory);
    }

    const PlatformWindowFactory& platform_window_factory() {
        // an uninstalled backend is reported as an empty factory rather than a crash (§5.3)
        return g_factory;
    }

    std::unique_ptr<PlatformWindow> PlatformWindow::create(const WindowOptions& options) {
        // an invalid option set is refused before any platform code runs (§5.1)
        if (!options.valid())
            return nullptr;
        // without an installed backend there is no native window to build (§5.3)
        if (!g_factory)
            return nullptr;
        // a backend that refuses the options reports no window rather than a broken one (§5.1)
        return g_factory(options);
    }
} // namespace albor
