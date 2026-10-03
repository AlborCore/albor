#pragma once

#include <cstddef>
#include <memory>

#include "prototypes/a1/resource_factory.h"

namespace albor::a1
{

    // the pan and zoom of a custom render node, which is the state a retained resource set is viewed through (§11.1)
    struct ViewTransform
    {
        float scale = 1.0F; // the zoom factor applied to resource space
        float offset_x = 0.0F; // the horizontal pan, in window pixels
        float offset_y = 0.0F; // the vertical pan, in window pixels

        // map a point from resource space into window space
        [[nodiscard]] float map_x(float x) const { return x * scale + offset_x; }

        [[nodiscard]] float map_y(float y) const { return y * scale + offset_y; }

        // map a point from window space back into resource space, which is what a hit query needs
        [[nodiscard]] float unmap_x(float x) const { return (x - offset_x) / scale; }

        [[nodiscard]] float unmap_y(float y) const { return (y - offset_y) / scale; }
    };

    // one pointer position the ui thread asks about, which is the custom render hit query of §12.3
    struct HitQuery
    {
        float x = 0.0F; // the pointer position in window pixels
        float y = 0.0F;
        float tolerance = 4.0F; // how far from a symbol a click still hits it, in window pixels
    };

    // what a hit query answered, which is the same shape the automation layer reads (§19.4)
    struct HitResult
    {
        bool hit = false;
        std::size_t symbol_index = 0; // the index of the nearest symbol that was within tolerance
        float distance = 0.0F; // how far the pointer was from that symbol, in window pixels
    };

    // the custom render node of §11.1: retained resources, a view transform and a hit test, no application code (§10.2)
    class CustomRenderNode
    {
    public:
        // take ownership of an immutable retained resource set
        explicit CustomRenderNode(ResourceSetPtr resources);

        // move the view without touching the retained resources, which is what makes a pan o(1)
        void set_view(const ViewTransform& view) { m_view = view; }

        [[nodiscard]] const ViewTransform& view() const { return m_view; }

        // replace the retained resources with a newer snapshot, which is how an append stream reaches the gpu (§10.5)
        void set_resources(ResourceSetPtr resources) { m_resources = std::move(resources); }

        [[nodiscard]] const ResourceSetPtr& resources() const { return m_resources; }

        // find the symbol under the pointer, which runs on the ui thread during pan and zoom (§12.3)
        [[nodiscard]] HitResult hit_test(const HitQuery& query) const;

    private:
        ResourceSetPtr m_resources;
        ViewTransform m_view;
    };

} // namespace albor::a1
