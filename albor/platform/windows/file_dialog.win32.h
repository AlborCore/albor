#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shobjidl.h>
// clang-format on

#include <string>
#include <vector>

#include "albor/application/file_dialog.h"

namespace albor::windows
{
    // the configuration a seam applied, which the platform suite asserts without showing a panel (§5.3)
    struct FileDialogConfiguration
    {
        // the title the caller wrote
        std::string title;

        // the filters, each spelled as "name:pattern"
        std::vector<std::string> filters;

        // the initial directory the caller wrote, as utf8
        std::string directory;

        // the proposed name a save panel carries
        std::string default_name;
    };

    // apply an open request to a dialog without running it, which the platform suite asserts on (§5.3)
    void configure_open_dialog(IFileDialog* dialog, const OpenFileOptions& options);

    // apply a save request to a dialog without running it, which the platform suite asserts on (§5.3)
    void configure_save_dialog(IFileDialog* dialog, const SaveFileOptions& options);

    // read the configuration the last seam applied, which the platform suite asserts on (§5.3)
    [[nodiscard]] const FileDialogConfiguration& last_file_dialog_configuration();
} // namespace albor::windows
