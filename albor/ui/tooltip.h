#pragma once

#include "albor/core/geometry.h"
#include "albor/core/paint.h"

namespace albor
{
    // the appearance a hover tooltip takes, which a theme fills through the widget module (§8.3, §8.4)
    struct TooltipStyle
    {
        // the plate the bubble paints, which reads as a floating card (§6.1)
        Color background = Color{0.13f, 0.15f, 0.19f, 1.0f};

        // the hairline that separates the bubble from the surface it covers (§6.1)
        Color border = Color{0.27f, 0.30f, 0.36f, 1.0f};

        // the colour the run is set in, which reads over the plate (§8.4)
        Color text = Color{0.92f, 0.94f, 0.96f, 1.0f};

        // the cast that lifts the bubble off the window surface (§6.2)
        Shadow shadow{Color::rgba(0.0f, 0.0f, 0.0f, 0.45f), Point{.x = 0.0f, .y = 2.0f}, 6.0f};

        // how wide the plate strokes its outline, where zero draws none (§6.1)
        float border_width = 1.0f;

        // the corner the plate rounds at, which a small bubble keeps modest (§6.2)
        float corner_radius = 4.0f;

        // how far the run sits from the plate on either side, which frames the text (§8.2)
        float padding_horizontal = 9.0f;

        // how far the run sits from the plate above and below (§8.2)
        float padding_vertical = 5.0f;

        // the distance between the anchor and the bubble, which keeps the control readable (§8.3)
        float gap = 6.0f;

        // the inset the bubble keeps from the viewport edge when it clamps (§8.3)
        float margin = 4.0f;

        // the size the run is measured at, which is the caption size of a hierarchy (§8.4, §9.1)
        float font_size = 11.0f;

        // the widest the bubble grows before the run wraps, which keeps a long tip readable (§8.2)
        float max_width = 320.0f;

        // compare two styles component wise
        friend bool operator==(const TooltipStyle& lhs, const TooltipStyle& rhs) = default;
    };

    // place a tooltip bubble against its anchor, flipping above near the bottom and clamping inside the view (§8.3)
    [[nodiscard]] Rect place_tooltip(Rect anchor, Size size, Rect viewport, float gap, float margin);
} // namespace albor
