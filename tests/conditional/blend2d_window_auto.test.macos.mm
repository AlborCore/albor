#import <Foundation/NSObject.h>

#include <memory>

#include <gtest/gtest.h>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "render/render-blend2d/blend2d_window_surface.h"
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
