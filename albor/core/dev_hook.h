#pragma once

#include <string>

namespace albor::dev
{
    // what a reload asks the runtime to do, which is to take a new component definition (§12.1, AD-02)
    using ReloadFunction = void (*)(const std::string& ir);

    // the handles a window lends an extension, which are opaque because the core names no ui or render type (§12.1, AD-05)
    struct WindowBinding
    {
        // the tree the window drives, which is what a reload patches (§8.1)
        void* root = nullptr;

        // the reactive engine that binds that tree (§7)
        void* engine = nullptr;

        // the text engine that shapes its runs (§9.1)
        void* text = nullptr;

        // the render surface a capture reads, which an automation command needs (§6.3, AD-10)
        void* surface = nullptr;

        // the executor that must settle first, because read_pixels() refuses a frame in flight (§6.3)
        void* executor = nullptr;

        // whether the tree is the window's own, because a hand drawn window carries none a reload could patch (§6.1)
        bool has_tree = false;
    };

    // what an extension does with the tree a window drives, which is opaque because the core names no ui type (§12.1)
    using RootBindFunction = void (*)(const WindowBinding& binding);

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

    // hand a live tree and its handles to every extension, all borrowed on the ui thread until unbind (§12.1, AD-02)
    void bind_root(const WindowBinding& binding);

    // tell every installed extension that a tree went away, doing nothing when none holds it (§12.1, §5.2)
    void unbind_root(void* root);
} // namespace albor::dev
