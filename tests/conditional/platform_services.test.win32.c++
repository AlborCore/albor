// the win32 platform services: clipboard, input method, dpi and the frame tick loop (§5.3, §8.3, AD-16)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "core/clipboard.h"
#include "platform/windows/clipboard.h"
#include "platform/windows/event_loop.h"
#include "platform/windows/platform_window.h"
#include "window/window_options.h"

using namespace albor;

namespace
{
    // the listener double the suite raises native events into, which records what the window reported (§5.3)
    class RecordingListener final : public PlatformWindowListener
    {
    public:
        void on_platform_resize(const WindowMetrics& metrics) override {
            // every resize is counted, because the dpi path is what raises it (§5.3, AD-27)
            resizes++;
            last_metrics = metrics;
        }

        bool on_platform_close_request() override { return true; }

        void on_platform_closed() override { closed++; }

        void on_platform_frame() override { frames++; }

        void on_platform_event(const Event& event) override {
            // only a composing event is what the input method suite reads (§8.3)
            if (event.kind == EventKind::Composing)
                composing++;
            last_event = event;
        }

        void on_platform_appearance_changed(const SystemAppearance&) override { appearance_changed++; }

        // how many resize reports the window raised, which the dpi change drives (§5.3, AD-27)
        int resizes = 0;

        // how many closed reports the window raised, which a close request drives (§5.2)
        int closed = 0;

        // how many frame requests the window raised, which the pacing source drives (§4.3)
        int frames = 0;

        // how many composing events the input method raised, which the lifecycle suite reads (§8.3)
        int composing = 0;

        // the last normalized event the window raised, which an input method message carries (§8.3)
        Event last_event{};

        // the last metrics the window reported, which a resize carries (§5.3)
        WindowMetrics last_metrics{};

        // how many appearance changes the window raised, which a theme flip drives (§8.4)
        int appearance_changed = 0;
    };

    // the installed clipboard factory is reset with the test, because the portable suites expect no platform (§3.5)
    class ClipboardInstall
    {
    public:
        ClipboardInstall() {
            // the win32 surface is installed over the portable factory, which is the platform selection (§3.5, AD-16)
            windows::install_windows_clipboard();
        }

        ~ClipboardInstall() {
            // the factory is cleared with the test, because the next suite installs its own double (§3.5)
            set_clipboard_factory(nullptr);
        }
    };
} // namespace

TEST(Win32PlatformServices, TheClipboardRoundTripsUnicodeText) {
    // arrange the installed win32 clipboard, which is the surface a field copies through (§3.5)
    ClipboardInstall install;
    const std::unique_ptr<Clipboard> clipboard = create_clipboard();
    ASSERT_NE(clipboard, nullptr);
    ASSERT_TRUE(clipboard->ready());
    // act a multi byte copy, which the clipboard stores as utf16 rather than bytes (§3.5)
    clipboard->set_text("Albor \xC3\xA9");
    // assert the text survives the conversion in both directions, which a paste reads back (§3.5)
    EXPECT_EQ(clipboard->text(), "Albor \xC3\xA9");
    EXPECT_TRUE(clipboard->has_text());
}

TEST(Win32PlatformServices, AnEmptyCopyClearsTheClipboard) {
    // arrange a clipboard that carries text from an earlier copy (§3.5)
    ClipboardInstall install;
    const std::unique_ptr<Clipboard> clipboard = create_clipboard();
    ASSERT_NE(clipboard, nullptr);
    clipboard->set_text("before");
    // act
    clipboard->set_text("");
    // assert the copy replaced the old text with nothing, which a paste reads as no text (§3.5)
    EXPECT_TRUE(clipboard->text().empty());
    EXPECT_FALSE(clipboard->has_text());
}

TEST(Win32PlatformServices, AnInputMethodLifecycleRaisesTheComposingEvents) {
    // arrange a native window with a listener, which the input method messages are delivered into (§8.3)
    WindowOptions options;
    options.title = "ime-lifecycle";
    options.size = Size{200.0f, 150.0f};
    const std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    ASSERT_NE(window, nullptr);
    // act the composition opens, which carries no text of its own (§8.3)
    (void)SendMessageW(window, WM_IME_STARTCOMPOSITION, 0, 0);
    // assert an open composition alone announces nothing, because the mark has not arrived yet (§8.3)
    EXPECT_EQ(listener.composing, 0);
    // act the composition ends without a commit, which renounces its mark (§8.3)
    (void)SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
    // assert the framework was told the mark is empty, because a field must clear what it painted (§8.3)
    EXPECT_EQ(listener.composing, 1);
    EXPECT_TRUE(listener.last_event.text.empty());
}

