#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "albor/core/clipboard.h"
#include "albor/platform/linux/wayland/event_loop.h"
#include "albor/platform/linux/wayland/input_normalization.h"
#include "albor/platform/linux/wayland/platform_window.h"
#include "albor/ui/ui_event.h"
#include "albor/window/platform_window.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

using namespace albor;

namespace
{
    // the listener a test window reports its events through, which the framework never owns (§5.3)
    class TestWindowListener final : public PlatformWindowListener
    {
    public:
        // every resize the platform reported, which a show and a resize raise (§5.3)
        std::size_t resizes = 0;

        // every close request the platform raised, which a compositor close would reach (§5.2)
        std::size_t close_requests = 0;

        // every close the platform reported, which a granted request follows (§5.2)
        std::size_t closed = 0;

        // every frame the pacing source raised, which an event loop tick would reach (§4.3)
        std::size_t frames = 0;

        // every normalized input event the platform raised, which the pointer and keyboard tests read (§8.3)
        std::size_t events = 0;

        // every focus loss the platform reported, which a drag cancellation follows (§8.3)
        std::size_t focus_losses = 0;

        // every appearance change the platform reported, which the portal path will raise in #234 (§8.4)
        std::size_t appearance_changes = 0;

        // override: count the resize, which a show and a set_size raise (§5.3)
        void on_platform_resize(const WindowMetrics&) override { resizes++; }

        // override: grant every close a compositor asks for, which the test window expects (§5.2)
        bool on_platform_close_request() override {
            close_requests++;
            return true;
        }

        // override: count the close the platform reported (§5.2)
        void on_platform_closed() override { closed++; }

        // override: count the frame the pacing source raised (§4.3)
        void on_platform_frame() override { frames++; }

        // override: count the normalized input event (§8.3)
        void on_platform_event(const Event&) override { events++; }

        // override: count the focus loss (§8.3)
        void on_platform_focus_lost() override { focus_losses++; }

        // override: count the appearance change (§8.4)
        void on_platform_appearance_changed(const SystemAppearance&) override { appearance_changes++; }
    };
} // namespace

TEST(WaylandPlatform, RefusesAWindowWithoutACompositor) {
    // arrange a display name that cannot connect, which the adapter must refuse (§5.3)
    const char* saved_display = std::getenv("WAYLAND_DISPLAY");
    const std::string saved = saved_display != nullptr ? saved_display : "";
    setenv("WAYLAND_DISPLAY", "/nonexistent-albor-wayland", 1);
    WindowOptions options;
    options.title = "albor-wayland-refusal";
    options.size = Size{200.0f, 120.0f};
    // act
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    // assert no compositor leaves no window, which the null reports (§5.3)
    EXPECT_EQ(window, nullptr);
    // the display name is restored, which the window tests connect through (§5.3)
    if (saved.empty())
        unsetenv("WAYLAND_DISPLAY");
    else
        setenv("WAYLAND_DISPLAY", saved.c_str(), 1);
}

TEST(WaylandPlatform, CreatesShowsResizesAndClosesAWindow) {
    // arrange a normal window under the headless compositor the suite runs against (§5.3)
    WindowOptions options;
    options.title = "albor-wayland-window";
    options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    TestWindowListener listener;
    window->set_listener(&listener);
    // act the window is shown, which maps its surface and reports its metrics (§5.3)
    window->show();
    // assert the window is on screen and carries a valid renderer handle (§5.3, §3.5)
    EXPECT_TRUE(window->visible());
    EXPECT_TRUE(window->native_handle().valid());
    const WindowMetrics shown = window->metrics();
    EXPECT_GT(shown.logical_size.width, 0.0f);
    EXPECT_GT(shown.logical_size.height, 0.0f);
    // act the window asks for another frame, which the pacing source owns (§4.3)
    window->request_frame();
    // assert the request is recorded until the frame is drawn (§4.3)
    EXPECT_TRUE(window->frame_requested());
    // act the window is resized, which the compositor applies through a configure (§5.3)
    window->set_size(Size{300.0f, 200.0f});
    // assert the metrics follow the requested logical extent (§5.3)
    EXPECT_FLOAT_EQ(window->metrics().logical_size.width, 300.0f);
    EXPECT_FLOAT_EQ(window->metrics().logical_size.height, 200.0f);
    // act the window is hidden, which detaches its buffer (§5.3)
    window->hide();
    // assert the window is off screen (§5.3)
    EXPECT_FALSE(window->visible());
    // act the framework closes the window, which raises no close request (§5.2)
    window->close();
    // assert the close detached the surface and left no pending frame (§5.2, §4.3)
    EXPECT_FALSE(window->visible());
    EXPECT_FALSE(window->frame_requested());
    // assert the platform reported the resize it applied (§5.3)
    EXPECT_GT(listener.resizes, 0u);
}

