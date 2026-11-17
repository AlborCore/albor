#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/geometry.h"
#include "albor/core/native_window_handle.h"
#include "albor/core/paint.h"
#include "albor/platform/linux/wayland/native_surface.linux.h"
#include "albor/render/render-blend2d/blend2d_window_surface.h"
#include "albor/render/render_context.h"
#include "albor/window/platform_window.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

using namespace albor;

namespace
{
    // the listener a resized window reports through, which waits the way the stage drains its executor (§6.1)
    class ResizeDrainListener final : public PlatformWindowListener
    {
    public:
        // the frame counter the render thread advances, which a resize waits on (§6.1)
        std::atomic<std::size_t>* frames = nullptr;

        // whether a resize waited past the deadline, which a lock across the dispatch would cause (§10.5)
        std::atomic<bool> timed_out{false};

        // override: wait for one frame to finish, which is what the stage does on a resize (§6.1)
        void on_platform_resize(const WindowMetrics&) override {
            // a listener without a counter waits for nothing, which keeps the other tests quiet (§5.3)
            if (frames == nullptr)
                return;
            // the count before the wait tells a finished frame from a stuck render thread (§6.1)
            const std::size_t before = frames->load();
            // the wait is bounded, so a regression fails the test instead of hanging the suite (§10.5)
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (frames->load() == before && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            // a frame that never finished is the deadlock a lock across dispatch would cause (§10.5)
            if (frames->load() == before)
                timed_out = true;
        }

        // override: grant the close, which the platform never asks for in these tests (§5.2)
        bool on_platform_close_request() override { return true; }

        // override: ignore the close notification, which the tests read from the window (§5.2)
        void on_platform_closed() override {}

        // override: ignore the frame request, which the pacing owned by the stage would answer (§4.3)
        void on_platform_frame() override {}

        // override: ignore the input event, which no test here injects (§8.3)
        void on_platform_event(const Event&) override {}

        // override: ignore the focus loss, which no test here drives (§8.3)
        void on_platform_focus_lost() override {}

        // override: ignore the appearance change, which the portal path will raise in #234 (§8.4)
        void on_platform_appearance_changed(const SystemAppearance&) override {}
    };
} // namespace

// the suite presents a real wayland buffer, because a canvas that attaches without a pool draws nothing (§6.3, AD-26)
TEST(Blend2DWaylandChecks, PresentsAFrameThroughTheWindowSurface) {
    // arrange a window whose surface the canvas presents through (§5.3, §6.3)
    WindowOptions options;
    options.title = "albor-wayland-canvas";
    options.size = Size{64.0f, 48.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const WindowMetrics metrics = window->metrics();
    ASSERT_TRUE(metrics.valid());
    // act the factory the whole archive link installed builds the canvas over the window handle (§5.3, AD-16)
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(window->native_handle(), metrics);
    // assert the canvas attached to the wayland surface at the metrics the window reports (§6.3)
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->metrics(), metrics);
    // act a frame is drawn with one opaque rectangle, which the present copies into the shm buffer (§6.3)
    RenderContext& context = surface->begin_frame();
    context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), Paint{.fill = Color::rgb(1.0f, 0.0f, 0.0f)});
    surface->end_frame();
    // assert the frame reached the compositor, which a present that never committed would miss (§6.3)
    EXPECT_EQ(surface->presented_frames(), 1u);
    // assert the finished frame reads back at the physical extent, which is what the compositor received (§6.3, AD-27)
    const std::vector<std::uint8_t> pixels = surface->read_pixels();
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(metrics.pixel_size.width) * static_cast<std::size_t>(metrics.pixel_size.height) * 4u);
    // the first pixel is the opaque red the frame drew, in the canonical rgba order (§6.3, AD-26)
    EXPECT_EQ(pixels[0], 255);
    EXPECT_EQ(pixels[1], 0);
    EXPECT_EQ(pixels[2], 0);
    EXPECT_EQ(pixels[3], 255);
    // act a second frame is presented, which reuses the display after the first commit (§6.3)
    surface->begin_frame();
    surface->end_frame();
    // assert the second frame reached the compositor at the same extent (§6.3)
    EXPECT_EQ(surface->presented_frames(), 2u);
    EXPECT_EQ(surface->read_pixels().size(), pixels.size());
}

