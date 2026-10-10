#include "text/text-skia/skia_text_engine.h"

namespace
{
    // the installer that registers the skparagraph factory at load, so linking the engine selects it (§9.1)
    struct TextAutoInstall
    {
        TextAutoInstall() { albor::SkiaTextEngine::install(); }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§9.1)
    const TextAutoInstall g_text_auto_install;
} // namespace

// the install mark for the text engine, so two engines on one line refuse the binary (§9.1)
extern "C" int albor_text_marker = 1;
