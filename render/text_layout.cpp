#include "render/text_layout.h"

namespace albor
{
    bool TextLayout::empty() const {
        // a run with no glyphs costs nothing to draw, whether it carries no shaped run or an empty one (§6.2)
        return !run || run->empty();
    }
} // namespace albor
