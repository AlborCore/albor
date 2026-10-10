#include "text/text-harfbuzz/harfbuzz_text_engine.h"

namespace
{
    // the installer that registers the vendored factory at load, which installs nothing yet (§9.1)
    struct TextAutoInstall
    {
        TextAutoInstall() { albor::harfbuzz::install_harfbuzz_text_engine(); }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§9.1)
    const TextAutoInstall g_text_auto_install;
} // namespace

// the install mark for the text engine, so two engines on one line refuse the binary (§9.1)
extern "C" int albor_text_marker = 2;
