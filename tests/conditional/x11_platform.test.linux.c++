#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <xcb/xcb.h>

#include "albor/core/geometry.h"
#include "albor/platform/linux/x11/event_dispatch.linux.h"
#include "albor/platform/linux/x11/native_surface.linux.h"
#include "albor/platform/linux/x11/platform_window.linux.h"
#include "albor/ui/ui_event.h"
#include "albor/window/platform_window.h"
#include "albor/window/window_metrics.h"
#include "albor/window/window_options.h"

using namespace albor;

namespace
{
    // the listener a test window reports its events through, which the framework never owns (§5.3)
    class X11TestListener final : public PlatformWindowListener
    {
    public:
        // every close request the window manager raised, which a granted request follows (§5.2)
        std::size_t close_requests = 0;

        // every resize the platform reported, which a configure raises (§5.3)
        std::size_t resizes = 0;

        // every close the platform reported, which a granted request and a dismissal raise (§5.2)
        std::size_t closed = 0;

        // the events the platform normalized, which the key and pointer tests read (§8.3)
        std::vector<Event> events;

        // override: count the resize, which a configure raises (§5.3)
        void on_platform_resize(const WindowMetrics&) override { resizes++; }

        // override: grant every close a window manager asks for, which the test window expects (§5.2)
        bool on_platform_close_request() override {
            close_requests++;
            return true;
        }

        // override: count the close, which the framework tears the window down for (§5.2)
        void on_platform_closed() override { closed++; }

        // override: the frame pacing is driven by the loop, which this suite does not run (§4.3)
        void on_platform_frame() override {}

        // override: keep the normalized event, which the input tests read (§8.3)
        void on_platform_event(const Event& event) override { events.push_back(event); }

        // override: the focus loss needs no handling in these tests (§8.3)
        void on_platform_focus_lost() override {}

        // override: the pointer exit needs no handling in these tests (§8.3)
        void on_platform_pointer_exited() override {}

        // override: the appearance change needs no handling in these tests (§8.4)
        void on_platform_appearance_changed(const SystemAppearance&) override {}
    };
} // namespace

TEST(X11Platform, CreatesShowsResizesAndClosesAWindow) {
    // arrange a display, which the x11 runner provides and a wayland session does not reach (§5.3)
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    WindowOptions options;
    options.title = "albor-x11-window";
    options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    // assert the window exists with a renderer handle, which the adapter reports (§5.3, §3.5)
    ASSERT_NE(window, nullptr);
    // act the window is shown
    window->show();
    // assert it is on screen and carries a valid native handle
    EXPECT_TRUE(window->visible());
    EXPECT_TRUE(window->native_handle().valid());
    const WindowMetrics shown = window->metrics();
    EXPECT_FLOAT_EQ(shown.logical_size.width, 240.0f);
    EXPECT_FLOAT_EQ(shown.logical_size.height, 160.0f);
    // act the window is resized, which the window manager applies
    window->set_size(Size{300.0f, 200.0f});
    // assert the requested extent is the one the window presents at
    EXPECT_FLOAT_EQ(window->metrics().logical_size.width, 300.0f);
    EXPECT_FLOAT_EQ(window->metrics().logical_size.height, 200.0f);
    // act the window is hidden
    window->hide();
    // assert it is off screen
    EXPECT_FALSE(window->visible());
    // act the window is closed, which takes the native window down
    window->close();
    // assert the handle reports the closed window
    EXPECT_FALSE(window->native_handle().valid());
}

TEST(X11Platform, ClosesOnTheWindowManagerDeleteProtocol) {
    // arrange a shown window and the delete atoms a window manager sends (§5.2)
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    WindowOptions options;
    options.title = "albor-x11-close";
    options.size = Size{120.0f, 80.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    X11TestListener listener;
    window->set_listener(&listener);
    window->show();
    const auto* native = static_cast<const x11::X11NativeSurface*>(window->native_handle().object());
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native->state, nullptr);
    const std::uint16_t protocols_length = static_cast<std::uint16_t>(std::strlen("WM_PROTOCOLS"));
    const xcb_intern_atom_cookie_t protocols_cookie = xcb_intern_atom(native->state->connection, 0, protocols_length, "WM_PROTOCOLS");
    xcb_intern_atom_reply_t* protocols = xcb_intern_atom_reply(native->state->connection, protocols_cookie, nullptr);
    ASSERT_NE(protocols, nullptr);
    const std::uint16_t delete_length = static_cast<std::uint16_t>(std::strlen("WM_DELETE_WINDOW"));
    const xcb_intern_atom_cookie_t delete_cookie = xcb_intern_atom(native->state->connection, 0, delete_length, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t* delete_window = xcb_intern_atom_reply(native->state->connection, delete_cookie, nullptr);
    ASSERT_NE(delete_window, nullptr);
    xcb_client_message_event_t message{};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = native->state->window;
    message.type = protocols->atom;
    message.data.data32[0] = delete_window->atom;
    // act the window manager asks the window to close
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&message));
    // assert the listener was consulted, the window went down and the framework was told (§5.2)
    EXPECT_EQ(listener.close_requests, 1u);
    EXPECT_EQ(listener.closed, 1u);
    EXPECT_FALSE(window->native_handle().valid());
    std::free(protocols);
    std::free(delete_window);
}

