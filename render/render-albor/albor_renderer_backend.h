#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "render/renderer_backend.h"

// the engine namespace drops the module's albor prefix, because the framework root namespace is already albor
namespace albor::render
{
    // the in-repo gpu backend, which draws the portable batches onto a metal device (§6.3, AD-16)
    class AlborRendererBackend final : public RendererBackend
    {
    public:
        // the backend starts without a device, which the metal translation unit supplies when it lands (§6.3, AD-16)
        AlborRendererBackend();
        ~AlborRendererBackend() override;

        AlborRendererBackend(const AlborRendererBackend&) = delete;
        AlborRendererBackend& operator=(const AlborRendererBackend&) = delete;
        AlborRendererBackend(AlborRendererBackend&&) = delete;
        AlborRendererBackend& operator=(AlborRendererBackend&&) = delete;

        // override: report whether the graphics device is ready, which the device-less shell never is (§6.2)
        [[nodiscard]] bool ready() const override;

        // override: build the surface a native window presents into, which the shell refuses until the device lands (§6.2)
        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) override;

        // override: resize a surface, which the shell has nothing to resize before the device lands (§6.2)
        void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) override;

        // override: execute an immutable frame scene, which the shell refuses without a device (§6.1, AD-18)
        void execute_frame(RenderSurface& surface, const FrameScene& scene) override;

        // report how many gpu batch buffers the backend allocated, which a steady frame must not raise (§6.3, AD-06)
        [[nodiscard]] std::uint64_t batch_buffer_allocations() const;

    private:
        // the gpu batch buffers this backend's surfaces allocated, which each surface raises on creation (§6.3, AD-06)
        std::shared_ptr<std::atomic<std::uint64_t>> m_batch_buffer_allocations;
    };

    // install the in-repo backend as the composition root's backend (§6.2)
    void install_albor_renderer_backend();
} // namespace albor::render
