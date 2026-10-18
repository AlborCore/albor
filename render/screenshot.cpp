#include "render/screenshot.h"

#include <algorithm>
#include <cstdlib>

namespace albor
{
    bool Screenshot::empty() const {
        // a read with no bytes is a refusal, and an empty extent is the cheapest way to say one (§6.3)
        return pixels.empty() || width <= 0 || height <= 0;
    }

    int Screenshot::max_channel_difference(const Screenshot& other) const {
        // two reads of different extents cannot be compared channel by channel (§6.3, AD-26)
        if (width != other.width || height != other.height)
            return 256;
        // a read of different lengths is a truncated frame rather than a different picture (§6.3)
        if (pixels.size() != other.pixels.size())
            return 256;
        // the largest per channel difference is what a tolerance is written against (§6.3, AD-26)
        int worst = 0;
        // every byte is compared, because a frame is wrong anywhere (§6.3)
        for (std::size_t index = 0; index < pixels.size(); index++) {
            // the channels differ by at most the range of one byte, so the absolute value is the distance (§6.3)
            const int difference = std::abs(static_cast<int>(pixels[index]) - static_cast<int>(other.pixels[index]));
            // only a worse difference matters, so the worst is kept rather than summed (§6.3)
            worst = std::max(worst, difference);
        }
        // the worst channel distance is what the caller compares against its tolerance (§6.3, AD-26)
        return worst;
    }

    std::size_t Screenshot::band_count(const Screenshot& other, int tolerance) const {
        // two reads of different extents share no pixel to count, which the caller checks first (§6.3)
        if (width != other.width || height != other.height || pixels.size() != other.pixels.size())
            return pixels.size() / 4u;
        // every pixel is judged by its worst channel, which the tolerance bounds (§15, AD-26)
        std::size_t count = 0;
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
            // the worst channel of the pixel decides whether it sits outside the band (§15)
            int worst = 0;
            for (std::size_t channel = 0; channel < 4; channel++)
                worst = std::max(worst, std::abs(static_cast<int>(pixels[index + channel]) - static_cast<int>(other.pixels[index + channel])));
            // a pixel past the tolerance is one the band counts (§15)
            if (worst > tolerance)
                count++;
        }
        return count;
    }

    Screenshot capture_surface(const RenderSurface& surface) {
        // the read is asked of the surface, which refuses when it cannot (§6.3)
        const std::vector<std::uint8_t> pixels = surface.read_pixels();
        // a refusal is reported as an empty screenshot rather than a black frame (§6.3)
        if (pixels.empty())
            return Screenshot{};
        // the extent comes from the metrics, because the read is in physical pixels (§6.3, AD-27)
        const Size size = surface.metrics().pixel_size;
        // a read of no extent cannot be a frame, so it is refused (§6.3)
        if (size.width <= 0.0f || size.height <= 0.0f)
            return Screenshot{};
        // the screenshot carries the extent the comparison needs alongside the bytes (§6.3, AD-26)
        return Screenshot{.width = static_cast<int>(size.width), .height = static_cast<int>(size.height), .pixels = pixels};
    }
} // namespace albor