TEST(WaylandPlatform, CreatesAPopupAgainstItsParent) {
    // arrange a parent window, which the popup anchors to and follows (§5.1)
    WindowOptions parent_options;
    parent_options.title = "albor-wayland-parent";
    parent_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> parent = PlatformWindow::create(parent_options);
    ASSERT_NE(parent, nullptr);
    parent->show();
    ASSERT_TRUE(parent->visible());
    // the popup names the parent through the opaque native handle, which the adapter casts back (§5.1)
    WindowOptions popup_options;
    popup_options.title = "albor-wayland-popup";
    popup_options.size = Size{120.0f, 80.0f};
    popup_options.role = WindowRole::Popup;
    popup_options.native_parent = parent->native_window_handle();
    popup_options.anchor = Rect{.x = 20.0f, .y = 20.0f, .width = 80.0f, .height = 24.0f};
    // act
    const std::unique_ptr<PlatformWindow> popup = PlatformWindow::create(popup_options);
    // assert the popup was built against its parent and maps its own surface (§5.1)
    ASSERT_NE(popup, nullptr);
    popup->show();
    EXPECT_TRUE(popup->visible());
    EXPECT_TRUE(popup->native_handle().valid());
    EXPECT_GT(popup->metrics().logical_size.width, 0.0f);
    // act the popup and the parent are closed, which takes both surfaces down (§5.2)
    popup->close();
    parent->close();
    // assert the parent is off screen after its close (§5.2)
    EXPECT_FALSE(parent->visible());
}

TEST(WaylandPlatform, MetricsFollowTheCompositorScale) {
    // arrange a shown window on the compositor the suite runs against, which may carry a scale (§5.3, AD-27)
    WindowOptions options;
    options.title = "albor-wayland-scale";
    options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    // act
    const WindowMetrics metrics = window->metrics();
    // assert the physical extent is the logical extent at the reported scale (AD-27)
    ASSERT_TRUE(metrics.valid());
    EXPECT_GT(metrics.scale_factor, 0.0f);
    EXPECT_FLOAT_EQ(metrics.pixel_size.width, metrics.logical_size.width * metrics.scale_factor);
    EXPECT_FLOAT_EQ(metrics.pixel_size.height, metrics.logical_size.height * metrics.scale_factor);
    // a run the compositor scaled expects the exact factor, which the headless runner passes in (§5.3)
    if (const char* expected = std::getenv("ALBOR_EXPECT_SCALE"); expected != nullptr)
        EXPECT_FLOAT_EQ(metrics.scale_factor, std::stof(expected));
    window->close();
}

