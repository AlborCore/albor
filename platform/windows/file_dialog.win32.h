#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shobjidl.h>
// clang-format on

#include "application/file_dialog.h"

namespace albor::windows
{
    // apply an open request to a dialog without running it, which the platform suite asserts on (§5.3)
    void configure_open_dialog(IFileDialog* dialog, const OpenFileOptions& options);

    // apply a save request to a dialog without running it, which the platform suite asserts on (§5.3)
    void configure_save_dialog(IFileDialog* dialog, const SaveFileOptions& options);
} // namespace albor::windows
