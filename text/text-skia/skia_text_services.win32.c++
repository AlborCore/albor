#include "text/text-skia/skia_text_services.h"

#include <skia/modules/skunicode/include/SkUnicode_icu.h>
#include <skia/ports/SkTypeface_win.h>

namespace albor
{
    TextServices platform_text_services() {
        // a null collection asks directwrite for the system collection, which is what every run resolves against (§9.1)
        TextServices services;
        services.font_manager = SkFontMgr_New_DirectWrite();
        // the icu implementation supplies line breaking and bidi, which is what skparagraph needs to run (§9.1)
        services.unicode = SkUnicodes::ICU::Make();
        return services;
    }
} // namespace albor
