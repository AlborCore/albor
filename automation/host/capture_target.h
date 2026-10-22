#pragma once

#include "automation/capture_commands.h"
#include "core/dev_hook.h"

namespace albor::automation
{
    // read the last frame a window presented, which is the one place a capture turns handles into bytes (§6.3, §10.3)
    [[nodiscard]] CapturedFrame capture_window(const dev::WindowBinding& binding);
} // namespace albor::automation
