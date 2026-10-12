#pragma once

#include "core/geometry.h"

namespace albor
{
    // the window a scroll container is showing, which is what content materializes against (§8.2)
    struct ScrollViewport
    {
        // the extent of the visible area, which is what the content is fitted to (§8.2)
        Size viewport;

        // the extent of the whole content, which is what the scroll offset ranges over (§8.2)
        Size content;

        // how far the content is shifted from its leading edge, which the container owns (§8.2)
        float offset = 0.0f;

        // how far beyond the visible area content is kept built, which is what hides a rebuild at the edge (§8.2)
        float overscan = 0.0f;
    };

    // the content a scroll container drives, which only content that builds itself on demand implements (§8.2, AD-13)
    class ScrollableContent
    {
    public:
        virtual ~ScrollableContent() = default;

        // report the extent the content would take if all of it were built (§8.2)
        [[nodiscard]] virtual Size content_extent() const = 0;

        // take the window the container is showing, which is what a virtualizing content builds against (§8.2)
        virtual void set_scroll_viewport(const ScrollViewport& viewport) = 0;

        // take the height one row takes, which is what turns an item index into a position (§8.2, AD-13)
        virtual void set_row_extent(float row_extent) = 0;
    };
} // namespace albor
