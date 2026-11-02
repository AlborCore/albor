#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace albor::resources
{
    // the straight rgba pixels one png decodes to, which the registry premultiplies before it becomes a resource (§6.4)
    struct DecodedPixels
    {
        // the width in pixels, which libpng carries in the header (§6.4)
        std::size_t width = 0;

        // the height in pixels, which libpng carries in the header (§6.4)
        std::size_t height = 0;

        // four bytes per pixel in straight row order, which png stores before premultiplication (§6.4)
        std::vector<std::uint8_t> rgba{};
    };

    // decode a png from memory, which carries palette, transparency, sixteen bit depth and interlace through libpng (§6.4)
    [[nodiscard]] std::optional<DecodedPixels> decode_png(std::span<const std::uint8_t> encoded);
} // namespace albor::resources