TEST(WaylandPlatform, ActivationIsReachableAndReleasesItsToken) {
    // arrange a shown window, which the compositor may raise through xdg activation (§5.1)
    WindowOptions options;
    options.title = "albor-wayland-activation";
    options.size = Size{200.0f, 120.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    // act the window is activated twice, which a superseded token must survive (§5.1)
    window->activate();
    window->activate();
    // assert the window stayed on screen, which the no-op fallback and the granted path share (§5.1)
    EXPECT_TRUE(window->visible());
    // act the window closes with a token possibly unanswered, which the teardown releases (§5.1)
    window->close();
    // assert the close left the window off screen (§5.2)
    EXPECT_FALSE(window->visible());
}

TEST(WaylandPlatform, PrefersNativeDecorationsOverLibdecor) {
    // act, assert the compositor chrome wins whenever xdg-decoration is advertised (§5.3)
    EXPECT_EQ(choose_decoration(true, true), WaylandDecorationMode::ServerSide);
    EXPECT_EQ(choose_decoration(true, false), WaylandDecorationMode::ServerSide);
    // act, assert libdecor is the fallback where the compositor has no xdg-decoration (§5.3)
    EXPECT_EQ(choose_decoration(false, true), WaylandDecorationMode::ClientSide);
    // act, assert a host with neither still opens the window, without chrome (§5.3)
    EXPECT_EQ(choose_decoration(false, false), WaylandDecorationMode::None);
}

TEST(WaylandPlatform, SelectsADecorationForAWindow) {
    // arrange a shown window on the headless compositor, which uses one of the two decoration paths (§5.3)
    WindowOptions options;
    options.title = "albor-wayland-decoration";
    options.size = Size{200.0f, 120.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    // act the platform window reports the decoration it selected (§5.3)
    const auto* wayland = dynamic_cast<const WaylandPlatformWindow*>(window.get());
    ASSERT_NE(wayland, nullptr);
    // assert the selected decoration and the chrome that carries it agree (§5.3)
    const WaylandDecorationMode mode = wayland->decoration_mode();
    if (mode == WaylandDecorationMode::ClientSide)
        EXPECT_TRUE(wayland->uses_client_side_frame());
    else
        EXPECT_FALSE(wayland->uses_client_side_frame());
    window->close();
}

TEST(WaylandPlatform, CreatesAPopupUnderAClientSideDecoratedWindow) {
    // arrange a parent window whose decoration the compositor answers (§5.3)
    WindowOptions parent_options;
    parent_options.title = "albor-wayland-decorated-parent";
    parent_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> parent = PlatformWindow::create(parent_options);
    ASSERT_NE(parent, nullptr);
    const auto* wayland_parent = dynamic_cast<const WaylandPlatformWindow*>(parent.get());
    ASSERT_NE(wayland_parent, nullptr);
    // the frame path needs a compositor without server-side chrome, which headless weston does not provide (§5.3)
    if (wayland_parent->decoration_mode() != WaylandDecorationMode::ClientSide)
        GTEST_SKIP() << "the compositor draws server-side decorations, so the libdecor parent path is not exercised";
    parent->show();
    ASSERT_TRUE(parent->visible());
    // arrange a popup parented to the decorated window, which needs the frame-owned shell surface (§5.1)
    WindowOptions popup_options;
    popup_options.title = "albor-wayland-decorated-popup";
    popup_options.size = Size{120.0f, 80.0f};
    popup_options.role = WindowRole::Popup;
    popup_options.native_parent = parent->native_window_handle();
    popup_options.anchor = Rect{.x = 20.0f, .y = 20.0f, .width = 80.0f, .height = 24.0f};
    // act
    const std::unique_ptr<PlatformWindow> popup = PlatformWindow::create(popup_options);
    // assert the parent's frame-owned role accepted the popup (§5.1, §5.3)
    ASSERT_NE(popup, nullptr);
    popup->show();
    EXPECT_TRUE(popup->visible());
    popup->close();
    parent->close();
}

TEST(WaylandPlatform, MapsXkbKeysAndModifiers) {
    // act, assert the named keys and the modifier set map onto the portable names (§8.3)
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_SPACE), Key::Space);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_RETURN), Key::Return);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_KEYPAD_ENTER), Key::Return);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_ESCAPE), Key::Escape);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_LEFT), Key::ArrowLeft);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_F1), Key::F1);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_A_LOWER), Key::A);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_Z_UPPER), Key::Z);
    EXPECT_EQ(wayland::key_from_keysym(wayland::KEYSYM_ZERO), Key::Zero);
    // act, assert the modifier set maps onto the portable flags (§8.3)
    const Modifiers modifiers = wayland::modifiers_from_state(true, false, true, false);
    EXPECT_TRUE(modifiers.shift);
    EXPECT_FALSE(modifiers.control);
    EXPECT_TRUE(modifiers.option);
    EXPECT_FALSE(modifiers.command);
}

TEST(WaylandPlatform, TheEventLoopPacesFrames) {
    // arrange a loop over the headless compositor, whose pacing timer raises the first tick (§4.3)
    wayland::WaylandEventLoop loop;
    EXPECT_GT(loop.refresh_hz(), 0.0);
    std::atomic<bool> pumped{false};
    loop.set_pump_handler([&pumped, &loop] {
        pumped = true;
        loop.quit();
    });
    // act the loop runs on its own thread until the first pump stops it (§4.3)
    loop.start_frame_pacing();
    std::thread runner([&loop] { loop.run(); });
    runner.join();
    // assert the pacing source delivered a frame, which is what the loop exists for (§4.3)
    EXPECT_TRUE(pumped.load());
}

TEST(WaylandPlatform, InstallsTheClipboard) {
    // act the platform auto install registers the data device clipboard (§3.5)
    Clipboard* clipboard = process_clipboard();
    // assert the clipboard is reachable and carries no text before a copy (§3.5)
    ASSERT_NE(clipboard, nullptr);
    EXPECT_TRUE(clipboard->text().empty());
    EXPECT_FALSE(clipboard->has_text());
    // a headless compositor has no seat, so the data device is not ready, which the report states (§3.5)
    EXPECT_FALSE(clipboard->ready());
}
