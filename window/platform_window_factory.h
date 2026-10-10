#pragma once

#include <functional>
#include <memory>

#include "window/platform_window.h"
#include "window/window_options.h"

namespace albor
{
    // the signature every native window backend implements to build its window (§5.3, AD-16)
    using PlatformWindowFactory = std::function<std::unique_ptr<PlatformWindow>(const WindowOptions& options)>;

    // the state the installed factory lives in, heap held behind a constant initialized pointer (§5.3)
    struct PlatformWindowFactoryState
    {
        // the installed factory, which is empty until a backend installs one (§5.3)
        PlatformWindowFactory factory;
    };

    // install the factory the framework builds native windows with, which a platform backend calls once at startup
    void set_platform_window_factory(PlatformWindowFactory factory);

    // hand out the installed factory, which is empty until a platform backend installs one
    [[nodiscard]] const PlatformWindowFactory& platform_window_factory();
} // namespace albor
