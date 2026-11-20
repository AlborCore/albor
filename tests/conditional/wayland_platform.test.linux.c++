#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "albor/core/clipboard.h"
#include "albor/core/env.h"
#include "albor/platform/linux/common/input_normalization.h"
#include "albor/platform/linux/wayland/event_loop.h"
#include "albor/platform/linux/wayland/native_surface.linux.h"
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

        // every appearance change the platform reported, which a portal setting change raises (§8.4)
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

        // override: ignore the pointer exit, which only a compositor pointer raises (§8.3)
        void on_platform_pointer_exited() override {}

        // override: count the appearance change (§8.4)
        void on_platform_appearance_changed(const SystemAppearance&) override { appearance_changes++; }
    };
} // namespace

TEST(WaylandPlatform, RefusesAWindowWithoutACompositor) {
    // arrange a display name that cannot connect, which the adapter must refuse (§5.3)
    const std::optional<std::string> saved_display = albor::core::get_environment_variable("WAYLAND_DISPLAY");
    const std::string saved = saved_display.value_or("");
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

TEST(WaylandPlatform, ShowsARendererOwnedSurfaceWithoutAPlaceholderBuffer) {
    // arrange a window whose surface a renderer presents through, which owns every commit it makes (§6.3)
    WindowOptions options;
    options.title = "albor-wayland-renderer-owned";
    options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    TestWindowListener listener;
    window->set_listener(&listener);
    auto* native = static_cast<albor::wayland::WaylandNativeSurface*>(const_cast<void*>(window->native_handle().object()));
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native->state, nullptr);
    // the renderer claims the surface, which is what the vulkan wsi does when it creates a swapchain (§6.3)
    native->state->renderer_presenting = true;
    // act the window is shown, which maps the surface for the renderer's own next buffer (§5.1)
    window->show();
    // assert the window is on screen and mapped without a platform buffer, which explicit sync refuses (§6.3)
    EXPECT_TRUE(window->visible());
    EXPECT_TRUE(native->state->mapped);
    EXPECT_GT(window->metrics().logical_size.width, 0.0f);
    // act the window is hidden and shown again, which a reopened popup does (§5.1)
    window->hide();
    EXPECT_FALSE(window->visible());
    window->show();
    // assert the second show maps it again, because the renderer presents the frame that follows (§6.1)
    EXPECT_TRUE(window->visible());
    EXPECT_TRUE(native->state->mapped);
    window->close();
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
    if (const std::optional<std::string> expected = albor::core::get_environment_variable("ALBOR_EXPECT_SCALE"); expected.has_value())
        EXPECT_FLOAT_EQ(metrics.scale_factor, std::stof(*expected));
    window->close();
}

TEST(WaylandPlatform, FractionalScaleMapsPreferredScaleToMetrics) {
    // act, assert one hundred twenty is the identity, which the protocol names as one logical pixel (AD-27)
    EXPECT_FLOAT_EQ(fractional_scale_factor(120), 1.0f);
    // act, assert a 1.25 desktop arrives as one hundred fifty, which the metric reports (AD-27)
    EXPECT_FLOAT_EQ(fractional_scale_factor(150), 1.25f);
    // act, assert a 1.5 desktop arrives as one hundred eighty, which the metric reports (AD-27)
    EXPECT_FLOAT_EQ(fractional_scale_factor(180), 1.5f);
    // act, assert a compositor that names no scale falls back to the identity (AD-27)
    EXPECT_FLOAT_EQ(fractional_scale_factor(0), 1.0f);
    // act, assert the physical extent at 1.5 is the logical extent times the factor (AD-27)
    const WindowMetrics at_1_5 = WindowMetrics::from_logical(Size{240.0f, 160.0f}, 1.5f);
    EXPECT_FLOAT_EQ(at_1_5.scale_factor, 1.5f);
    EXPECT_FLOAT_EQ(at_1_5.pixel_size.width, 360.0f);
    EXPECT_FLOAT_EQ(at_1_5.pixel_size.height, 240.0f);
    // act, assert the physical extent at 1.25 follows the same mapping (AD-27)
    const WindowMetrics at_1_25 = WindowMetrics::from_logical(Size{320.0f, 200.0f}, 1.25f);
    EXPECT_FLOAT_EQ(at_1_25.pixel_size.width, 400.0f);
    EXPECT_FLOAT_EQ(at_1_25.pixel_size.height, 250.0f);
    // act, assert the viewport destination is the logical extent the buffer is scaled onto (AD-27)
    EXPECT_EQ(viewport_destination(Size{240.0f, 160.0f}), (Size{240.0f, 160.0f}));
    // act, assert the destination rounds halfway away from zero, which the toplevel rule names (AD-27)
    EXPECT_EQ(viewport_destination(Size{240.5f, 160.5f}), (Size{241.0f, 161.0f}));
}

