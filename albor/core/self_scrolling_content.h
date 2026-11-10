#pragma once

namespace albor
{
    // content that keeps its own viewport and offset, which a scroll container must not scroll again (§8.2)
    class SelfScrollingContent
    {
    public:
        virtual ~SelfScrollingContent() = default;

        // report whether this node scrolls its own vertical viewport, which a container has to respect (§8.2)
        [[nodiscard]] virtual bool owns_vertical_scroll() const = 0;
    };
} // namespace albor
