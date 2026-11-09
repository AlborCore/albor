#pragma once

#include <optional>

#include "albor/ui/ui_node.h"

namespace albor
{
    // the container that sizes or constrains its single child (§8.2)
    class SizedBoxLayout final : public UiNode
    {
    public:
        SizedBoxLayout() = default;

        // measure the child against the sizing constraints, which is the measure phase of §8.2
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // arrange the child inside the assigned bounds respecting its alignment, which is the arrange phase of §8.2
        void arrange(const Rect& bounds) override;

        // set the exact width of the box, which a width change remeasures (§8.2)
        void set_width(float width);

        // clear the exact width constraint, which an unconstrained width remeasures (§8.2)
        void clear_width();

        // hand out the exact width constraint if set (§8.2)
        [[nodiscard]] std::optional<float> width() const;

        // set the exact height of the box, which a height change remeasures (§8.2)
        void set_height(float height);

        // clear the exact height constraint, which an unconstrained height remeasures (§8.2)
        void clear_height();

        // hand out the exact height constraint if set (§8.2)
        [[nodiscard]] std::optional<float> height() const;

        // set the minimum width constraint, which a bound change remeasures (§8.2)
        void set_min_width(float min_width);

        // clear the minimum width constraint, which an unconstrained floor remeasures (§8.2)
        void clear_min_width();

        // hand out the minimum width constraint if set (§8.2)
        [[nodiscard]] std::optional<float> min_width() const;

        // set the minimum height constraint, which a bound change remeasures (§8.2)
        void set_min_height(float min_height);

        // clear the minimum height constraint, which an unconstrained floor remeasures (§8.2)
        void clear_min_height();

        // hand out the minimum height constraint if set (§8.2)
        [[nodiscard]] std::optional<float> min_height() const;

        // set the maximum width constraint, which a bound change remeasures (§8.2)
        void set_max_width(float max_width);

        // clear the maximum width constraint, which an unconstrained ceiling remeasures (§8.2)
        void clear_max_width();

        // hand out the maximum width constraint if set (§8.2)
        [[nodiscard]] std::optional<float> max_width() const;

        // set the maximum height constraint, which a bound change remeasures (§8.2)
        void set_max_height(float max_height);

        // clear the maximum height constraint, which an unconstrained ceiling remeasures (§8.2)
        void clear_max_height();

        // hand out the maximum height constraint if set (§8.2)
        [[nodiscard]] std::optional<float> max_height() const;

    private:
        // the exact width constraint if specified (§8.2)
        std::optional<float> m_width;

        // the exact height constraint if specified (§8.2)
        std::optional<float> m_height;

        // the floor width constraint if specified (§8.2)
        std::optional<float> m_min_width;

        // the floor height constraint if specified (§8.2)
        std::optional<float> m_min_height;

        // the ceiling width constraint if specified (§8.2)
        std::optional<float> m_max_width;

        // the ceiling height constraint if specified (§8.2)
        std::optional<float> m_max_height;
    };

    // clean alias for the sized box container (§8.2)
    using SizedBox = SizedBoxLayout;
} // namespace albor
