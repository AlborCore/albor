#pragma once

#include <cstdint>
#include <vector>

#include "render/render_context.h"

namespace albor
{
    // the bytes one finished frame was read back as; the read rules are in DEFECT-METAL-BACKEND-TEARDOWN (§6.3)
    struct Screenshot
    {
        // the width in pixels, which is the physical extent and not the logical one (§6.3, AD-27)
        int width = 0;

        // the height in pixels, which is the physical extent and not the logical one (§6.3, AD-27)
        int height = 0;

        // the pixels as tightly packed bytes, four per pixel in the canonical order (§6.3, AD-26)
        std::vector<std::uint8_t> pixels;

        // report whether the read produced anything, because an empty read is a refusal and not a black frame (§6.3)
        [[nodiscard]] bool empty() const;

        // compare two reads of the same frame and report the largest per channel difference (§6.3, AD-26)
        [[nodiscard]] int max_channel_difference(const Screenshot& other) const;

        // how many pixels differ by more than a tolerance, which a flat interior must keep at zero (§15, AD-26)
        [[nodiscard]] std::size_t band_count(const Screenshot& other, int tolerance) const;
    };

    // read a surface back into a screenshot, which is what a parity check and an automation capture both do (§6.3, AD-10)
    [[nodiscard]] Screenshot capture_surface(const RenderSurface& surface);
} // namespace albor
