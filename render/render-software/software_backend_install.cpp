#include "render/render-software/software_renderer_backend.h"

namespace albor::software
{
    namespace
    {
        // the installer that registers the cpu reference factory at load, so linking the backend selects it (§6.3, AD-26)
        struct RendererAutoInstall
        {
            RendererAutoInstall() { install_software_renderer_backend(); }
        };

        // the installer is never referenced, because the program load runs it before any factory is read (§6.3)
        const RendererAutoInstall g_renderer_auto_install;
    } // namespace
} // namespace albor::software

// the install mark for the renderer, so two backends on one line refuse the binary (§6.3, AD-26)
extern "C" int albor_renderer_marker = 2;
