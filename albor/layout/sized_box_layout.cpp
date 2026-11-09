#include "albor/layout/sized_box_layout.h"

#include <algorithm>

#include "albor/core/align.h"

namespace albor
{
    void SizedBoxLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // a box with no visible child has only its own dimensions to measure (§11.1, §18.2)
        if (children().empty() || !children().front()->visible()) {
            // the width resolves from the exact constraint, the floor, or zero (§8.2)
            float empty_width = m_width.value_or(m_min_width.value_or(0.0f));
            // the maximum width caps the empty width when specified (§8.2)
            if (!m_width.has_value() && m_max_width.has_value())
                empty_width = std::min(empty_width, *m_max_width);
            // the height resolves from the exact constraint, the floor, or zero (§8.2)
            float empty_height = m_height.value_or(m_min_height.value_or(0.0f));
            // the maximum height caps the empty height when specified (§8.2)
            if (!m_height.has_value() && m_max_height.has_value())
                empty_height = std::min(empty_height, *m_max_height);
            // the empty extent is clamped into the offered constraints, because a parent decision is final (§8.2)
            m_measured_size = constraints.constrain(Size{.width = empty_width, .height = empty_height});
            // the measured size is the final size of the box when it has no visible child (§8.2)
            out_size = m_measured_size;
            // exit
            return;
        }
        // the constraints offered to the child derive from the parent constraints and the box bounds (§8.2)
        SizeConstraints inner = constraints;
        // the child alignment decides whether exact bounds force a minimum extent (§8.2)
        const UiNode* child_node = children().front().get();
        // the horizontal alignment resolves against the stretch default (§8.2)
        const Align horizontal = resolve_align(child_node->horizontal_align(), Align::Stretch);
        // the vertical alignment resolves against the stretch default (§8.2)
        const Align vertical = resolve_align(child_node->vertical_align(), Align::Stretch);
        // an exact width forces the maximum ceiling, and forces the floor only when stretching (§8.2)
        if (m_width.has_value()) {
            // the maximum width follows the exact constraint (§8.2)
            inner.maximum.width = *m_width;
            // a stretching child is forced to the exact floor, while an aligned child measures naturally (§8.2)
            if (horizontal == Align::Stretch)
                inner.minimum.width = *m_width;
            // the preferred width follows the exact constraint (§8.2)
            inner.preferred.width = *m_width;
        }
        else {
            // the floor lifts the minimum width offered to the child (§8.2)
            if (m_min_width.has_value())
                inner.minimum.width = std::max(inner.minimum.width, *m_min_width);
            // the ceiling caps the maximum width offered to the child (§8.2)
            if (m_max_width.has_value())
                inner.maximum.width = std::min(inner.maximum.width, *m_max_width);
        }
        // the minimum width cannot exceed the maximum ceiling (§8.2)
        inner.minimum.width = std::min(inner.minimum.width, inner.maximum.width);
        // an exact height forces the maximum ceiling, and forces the floor only when stretching (§8.2)
        if (m_height.has_value()) {
            // the maximum height follows the exact constraint (§8.2)
            inner.maximum.height = *m_height;
            // a stretching child is forced to the exact floor, while an aligned child measures naturally (§8.2)
            if (vertical == Align::Stretch)
                inner.minimum.height = *m_height;
            // the preferred height follows the exact constraint (§8.2)
            inner.preferred.height = *m_height;
        }
        else {
            // the floor lifts the minimum height offered to the child (§8.2)
            if (m_min_height.has_value())
                inner.minimum.height = std::max(inner.minimum.height, *m_min_height);
            // the ceiling caps the maximum height offered to the child (§8.2)
            if (m_max_height.has_value())
                inner.maximum.height = std::min(inner.maximum.height, *m_max_height);
        }
        // the minimum height cannot exceed the maximum ceiling (§8.2)
        inner.minimum.height = std::min(inner.minimum.height, inner.maximum.height);
        // the child measures against the constrained inner bounds (§8.2)
        Size child_size{};
        children().front()->measure(inner, child_size);
        // the target width takes the exact constraint or the measured child extent (§8.2)
        float target_width = m_width.has_value() ? *m_width : child_size.width;
        // the floor enforces the minimum width when no exact width is specified (§8.2)
        if (!m_width.has_value() && m_min_width.has_value())
            target_width = std::max(target_width, *m_min_width);
        // the ceiling enforces the maximum width when no exact width is specified (§8.2)
        if (!m_width.has_value() && m_max_width.has_value())
            target_width = std::min(target_width, *m_max_width);
        // the target height takes the exact constraint or the measured child extent (§8.2)
        float target_height = m_height.has_value() ? *m_height : child_size.height;
        // the floor enforces the minimum height when no exact height is specified (§8.2)
        if (!m_height.has_value() && m_min_height.has_value())
            target_height = std::max(target_height, *m_min_height);
        // the ceiling enforces the maximum height when no exact height is specified (§8.2)
        if (!m_height.has_value() && m_max_height.has_value())
            target_height = std::min(target_height, *m_max_height);
        // the target extent is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(Size{.width = target_width, .height = target_height});
        out_size = m_measured_size;
    }

    void SizedBoxLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, while the measured size stands as the measure settled it (§8.2)
        m_bounds = bounds;
        // a wrapper with no visible child arranges nothing, because there is no child to place (§11.1, §18.2)
        UiNode* child = child_at(0);
        if (child == nullptr || !child->visible())
            return;
        // the horizontal alignment is resolved against the stretch default (§8.2)
        const Align horizontal = resolve_align(child->horizontal_align(), Align::Stretch);
        // the vertical alignment is resolved against the stretch default (§8.2)
        const Align vertical = resolve_align(child->vertical_align(), Align::Stretch);
        // the width the child asked for is capped at the bounds, which keeps the child inside the box (§8.2)
        const float child_width = std::min(child->measured_size().width, bounds.width);
        // the horizontal origin starts at the leading edge (§8.2)
        float child_x = bounds.x;
        // a centred child sits half the leftover in, which is centring on the horizontal axis (§8.2)
        if (horizontal == Align::Center)
            child_x += (bounds.width - child_width) * 0.5f;
        // an end aligned child sits the whole leftover in, which pushes it to the right edge (§8.2)
        else if (horizontal == Align::End)
            child_x += bounds.width - child_width;
        // the height the child asked for is capped at the bounds, which keeps the child inside the box (§8.2)
        const float child_height = std::min(child->measured_size().height, bounds.height);
        // the vertical origin starts at the top edge (§8.2)
        float child_y = bounds.y;
        // a vertically centred child sits half the leftover in, which is centring on the vertical axis (§8.2)
        if (vertical == Align::Center)
            child_y += (bounds.height - child_height) * 0.5f;
        // a vertically end aligned child sits the whole leftover in, which pushes it to the bottom edge (§8.2)
        else if (vertical == Align::End)
            child_y += bounds.height - child_height;
        // a stretching child takes the full bounds width, while another alignment keeps what it measured (§8.2)
        const float width = horizontal == Align::Stretch ? bounds.width : child_width;
        // a stretching child takes the full bounds height, while another alignment keeps what it measured (§8.2)
        const float height = vertical == Align::Stretch ? bounds.height : child_height;
        // the child receives the arranged rectangle, which respects its alignments (§8.2)
        arrange_if_changed(*child, Rect::from_origin_size(Point{.x = child_x, .y = child_y}, Size{.width = width, .height = height}));
    }

    void SizedBoxLayout::set_width(float width) {
        // an unchanged width remeasures nothing, because the layout did not change (§8.2)
        if (m_width == width)
            return;
        // the exact width is recorded before the layout is dirtied (§8.2)
        m_width = width;
        // a width change resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    void SizedBoxLayout::clear_width() {
        // clearing an already empty width remeasures nothing (§8.2)
        if (!m_width.has_value())
            return;
        // the exact width constraint is reset (§8.2)
        m_width.reset();
        // an unconstrained width resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    std::optional<float> SizedBoxLayout::width() const {
        // the width is the exact constraint the last measure pass accounted for (§8.2)
        return m_width;
    }

    void SizedBoxLayout::set_height(float height) {
        // an unchanged height remeasures nothing, because the layout did not change (§8.2)
        if (m_height == height)
            return;
        // the exact height is recorded before the layout is dirtied (§8.2)
        m_height = height;
        // a height change resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    void SizedBoxLayout::clear_height() {
        // clearing an already empty height remeasures nothing (§8.2)
        if (!m_height.has_value())
            return;
        // the exact height constraint is reset (§8.2)
        m_height.reset();
        // an unconstrained height resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    std::optional<float> SizedBoxLayout::height() const {
        // the height is the exact constraint the last measure pass accounted for (§8.2)
        return m_height;
    }

    void SizedBoxLayout::set_min_width(float min_width) {
        // an unchanged minimum width remeasures nothing (§8.2)
        if (m_min_width == min_width)
            return;
        // the floor width is recorded before the layout is dirtied (§8.2)
        m_min_width = min_width;
        // a bound change resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    void SizedBoxLayout::clear_min_width() {
        // clearing an already empty floor remeasures nothing (§8.2)
        if (!m_min_width.has_value())
            return;
        // the minimum width constraint is reset (§8.2)
        m_min_width.reset();
        // an unconstrained floor resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    std::optional<float> SizedBoxLayout::min_width() const {
        // the minimum width is the floor the last measure pass accounted for (§8.2)
        return m_min_width;
    }

    void SizedBoxLayout::set_min_height(float min_height) {
        // an unchanged minimum height remeasures nothing (§8.2)
        if (m_min_height == min_height)
            return;
        // the floor height is recorded before the layout is dirtied (§8.2)
        m_min_height = min_height;
        // a bound change resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    void SizedBoxLayout::clear_min_height() {
        // clearing an already empty floor remeasures nothing (§8.2)
        if (!m_min_height.has_value())
            return;
        // the minimum height constraint is reset (§8.2)
        m_min_height.reset();
        // an unconstrained floor resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    std::optional<float> SizedBoxLayout::min_height() const {
        // the minimum height is the floor the last measure pass accounted for (§8.2)
        return m_min_height;
    }

    void SizedBoxLayout::set_max_width(float max_width) {
        // an unchanged maximum width remeasures nothing (§8.2)
        if (m_max_width == max_width)
            return;
        // the ceiling width is recorded before the layout is dirtied (§8.2)
        m_max_width = max_width;
        // a bound change resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    void SizedBoxLayout::clear_max_width() {
        // clearing an already empty ceiling remeasures nothing (§8.2)
        if (!m_max_width.has_value())
            return;
        // the maximum width constraint is reset (§8.2)
        m_max_width.reset();
        // an unconstrained ceiling resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    std::optional<float> SizedBoxLayout::max_width() const {
        // the maximum width is the ceiling the last measure pass accounted for (§8.2)
        return m_max_width;
    }

    void SizedBoxLayout::set_max_height(float max_height) {
        // an unchanged maximum height remeasures nothing (§8.2)
        if (m_max_height == max_height)
            return;
        // the ceiling height is recorded before the layout is dirtied (§8.2)
        m_max_height = max_height;
        // a bound change resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    void SizedBoxLayout::clear_max_height() {
        // clearing an already empty ceiling remeasures nothing (§8.2)
        if (!m_max_height.has_value())
            return;
        // the maximum height constraint is reset (§8.2)
        m_max_height.reset();
        // an unconstrained ceiling resizes the box, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    std::optional<float> SizedBoxLayout::max_height() const {
        // the maximum height is the ceiling the last measure pass accounted for (§8.2)
        return m_max_height;
    }
} // namespace albor
