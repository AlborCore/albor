// the win32 half of the window install: a real handle and a foreign one drive the swapchain factory (§5.3, AD-16)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "render/render-blend2d/blend2d_window_surface.h"
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
