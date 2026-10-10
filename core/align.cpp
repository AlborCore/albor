#include "core/align.h"

namespace albor
{
    Align resolve_align(Align authored, Align axis_default) {
        // an authored alignment is what the author asked for, so it stands (§8.2)
        if (authored != Align::Inherit)
            return authored;
        // an unset alignment takes the default its container implies, which is what keeps the old layout (§8.2)
        return axis_default;
    }
} // namespace albor
