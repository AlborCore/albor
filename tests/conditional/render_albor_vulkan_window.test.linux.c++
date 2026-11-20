#include <cstdlib>
#include <memory>

#include <gtest/gtest.h>

#include "albor/core/env.h"
#include "albor/core/paint.h"
#include "albor/render/draw_command.h"
#include "albor/render/frame_scene.h"
#include "albor/render/render-albor/albor_renderer_backend.h"
#include "albor/render/render-albor/albor_vulkan_diagnostics.h"
#include "albor/render/render_context.h"
#include "albor/window/platform_window.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

using namespace albor;
using namespace albor::render;

TEST(AlborVulkanWindowChecks, PresentsThroughTheNativeWindow) {
    // arrange a display, which an x11 and a wayland runner both provide (§5.3)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions options;
    options.title = "albor-vulkan-window";
    options.size = Size{200.0f, 150.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    if (window == nullptr)
        GTEST_SKIP() << "the platform adapter could not create a window";
    // act the window is shown, which supplies the surface the backend presents through (§5.3)
    window->show();
    ASSERT_TRUE(window->visible());
    const WindowMetrics metrics = window->metrics();
    ASSERT_TRUE(metrics.valid());
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::uint64_t before = vulkan_presented_frames();
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(window->native_handle(), metrics);
    // assert a displayed window accepts the swapchain the backend builds (§6.3)
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act one frame is executed into the window
    FrameScene scene(metrics, Color::rgb(0.1f, 0.2f, 0.3f));
    scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{8.0f, 8.0f}, Size{32.0f, 24.0f}), Paint{.fill = Color::rgb(0.8f, 0.3f, 0.2f)}));
    backend.execute_frame(*surface, scene);
    // assert the frame reached the swapchain, which the presented counter records (§6.3)
    EXPECT_GT(vulkan_presented_frames(), before);
}

TEST(AlborVulkanWindowChecks, PresentsAFrameStreamWithoutDrainingTheQueue) {
    // arrange a displayed window, which the backend presents a stream of frames through (§6.3)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions options;
    options.title = "albor-vulkan-stream";
    options.size = Size{160.0f, 120.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    if (window == nullptr)
        GTEST_SKIP() << "the platform adapter could not create a window";
    window->show();
    const WindowMetrics metrics = window->metrics();
    ASSERT_TRUE(metrics.valid());
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(window->native_handle(), metrics);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act sixteen frames are submitted back to back, each carrying a sliding rectangle (§6.1, AD-18b)
    const std::uint64_t before = vulkan_presented_frames();
    for (int frame = 0; frame < 16; frame++) {
        FrameScene scene(metrics, Color::rgb(0.1f, 0.2f, 0.3f));
        const float left = 4.0f + static_cast<float>(frame) * 4.0f;
        scene.add_command(DrawCommand::rectangle(Rect::from_origin_size(Point{left, 4.0f}, Size{24.0f, 16.0f}), Paint{.fill = Color::rgb(0.8f, 0.3f, 0.2f)}));
        backend.execute_frame(*surface, scene);
    }
    // assert every frame reached the swapchain, which a blocking present would still satisfy
    EXPECT_GE(vulkan_presented_frames(), before + 16u);
}

TEST(AlborVulkanWindowChecks, SurvivesAWindowTornDownWhileTheSurfaceStands) {
    // arrange a window and a surface that already presented one frame (§6.3, §10.5)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions options;
    options.title = "albor-vulkan-teardown";
    options.size = Size{160.0f, 120.0f};
    std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    if (window == nullptr)
        GTEST_SKIP() << "the platform adapter could not create a window";
    window->show();
    const WindowMetrics metrics = window->metrics();
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(window->native_handle(), metrics);
    ASSERT_NE(surface, nullptr);
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.2f, 0.4f, 0.6f)));
    // act the native window is taken down while the surface still holds it, which the deferred teardown covers (§10.5)
    window = nullptr;
    // assert the surface refuses later frames rather than presenting into a destroyed window (§6.3)
    EXPECT_FALSE(surface->valid());
    const std::uint64_t before = vulkan_presented_frames();
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.6f, 0.4f, 0.2f)));
    EXPECT_EQ(vulkan_presented_frames(), before);
}

