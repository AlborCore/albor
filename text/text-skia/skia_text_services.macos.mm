#include "text/text-skia/skia_text_services.h"

#include <skia/modules/skunicode/include/SkUnicode_icu.h>
#include <skia/ports/SkFontMgr_mac_ct.h>

namespace albor
{
    TextServices platform_text_services() {
        // a null collection asks core text for the system font collection, which is what every run resolves against (§9.1)
        TextServices services;
        services.font_manager = SkFontMgr_New_CoreText(nullptr);
        // the icu implementation supplies line breaking and bidi, which is what skparagraph needs to run (§9.1)
        services.unicode = SkUnicodes::ICU::Make();
        return services;
    }
} // namespace albor
