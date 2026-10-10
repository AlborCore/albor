#include "render/render-skia/skia_renderer_backend.h"

namespace
{
    // the installer that registers the metal backed skia factory at load, so linking the backend selects it (§6.3, AD-26)
    struct RendererAutoInstall
    {
        RendererAutoInstall() { albor::install_skia_renderer_backend(); }
    };

    // the installer is never referenced, because the program load runs it before any factory is read (§6.3)
    const RendererAutoInstall g_renderer_auto_install;
} // namespace

// the install mark for the renderer, so two backends on one line refuse the binary (§6.3, AD-26)
extern "C" int albor_renderer_marker = 1;
