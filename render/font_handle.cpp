#include "render/font_handle.h"

namespace albor
{
    FontHandle FontHandle::from_object(const void* object) {
        // wrap the typeface in a handle, which is the only form a shaped run names it by (§9.1)
        FontHandle handle;
        handle.m_object = object;
        return handle;
    }

    bool FontHandle::valid() const {
        // a handle with no typeface names nothing, so a renderer skips the run rather than guessing a font (§9.1)
        return m_object != nullptr;
    }

    const void* FontHandle::object() const {
        // the typeface leaves the handle as an opaque pointer, which no shaping type may name (§3.5, §9.1)
        return m_object;
    }
} // namespace albor
