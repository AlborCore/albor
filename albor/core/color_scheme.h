#pragma once

namespace albor
{
    // the light and dark schemes the operating system renders with and a theme is built for (§5.3, §8.4)
    enum class ColorScheme
    {
        // the scheme drawn on a light surface
        Light,

        // the scheme drawn on a dark surface
        Dark
    };
} // namespace albor
