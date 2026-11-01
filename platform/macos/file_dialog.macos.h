#pragma once

#import <AppKit/AppKit.h>

#include "application/file_dialog.h"

namespace albor::macos
{
    // apply an open request to a panel without running it, which the platform suite asserts on (§5.3)
    void configure_open_panel(NSOpenPanel* panel, const OpenFileOptions& options);

    // apply a save request to a panel without running it, which the platform suite asserts on (§5.3)
    void configure_save_panel(NSSavePanel* panel, const SaveFileOptions& options);
} // namespace albor::macos
