// the win32 platform state of the prototype, so that no windows type reaches prototypes/a2/platform.h (§3.5, AD-05)

#include "prototypes/a2/platform.h"

#ifndef _WIN32
#error "prototypes/a2/win32 is only built on windows hosts"
#endif

#include <windows.h>

namespace albor::a2
{

    namespace win32
    {

        // the opaque dxgi swap chain the platform presents into, which is the windows counterpart of the metal layer
        struct SwapChain
        {
            IDXGISwapChain4* swap_chain = nullptr;
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* queue = nullptr;
            ID3D12CommandAllocator* allocator = nullptr;
            ID3D12GraphicsCommandList* commands = nullptr;
            IDCompositionDevice* composition_device = nullptr;
            IDCompositionVisual* visual = nullptr;
            IDCompositionSurface* surface = nullptr;
            unsigned int width = 0;
            unsigned int height = 0;
            double scale_factor = 1.0;
        };

        // the opaque native surface, mirroring native_surface.macos.h so the render thread reaches the same contract
        struct NativeSurface
        {
            SwapChain* swap_chain = nullptr;
        };

        // the counters the display equivalent on windows must fill in, mirroring the macos state
        struct FrameCounters
        {
            unsigned long long frames_presented = 0;
            unsigned long long frames_presented_while_resizing = 0;
            unsigned long long resize_events = 0;
            unsigned long long scale_changes = 0;
            unsigned long long messages_during_nested_loop = 0;
            unsigned long long messages_total = 0;
            unsigned long long close_after_frames = 0;
            unsigned long long nested_loops_running = 0;
            int resizing = 0;
        };

    } // namespace win32

    // the opaque platform state, so that the interface stays free of windows types (§3.5, AD-16)
    struct PlatformWindowState
    {
        HWND hwnd = nullptr;
        HINSTANCE instance = nullptr;
        win32::SwapChain* chain = nullptr;
        win32::NativeSurface surface{};
        win32::FrameCounters counters{};
        void* frame_callback = nullptr;
    };

} // namespace albor::a2
