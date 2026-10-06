#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "window/platform_window_factory.h"
#include "window/window.h"
#include "window/window_handle.h"
#include "window/window_metrics.h"
#include "window/window_options.h"
#include "window/window_registry.h"

using namespace albor;

namespace
{
    // the native window test double, which records what the framework asked it to do (§5.3)
    class TestPlatformWindow final : public PlatformWindow
    {
    public:
        // the default double counts nothing, which is what a test that only forwards calls needs
        TestPlatformWindow() = default;

        // the double counts its closes into a counter the test also holds, because the double is destroyed together with the window and its own flag would not survive to be read
        explicit TestPlatformWindow(std::shared_ptr<int> close_counter) :
            m_close_counter(std::move(close_counter)) {}

        // the double reports that its listener was detached through a flag the test also holds, because the double dies with the window and its own member would not survive to be read
        explicit TestPlatformWindow(std::shared_ptr<bool> detached_flag) :
            m_detached_flag(std::move(detached_flag)) {}

        // the listener the framework attached, which the double raises events into
        PlatformWindowListener* listener = nullptr;

        // how many times the native window was shown
        std::size_t show_count = 0;

        // how many times the native window was hidden
        std::size_t hide_count = 0;

        // how many frames the framework requested
        std::size_t frame_requests = 0;

        // whether the native window is on screen
        bool is_visible = false;

        // whether the native window was closed
        bool was_closed = false;

        // the title the framework last wrote
        std::string title;

        // the size the framework last asked for
        Size requested_size;

        // the metrics the native window reports, which a test changes to simulate a resize
        WindowMetrics reported = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);

        void set_listener(PlatformWindowListener* attached) override {
            listener = attached;
            // a test that only watches the detach never reads the member, because the double dies with the window
            if (attached == nullptr && m_detached_flag)
                *m_detached_flag = true;
        }

        void show() override {
            show_count++;
            is_visible = true;
        }

        void hide() override {
            hide_count++;
            is_visible = false;
        }

        void set_title(std::string_view value) override { title = std::string(value); }

        void set_size(Size size) override { requested_size = size; }

        [[nodiscard]] WindowMetrics metrics() const override { return reported; }

        [[nodiscard]] NativeWindowHandle native_handle() const override {
            // the double wraps its own member, so a handle resolves to something with a stable address
            return NativeWindowHandle::from_object(&layer);
        }

        void request_frame() override { frame_requests++; }

        [[nodiscard]] bool frame_requested() const override { return frame_requests > 0; }

        [[nodiscard]] bool visible() const override { return is_visible; }

        void close() override {
            was_closed = true;
            is_visible = false;
            // a test that only counts closes never reads the flag, because the double dies with the window
            if (m_close_counter)
                (*m_close_counter)++;
        }

        // the opaque object a renderer backend would attach to, which the handle wraps (§3.5)
        int layer = 0;

    private:
        // the counter the double reports its close into, which the test also holds
        std::shared_ptr<int> m_close_counter;

        // the flag the double reports its detach into, which the test also holds
        std::shared_ptr<bool> m_detached_flag;
    };
} // namespace

TEST(WindowOptions, DefaultIsTheDocumentedEightHundredBySixHundred) {
    // arrange, act
    const WindowOptions options;
    // assert
    EXPECT_FLOAT_EQ(options.size.width, 800.0f);
    EXPECT_FLOAT_EQ(options.size.height, 600.0f);
}

TEST(WindowOptions, DefaultCarriesATitle) {
    // arrange, act
    const WindowOptions options;
    // assert
    EXPECT_FALSE(options.title.empty());
}

TEST(WindowOptions, DefaultIsANormalUnrestrictedWindow) {
    // arrange, act
    const WindowOptions options;
    // assert
    EXPECT_EQ(options.role, WindowRole::Normal);
    EXPECT_EQ(options.modality, Modality::None);
    EXPECT_TRUE(options.resizable);
    EXPECT_TRUE(options.visible);
}

TEST(WindowOptions, TheDefaultOptionsAreValid) {
    // arrange, act
    const WindowOptions options;
    // assert
    EXPECT_TRUE(options.valid());
    EXPECT_EQ(options.validation_error(), nullptr);
}

