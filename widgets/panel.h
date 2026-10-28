#pragma once

#include <string>

#include "widgets/widget.h"

namespace albor
{
    // the themed container that frames a section of a surface, which is the reusable card a settings page uses (§8)
    class Panel final : public Widget
    {
    public:
        // build an empty panel over the engine that settles its properties (§8)
        explicit Panel(BindingEngine* engine, std::string explicit_id = {});

        // override: measure the children stacked inside the padding, because a panel grows with its content (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the children top to bottom inside the padding (§8.2)
        void arrange(const Rect& bounds) override;

        // override: announce the panel as a group, which is the semantic shape of a card (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: a panel is a container, so the focus belongs to its content (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // replace the inset between the panel edge and its content (§8.2)
        void set_padding(float padding);

        // hand out the inset the panel applies (§8.2)
        [[nodiscard]] float padding() const;

        // replace the gap between two children (§8.2)
        void set_spacing(float spacing);

        // hand out the gap between two children (§8.2)
        [[nodiscard]] float spacing() const;

        // replace the panel face, which the theme resolves by state when one is installed (§6.1)
        void set_background(Color background);

        // override: hand out the panel face (§6.1)
        [[nodiscard]] Color background() const override;

        // replace the corner radius, where a negative value follows the theme (§6.2)
        void set_corner_radius(float radius);

        // override: hand out the corner radius the panel draws (§6.2)
        [[nodiscard]] float corner_radius() const override;

        // replace the border width, where a negative value follows the theme (§8.4)
        void set_border_width(float width);

        // replace the border colour, where the theme owns it until the author writes one (§8.4)
        void set_border_color(Color color);

        // override: hand out the panel border width, which the theme owns when installed (§8.4)
        [[nodiscard]] float border_width() const override;

        // override: hand out the panel border colour, which resolves by state (§8.4)
        [[nodiscard]] Color border_color() const override;

    private:
        // hand out the inset the theme or the explicit value decides (§8.2)
        [[nodiscard]] float resolved_padding() const;

        // the visual face of the panel, which the theme replaces by state when installed (§6.1)
        Color m_background{0.13f, 0.15f, 0.19f, 1.0f};

        // whether the author wrote a face, which wins over the theme (§6.1)
        bool m_background_set = false;

        // the inset the author set, where a negative value follows the theme (§8.2)
        float m_padding = -1.0f;

        // the gap between two children, where zero is the default (§8.2)
        float m_spacing = 0.0f;

        // the corner radius the author set, where a negative value follows the theme (§6.2)
        float m_radius = -1.0f;

        // the border width the author set, where a negative value follows the theme (§8.4)
        float m_border = -1.0f;

        // whether the author wrote a border colour, which wins over the theme (§8.4)
        bool m_border_color_set = false;
    };
} // namespace albor
