#pragma once

#include "core/color_scheme.h"
#include "core/paint.h"

namespace albor
{
    // the appearance the operating system renders with, which a theme follows (§5.3, §8.4)
    struct SystemAppearance
    {
        // the colour scheme the system drew its own controls in, which a theme mirrors (§5.3)
        ColorScheme scheme = ColorScheme::Dark;

        // the colour the system highlights controls with, which a theme takes as its accent (§5.3)
        Color accent{0.20f, 0.55f, 0.90f, 1.0f};

        // compare two appearances component wise, which a test asserts on
        friend bool operator==(const SystemAppearance& lhs, const SystemAppearance& rhs) = default;
    };
} // namespace albor
