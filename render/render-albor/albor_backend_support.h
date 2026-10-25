#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "core/native_window_handle.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::render
{
    // report whether the platform graphics device came up, which every backend answers on its own (§6.3)
    [[nodiscard]] bool backend_device_available();

    // build the surface over the native handle, offscreen when null, raising the counter per batch buffer (§6.2, AD-06)
    [[nodiscard]] std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics, std::shared_ptr<std::atomic<std::uint64_t>> batch_buffer_allocations = nullptr);
} // namespace albor::render