TEST(WindowOptions, AnEmptyTitleIsRefused) {
    // arrange
    WindowOptions options;
    options.title.clear();
    // act, assert
    EXPECT_FALSE(options.valid());
}

TEST(WindowOptions, TheEmptyTitleErrorNamesTheTitle) {
    // arrange
    WindowOptions options;
    options.title.clear();
    // act
    const char* error = options.validation_error();
    // assert
    EXPECT_NE(error, nullptr);
    EXPECT_STREQ(error, "window title must not be empty");
}

TEST(WindowOptions, ANonEmptyTitleIsAccepted) {
    // arrange
    WindowOptions options;
    options.title = "Albor";
    // act, assert
    EXPECT_TRUE(options.valid());
}

TEST(WindowOptions, TheEmptyTitleIsRefusedBeforeTheSize) {
    // arrange
    WindowOptions options;
    options.title.clear();
    options.size.width = 0.0f;
    // act
    const char* error = options.validation_error();
    // assert the title is checked first, so an option set with two faults names the one that blocks creation (§5.1)
    EXPECT_STREQ(error, "window title must not be empty");
}

TEST(WindowOptions, AnEmptyWidthIsRefused) {
    // arrange
    WindowOptions options;
    options.size.width = 0.0f;
    // act, assert
    EXPECT_FALSE(options.valid());
}

TEST(WindowOptions, AnEmptyHeightIsRefused) {
    // arrange
    WindowOptions options;
    options.size.height = 0.0f;
    // act, assert
    EXPECT_FALSE(options.valid());
}

TEST(WindowOptions, ANegativeSizeIsRefused) {
    // arrange
    WindowOptions options;
    options.size = Size{-100.0f, 100.0f};
    // act, assert
    EXPECT_FALSE(options.valid());
}

TEST(WindowOptions, ValidationErrorNamesTheOffendingField) {
    // arrange
    WindowOptions options;
    options.size.width = -1.0f;
    // act
    const char* error = options.validation_error();
    // assert
    ASSERT_NE(error, nullptr);
    EXPECT_STREQ(error, "window size must be positive");
}

TEST(WindowOptions, AValidOptionSetReportsNoError) {
    // arrange, act
    const WindowOptions options;
    // act
    const char* error = options.validation_error();
    // assert
    EXPECT_EQ(error, nullptr);
}

TEST(WindowOptions, AModalPopupIsRefused) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.modality = Modality::WindowModal;
    // act, assert
    EXPECT_FALSE(options.valid());
}

TEST(WindowOptions, AModalToolIsRefused) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Tool;
    options.modality = Modality::ApplicationModal;
    // act, assert
    EXPECT_FALSE(options.valid());
}

TEST(WindowOptions, ANonModalPopupIsAccepted) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Popup;
    options.modality = Modality::None;
    // act, assert
    EXPECT_TRUE(options.valid());
}

TEST(WindowOptions, AModalDialogIsAccepted) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Dialog;
    options.modality = Modality::ApplicationModal;
    // act, assert
    EXPECT_TRUE(options.valid());
}

TEST(WindowOptions, ANormalWindowIsTopLevel) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Normal;
    // act, assert
    EXPECT_TRUE(options.is_top_level());
}

TEST(WindowOptions, ADialogWindowIsTopLevel) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Dialog;
    // act, assert
    EXPECT_TRUE(options.is_top_level());
}

TEST(WindowOptions, AToolWindowIsNotTopLevel) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Tool;
    // act, assert
    EXPECT_FALSE(options.is_top_level());
}

TEST(WindowOptions, APopupWindowIsNotTopLevel) {
    // arrange
    WindowOptions options;
    options.role = WindowRole::Popup;
    // act, assert
    EXPECT_FALSE(options.is_top_level());
}

TEST(WindowOptions, EqualityComparesEveryField) {
    // arrange
    const WindowOptions first;
    WindowOptions same;
    WindowOptions other;
    other.resizable = false;
    // act, assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
}

TEST(WindowMetrics, FromLogicalKeepsTheLogicalSize) {
    // arrange
    const Size logical{800.0f, 600.0f};
    // act
    const WindowMetrics metrics = WindowMetrics::from_logical(logical, 2.0f);
    // assert
    EXPECT_EQ(metrics.logical_size, logical);
}

