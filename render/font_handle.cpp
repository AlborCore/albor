#include "render/font_handle.h"

#include <utility>

#include "render/font_face.h"

namespace albor
{
    FontHandle FontHandle::from_face(std::shared_ptr<const FontFace> face) {
        // wrap the font in a handle, which is the only form a shaped run names it by (§9.1, AD-34)
        FontHandle handle;
        handle.m_face = std::move(face);
        return handle;
    }

    bool FontHandle::valid() const {
        // a handle no engine filled names nothing, so a renderer skips the run rather than guessing (§9.1)
        return m_face != nullptr;
    }

    const std::shared_ptr<const FontFace>& FontHandle::face() const {
        // the font leaves the handle with no shaping type named, which a backend outlines through (§3.5)
        return m_face;
    }

    std::uint64_t FontHandle::font_hash() const {
        // a handle that carries no font has no content identity, which a caller reports as zero (§15, AD-34)
        return m_face != nullptr ? m_face->hash() : 0u;
    }
} // namespace albor
