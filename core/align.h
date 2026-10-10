#pragma once

namespace albor
{
    // the closed set of positions a child takes inside the space its parent gives it (§8.2)
    enum class Align
    {
        // the parent decides, which is what an unset alignment carries and what preserves the previous layout (§8.2)
        Inherit,

        // the leading edge of the available space, which is the main axis default of every container (§8.2)
        Start,

        // the middle of the available space, which centres a child on its axis (§8.2)
        Center,

        // the trailing edge of the available space, which pushes a child to the far end (§8.2)
        End,

        // the whole available space, which is the cross axis default and the spacer idiom (§8.2)
        Stretch
    };

    // the two axes a child aligns on independently, which are screen axes rather than a main and cross pair (§8.2)
    enum class Axis
    {
        // the horizontal axis, which a row runs along and a column crosses (§8.2)
        Horizontal,

        // the vertical axis, which a column runs along and a row crosses (§8.2)
        Vertical
    };

    // resolve an alignment against the default its axis and container imply, which is what Inherit asks for (§8.2)
    [[nodiscard]] Align resolve_align(Align authored, Align axis_default);
} // namespace albor