TEST(WindowMetrics, FromLogicalDerivesThePixelSize) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 2.0f);
    // assert
    EXPECT_FLOAT_EQ(metrics.pixel_size.width, 1600.0f);
    EXPECT_FLOAT_EQ(metrics.pixel_size.height, 1200.0f);
}

TEST(WindowMetrics, FromLogicalKeepsTheScaleFactor) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{100.0f, 100.0f}, 3.0f);
    // assert
    EXPECT_FLOAT_EQ(metrics.scale_factor, 3.0f);
}

TEST(WindowMetrics, ARetinaScaleDoublesThePixelSize) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{400.0f, 300.0f}, 2.0f);
    // assert
    EXPECT_EQ(metrics.pixel_size, (Size{800.0f, 600.0f}));
}

TEST(WindowMetrics, APositiveSizeAtScaleOneIsValid) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    // assert
    EXPECT_TRUE(metrics.valid());
}

TEST(WindowMetrics, AnEmptyLogicalSizeIsInvalid) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{0.0f, 480.0f}, 1.0f);
    // assert
    EXPECT_FALSE(metrics.valid());
}

TEST(WindowMetrics, ANonPositiveScaleIsInvalid) {
    // arrange, act
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 0.0f);
    // assert
    EXPECT_FALSE(metrics.valid());
}

TEST(WindowMetrics, AnEmptyPixelSizeIsInvalid) {
    // arrange
    WindowMetrics metrics = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    metrics.pixel_size = Size{0.0f, 0.0f};
    // act, assert
    EXPECT_FALSE(metrics.valid());
}

TEST(WindowMetrics, EqualityComparesEveryField) {
    // arrange
    const WindowMetrics first = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 2.0f);
    const WindowMetrics same = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 2.0f);
    const WindowMetrics other = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);
    // act, assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
}

TEST(WindowHandle, DefaultIsInvalid) {
    // arrange, act
    const WindowHandle handle;
    // assert
    EXPECT_FALSE(handle.valid());
}

TEST(WindowHandle, DefaultCarriesNoIdentifier) {
    // arrange, act
    const WindowHandle handle;
    // assert
    EXPECT_EQ(handle.id(), 0u);
}

TEST(WindowHandle, EqualityComparesTheIdentifier) {
    // arrange
    const WindowHandle first = WindowHandle::from_id(7u);
    // act
    const WindowHandle same = WindowHandle::from_id(7u);
    const WindowHandle other = WindowHandle::from_id(8u);
    // assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other);
}

TEST(WindowHandle, TheIdentifierSurvivesCopies) {
    // arrange
    const WindowHandle original = WindowHandle::from_id(12u);
    // act
    const WindowHandle copied = original;
    // assert
    EXPECT_EQ(copied.id(), 12u);
}

TEST(WindowRegistry, StartsWithNoWindows) {
    // arrange, act
    WindowRegistry registry;
    // assert
    EXPECT_EQ(registry.size(), 0u);
}

TEST(WindowRegistry, CreatingAWindowRegistersIt) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = registry.create(WindowOptions{});
    // assert
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(registry.size(), 1u);
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, ARefusedFactoryRegistersNoWindow) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return nullptr; });
    // act
    const WindowHandle handle = registry.create(WindowOptions{});
    // assert
    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(registry.size(), 0u);
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, AVisibleWindowIsShownOnCreation) {
    // arrange
    WindowRegistry registry;
    WindowOptions options;
    options.visible = true;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = registry.create(options);
    // assert
    EXPECT_TRUE(registry.find(handle)->visible());
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, AHiddenWindowIsNotShownOnCreation) {
    // arrange
    WindowRegistry registry;
    WindowOptions options;
    options.visible = false;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = registry.create(options);
    // assert
    EXPECT_FALSE(registry.find(handle)->visible());
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, TwoCreatedWindowsGetDistinctIdentifiers) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle first = registry.create(WindowOptions{});
    const WindowHandle second = registry.create(WindowOptions{});
    // assert
    EXPECT_NE(first.id(), second.id());
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, ARegisteredWindowResolvesToItsEntry) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = registry.create(WindowOptions{});
    Window* found = registry.find(handle);
    // assert
    EXPECT_NE(found, nullptr);
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, AnInvalidHandleResolvesToNothing) {
    // arrange, act
    WindowRegistry registry;
    // act
    Window* found = registry.find(WindowHandle{});
    // assert
    EXPECT_EQ(found, nullptr);
}