TEST(X11Platform, MapsKeysButtonsAndWheelFromXcbEvents) {
    // arrange a shown window and the raw events an x server sends (§8.3)
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    WindowOptions options;
    options.title = "albor-x11-input";
    options.size = Size{120.0f, 80.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    X11TestListener listener;
    window->set_listener(&listener);
    window->show();
    const auto* native = static_cast<const x11::X11NativeSurface*>(window->native_handle().object());
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native->state, nullptr);
    xcb_key_press_event_t key{};
    key.response_type = XCB_KEY_PRESS;
    key.detail = 38;
    key.event = native->state->window;
    // act a plain letter press and release
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&key));
    key.response_type = XCB_KEY_RELEASE;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&key));
    // assert the key, the character it types and its release reach the portable set (§8.3)
    ASSERT_EQ(listener.events.size(), 3u);
    EXPECT_EQ(listener.events[0].kind, EventKind::KeyDown);
    EXPECT_EQ(listener.events[0].key, Key::A);
    EXPECT_EQ(listener.events[1].kind, EventKind::TextInput);
    EXPECT_EQ(listener.events[1].text, "a");
    EXPECT_EQ(listener.events[2].kind, EventKind::KeyUp);
    // act a primary press and release at one point
    xcb_button_press_event_t button{};
    button.response_type = XCB_BUTTON_PRESS;
    button.detail = 1;
    button.event = native->state->window;
    button.event_x = 10;
    button.event_y = 20;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&button));
    button.response_type = XCB_BUTTON_RELEASE;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&button));
    // assert the pointer pair carries the logical position and the primary button
    ASSERT_EQ(listener.events.size(), 5u);
    EXPECT_EQ(listener.events[3].kind, EventKind::PointerDown);
    EXPECT_EQ(listener.events[3].button, PointerButton::Left);
    EXPECT_FLOAT_EQ(listener.events[3].position.x, 10.0f);
    EXPECT_FLOAT_EQ(listener.events[3].position.y, 20.0f);
    EXPECT_EQ(listener.events[4].kind, EventKind::PointerUp);
    // act one wheel notch and its release, which carries no portable event
    button.response_type = XCB_BUTTON_PRESS;
    button.detail = 4;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&button));
    button.response_type = XCB_BUTTON_RELEASE;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&button));
    // assert the notch is one wheel event and the release adds nothing
    ASSERT_EQ(listener.events.size(), 6u);
    EXPECT_EQ(listener.events[5].kind, EventKind::Wheel);
    EXPECT_FLOAT_EQ(listener.events[5].scroll, 1.0f);
}

