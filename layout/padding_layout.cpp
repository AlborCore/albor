#include "layout/padding_layout.h"

#include <algorithm>

namespace albor
{
    bool EdgeInsets::empty() const {
        // an inset set that consumes nothing on every edge is the unconfigured one (§8.2)
        return top == 0.0f && right == 0.0f && bottom == 0.0f && left == 0.0f;
    }

    void PaddingLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // a wrapper with no child has only its insets to measure, so the result is the insets themselves (§8.2)
        if (children().empty()) {
            // the insets alone are the smallest extent the wrapper can have (§8.2)
            const Size insets_only{.width = m_padding.left + m_padding.right, .height = m_padding.top + m_padding.bottom};
            // the insets are clamped like any other result, because a parent decision is final (§8.2)
            m_measured_size = constraints.constrain(insets_only);
            out_size = m_measured_size;
            return;
        }
        // the child only sees the space the insets leave on each axis, because it must not overflow the wrapper (§8.2)
        SizeConstraints inner;
        // the minimum shrinks because a smaller child is what the insets allow (§8.2)
        inner.minimum = Size{.width = std::max(0.0f, constraints.minimum.width - m_padding.left - m_padding.right), .height = std::max(0.0f, constraints.minimum.height - m_padding.top - m_padding.bottom)};
        // the preferred shrinks because the child would otherwise overflow the wrapper (§8.2)
        inner.preferred = Size{.width = std::max(0.0f, constraints.preferred.width - m_padding.left - m_padding.right), .height = std::max(0.0f, constraints.preferred.height - m_padding.top - m_padding.bottom)};
        // the maximum shrinks for the same reason, and an infinite ceiling stays infinite (§8.2)
        inner.maximum = Size{.width = std::max(0.0f, constraints.maximum.width - m_padding.left - m_padding.right), .height = std::max(0.0f, constraints.maximum.height - m_padding.top - m_padding.bottom)};
        // the child measures against the shrunk constraints, which is the measure phase of §8.2
        Size child_size{};
        children().front()->measure(inner, child_size);
        // the insets are added back, because the wrapper is the child plus the space around it (§8.2)
        const Size total{.width = child_size.width + m_padding.left + m_padding.right, .height = child_size.height + m_padding.top + m_padding.bottom};
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void PaddingLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // a wrapper with no child arranges nothing, because there is no child to inset (§8.2)
        UiNode* child = child_at(0);
        if (child == nullptr)
            return;
        // an inset wider than the wrapper would push the child outside it, so each edge is capped at the extent (§8.2)
        const float left = std::min(m_padding.left, bounds.width);
        const float top = std::min(m_padding.top, bounds.height);
        // the trailing edges are capped at what the leading ones left, which keeps the child non negative (§8.2)
        const float right = std::min(m_padding.right, std::max(0.0f, bounds.width - left));
        const float bottom = std::min(m_padding.bottom, std::max(0.0f, bounds.height - top));
        // the child fills whatever the insets leave, which is the arrange phase of §8.2
        child->arrange(Rect::from_origin_size(Point{.x = bounds.x + left, .y = bounds.y + top}, Size{.width = bounds.width - left - right, .height = bounds.height - top - bottom}));
    }

    void PaddingLayout::set_padding(float uniform) {
        // the same inset on all four edges is the uniform form of the per edge setter (§8.2)
        set_padding(uniform, uniform, uniform, uniform);
    }

    void PaddingLayout::set_padding(float top, float right, float bottom, float left) {
        // an unchanged inset set remeasures nothing, because the layout did not change (§8.2)
        if (m_padding == EdgeInsets{.top = top, .right = right, .bottom = bottom, .left = left})
            return;
        // the insets are stored first, so the invalidation walks a wrapper that already knows its new spacing (§8.2)
        m_padding = EdgeInsets{.top = top, .right = right, .bottom = bottom, .left = left};
        // an inset change resizes the child, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    const EdgeInsets& PaddingLayout::padding() const {
        // the insets are the ones the last measure pass accounted for (§8.2)
        return m_padding;
    }
} // namespace albor
