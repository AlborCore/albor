#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "automation/automation_registry.h"

namespace albor::automation
{
    // the command a host allow-lists and a client calls, which one spelling serves both (§10.3)
    inline constexpr std::string_view SCREENSHOT_COMMAND = "capture.screenshot";

    // the format a client names when it wants the exact bytes rather than a picture (§6.3)
    inline constexpr std::string_view RAW_FORMAT = "raw";

    // the format a client names when it wants a picture a person opens, which is the default (§6.3)
    inline constexpr std::string_view PNG_FORMAT = "png";

    // one frame the host read from the active window, which is the bytes a capture transports (§6.3)
    struct CapturedFrame
    {
        // the reason the read produced no frame, which is empty when the bytes are usable (§6.3)
        std::string error;

        // the frame width in device pixels, which the header and the image both carry (§6.3, AD-27)
        int width = 0;

        // the frame height in device pixels (§6.3, AD-27)
        int height = 0;

        // the tightly packed premultiplied rgba8 bytes, which every backend reads in the same order (§6.3, AD-26)
        std::vector<std::uint8_t> pixels;
    };

    // the last frame the most recently bound window presented, which a capture flushes but never rebuilds (§10.3, §6.1)
    using CaptureSnapshot = std::function<CapturedFrame()>;

    // the commands that read a window, which a production build never links (§10.3, AD-10)
    void register_capture_commands(AutomationRegistry& registry, CaptureSnapshot snapshot);

    // the command names the capture commands register, which a host allow-lists by name (§10.3, AD-10)
    [[nodiscard]] std::vector<std::string> capture_command_names();
} // namespace albor::automation
