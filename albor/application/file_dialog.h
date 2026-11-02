#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "albor/core/native_window_handle.h"
#include "albor/window/window_handle.h"

namespace albor
{
    // one filter a panel offers, where the extensions are bare and an empty list means any file (§5.3)
    struct FileFilter
    {
        // the label the panel shows on Windows; macOS unions the extensions and has no per-filter label (§5.3)
        std::string name;

        // the extensions the filter accepts, without a dot
        std::vector<std::string> extensions;
    };

    // the description of an open panel, which is what the platform turns into a native dialog on the ui thread (§5.3)
    struct OpenFileOptions
    {
        // the window the panel is owned by, which is invalid for an unowned panel (§5.1)
        WindowHandle parent{};

        // the panel title, which the window manager shows
        std::string title;

        // the filters the panel offers, in the order the author wrote them
        std::vector<FileFilter> filters;

        // the directory the panel opens at, as utf8, where empty means the system default
        std::string initial_directory;
    };

    // the description of a save panel, which is what the platform turns into a native dialog on the ui thread (§5.3)
    struct SaveFileOptions
    {
        // the window the panel is owned by, which is invalid for an unowned panel (§5.1)
        WindowHandle parent{};

        // the panel title, which the window manager shows
        std::string title;

        // the filters the panel offers, in the order the author wrote them
        std::vector<FileFilter> filters;

        // the directory the panel opens at, as utf8, where empty means the system default
        std::string initial_directory;

        // the name the panel proposes, which the user may replace
        std::string default_name;
    };

    // the platform half of a native file dialog, which every backend implements (§5.3, AD-16)
    class PlatformFileDialog
    {
    public:
        virtual ~PlatformFileDialog() = default;

        // run an open panel on the ui thread and hand out the chosen path, where nullopt means the user cancelled (§5.3)
        [[nodiscard]] virtual std::optional<std::filesystem::path> open(const OpenFileOptions& options, NativeWindowHandle parent) = 0;

        // run a save panel on the ui thread and hand out the chosen path, where nullopt means the user cancelled (§5.3)
        [[nodiscard]] virtual std::optional<std::filesystem::path> save(const SaveFileOptions& options, NativeWindowHandle parent) = 0;
    };

    // the signature every native file dialog backend implements to build its panel (§5.3, AD-16)
    using PlatformFileDialogFactory = std::function<std::unique_ptr<PlatformFileDialog>()>;

    // the state the installed factory lives in, heap held behind a constant initialized pointer (§5.3)
    struct PlatformFileDialogFactoryState
    {
        // the installed factory, which is empty until a backend installs one (§5.3)
        PlatformFileDialogFactory factory;
    };

    // install the factory the framework builds native file dialogs with, which a platform backend calls at startup
    void set_platform_file_dialog_factory(PlatformFileDialogFactory factory);

    // hand out the installed factory, which is empty until a backend installs one
    [[nodiscard]] const PlatformFileDialogFactory& platform_file_dialog_factory();
} // namespace albor
