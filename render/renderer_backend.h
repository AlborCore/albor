#pragma once

#include <functional>
#include <memory>

#include "core/native_window_handle.h"
#include "render/frame_scene.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor
{
    // the renderer half of the framework, which every backend implements (§6.2, AD-05)
    class RendererBackend
    {
    public:
        virtual ~RendererBackend() = default;

        // report whether the backend initialised its graphics device
        [[nodiscard]] virtual bool ready() const = 0;

        // build the surface a native window presents into, returning null when the backend refuses the handle (§6.2)
        [[nodiscard]] virtual std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) = 0;

        // resize an existing surface, which a window resize requires (§6.2)
        virtual void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) = 0;

        // execute an immutable frame scene, which is stage two of the frame pipeline (§6.1, AD-18)
        virtual void execute_frame(RenderSurface& surface, const FrameScene& scene) = 0;
    };

    // the signature a renderer backend implements to build itself (§6.2)
    using RendererBackendFactory = std::function<std::unique_ptr<RendererBackend>()>;

    // install the renderer backend the framework draws with, which a backend calls once at startup (§6.2)
    void set_renderer_backend_factory(RendererBackendFactory factory);

    // hand out the installed factory, which is empty until a backend installs one
    [[nodiscard]] const RendererBackendFactory& renderer_backend_factory();

    // build the installed backend, returning null when no backend is installed
    [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer_backend();
} // namespace albor
