#pragma once

#include <cstdint>

#include <skia/core/SkTypeface.h>

namespace albor::skia
{
#if defined(__APPLE__)
    // parse the typeface whose bytes the registry carries under the hash the run names (§15, AD-34)
    [[nodiscard]] sk_sp<SkTypeface> typeface_from_font_hash(std::uint64_t font_hash);
#else
    // no font manager here parses carried bytes, so a platform run draws nothing (§15)
    [[nodiscard]] inline sk_sp<SkTypeface> typeface_from_font_hash(std::uint64_t) { return nullptr; }
#endif
} // namespace albor::skia
