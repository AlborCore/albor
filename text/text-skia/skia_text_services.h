#pragma once

#include <cstdint>
#include <vector>

#include <skia/core/SkFontMgr.h>
#include <skia/core/SkRefCnt.h>
#include <skia/modules/skunicode/include/SkUnicode.h>

namespace albor
{
    // the platform services a text engine shapes with, which is everything it cannot own a second way (§9.1)
    struct TextServices
    {
        // the system font collection the shaper resolves families against, which is the platform half of §9.1
        sk_sp<SkFontMgr> font_manager;

        // the unicode implementation, which supplies the line breaking and bidi skparagraph needs (§9.1)
        sk_sp<SkUnicode> unicode;
    };

    // the platform services the process resolves once, which is what an application engine builds over (§9.1)
    [[nodiscard]] TextServices platform_text_services();

    // the raw bytes of one face of a shaped typeface, which the registry keys a run on (§15, AD-34)
    [[nodiscard]] std::vector<std::uint8_t> platform_font_face_bytes(const void* typeface_object);
} // namespace albor
