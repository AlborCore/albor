#include "render/render-albor/albor_renderer_backend.h"

namespace albor::render
{
    namespace
    {
        // the installer that registers the in-repo factory at load, so linking the backend selects it (§6.3, AD-26)
        struct RendererAutoInstall
        {
            RendererAutoInstall() { install_albor_renderer_backend(); }
        };

        // the installer is never referenced, because the program load runs it before any factory is read (§6.3)
        const RendererAutoInstall g_renderer_auto_install;
    } // namespace
} // namespace albor::render

// the install mark, so two whole archive backends refuse one binary; a lazy link resolves it first-wins (issue 102)
extern "C" int albor_renderer_marker = 4;