TEST(WaylandPlatform, FractionalScaleFallsBackWithoutBothProtocols) {
    // act, assert only the pair of protocols enables the fractional mapping (AD-27)
    EXPECT_TRUE(fractional_scale_available(true, true));
    // act, assert a compositor without the viewporter keeps the integer scale (AD-27)
    EXPECT_FALSE(fractional_scale_available(true, false));
    // act, assert a compositor without the manager keeps the integer scale (AD-27)
    EXPECT_FALSE(fractional_scale_available(false, true));
    // act, assert a compositor with neither keeps the integer scale (AD-27)
    EXPECT_FALSE(fractional_scale_available(false, false));
    // arrange a shown window on whichever compositor the suite runs against (AD-27)
    WindowOptions options;
    options.title = "albor-wayland-fractional-fallback";
    options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const auto* wayland = dynamic_cast<const WaylandPlatformWindow*>(window.get());
    ASSERT_NE(wayland, nullptr);
    // act
    const WindowMetrics metrics = window->metrics();
    // assert a compositor without the protocols reports an integral factor, which the fallback path applies (AD-27)
    if (!wayland->fractional_scale_active())
        EXPECT_FLOAT_EQ(metrics.scale_factor, static_cast<float>(static_cast<int>(metrics.scale_factor)));
    // assert the physical extent follows the reported factor on either path (AD-27)
    EXPECT_FLOAT_EQ(metrics.pixel_size.width, metrics.logical_size.width * metrics.scale_factor);
    EXPECT_FLOAT_EQ(metrics.pixel_size.height, metrics.logical_size.height * metrics.scale_factor);
    window->close();
}

TEST(WaylandPlatform, FractionalScaleReportsThePhysicalExtent) {
    // arrange a shown window, which a compositor with both protocols reports the preference on (AD-27)
    WindowOptions options;
    options.title = "albor-wayland-fractional";
    options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    ASSERT_TRUE(window->visible());
    const auto* wayland = dynamic_cast<const WaylandPlatformWindow*>(window.get());
    ASSERT_NE(wayland, nullptr);
    // a compositor without both protocols cannot express the preference, which the fallback test covers (AD-27)
    if (!wayland->fractional_scale_active())
        GTEST_SKIP() << "the compositor does not advertise fractional scale, so the fractional path is not exercised";
    // act
    const WindowMetrics metrics = window->metrics();
    // assert the factor is a whole number of one hundred twentieths, which the protocol carries (AD-27)
    EXPECT_NEAR(metrics.scale_factor * 120.0f, std::round(metrics.scale_factor * 120.0f), 0.001f);
    // assert the physical extent is the logical extent at the reported factor (AD-27)
    EXPECT_FLOAT_EQ(metrics.pixel_size.width, metrics.logical_size.width * metrics.scale_factor);
    EXPECT_FLOAT_EQ(metrics.pixel_size.height, metrics.logical_size.height * metrics.scale_factor);
    // a run that names the expected factor asserts it exactly, which the 1.25 and 1.5 runners pass in (AD-27)
    if (const std::optional<std::string> expected = albor::core::get_environment_variable("ALBOR_EXPECT_SCALE"); expected.has_value())
        EXPECT_FLOAT_EQ(metrics.scale_factor, std::stof(*expected));
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
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_SPACE), Key::Space);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_RETURN), Key::Return);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_KEYPAD_ENTER), Key::Return);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_ESCAPE), Key::Escape);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_LEFT), Key::ArrowLeft);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_F1), Key::F1);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_A_LOWER), Key::A);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_Z_UPPER), Key::Z);
    EXPECT_EQ(linux_platform::key_from_keysym(linux_platform::KEYSYM_ZERO), Key::Zero);
    // act, assert the modifier set maps onto the portable flags (§8.3)
    const Modifiers modifiers = linux_platform::modifiers_from_state(true, false, true, false);
    EXPECT_TRUE(modifiers.shift);
    EXPECT_FALSE(modifiers.control);
    EXPECT_TRUE(modifiers.option);
    EXPECT_FALSE(modifiers.command);
}

TEST(WaylandPlatform, AKeyCommitsTheTextItTypes) {
    // arrange the modifier sets a plain press and an accelerator chord carry (§8.3)
    const Modifiers plain = linux_platform::modifiers_from_state(false, false, false, false);
    const Modifiers chorded = linux_platform::modifiers_from_state(false, true, false, false);
    // act, assert a printable character commits the utf8 the keymap produced (§8.3)
    EXPECT_EQ(linux_platform::text_from_key(plain, "A", 1), "A");
    EXPECT_EQ(linux_platform::text_from_key(plain, " ", 1), " ");
    EXPECT_EQ(linux_platform::text_from_key(plain, "\xc3\xa9", 2), "\xc3\xa9");
    // act, assert a named key and an accelerator chord commit no text (§8.3)
    EXPECT_TRUE(linux_platform::text_from_key(plain, "\r", 1).empty());
    EXPECT_TRUE(linux_platform::text_from_key(plain, "\t", 1).empty());
    EXPECT_TRUE(linux_platform::text_from_key(plain, "\x7f", 1).empty());
    EXPECT_TRUE(linux_platform::text_from_key(chorded, "c", 1).empty());
    EXPECT_TRUE(linux_platform::text_from_key(plain, nullptr, 0).empty());
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
