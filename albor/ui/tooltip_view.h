#pragma once

#include <string>

#include "albor/render/text_layout.h"
#include "albor/ui/component.h"
#include "albor/ui/tooltip.h"

namespace albor
{
    // the bubble a tooltip controller presents, which paints a shaped run over a rounded plate (§8.3)
    class TooltipView final : public Component
    {
    public:
        // build a bubble from the text it presents and the run the controller shaped for it (§8.3)
        TooltipView(std::string text, TextLayout layout, TooltipStyle style = {});

        // override: report the extent the bubble needs, which the controller places (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: take the rect the controller assigned, which the rich path arranges (§8.2)
        void arrange(const Rect& bounds) override;

        // override: a tooltip never takes a pointer, because it would keep itself open (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: paint the run inside the plate, at the padding the style names (§6.1)
        void paint_content(RenderContext& context) override;

        // override: the plate is the tooltip face, which a theme fills (§6.1, §8.4)
        [[nodiscard]] Color background() const override;

        // override: the hairline separates the bubble from the surface it floats over (§6.1)
        [[nodiscard]] float border_width() const override;

        // override: the hairline colour, which the style carries (§6.1)
        [[nodiscard]] Color border_color() const override;

        // override: the corner the bubble rounds at, which the style names (§6.2)
        [[nodiscard]] float corner_radius() const override;

        // replace the run and its text, which a later target supplies (§8.3)
        void set_content(std::string text, TextLayout layout);

        // hand out the text the bubble presents, which a test and a reader assert on (§8.3, §10.1)
        [[nodiscard]] const std::string& text() const;

        // hand out the run the bubble presents (§8.3)
        [[nodiscard]] const TextLayout& layout() const;

        // replace the style, which a theme install applies before the bubble shows (§8.4)
        void set_style(TooltipStyle style);

        // hand out the style the bubble paints through (§8.4)
        [[nodiscard]] const TooltipStyle& style() const;

        // place the bubble against the anchor inside the viewport, reporting the rect it took (§8.3)
        Rect place(Rect anchor, Rect viewport);

        // hand out the extent the bubble needs for the run it carries (§8.2)
        [[nodiscard]] Size bubble_size() const;

    private:
        // the text the bubble presents, which the controller read from the target (§8.3)
        std::string m_text;

        // the run the controller shaped for the text (§9.1)
        TextLayout m_layout;

        // the appearance the bubble paints through (§8.4)
        TooltipStyle m_style;
    };
} // namespace albor
