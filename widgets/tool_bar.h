#pragma once

#include <string>

#include "widgets/widget.h"

namespace albor
{
    // the horizontal strip that hosts controls or menu items, which is the container shape a shell puts at the top (§8)
    class ToolBar : public Widget
    {
    public:
        // build an empty strip over the engine that settles its properties (§8)
        explicit ToolBar(BindingEngine* engine, std::string explicit_id = {});

        // override: measure the visible children side by side inside the padding (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the visible children left to right inside the padding (§8.2)
        void arrange(const Rect& bounds) override;

        // override: a strip announces itself as a group, because it frames its controls (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: the focus belongs to the controls, because a strip is a container (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // replace the inset between the strip edge and its children (§8.2)
        void set_padding(float padding);

        // hand out the inset the strip applies (§8.2)
        [[nodiscard]] float padding() const;

        // replace the gap between two children (§8.2)
        void set_spacing(float spacing);

        // hand out the gap between two children (§8.2)
        [[nodiscard]] float spacing() const;

        // replace the strip face, which is the surface the controls sit on (§6.1)
        void set_background(Color background);

        // override: hand out the strip face (§6.1)
        [[nodiscard]] Color background() const override;

        // replace the corner radius, where a negative value keeps the hard edge (§6.2)
        void set_corner_radius(float radius);

        // override: hand out the radius the strip face draws (§6.2)
        [[nodiscard]] float corner_radius() const override;

    private:
        // the inset between the strip edge and its children (§8.2)
        float m_padding = 6.0f;

        // the gap between two children (§8.2)
        float m_spacing = 6.0f;

        // the strip surface, which the theme replaces by state when one is installed (§6.1)
        Color m_background{0.13f, 0.15f, 0.19f, 1.0f};

        // the radius the strip face draws, where a negative value is a hard rectangle (§6.2)
        float m_radius = 6.0f;
    };
} // namespace albor
