#include "text/text-skia/skia_text_services.h"

#include <skia/modules/skunicode/include/SkUnicode_icu.h>
#include <skia/ports/SkFontMgr_mac_ct.h>

namespace albor
{
    sk_sp<SkFontMgr> skia_text_font_manager() {
        // a null collection asks core text for the system font collection, which is what every run resolves against (§9.1)
        return SkFontMgr_New_CoreText(nullptr);
    }

    sk_sp<SkUnicode> skia_text_unicode() {
        // the icu implementation supplies line breaking and bidi, which is what skparagraph needs to run (§9.1)
        return SkUnicodes::ICU::Make();
    }
} // namespace albor
