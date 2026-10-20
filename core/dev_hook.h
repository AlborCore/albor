#pragma once

#include <string>

namespace albor::dev
{
    // what a reload asks the runtime to do, which is to take a new component definition (§12.1, AD-02)
    using ReloadFunction = void (*)(const std::string& ir);

    // what an extension does with the tree a window drives, which is opaque because the core names no ui type (§12.1)
    using RootBindFunction = void (*)(void* root, void* engine, void* text);

    // what an extension does when that tree goes away, named so only that window's binding is released (§5.2)
    using RootUnbindFunction = void (*)(void* root);

    // install the dev extension's entry point, which a Dev configuration does once at startup (§12.1, AD-02)
    void install(ReloadFunction reload);

    // report whether an extension installed itself, which every call site checks before calling (§12.1, AD-02)
    [[nodiscard]] bool present();

    // hand a serialized component definition to the extension, doing nothing when none is installed (§12.1, AD-02)
    void reload(const std::string& ir);

    // replace every binding with one pair (§12.1)
    void install_root_binder(RootBindFunction bind, RootUnbindFunction unbind);

    // append one binding (§12.1, AD-02)
    [[nodiscard]] bool add_root_binding(RootBindFunction bind, RootUnbindFunction unbind);

    // report whether any extension installed a binder, which every call site checks before calling (§12.1, AD-02)
    [[nodiscard]] bool binder_present();

    // hand a live tree and its engines to every installed extension, doing nothing when none is (§12.1, AD-02)
    void bind_root(void* root, void* engine, void* text);

    // tell every installed extension that a tree went away, doing nothing when none holds it (§12.1, §5.2)
    void unbind_root(void* root);
} // namespace albor::dev
