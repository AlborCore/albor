#pragma once

#include <cstdint>

#include <skia/core/SkTypeface.h>

namespace albor::skia
{
#if defined(__APPLE__)
    // adapt the platform font a run carries to the typeface this backend draws with (§9.1, AD-28)
    [[nodiscard]] sk_sp<SkTypeface> typeface_from_platform_font(const void* font, std::uint64_t font_hash);
#else
    // no platform font source exists here, so a run that names one draws nothing (§9.1)
    [[nodiscard]] inline sk_sp<SkTypeface> typeface_from_platform_font(const void*, std::uint64_t) { return nullptr; }
#endif
} // namespace albor::skia
