#include "render/render-blend2d/blend2d_renderer_backend.h"

namespace albor::blend2d
{
    namespace
    {
        // the installer that registers the blend2d factory at load, so linking the backend selects it (§6.3, AD-26)
        struct RendererAutoInstall
        {
            RendererAutoInstall() { install_blend2d_renderer_backend(); }
        };

        // the installer is never referenced, because the program load runs it before any factory is read (§6.3)
        const RendererAutoInstall g_renderer_auto_install;
    } // namespace
} // namespace albor::blend2d

// the install mark for the renderer, so two backends on one line refuse the binary (§6.3, AD-26)
extern "C" int albor_renderer_marker = 3;
