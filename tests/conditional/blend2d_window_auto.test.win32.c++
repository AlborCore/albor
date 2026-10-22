// the win32 window install and frame stream, which the swapchain factory and a real window drive (§5.3, §6.3, AD-16)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <psapi.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "core/paint.h"
#include "render/frame_scene.h"
#include "render/render-blend2d/blend2d_renderer_backend.h"
#include "render/render-blend2d/blend2d_window_surface.h"
#include "render/render_executor.h"
#include "render/renderer_backend.h"
#include "window/window_metrics.h"

using namespace albor;

TEST(Blend2DAutoInstallChecks, TheWindowSurfaceFactoryInstallsItselfAtLoad) {
    // arrange a value that is no window, which is what a wrong handle looks like (§5.3, AD-16)
    HWND foreign = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));
    // act
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(NativeWindowHandle::from_object(reinterpret_cast<const void*>(foreign)), WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f));
    // assert the refusal came back, because a raster fallback here would draw a blank window (§5.3, §6.3)
    EXPECT_EQ(surface, nullptr);
}

TEST(Blend2DAutoInstallChecks, ARealWindowGetsAPresentingSurface) {
    // the guard owns the hidden window and its class, so a failed expectation still tears both down (§5.3)
    struct HiddenWindow
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND window = nullptr;
        bool registered = false;

        HiddenWindow() {
            // the class is registered once, because the suite is the only owner of the name (§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = instance;
            window_class.lpszClassName = L"albor-blend2d-auto-surface";
            registered = RegisterClassExW(&window_class) != 0;
            // the hidden window is the surface a host presents through (§5.3, AD-16)
            if (registered)
                window = CreateWindowExW(0, window_class.lpszClassName, L"blend2d-auto", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 96, 72, nullptr, nullptr, instance, nullptr);
        }

        ~HiddenWindow() {
            // the window is destroyed first, because the surface presents into it (§5.3)
            if (window != nullptr)
                DestroyWindow(window);
            // the class is unregistered, so a later run registers it afresh (§5.3)
            if (registered)
                UnregisterClassW(L"albor-blend2d-auto-surface", instance);
        }

        HiddenWindow(const HiddenWindow&) = delete;
        HiddenWindow& operator=(const HiddenWindow&) = delete;
    };

    // arrange
    HiddenWindow host;
    ASSERT_NE(host.window, nullptr);
    // act
    std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(NativeWindowHandle::from_object(reinterpret_cast<const void*>(host.window)), WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f));
    // assert the surface attached to the window at the metrics the scene is built for (§5.3, §6.2)
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f));
    // act
    surface->begin_frame();
    surface->end_frame();
    // assert the finished frame reads back at the physical extent, which is what the copy presents (§6.3, AD-27)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->read_pixels().size(), static_cast<std::size_t>(64) * 48u * 4u);
    // act
    surface->resize(WindowMetrics::from_logical(Size{40.0f, 30.0f}, 2.0f));
    // assert the surface stayed presentable, which a refused buffer resize would betray (§6.3)
    EXPECT_TRUE(surface->valid());
    surface->begin_frame();
    surface->end_frame();
    // assert the presentation moved to the new metrics, whose physical extent the buffers took (§6.2, AD-27)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), WindowMetrics::from_logical(Size{40.0f, 30.0f}, 2.0f));
    EXPECT_EQ(surface->read_pixels().size(), static_cast<std::size_t>(80) * 60u * 4u);
    // act a resize to metrics with no area, which the platform reports before a window is sized (§6.2)
    surface->resize(WindowMetrics{});
    // assert the refused resize kept the last presentable metrics rather than half resizing the buffers (§6.2)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), WindowMetrics::from_logical(Size{40.0f, 30.0f}, 2.0f));
    // the surface is released before the window it presents into, which the guard does at the end of the test (§5.3)
    surface.reset();
}

TEST(Blend2DWindowSurfaceChecks, AStreamOfPresentedFramesKeepsTheFootprintFlat) {
    // the guard owns a hidden window, so a failed expectation still tears the presentation target down (§5.3)
    struct HiddenWindow
    {
        HWND window = CreateWindowExW(0, L"STATIC", L"blend2d-stream", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 96, 72, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        HiddenWindow() = default;

        ~HiddenWindow() {
            // the window is destroyed only after the surface that presents into it (§5.3)
            if (window != nullptr)
                DestroyWindow(window);
        }

        HiddenWindow(const HiddenWindow&) = delete;
        HiddenWindow& operator=(const HiddenWindow&) = delete;
    };

    // arrange the hidden window and the physical extent of an 800x600 window at 200% (§5.3, AD-27)
    HiddenWindow host;
    ASSERT_NE(host.window, nullptr);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    // arrange the window surface the install-time factory builds over the handle (§5.3, §6.3)
    std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(NativeWindowHandle::from_object(reinterpret_cast<const void*>(host.window)), metrics);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // arrange the immutable scene the stream replays, which is the background an idle window clears to (§6.1)
    const std::shared_ptr<const FrameScene> scene = std::make_shared<FrameScene>(metrics, Color::rgb(0.06f, 0.07f, 0.09f));
    // arrange the backend and the executor, because stage two runs on its own thread (§6.1, AD-18)
    blend2d::Blend2DRendererBackend backend;
    RenderExecutor executor(&backend);
    // act a warmup, so the measurement pays neither the raster allocation nor the driver's first buffers (§2.2)
    for (int frame = 0; frame < 20; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the private commit the stream settles at, which is the baseline the bound is judged against (§2.2)
    PROCESS_MEMORY_COUNTERS_EX before{};
    before.cb = sizeof(before);
    ASSERT_NE(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before), sizeof(before)), 0);
    // act the stream itself, which is a window that keeps presenting one unchanged scene (§6.1)
    for (int frame = 0; frame < 120; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the private commit the stream left behind, which one leaked upload texture per frame lifts by 900 MB (§2.2)
    PROCESS_MEMORY_COUNTERS_EX after{};
    after.cb = sizeof(after);
    ASSERT_NE(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after)), 0);
    // assert every frame ran, because a stream that dropped them would hold the bound by drawing nothing (§6.1)
    EXPECT_EQ(executor.executed_count(), 140u);
    // assert a per-frame upload leak cannot hide here, because 120 uploads are 900 MB (§6.3)
    const std::int64_t growth = static_cast<std::int64_t>(after.PrivateUsage) - static_cast<std::int64_t>(before.PrivateUsage);
    EXPECT_LT(growth, 8 * 1024 * 1024);
    // act a resize after the stream, which only succeeds once no back buffer reference is outstanding (§6.3)
    surface->resize(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 2.0f));
    // assert the buffers moved, because an outstanding GetBuffer reference makes ResizeBuffers fail (§6.3)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), WindowMetrics::from_logical(Size{640.0f, 480.0f}, 2.0f));
}
