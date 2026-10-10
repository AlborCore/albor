#include "core/constraints.h"

namespace albor
{
    SizeConstraints SizeConstraints::unconstrained() {
        // the constraints allow anything, which is what a root node measures its children under (§8.2)
        return SizeConstraints{};
    }

    SizeConstraints SizeConstraints::tight(Size size) {
        // minimum and maximum agree, so the child receives exactly one extent (§8.2)
        return {.minimum = size, .preferred = size, .maximum = size};
    }

    SizeConstraints SizeConstraints::at_most(Size maximum) {
        // the preferred extent stays the unbounded one, because only the ceiling is known (§8.2)
        return {.maximum = maximum};
    }

    bool SizeConstraints::allows(Size size) const {
        // both axes are checked, because a child may be too wide even when its height fits (§8.2)
        if (size.width < minimum.width || size.height < minimum.height)
            return false;
        // an infinite maximum never refuses, which is what makes the unconstrained case permissive (§8.2)
        return size.width <= maximum.width && size.height <= maximum.height;
    }

    Size SizeConstraints::constrain(Size size) const {
        // a width below the minimum is raised, and a width above the maximum is lowered (§8.2)
        const float width = size.width < minimum.width ? minimum.width : (size.width > maximum.width ? maximum.width : size.width);
        // the height goes through the same clamp, so one axis never leaks past the bounds (§8.2)
        const float height = size.height < minimum.height ? minimum.height : (size.height > maximum.height ? maximum.height : size.height);
        // the clamped extent is returned whole, so one axis never leaks past the bounds it was given (§8.2)
        return Size{.width = width, .height = height};
    }
} // namespace albor
