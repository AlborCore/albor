#pragma once

#include <functional>
#include <memory>

#include "albor/core/native_window_handle.h"
#include "albor/render/frame_scene.h"
#include "albor/render/render_context.h"
#include "albor/window/window_metrics.h"

namespace albor
{
    // the outcome of executing one immutable scene, which stage two reports to the frame pipeline (§6.1)
    enum class FrameOutcome
    {
        // the scene reached the surface and was presented (§6.1)
        Presented,

        // the surface presents at other metrics, so the scene was dropped (§6.1, D9.1)
        RefusedStaleMetrics,

        // the surface could not retain its target, so a culled scene must be rebuilt whole (§6.1)
        RefusedNoRetention
    };

    // the renderer half of the framework, which every backend implements (§6.2, AD-05)
    class RendererBackend
    {
    public:
        virtual ~RendererBackend() = default;

        // report whether the backend initialised its graphics device
        [[nodiscard]] virtual bool ready() const = 0;

        // build the surface a native window presents into, returning null when the backend refuses the handle (§6.2)
        [[nodiscard]] virtual std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) = 0;

        // resize an existing surface, which a composition root calls from the ui thread (§6.2, D9.1)
        virtual void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) = 0;

        // execute an immutable frame scene, which is stage two of the frame pipeline (§6.1, AD-18)
        virtual FrameOutcome execute_frame(RenderSurface& surface, const FrameScene& scene) = 0;
    };

    // the signature a renderer backend implements to build itself (§6.2)
    using RendererBackendFactory = std::function<std::unique_ptr<RendererBackend>()>;

    // the state the installed factory lives in, heap held behind a constant initialized pointer (§6.2)
    struct RendererBackendFactoryState
    {
        // the installed factory, which is empty until a backend installs one (§6.2)
        RendererBackendFactory factory;
    };

    // install the renderer backend the framework draws with, which a backend calls once at startup (§6.2)
    void set_renderer_backend_factory(RendererBackendFactory factory);

    // hand out the installed factory, which is empty until a backend installs one
    [[nodiscard]] const RendererBackendFactory& renderer_backend_factory();

    // build the installed backend, returning null when no backend is installed
    [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer_backend();
} // namespace albor