TEST(WindowRegistry, AStaleIdentifierResolvesToNothing) {
    // arrange
    WindowRegistry registry;
    // act
    Window* found = registry.find(WindowHandle::from_id(99u));
    // assert
    EXPECT_EQ(found, nullptr);
}

TEST(WindowRegistry, TheReadingLookupResolvesTheSameWindow) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = registry.create(WindowOptions{});
    const WindowRegistry& reading = registry;
    const Window* found = reading.find(handle);
    // assert
    EXPECT_NE(found, nullptr);
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, ARegisteredWindowIsAlive) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = registry.create(WindowOptions{});
    // assert
    EXPECT_TRUE(registry.alive(handle));
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, DestroyingAWindowStopsResolvingIt) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = registry.create(WindowOptions{});
    // act
    registry.destroy(handle);
    // assert
    EXPECT_EQ(registry.find(handle), nullptr);
    EXPECT_FALSE(registry.alive(handle));
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, AStaleHandleIsNeverReusedForANewWindow) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle first = registry.create(WindowOptions{});
    registry.destroy(first);
    // act
    const WindowHandle second = registry.create(WindowOptions{});
    // assert
    EXPECT_NE(second.id(), first.id());
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, DestroyingAStaleWindowReportsFailure) {
    // arrange
    WindowRegistry registry;
    // act
    const bool destroyed = registry.destroy(WindowHandle{});
    // assert
    EXPECT_FALSE(destroyed);
}

TEST(WindowRegistry, DestroyedWindowsAreNotCounted) {
    // arrange
    WindowRegistry registry;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle first = registry.create(WindowOptions{});
    const WindowHandle second = registry.create(WindowOptions{});
    EXPECT_NE(second.id(), first.id());
    // act
    registry.destroy(first);
    // assert
    EXPECT_EQ(registry.size(), 1u);
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, DestroyAllClosesEveryWindow) {
    // arrange
    WindowRegistry registry;
    auto closes = std::make_shared<int>(0);
    std::shared_ptr<int> observed = closes;
    set_platform_window_factory([closes](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(closes); });
    const WindowHandle first = registry.create(WindowOptions{});
    const WindowHandle second = registry.create(WindowOptions{});
    EXPECT_NE(first.id(), second.id());
    EXPECT_EQ(registry.size(), 2u);
    // act
    registry.destroy_all();
    // assert
    EXPECT_EQ(*observed, 2);
    EXPECT_EQ(registry.size(), 0u);
    set_platform_window_factory(nullptr);
}

TEST(WindowRegistry, AdoptingAWindowRegistersIt) {
    // arrange
    WindowRegistry registry;
    auto window = std::make_unique<Window>(WindowHandle::from_id(4u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // act
    const WindowHandle handle = registry.adopt(std::move(window));
    // assert
    EXPECT_EQ(handle.id(), 4u);
    EXPECT_EQ(registry.size(), 1u);
}

TEST(WindowRegistry, AdoptingNothingRegistersNothing) {
    // arrange
    WindowRegistry registry;
    // act
    const WindowHandle handle = registry.adopt(nullptr);
    // assert
    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(registry.size(), 0u);
}

TEST(WindowRegistry, AnAdoptedWindowKeepsItsOwnIdentifier) {
    // arrange
    WindowRegistry registry;
    auto window = std::make_unique<Window>(WindowHandle::from_id(9u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // act
    const WindowHandle handle = registry.adopt(std::move(window));
    Window* found = registry.find(handle);
    // assert
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->handle().id(), 9u);
}

TEST(Window, ConstructionSeedsTheMetricsFromThePlatform) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    native->reported = WindowMetrics::from_logical(Size{320.0f, 240.0f}, 2.0f);
    // act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // assert
    EXPECT_EQ(window.metrics(), WindowMetrics::from_logical(Size{320.0f, 240.0f}, 2.0f));
}

TEST(Window, AWindowWithANativeWindowIsAlive) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // assert
    EXPECT_TRUE(window.alive());
}

TEST(Window, AWindowWithoutANativeWindowIsNotAlive) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, nullptr);
    // assert
    EXPECT_FALSE(window.alive());
}