TEST(Win32PlatformServices, TheTextAnchorIsAcceptedWithoutAnInputMethod) {
    // arrange a native window, which a host without an input method still owns (§8.3)
    WindowOptions options;
    options.title = "ime-anchor";
    options.size = Size{200.0f, 150.0f};
    const std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    // act the caret anchor and the document selection are pushed, which an input method would consume (§8.3)
    platform->set_text_anchor(Rect{2.0f, 3.0f, 40.0f, 18.0f});
    platform->set_text_selection(TextInputRange{.location = 0, .length = 0});
    // assert the window stayed usable, because a missing input method is the portable no-op (§8.3)
    EXPECT_TRUE(platform->metrics().valid());
    EXPECT_NE(platform->native_handle().object(), nullptr);
}

TEST(Win32PlatformServices, TheWindowReportsTheScaleItSitsUnder) {
    // arrange a window whose logical size is fixed, which the platform turns into physical pixels (AD-27)
    WindowOptions options;
    options.title = "dpi-scale";
    options.size = Size{200.0f, 150.0f};
    const std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    ASSERT_NE(window, nullptr);
    // act
    const WindowMetrics metrics = platform->metrics();
    // assert the scale is the dpi the window sits under, which the client rect is divided by (AD-27)
    const UINT dpi = GetDpiForWindow(window);
    const float expected = dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
    EXPECT_FLOAT_EQ(metrics.scale_factor, expected);
    // assert the logical size is the requested one and the physical size is that grown by the scale (AD-27)
    EXPECT_NEAR(metrics.logical_size.width, 200.0f, 1.0f);
    EXPECT_NEAR(metrics.logical_size.height, 150.0f, 1.0f);
    EXPECT_NEAR(metrics.pixel_size.width, metrics.logical_size.width * expected, 1.0f);
    EXPECT_TRUE(metrics.valid());
}

TEST(Win32PlatformServices, ADpiChangeRaisesAResize) {
    // arrange a window with a listener, which the dpi message reports through (§5.3, AD-27)
    WindowOptions options;
    options.title = "dpi-change";
    options.size = Size{200.0f, 150.0f};
    const std::unique_ptr<Win32PlatformWindow> platform = Win32PlatformWindow::create(options);
    ASSERT_NE(platform, nullptr);
    RecordingListener listener;
    platform->set_listener(&listener);
    HWND window = static_cast<HWND>(const_cast<void*>(platform->native_handle().object()));
    ASSERT_NE(window, nullptr);
    // act the suggested rectangle a monitor change carries, which the window applies before it reports (§5.3)
    RECT suggested{0, 0, 400, 300};
    (void)SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
    // assert the framework received the new metrics, because a scale change is a resize (AD-27)
    EXPECT_GT(listener.resizes, 0);
    EXPECT_TRUE(listener.last_metrics.valid());
}

TEST(Win32PlatformServices, AFrameTickIsIgnoredBeforeTheLoopRuns) {
    // arrange a loop with a pump counter, which only a delivered tick increments (§4.3)
    windows::Win32EventLoop loop;
    int pumped = 0;
    loop.set_pump_handler([&pumped] { pumped++; });
    // act a delivery while the loop is stopped, which a stale posted tick would name (§4.3)
    loop.deliver_on_ui_thread();
    // assert the stopped loop counted nothing, because a teardown must not run a frame (§4.2)
    EXPECT_EQ(pumped, 0);
    EXPECT_EQ(loop.frame_ticks(), 0u);
    EXPECT_FALSE(loop.running());
    EXPECT_GE(loop.refresh_hz(), 1.0);
}

TEST(Win32PlatformServices, TheFramePacingThreadPumpsOnTheUiThread) {
    // arrange the loop on its own ui thread, because the pacing thread posts to the thread that built it (§4.1, §4.3)
    std::atomic<DWORD> ui_thread{0};
    std::atomic<int> pumped{0};
    std::thread runner([&ui_thread, &pumped] {
        // the thread id is published before the loop is built, because the pacing source addresses it (§4.3)
        ui_thread.store(GetCurrentThreadId());
        windows::Win32EventLoop loop;
        loop.set_pump_handler([&pumped] { pumped.fetch_add(1); });
        loop.start_frame_pacing();
        loop.run();
    });
    // act the loop runs until the tick reaches the ui thread, which is the pump the frame pipeline waits on (§4.3)
    for (int attempt = 0; attempt < 200 && pumped.load() == 0; attempt++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // assert the pump ran at least once, because a loop that never ticked would hide a stalled pacing source (§4.3)
    EXPECT_GT(pumped.load(), 0);
    // act the loop is asked to leave, which is the posted quit a shutdown uses (§4.1)
    (void)PostThreadMessageW(ui_thread.load(), WM_QUIT, 0, 0);
    runner.join();
}
