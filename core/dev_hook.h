#pragma once

#include <string>

namespace albor::dev
{
    // what a reload asks the runtime to do, which is to take a new component definition (§12.1, AD-02)
    using ReloadFunction = void (*)(const std::string& ir);

    // install the dev extension's entry point, which a Dev configuration does once at startup (§12.1, AD-02)
    void install(ReloadFunction reload);

    // report whether an extension installed itself, which every call site checks before calling (§12.1, AD-02)
    [[nodiscard]] bool present();

    // hand a serialized component definition to the extension, doing nothing when none is installed (§12.1, AD-02)
    void reload(const std::string& ir);
} // namespace albor::dev
