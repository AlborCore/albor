#include "automation/host/capture_target.h"

#include <utility>

#include "render/render_context.h"
#include "render/render_executor.h"
#include "render/screenshot.h"

namespace albor::automation
{
    CapturedFrame capture_window(const dev::WindowBinding& binding) {
        // a binding without a surface or an executor names no window a capture can read (§6.3, §10.3)
        if (binding.surface == nullptr || binding.executor == nullptr)
            return CapturedFrame{.error = "the binding carries no surface to capture", .width = 0, .height = 0, .pixels = {}};
        // the executor is settled first, because read_pixels() refuses a frame in flight (§6.3)
        static_cast<RenderExecutor*>(binding.executor)->flush();
        // the last frame the window presented is read, which is the contract a capture carries (§6.3, §6.1)
        Screenshot shot = capture_surface(*static_cast<const RenderSurface*>(binding.surface));
        // a read the surface refused carries no bytes, which is refused rather than reported as a black frame (§6.3)
        if (shot.empty())
            return CapturedFrame{.error = "the window has not presented a frame at its current size", .width = 0, .height = 0, .pixels = {}};
        // the bytes move out of the read, because a window frame is large enough that a copy would be felt (§6.3)
        return CapturedFrame{.error = "", .width = shot.width, .height = shot.height, .pixels = std::move(shot.pixels)};
    }
} // namespace albor::automation
