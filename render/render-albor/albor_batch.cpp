#include "render/render-albor/albor_batch.h"

#include <algorithm>

namespace albor::render
{
    std::uint32_t pack_color(const Color& color) {
        // every channel clamps into the unit range before the pack reads it, and a non finite one packs as zero
        const auto bounded = [](float value) -> float {
            // a channel that is not comparable falls back to the empty endpoint the clamp would pick
            return value != value ? 0.0f : std::clamp(value, 0.0f, 1.0f);
        };
        const float red = bounded(color.red);
        const float green = bounded(color.green);
        const float blue = bounded(color.blue);
        const float alpha = bounded(color.alpha);
        // the colour premultiplies by its own alpha, which is the convention every upload carries (§6.3)
        const auto channel = [](float value) -> std::uint32_t {
            // the scale rounds to the nearest byte the unit range maps to
            return static_cast<std::uint32_t>(value * 255.0f + 0.5f);
        };
        // the packed word carries red in the low byte and alpha in the high one
        return channel(red * alpha) | (channel(green * alpha) << 8) | (channel(blue * alpha) << 16) | (channel(alpha) << 24);
    }
} // namespace albor::render