// the suite presents a frame stream from a render thread, because both threads share one connection (§10.5)
TEST(Blend2DWaylandChecks, PresentsFromARenderThreadWhileTheUiThreadReadsTheWindow) {
    // arrange a window and the canvas the render thread presents through (§5.3, §10.5)
    WindowOptions options;
    options.title = "albor-wayland-canvas-thread";
    options.size = Size{48.0f, 36.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(window->native_handle(), window->metrics());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act the render thread presents frames while the ui thread reads the window it shares (§10.5)
    std::atomic<bool> finished{false};
    std::thread render([&surface, &finished] {
        for (int frame = 0; frame < 8; ++frame) {
            surface->begin_frame();
            surface->end_frame();
        }
        finished = true;
    });
    while (!finished.load()) {
        // the ui thread resizes the window, which marshals and dispatches on the same connection (§10.5)
        window->set_size(Size{49.0f, 37.0f});
        window->set_size(Size{48.0f, 36.0f});
    }
    render.join();
    // assert the shared display survived the interleaving and every frame reached the compositor (§10.5, §6.3)
    EXPECT_TRUE(surface->valid());
    EXPECT_EQ(surface->presented_frames(), 8u);
    EXPECT_FALSE(surface->read_pixels().empty());
}

// the suite resizes while a frame is in flight, because the stage drains the executor from the configure listener (§6.1)
TEST(Blend2DWaylandChecks, ResizeDoesNotWaitOnTheRenderThread) {
    // arrange a window whose resize listener waits for the render thread, which the stage does (§6.1)
    WindowOptions options;
    options.title = "albor-wayland-canvas-resize";
    options.size = Size{64.0f, 48.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(window->native_handle(), window->metrics());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // the listener waits on the frame counter, which the render thread advances after every present (§6.1)
    std::atomic<std::size_t> frames{0};
    ResizeDrainListener listener;
    listener.frames = &frames;
    window->set_listener(&listener);
    // act the render thread presents continuously while the ui thread resizes the window (§10.5)
    std::atomic<bool> finished{false};
    std::thread render([&surface, &frames, &finished] {
        while (!finished.load()) {
            surface->begin_frame();
            surface->end_frame();
            frames++;
        }
    });
    window->set_size(Size{80.0f, 60.0f});
    window->set_size(Size{64.0f, 48.0f});
    finished = true;
    render.join();
    // assert the resize never had to wait out the render thread, which a lock across the dispatch would force (§10.5)
    EXPECT_FALSE(listener.timed_out.load());
    EXPECT_TRUE(surface->valid());
    // the listener is detached before the window is destroyed, because it points at a stack object (§4.2)
    window->set_listener(nullptr);
}

// the suite hides a window while a canvas exists, because a present must not re-map it (§5.3)
TEST(Blend2DWaylandChecks, DrawingAHiddenWindowKeepsItOffScreen) {
    // arrange a shown window and the canvas that presents through it (§5.3, §6.3)
    WindowOptions options;
    options.title = "albor-wayland-canvas-hidden";
    options.size = Size{64.0f, 48.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(window->native_handle(), window->metrics());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act one frame is presented, which the compositor receives (§6.3)
    surface->begin_frame();
    surface->end_frame();
    ASSERT_EQ(surface->presented_frames(), 1u);
    // act the window is hidden and a frame is drawn, which must not re-map it (§5.3)
    window->hide();
    surface->begin_frame();
    surface->end_frame();
    // assert the hidden frame never reached the compositor (§5.3, §10.5)
    EXPECT_FALSE(window->visible());
    EXPECT_EQ(surface->presented_frames(), 1u);
    // act the window is shown again, which the next frame presents through (§5.3)
    window->show();
    surface->begin_frame();
    surface->end_frame();
    // assert the reopen presented the frame, which a dismissed popup reopens the same way (§5.1)
    EXPECT_TRUE(window->visible());
    EXPECT_EQ(surface->presented_frames(), 2u);
}

// the suite closes a window while a frame is in flight, because teardown must stop the present (§5.2, §10.5)
TEST(Blend2DWaylandChecks, ClosingWhileAFrameIsInFlightStopsPresentation) {
    // arrange a shown window and the canvas a render thread presents through (§5.3, §10.5)
    WindowOptions options;
    options.title = "albor-wayland-canvas-close";
    options.size = Size{48.0f, 36.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(window->native_handle(), window->metrics());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act the render thread presents while the ui thread closes the window it presents through (§10.5)
    std::atomic<bool> started{false};
    std::thread render([&surface, &started] {
        started = true;
        for (int frame = 0; frame < 64; ++frame) {
            surface->begin_frame();
            surface->end_frame();
        }
    });
    while (!started.load())
        std::this_thread::yield();
    window->close();
    render.join();
    // assert the closed window presents nothing, which the cleared shared state reports (§5.2, §10.5)
    EXPECT_FALSE(surface->valid());
    const std::size_t after_close = surface->presented_frames();
    surface->begin_frame();
    surface->end_frame();
    EXPECT_EQ(surface->presented_frames(), after_close);
}

// the suite reopens a hidden popup, which the framework's close and show popup pair drives (§5.1)
TEST(Blend2DWaylandChecks, ReopensAPopupAfterAHide) {
    // arrange a parent and a popup, which the popup anchors to (§5.1)
    WindowOptions parent_options;
    parent_options.title = "albor-wayland-popup-parent";
    parent_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> parent = PlatformWindow::create(parent_options);
    ASSERT_NE(parent, nullptr);
    parent->show();
    ASSERT_TRUE(parent->visible());
    WindowOptions popup_options;
    popup_options.title = "albor-wayland-popup-reopen";
    popup_options.size = Size{120.0f, 80.0f};
    popup_options.role = WindowRole::Popup;
    popup_options.native_parent = parent->native_window_handle();
    popup_options.anchor = Rect{.x = 20.0f, .y = 20.0f, .width = 80.0f, .height = 24.0f};
    const std::unique_ptr<PlatformWindow> popup = PlatformWindow::create(popup_options);
    ASSERT_NE(popup, nullptr);
    popup->show();
    ASSERT_TRUE(popup->visible());
    // arrange the canvas the popup presents through (§6.3)
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(popup->native_handle(), popup->metrics());
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(surface->valid());
    // act one frame is presented, which the compositor receives (§6.3)
    surface->begin_frame();
    surface->end_frame();
    ASSERT_EQ(surface->presented_frames(), 1u);
    // act the popup is hidden and shown again, which the control does when it reopens (§5.1)
    popup->hide();
    EXPECT_FALSE(popup->visible());
    popup->show();
    surface->begin_frame();
    surface->end_frame();
    // assert the reopened popup presented again, which a spent surface would not (§5.1, §6.3)
    EXPECT_TRUE(popup->visible());
    EXPECT_EQ(surface->presented_frames(), 2u);
    // the popup and its parent are closed, which takes both surfaces down (§5.2)
    popup->close();
    parent->close();
}

// the suite refuses a handle that names no wayland surface, because a fallback would silently draw nothing (§6.3)
TEST(Blend2DWaylandChecks, RefusesAHandleWithoutNativeObjects) {
    // arrange a native surface bundle with no objects, which is what an unbound handle looks like (§3.5, AD-05)
    const wayland::WaylandNativeSurface empty;
    // act
    const std::unique_ptr<RenderSurface> surface = blend2d::create_window_surface(NativeWindowHandle::from_object(&empty), WindowMetrics::from_logical(Size{64.0f, 48.0f}, 1.0f));
    // assert the refusal came back, because a raster fallback here would draw a blank window (§6.3)
    EXPECT_EQ(surface, nullptr);
}
