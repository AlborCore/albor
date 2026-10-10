#include "window/platform_window.h"

#include "window/platform_window_factory.h"

namespace albor
{
    std::unique_ptr<PlatformWindow> PlatformWindow::create(const WindowOptions& options) {
        // an invalid option set is refused before any platform code runs (§5.1)
        if (!options.valid())
            return nullptr;
        // without an installed backend there is no native window to build (§5.3)
        const PlatformWindowFactory& factory = platform_window_factory();
        if (!factory)
            return nullptr;
        // a backend that refuses the options reports no window rather than a broken one (§5.1)
        return factory(options);
    }
} // namespace albor
