#include "render/custom_render_node.h"

namespace albor
{
    void CustomRenderNode::measure(const SizeConstraints& constraints, Size& out_size) {
        // the intrinsic size is what the node needs, which the offered constraints then clamp (§8.2)
        m_measured_size = constraints.constrain(m_intrinsic);
        out_size = m_measured_size;
    }

    void CustomRenderNode::render(RenderContext& context) {
        // an invisible node draws nothing, matching the scene builder's skip (§6.4)
        if (!visible())
            return;
        // a node the layout gave no area draws nothing, because it occupies no pixel (§6.4)
        if (m_bounds.empty())
            return;
        // the retained content is the node, so the base recursion is deliberately not called (§6.4, AD-06)
        render_content(context, m_bounds);
    }

    void CustomRenderNode::set_intrinsic_size(Size size) {
        // an unchanged size invalidates nothing (§6.4)
        if (size.width == m_intrinsic.width && size.height == m_intrinsic.height)
            return;
        m_intrinsic = size;
        // a new intrinsic size changes the layout, so the node asks for a measure pass (§6.4)
        invalidate_layout();
    }

    const Size& CustomRenderNode::intrinsic_size() const {
        // the intrinsic size is what the node measures to (§6.4)
        return m_intrinsic;
    }
} // namespace albor