TEST(Window, ConstructionKeepsTheHandleItWasGiven) {
    // arrange, act
    const Window window(WindowHandle::from_id(5u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // assert
    EXPECT_EQ(window.handle().id(), 5u);
}

TEST(Window, ConstructionKeepsTheOptionsItWasGiven) {
    // arrange
    WindowOptions options;
    options.title = "kept";
    // act
    const Window window(WindowHandle::from_id(1u), options, std::make_unique<TestPlatformWindow>());
    // assert
    EXPECT_EQ(window.options().title, "kept");
}

TEST(Window, DestructionDetachesTheListener) {
    // arrange the double dies with the window, so it reports the detach through a flag this test holds
    auto detached = std::make_shared<bool>(false);
    std::shared_ptr<bool> observed = detached;
    {
        Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>(detached));
        EXPECT_FALSE(*observed);
    }
    // act, assert
    EXPECT_TRUE(*observed);
}

TEST(Window, ConstructionAttachesTheListenerToTheNativeWindow) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* platform = native.get();
    // act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // assert
    EXPECT_EQ(platform->listener, &window);
}

TEST(Window, ShowingPutsTheNativeWindowOnScreen) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* observed = native.get();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // act
    window.show();
    // assert
    EXPECT_TRUE(window.visible());
    EXPECT_EQ(observed->show_count, 1u);
}

TEST(Window, HidingTakesTheNativeWindowOffScreen) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* observed = native.get();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    window.show();
    // act
    window.hide();
    // assert
    EXPECT_FALSE(window.visible());
    EXPECT_EQ(observed->hide_count, 1u);
}

TEST(Window, ShowingWithoutANativeWindowDoesNothing) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, nullptr);
    // act
    window.show();
    // assert
    EXPECT_FALSE(window.visible());
}

TEST(Window, SettingTheTitleReachesTheNativeWindow) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* observed = native.get();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // act
    window.set_title("renamed");
    // assert
    EXPECT_EQ(observed->title, "renamed");
}

TEST(Window, SettingTheTitleWithoutANativeWindowDoesNothing) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, nullptr);
    // act
    window.set_title("renamed");
    // assert
    EXPECT_TRUE(window.visible() == false);
}

TEST(Window, SettingTheSizeReachesTheNativeWindow) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* observed = native.get();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // act
    window.set_size(Size{1024.0f, 768.0f});
    // assert
    EXPECT_EQ(observed->requested_size, (Size{1024.0f, 768.0f}));
}

TEST(Window, RequestingAFrameReachesTheNativeWindow) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* observed = native.get();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // act
    window.request_frame();
    // assert
    EXPECT_EQ(observed->frame_requests, 1u);
}

TEST(Window, RequestingAFrameWithoutANativeWindowDoesNothing) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, nullptr);
    // act
    window.request_frame();
    // assert
    EXPECT_TRUE(window.visible() == false);
}

TEST(Window, TheNativeHandleIsOpaqueAndForwarded) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    // act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    // assert
    EXPECT_TRUE(window.native_handle().valid());
}

TEST(Window, AWindowWithoutANativeWindowHandsOutTheInvalidHandle) {
    // arrange, act
    const Window window(WindowHandle::from_id(1u), WindowOptions{}, nullptr);
    // assert
    EXPECT_FALSE(window.native_handle().valid());
}

TEST(Window, ClosingClosesTheNativeWindow) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    TestPlatformWindow* observed = native.get();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    window.show();
    // act
    window.close();
    // assert
    EXPECT_TRUE(observed->was_closed);
    EXPECT_FALSE(window.visible());
}

TEST(Window, ClosingClearsAPendingClose) {
    // arrange
    auto native = std::make_unique<TestPlatformWindow>();
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::move(native));
    window.set_close_handler([] { return false; });
    window.on_platform_close_request();
    ASSERT_TRUE(window.close_pending());
    // act
    window.close();
    // assert
    EXPECT_FALSE(window.close_pending());
}

