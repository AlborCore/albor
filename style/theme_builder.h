#pragma once

#include "core/color_scheme.h"
#include "style/theme.h"

namespace albor
{
    // the set of source colours a theme is derived from, which is what an application actually chooses (§6.1)
    struct Accents
    {
        // the surface the frame is drawn on, which every other value is read against (§6.1)
        Color surface{0.07f, 0.08f, 0.10f, 1.0f};

        // the text drawn on the surface, which is the value with the most contrast against it (§6.1)
        Color on_surface{0.92f, 0.94f, 0.96f, 1.0f};

        // the colour a control is filled with, which is where the interaction states come from (§6.1)
        Color accent{0.20f, 0.55f, 0.90f, 1.0f};

        // the colour a control is outlined with, which reads as a line rather than a plate (§6.1)
        Color outline{0.45f, 0.50f, 0.58f, 1.0f};
    };

    // build a theme whose states are derived from a few accents, rather than written out by hand (§6.1)
    class ThemeBuilder
    {
    public:
        // build from the accents, in the scheme that decides which way the states move (§6.1)
        [[nodiscard]] static Theme build(const Accents& accents, ColorScheme scheme);

        // hand out the light theme for a set of accents, which is the common case (§6.1)
        [[nodiscard]] static Theme light(const Accents& accents);

        // hand out the dark theme for a set of accents, which is the other common case (§6.1)
        [[nodiscard]] static Theme dark(const Accents& accents);

        // move a colour toward white by a fraction, which is what a hover does on a dark surface (§6.1)
        [[nodiscard]] static Color lighten(Color value, float amount);

        // move a colour toward black by a fraction, which is what a press does on a light surface (§6.1)
        [[nodiscard]] static Color darken(Color value, float amount);
    };
} // namespace albor