TEST(X11Platform, ReportsARequestedFrame) {
    // arrange a shown window
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    WindowOptions options;
    options.title = "albor-x11-frame";
    options.size = Size{120.0f, 80.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    ASSERT_NE(window, nullptr);
    window->show();
    // act the window asks for another frame
    window->request_frame();
    // assert the request is recorded until the loop draws it (§4.3)
    EXPECT_TRUE(window->frame_requested());
    // act the window is hidden
    window->hide();
    // assert a hidden window has no frame to draw, which the other adapters also clear (§4.3)
    EXPECT_FALSE(window->frame_requested());
    // act the window is shown and asks again, then closed
    window->show();
    window->request_frame();
    window->close();
    // assert a closed window has no frame to draw either (§4.3)
    EXPECT_FALSE(window->frame_requested());
}

TEST(X11Platform, MapsTheDeviceScaleOntoPixelsAndLogicalInput) {
    // arrange a hidpi session, which the adapter resolves once per window (AD-27)
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    setenv("GDK_SCALE", "2", 1);
    WindowOptions options;
    options.title = "albor-x11-scale";
    options.size = Size{120.0f, 80.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    unsetenv("GDK_SCALE");
    ASSERT_NE(window, nullptr);
    // assert the logical request became the device extent the server sizes in (AD-27)
    const WindowMetrics metrics = window->metrics();
    EXPECT_FLOAT_EQ(metrics.scale_factor, 2.0f);
    EXPECT_FLOAT_EQ(metrics.logical_size.width, 120.0f);
    EXPECT_FLOAT_EQ(metrics.pixel_size.width, 240.0f);
    EXPECT_FLOAT_EQ(metrics.pixel_size.height, 160.0f);
    const auto* native = static_cast<const x11::X11NativeSurface*>(window->native_handle().object());
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native->state, nullptr);
    xcb_get_geometry_cookie_t geometry_cookie = xcb_get_geometry(native->state->connection, native->state->window);
    xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(native->state->connection, geometry_cookie, nullptr);
    ASSERT_NE(geometry, nullptr);
    // assert the native window carries the device extent, which is what the renderer presents (AD-27)
    EXPECT_EQ(geometry->width, 240);
    EXPECT_EQ(geometry->height, 160);
    std::free(geometry);
    // act a motion and a press arrive in device pixels, which the framework reads in logical units (AD-27)
    X11TestListener listener;
    window->set_listener(&listener);
    xcb_motion_notify_event_t motion{};
    motion.response_type = XCB_MOTION_NOTIFY;
    motion.event = native->state->window;
    motion.event_x = 100;
    motion.event_y = 50;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&motion));
    xcb_button_press_event_t press{};
    press.response_type = XCB_BUTTON_PRESS;
    press.event = native->state->window;
    press.detail = 1;
    press.event_x = 20;
    press.event_y = 10;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&press));
    // assert both positions were divided by the resolved scale (AD-27)
    ASSERT_EQ(listener.events.size(), 2u);
    EXPECT_FLOAT_EQ(listener.events[0].position.x, 50.0f);
    EXPECT_FLOAT_EQ(listener.events[0].position.y, 25.0f);
    EXPECT_FLOAT_EQ(listener.events[1].position.x, 10.0f);
    EXPECT_FLOAT_EQ(listener.events[1].position.y, 5.0f);
    // act the window manager reports a configure in device pixels (AD-27)
    xcb_configure_notify_event_t configure{};
    configure.response_type = XCB_CONFIGURE_NOTIFY;
    configure.event = native->state->window;
    configure.window = native->state->window;
    configure.width = 200;
    configure.height = 100;
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&configure));
    // assert the reported logical extent divides the scale out, which the layout measures against (AD-27)
    EXPECT_FLOAT_EQ(window->metrics().logical_size.width, 100.0f);
    EXPECT_FLOAT_EQ(window->metrics().logical_size.height, 50.0f);
    EXPECT_FLOAT_EQ(window->metrics().pixel_size.width, 200.0f);
}

