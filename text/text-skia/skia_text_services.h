#pragma once

#include <skia/core/SkFontMgr.h>
#include <skia/core/SkRefCnt.h>
#include <skia/modules/skunicode/include/SkUnicode.h>

namespace albor
{
    // the system font collection the shaper resolves families against, which is the platform half of §9.1
    [[nodiscard]] sk_sp<SkFontMgr> skia_text_font_manager();

    // the icu unicode implementation, which supplies the line breaking and bidi skparagraph needs (§9.1)
    [[nodiscard]] sk_sp<SkUnicode> skia_text_unicode();
} // namespace albor
