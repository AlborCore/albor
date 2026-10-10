#pragma once

#include "core/constraints.h"
#include "core/geometry.h"
#include "render/render_context.h"
#include "ui/ui_node.h"

namespace albor
{
    // the node that draws its own retained content through a render context, which a domain view subclasses (§6.4, AD-06)
    class CustomRenderNode : public UiNode
    {
    public:
        // build a node on the default intrinsic size (§6.4)
        CustomRenderNode() = default;

        CustomRenderNode(const CustomRenderNode&) = delete;
        CustomRenderNode& operator=(const CustomRenderNode&) = delete;
        CustomRenderNode(CustomRenderNode&&) = delete;
        CustomRenderNode& operator=(CustomRenderNode&&) = delete;
        ~CustomRenderNode() override = default;

        // draw the retained content of the node into the context, within the bounds the arrange pass gave it (§6.4, AD-06)
        virtual void render_content(RenderContext& context, const Rect& bounds) = 0;

        // override: measure to the intrinsic size clamped into the offered constraints (§6.4)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: record the retained content, which stays the whole role of the node (§6.4)
        void render(RenderContext& context) override;

        // replace the intrinsic size the node measures to, which drives its natural extent (§6.4)
        void set_intrinsic_size(Size size);

        // hand out the intrinsic size the node measures to (§6.4)
        [[nodiscard]] const Size& intrinsic_size() const;

    private:
        // the intrinsic size the node measures to when nothing constrains it (§6.4)
        Size m_intrinsic{100.0f, 100.0f};
    };
} // namespace albor