TEST(X11Platform, CreatesAPopupAsAnUndecoratedTransientAtItsAnchor) {
    // arrange a shown owner, which the popup anchors to (§5.1)
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    WindowOptions owner_options;
    owner_options.title = "albor-x11-popup-owner";
    owner_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> owner = PlatformWindow::create(owner_options);
    ASSERT_NE(owner, nullptr);
    const auto* owner_native = static_cast<const x11::X11NativeSurface*>(owner->native_handle().object());
    ASSERT_NE(owner_native, nullptr);
    // the owner is kept unmapped, because a window manager places a mapped owner on its own (§5.1)
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(owner_native->state->connection)).data;
    xcb_translate_coordinates_cookie_t owner_cookie = xcb_translate_coordinates(owner_native->state->connection, owner_native->state->window, screen->root, 0, 0);
    xcb_translate_coordinates_reply_t* owner_origin = xcb_translate_coordinates_reply(owner_native->state->connection, owner_cookie, nullptr);
    ASSERT_NE(owner_origin, nullptr);
    const std::int16_t owner_x = owner_origin->dst_x;
    const std::int16_t owner_y = owner_origin->dst_y;
    std::free(owner_origin);
    WindowOptions popup_options;
    popup_options.title = "albor-x11-popup";
    popup_options.size = Size{120.0f, 80.0f};
    popup_options.role = WindowRole::Popup;
    popup_options.native_parent = owner->native_window_handle();
    popup_options.anchor = Rect{.x = 10.0f, .y = 20.0f, .width = 80.0f, .height = 24.0f};
    const std::unique_ptr<PlatformWindow> popup = PlatformWindow::create(popup_options);
    ASSERT_NE(popup, nullptr);
    X11TestListener listener;
    popup->set_listener(&listener);
    const auto* popup_native = static_cast<const x11::X11NativeSurface*>(popup->native_handle().object());
    ASSERT_NE(popup_native, nullptr);
    // act the popup is shown
    popup->show();
    ASSERT_TRUE(popup->visible());
    // assert the window manager never manages it, which an override redirect window says (§5.1)
    xcb_get_window_attributes_cookie_t attributes_cookie = xcb_get_window_attributes(popup_native->state->connection, popup_native->state->window);
    xcb_get_window_attributes_reply_t* attributes = xcb_get_window_attributes_reply(popup_native->state->connection, attributes_cookie, nullptr);
    ASSERT_NE(attributes, nullptr);
    EXPECT_TRUE(attributes->override_redirect);
    std::free(attributes);
    // assert the popup hangs under the trigger, which keeps the current selection visible (§5.1)
    xcb_get_geometry_cookie_t popup_geometry_cookie = xcb_get_geometry(popup_native->state->connection, popup_native->state->window);
    xcb_get_geometry_reply_t* popup_geometry = xcb_get_geometry_reply(popup_native->state->connection, popup_geometry_cookie, nullptr);
    ASSERT_NE(popup_geometry, nullptr);
    EXPECT_EQ(popup_geometry->x, owner_x + 10);
    EXPECT_EQ(popup_geometry->y, owner_y + 20 + 24);
    EXPECT_EQ(popup_geometry->width, 120);
    EXPECT_EQ(popup_geometry->height, 80);
    std::free(popup_geometry);
    // act a second popup anchors at the screen bottom, where there is no room below it (§5.1)
    WindowOptions flipped_options = popup_options;
    flipped_options.anchor = Rect{.x = 10.0f, .y = 1000.0f, .width = 80.0f, .height = 24.0f};
    const std::unique_ptr<PlatformWindow> flipped = PlatformWindow::create(flipped_options);
    ASSERT_NE(flipped, nullptr);
    const auto* flipped_native = static_cast<const x11::X11NativeSurface*>(flipped->native_handle().object());
    ASSERT_NE(flipped_native, nullptr);
    flipped->show();
    xcb_get_geometry_cookie_t flipped_cookie = xcb_get_geometry(flipped_native->state->connection, flipped_native->state->window);
    xcb_get_geometry_reply_t* flipped_geometry = xcb_get_geometry_reply(flipped_native->state->connection, flipped_cookie, nullptr);
    ASSERT_NE(flipped_geometry, nullptr);
    // assert the popup flipped above the trigger rather than run off the screen (§5.1)
    EXPECT_EQ(flipped_geometry->y, owner_y + 1000 - 80);
    std::free(flipped_geometry);
    flipped->close();
    // act a press arrives outside the popup, which the grab routes to it (§5.1)
    xcb_button_press_event_t outside{};
    outside.response_type = XCB_BUTTON_PRESS;
    outside.event = popup_native->state->window;
    outside.detail = 1;
    outside.event_x = static_cast<std::int16_t>(-5);
    outside.event_y = static_cast<std::int16_t>(-5);
    x11::dispatch_x11_event(reinterpret_cast<xcb_generic_event_t*>(&outside));
    // assert the popup left the screen and the framework was told, so the control closes (§5.1)
    EXPECT_FALSE(popup->visible());
    EXPECT_EQ(listener.closed, 1u);
}

TEST(X11Platform, SelectsTheAdapterFromTheEnvironment) {
    // arrange the x11 environment, which the loader names when no wayland session is present (§5.3, AD-22)
    if (std::getenv("DISPLAY") == nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
        GTEST_SKIP() << "no x11 display on this host";
    WindowOptions options;
    options.title = "albor-x11-selection";
    options.size = Size{120.0f, 80.0f};
    // act the installed factory builds the window
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(options);
    // assert the x11 adapter is the one the environment selected, which carries no marker of its own (§5.3)
    ASSERT_NE(window, nullptr);
    EXPECT_NE(dynamic_cast<x11::X11PlatformWindow*>(window.get()), nullptr);
}
