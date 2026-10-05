#pragma once

#include <limits>

#include "core/geometry.h"

namespace albor
{
    // the bounds a parent offers a child while measuring it, which is the two pass layout contract (§8.2)
    struct SizeConstraints
    {
        // the smallest extent the child may take
        Size minimum;

        // the extent the child would take if nothing constrained it
        Size preferred;

        // the largest extent the child may take, which infinity leaves unbounded
        Size maximum{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};

        // build constraints that leave the child entirely free
        [[nodiscard]] static SizeConstraints unconstrained();

        // build constraints that force one exact extent, which a fixed size container passes down (§8.2)
        [[nodiscard]] static SizeConstraints tight(Size size);

        // build constraints that allow any extent up to a ceiling
        [[nodiscard]] static SizeConstraints at_most(Size maximum);

        // report whether an extent satisfies the constraints, which a test asserts on (§8.2)
        [[nodiscard]] bool allows(Size size) const;

        // bring an extent inside the constraints, which is what a layout does when it cannot honour one (§8.2)
        [[nodiscard]] Size constrain(Size size) const;

        // compare two constraint sets field by field
        friend bool operator==(const SizeConstraints& lhs, const SizeConstraints& rhs) = default;
    };
} // namespace albor