TEST(Window, AWindowStartsWithNoPendingClose) {
    // arrange, act
    const Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // assert
    EXPECT_FALSE(window.close_pending());
}

TEST(Window, AResizeUpdatesTheCachedMetrics) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // act
    const WindowMetrics resized = WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f);
    window.on_platform_resize(resized);
    // assert
    EXPECT_EQ(window.metrics(), resized);
}

TEST(Window, AResizeReachesTheResizeHandler) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    WindowMetrics seen;
    bool called = false;
    window.set_resize_handler([&seen, &called](const WindowMetrics& metrics) {
        seen = metrics;
        called = true;
    });
    const WindowMetrics resized = WindowMetrics::from_logical(Size{320.0f, 240.0f}, 1.0f);
    // act
    window.on_platform_resize(resized);
    // assert
    EXPECT_TRUE(called);
    EXPECT_EQ(seen, resized);
}

TEST(Window, AResizeWithoutAHandlerStillUpdatesTheMetrics) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    const WindowMetrics resized = WindowMetrics::from_logical(Size{320.0f, 240.0f}, 1.0f);
    // act
    window.on_platform_resize(resized);
    // assert
    EXPECT_EQ(window.metrics(), resized);
}

TEST(Window, ACloseRequestWithoutAHandlerIsGranted) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // act
    const bool granted = window.on_platform_close_request();
    // assert
    EXPECT_TRUE(granted);
    EXPECT_FALSE(window.close_pending());
}

TEST(Window, AGrantedCloseLeavesNothingPending) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    window.set_close_handler([] { return true; });
    // act
    const bool granted = window.on_platform_close_request();
    // assert
    EXPECT_TRUE(granted);
    EXPECT_FALSE(window.close_pending());
}

TEST(Window, AVetoedCloseIsRefusedAndMarkedPending) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    window.set_close_handler([] { return false; });
    // act
    const bool granted = window.on_platform_close_request();
    // assert
    EXPECT_FALSE(granted);
    EXPECT_TRUE(window.close_pending());
}

TEST(Window, AVetoedCloseMayBeRetried) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    bool grant = false;
    window.set_close_handler([&grant] { return grant; });
    EXPECT_FALSE(window.on_platform_close_request());
    // act
    grant = true;
    const bool granted = window.on_platform_close_request();
    // assert
    EXPECT_TRUE(granted);
    EXPECT_FALSE(window.close_pending());
}

TEST(Window, ACloseRequestConsultsTheHandlerEveryTime) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    int consults = 0;
    window.set_close_handler([&consults] {
        consults++;
        return true;
    });
    // act
    window.on_platform_close_request();
    window.on_platform_close_request();
    // assert
    EXPECT_EQ(consults, 2);
}

TEST(Window, ReplacingTheCloseHandlerChangesTheNextVerdict) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    window.set_close_handler([] { return false; });
    // the veto is recorded, so the arrange step really has a pending close
    ASSERT_FALSE(window.on_platform_close_request());
    ASSERT_TRUE(window.close_pending());
    // act
    window.set_close_handler([] { return true; });
    const bool granted = window.on_platform_close_request();
    // assert
    EXPECT_TRUE(granted);
}

TEST(Window, APlatformFrameReachesTheFrameHandler) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    int frames = 0;
    window.set_frame_handler([&frames] { frames++; });
    // act
    window.on_platform_frame();
    // assert
    EXPECT_EQ(frames, 1);
}

TEST(Window, APlatformFrameWithoutAHandlerDrawsNothing) {
    // arrange, act
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    // act
    window.on_platform_frame();
    // assert
    EXPECT_TRUE(window.visible() == false);
}

TEST(Window, EveryPlatformFrameReachesTheHandler) {
    // arrange
    Window window(WindowHandle::from_id(1u), WindowOptions{}, std::make_unique<TestPlatformWindow>());
    int frames = 0;
    window.set_frame_handler([&frames] { frames++; });
    // act
    window.on_platform_frame();
    window.on_platform_frame();
    window.on_platform_frame();
    // assert
    EXPECT_EQ(frames, 3);
}
