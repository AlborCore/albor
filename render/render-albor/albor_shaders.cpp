#include "render/render-albor/albor_shaders.h"

namespace albor::render
{
    std::span<const std::byte> metal_library_data() {
        // a platform that builds no metal backend carries no library, which only that backend would ask for (§6.3, AD-16)
        return {};
    }
} // namespace albor::render