TEST(AlborVulkanWindowChecks, RecreatesTheSwapchainAcrossRapidResizes) {
    // arrange a displayed window with a surface at its own extent (§6.3)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions options;
    options.title = "albor-vulkan-resize";
    options.size = Size{200.0f, 150.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    if (window == nullptr)
        GTEST_SKIP() << "the platform adapter could not create a window";
    window->show();
    const WindowMetrics metrics = window->metrics();
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(window->native_handle(), metrics);
    ASSERT_NE(surface, nullptr);
    // act fifty resizes alternate between two extents, and every one presents a frame (§6.3)
    for (int step = 0; step < 50; step++) {
        const Size extent = step % 2 == 0 ? Size{100.0f, 100.0f} : Size{320.0f, 240.0f};
        const WindowMetrics resized = WindowMetrics::from_logical(extent, metrics.scale_factor);
        backend.resize_surface(*surface, resized);
        backend.execute_frame(*surface, FrameScene(resized, Color::rgb(0.3f, 0.5f, 0.7f)));
    }
    // assert the surface still presents after every rebuild, because a refused swapchain would report invalid (§6.3)
    EXPECT_TRUE(surface->valid());
}

TEST(AlborVulkanWindowChecks, PresentsTwoWindowsInterleaved) {
    // arrange two displayed windows, which the backend draws through separate surfaces (§6.3, AD-18b)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions first_options;
    first_options.title = "albor-vulkan-first";
    first_options.size = Size{160.0f, 120.0f};
    WindowOptions second_options;
    second_options.title = "albor-vulkan-second";
    second_options.size = Size{200.0f, 140.0f};
    const std::unique_ptr<PlatformWindow> first_window = PlatformWindow::create(first_options);
    const std::unique_ptr<PlatformWindow> second_window = PlatformWindow::create(second_options);
    if (first_window == nullptr || second_window == nullptr)
        GTEST_SKIP() << "the platform adapter could not create two windows";
    first_window->show();
    second_window->show();
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const WindowMetrics first_metrics = first_window->metrics();
    const WindowMetrics second_metrics = second_window->metrics();
    const std::unique_ptr<RenderSurface> first = backend.create_surface(first_window->native_handle(), first_metrics);
    const std::unique_ptr<RenderSurface> second = backend.create_surface(second_window->native_handle(), second_metrics);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    // act the two windows alternate frames, which a process wide command buffer would serialize (§6.3, AD-18b)
    const std::uint64_t before = vulkan_presented_frames();
    for (int frame = 0; frame < 8; frame++) {
        backend.execute_frame(*first, FrameScene(first_metrics, Color::rgb(0.2f, 0.4f, 0.6f)));
        backend.execute_frame(*second, FrameScene(second_metrics, Color::rgb(0.6f, 0.4f, 0.2f)));
    }
    // assert both windows presented every frame, which one window starving the other would contradict (§6.3)
    EXPECT_GE(vulkan_presented_frames(), before + 16u);
    EXPECT_TRUE(first->valid());
    EXPECT_TRUE(second->valid());
}

TEST(AlborVulkanWindowChecks, PresentsThroughAWindowShownAfterTheSurfaceExists) {
    // arrange a window that stays off screen while its renderer claims the surface (§5.1, §6.3)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions options;
    options.title = "albor-vulkan-late-show";
    options.size = Size{160.0f, 120.0f};
    options.visible = false;
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    if (window == nullptr)
        GTEST_SKIP() << "the platform adapter could not create a window";
    const WindowMetrics metrics = window->metrics();
    ASSERT_TRUE(metrics.valid());
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    const std::unique_ptr<RenderSurface> surface = backend.create_surface(window->native_handle(), metrics);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act the window is shown after the swapchain exists, which is how a popup opens under its trigger (§5.1)
    window->show();
    ASSERT_TRUE(window->visible());
    const std::uint64_t before = vulkan_presented_frames();
    backend.execute_frame(*surface, FrameScene(metrics, Color::rgb(0.2f, 0.4f, 0.6f)));
    // assert the first frame mapped the window, which a refused present would leave blank (§6.3)
    EXPECT_GE(vulkan_presented_frames(), before + 1u);
}

TEST(AlborVulkanWindowChecks, PresentsThroughAPopupShownHiddenAndShownAgain) {
    // arrange a window that owns a transient surface, which is the popup a menu opens (§5.1)
    const bool wayland = albor::core::get_environment_variable("WAYLAND_DISPLAY").has_value();
    const bool x11 = albor::core::get_environment_variable("DISPLAY").has_value();
    if (!wayland && !x11)
        GTEST_SKIP() << "no display on this host";
    WindowOptions owner_options;
    owner_options.title = "albor-vulkan-popup-owner";
    owner_options.size = Size{240.0f, 180.0f};
    const std::unique_ptr<PlatformWindow> owner = PlatformWindow::create(owner_options);
    if (owner == nullptr)
        GTEST_SKIP() << "the platform adapter could not create a window";
    owner->show();
    WindowOptions popup_options;
    popup_options.role = WindowRole::Popup;
    popup_options.title = "albor-vulkan-popup";
    popup_options.size = Size{120.0f, 90.0f};
    popup_options.visible = false;
    popup_options.parent = WindowHandle::from_id(1u);
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = Rect{.x = 0.0f, .y = 0.0f, .width = 120.0f, .height = 24.0f};
    const std::unique_ptr<PlatformWindow> popup = PlatformWindow::create(popup_options);
    AlborRendererBackend backend;
    if (!backend.ready())
        GTEST_SKIP() << "no vulkan device on this host";
    // a platform that refuses the transient role leaves the tree fallback to the owner (§5.1)
    if (popup == nullptr)
        GTEST_SKIP() << "the platform adapter refused a popup";
    const WindowMetrics popup_metrics = popup->metrics();
    const std::unique_ptr<RenderSurface> popup_surface = backend.create_surface(popup->native_handle(), popup_metrics);
    ASSERT_NE(popup_surface, nullptr);
    popup->show();
    const std::uint64_t before = vulkan_presented_frames();
    backend.execute_frame(*popup_surface, FrameScene(popup_metrics, Color::rgb(0.3f, 0.3f, 0.4f)));
    EXPECT_GE(vulkan_presented_frames(), before + 1u);
    // act the popup is dismissed and opened again, which rebuilds its native surface (§5.1)
    popup->hide();
    popup->show();
    ASSERT_TRUE(popup->visible());
    const std::uint64_t reopened = vulkan_presented_frames();
    for (int frame = 0; frame < 4; frame++)
        backend.execute_frame(*popup_surface, FrameScene(popup_metrics, Color::rgb(0.4f, 0.3f, 0.3f)));
    // assert the reopened surface presents again, which a stale wsi surface would refuse (§6.3)
    EXPECT_GE(vulkan_presented_frames(), reopened + 4u);
}
