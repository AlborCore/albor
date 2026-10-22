#import <Foundation/NSObject.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <mach/mach.h>
#include <mach/task_info.h>
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

// the suite only runs on apple, because it asserts the macos window half self installed at load (§5.3, AD-16)
TEST(Blend2DAutoInstallChecks, TheWindowSurfaceFactoryInstallsItselfAtLoad) {
    // arrange a real object that is no metal layer, which is what a wrong handle looks like (§5.3, AD-16)
    NSObject* foreign = [[NSObject alloc] init];
    // act
    const std::unique_ptr<albor::RenderSurface> surface = albor::blend2d::create_window_surface(albor::NativeWindowHandle::from_object((__bridge const void*)foreign), albor::WindowMetrics::from_logical(albor::Size{64.0f, 48.0f}, 1.0f));
    // assert the refusal came back, because a raster fallback here would draw a blank window (§5.3, §6.3)
    EXPECT_EQ(surface, nullptr);
}

// the suite presents a real stream, because a present that forgets its pool leaks what no header check sees (§6.3)
TEST(Blend2DWindowSurfaceChecks, AStreamOfPresentedFramesKeepsTheFootprintFlat) {
    // arrange the layer a window hands over, which is what the present path blits into (§5.3, AD-16)
    CAMetalLayer* layer = [[CAMetalLayer alloc] init];
    layer.device = MTLCreateSystemDefaultDevice();
    // assert a host without a metal device has no drawable to measure, so it is reported rather than passed (§6.3)
    ASSERT_NE(layer.device, nil);
    // arrange the physical extent the example opens at, which makes one staging buffer 7.68 MB (§6.3, AD-27)
    const albor::WindowMetrics metrics = albor::WindowMetrics::from_logical(albor::Size{800.0f, 600.0f}, 2.0f);
    // arrange the window surface, which the factory the whole archive link installed builds (§5.3, AD-16)
    const std::unique_ptr<albor::RenderSurface> surface = albor::blend2d::create_window_surface(albor::NativeWindowHandle::from_object((__bridge const void*)layer), metrics);
    // assert the surface came up, because a refused surface presents nothing and would measure nothing (§6.2)
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // arrange the immutable scene the stream replays, which is the background an idle window clears to (§6.1)
    const std::shared_ptr<const albor::FrameScene> scene = std::make_shared<albor::FrameScene>(metrics, albor::Color::rgb(0.06f, 0.07f, 0.09f));
    // arrange the backend the frames execute on, which is the one the composition root installs (§6.3)
    albor::blend2d::Blend2DRendererBackend backend;
    // arrange the executor, because stage two runs on its own thread, which owns no autorelease pool (§6.1, AD-18)
    albor::RenderExecutor executor(&backend);
    // act a warmup, so the measurement pays neither the raster allocation nor the driver's first buffers (§2.2)
    for (int frame = 0; frame < 20; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the footprint the stream settles at, which is the baseline the bound is judged against (§2.2)
    task_vm_info_data_t before{};
    mach_msg_type_number_t before_count = TASK_VM_INFO_COUNT;
    ASSERT_EQ(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&before), &before_count), KERN_SUCCESS);
    // act the stream itself, which is a window that keeps presenting one unchanged scene (§6.1)
    for (int frame = 0; frame < 120; frame++) {
        ASSERT_TRUE(executor.submit(*surface, scene));
        executor.flush();
    }
    // arrange the footprint the stream left behind, which one leaked staging buffer per frame lifts by 900 MB (§2.2)
    task_vm_info_data_t after{};
    mach_msg_type_number_t after_count = TASK_VM_INFO_COUNT;
    ASSERT_EQ(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&after), &after_count), KERN_SUCCESS);
    // assert every frame ran, because a stream that dropped them would hold the bound by drawing nothing (§6.1)
    EXPECT_EQ(executor.executed_count(), 140u);
    // assert a leak of one staging buffer per frame cannot pass, because 120 frames of it is 900 MB (§6.3)
    const std::int64_t growth = static_cast<std::int64_t>(after.phys_footprint) - static_cast<std::int64_t>(before.phys_footprint);
    EXPECT_LT(growth, 96 * 1024 * 1024);
}
