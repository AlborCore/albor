// the portable file dialog API over the scripted headless backend, which is how automation drives the flow (§5.3)

#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "albor/application/application.h"
#include "albor/application/file_dialog.h"
#include "albor/platform/headless/file_dialog.h"
#include "albor/platform/headless/headless_event_loop.h"
#include "albor/window/platform_window_factory.h"

using namespace albor;

namespace
{
    // the scope that installs the headless platform and clears every factory after it (§5.3, AD-16)
    class HeadlessScope
    {
    public:
        HeadlessScope() { headless::install_headless_platform(); }

        ~HeadlessScope() {
            set_platform_window_factory(nullptr);
            set_platform_file_dialog_factory(nullptr);
            set_platform_event_loop_factory(nullptr);
        }

        HeadlessScope(const HeadlessScope&) = delete;
        HeadlessScope& operator=(const HeadlessScope&) = delete;
    };

    // the window double that reports a native object, which a headless window cannot (§5.3)
    class NativeWindowDouble final : public PlatformWindow
    {
    public:
        void set_listener(PlatformWindowListener*) override {}
        void show() override { m_visible = true; }
        void activate() override { m_visible = true; }
        void hide() override { m_visible = false; }
        void set_title(std::string_view) override {}
        void set_size(Size) override {}
        [[nodiscard]] WindowMetrics metrics() const override { return WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f); }
        [[nodiscard]] NativeWindowHandle native_handle() const override { return NativeWindowHandle::from_object(&m_object); }
        [[nodiscard]] NativeWindowHandle native_window_handle() const override { return NativeWindowHandle::from_object(&m_object); }
        void request_frame() override {}
        [[nodiscard]] bool frame_requested() const override { return false; }
        [[nodiscard]] bool visible() const override { return m_visible; }
        void close() override {}

    private:
        // the object a renderer and a panel would attach to, which is unique per double (§5.3)
        int m_object = 0;

        // whether the double is on screen (§5.3)
        bool m_visible = false;
    };

    // the file dialog double that records the native parent the application resolved (§5.3)
    class RecordingFileDialog final : public PlatformFileDialog
    {
    public:
        // the handle the application hands over is kept in the test's own variable (§5.1)
        explicit RecordingFileDialog(NativeWindowHandle& seen) :
            m_seen(seen) {}

        std::optional<std::filesystem::path> open(const OpenFileOptions&, NativeWindowHandle parent) override {
            // the resolved parent is what the test asserts on (§5.1)
            m_seen = parent;
            // the call cancels, because the resolution is the whole subject (§5.3)
            return std::nullopt;
        }

        std::optional<std::filesystem::path> save(const SaveFileOptions&, NativeWindowHandle parent) override {
            // the resolved parent is what the test asserts on (§5.1)
            m_seen = parent;
            // the call cancels, because the resolution is the whole subject (§5.3)
            return std::nullopt;
        }

    private:
        // the test variable the resolved handle is written into (§5.1)
        NativeWindowHandle& m_seen;
    };
} // namespace

TEST(FileDialog, AHeadlessOpenPanelReturnsTheScriptedPath) {
    // arrange a headless application and one owned window (§5.1, §5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    WindowOptions window_options;
    window_options.visible = false;
    const WindowHandle window = application.create_window(window_options);
    ASSERT_TRUE(window.valid());
    // the scripted answer is what the panel returns, as if the user chose it (§5.3)
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/netlist.kicad_sch"));
    OpenFileOptions options;
    options.parent = window;
    options.title = "Open netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch", "kicad_pcb"}}};
    options.initial_directory = "/tmp";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert the answer reached the caller and the panel was owned by the window (§5.1, §5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->string(), "/tmp/netlist.kicad_sch");
    EXPECT_EQ(headless::file_dialog_count(), 1U);
    EXPECT_EQ(headless::last_file_dialog_title(), "Open netlist");
    EXPECT_EQ(headless::last_file_dialog_directory(), "/tmp");
    // cleanup
    EXPECT_TRUE(application.destroy_window(window));
}

TEST(FileDialog, AHeadlessPanelWithNoScriptedAnswerCancels) {
    // arrange a headless application and no scripted answer (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert an empty queue is a cancel, which keeps the calling flow honest (§5.3)
    EXPECT_FALSE(chosen.has_value());
    EXPECT_EQ(headless::file_dialog_count(), 1U);
}

TEST(FileDialog, AHeadlessSavePanelRecordsTheDefaultName) {
    // arrange a headless application and a scripted save answer (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    WindowOptions window_options;
    window_options.visible = false;
    const WindowHandle window = application.create_window(window_options);
    ASSERT_TRUE(window.valid());
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/mine.kicad_sch"));
    SaveFileOptions options;
    options.parent = window;
    options.title = "Save netlist";
    options.default_name = "netlist.kicad_sch";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}};
    // act
    const std::optional<std::filesystem::path> chosen = application.save_file(options);
    // assert the proposed name reached the panel, which is what a save flow edits (§5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(headless::last_file_dialog_default_name(), "netlist.kicad_sch");
    // cleanup
    EXPECT_TRUE(application.destroy_window(window));
}

TEST(FileDialog, AStaleParentIsRefusedWithoutShowingAPanel) {
    // arrange a headless application and a handle that names no window (§5.2)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    OpenFileOptions options;
    options.parent = WindowHandle::from_id(99);
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert the stale owner is refused rather than opening an unowned panel (§5.1)
    EXPECT_FALSE(chosen.has_value());
    EXPECT_EQ(headless::file_dialog_count(), 0U);
}

TEST(FileDialog, AnUnownedPanelStillRuns) {
    // arrange a headless application and an open request without a parent (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/netlist.kicad_sch"));
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert an unowned panel runs and reports that it had no parent (§5.1)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_FALSE(headless::last_file_dialog_had_parent());
}

TEST(FileDialog, ANonAsciiPathRoundTrips) {
    // arrange a scripted answer holding a non ascii byte, which the panel must not mangle (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    const std::filesystem::path expected = std::filesystem::path("/tmp/netliste_größe.kicad_sch");
    headless::push_file_dialog_answer(expected);
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert the path came back byte for byte, which is what a utf8 panel needs (§5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(*chosen, expected);
}

TEST(FileDialog, ALiveParentResolvesToItsNativeHandle) {
    // arrange a window whose platform half reports a native object, which a headless window cannot (§5.3)
    HeadlessScope scope;
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<NativeWindowDouble>(); });
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    WindowOptions window_options;
    window_options.visible = false;
    const WindowHandle window = application.create_window(window_options);
    ASSERT_TRUE(window.valid());
    // the recording backend keeps the handle the application resolves for the panel (§5.1)
    NativeWindowHandle seen;
    set_platform_file_dialog_factory([&seen] { return std::make_unique<RecordingFileDialog>(seen); });
    OpenFileOptions options;
    options.parent = window;
    options.title = "Open netlist";
    // act
    (void)application.open_file(options);
    // assert the live window resolved to its native object, which is what a panel is owned by (§5.1)
    EXPECT_TRUE(seen.valid());
    // cleanup
    EXPECT_TRUE(application.destroy_window(window));
}

TEST(FileDialog, TheFiltersAndDirectionReachTheBackend) {
    // arrange a headless application and a scripted answer (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/netlist.kicad_sch"));
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/netlist.kicad_sch"));
    OpenFileOptions open_options;
    open_options.title = "Open netlist";
    open_options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch", "kicad_pcb"}}};
    // act an open panel, then a save panel with the same filter (§5.3)
    (void)application.open_file(open_options);
    // assert the filter and the direction reached the backend, which the headless record keeps (§5.3)
    ASSERT_EQ(headless::last_file_dialog_filters().size(), 1U);
    EXPECT_EQ(headless::last_file_dialog_filters().at(0), "Netlists:kicad_sch,kicad_pcb");
    EXPECT_FALSE(headless::last_file_dialog_was_save());
    SaveFileOptions save_options;
    save_options.title = "Save netlist";
    save_options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}};
    (void)application.save_file(save_options);
    // assert the save direction is the one recorded last (§5.3)
    EXPECT_TRUE(headless::last_file_dialog_was_save());
    EXPECT_EQ(headless::last_file_dialog_filters().at(0), "Netlists:kicad_sch");
}

TEST(FileDialog, TheAnswersAreConsumedInCallOrder) {
    // arrange two queued answers, which one panel each consumes (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/first.kicad_sch"));
    headless::push_file_dialog_answer(std::filesystem::path("/tmp/second.kicad_sch"));
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> first = application.open_file(options);
    const std::optional<std::filesystem::path> second = application.open_file(options);
    // assert the queue is first in first out, which is what a scripted flow assumes (§5.3)
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->string(), "/tmp/first.kicad_sch");
    EXPECT_EQ(second->string(), "/tmp/second.kicad_sch");
    EXPECT_EQ(headless::file_dialog_count(), 2U);
}

TEST(FileDialog, NoInstalledBackendCancelsWithoutShowingAPanel) {
    // arrange a headless platform whose file dialog backend is removed (§5.3)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    set_platform_file_dialog_factory(nullptr);
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert no backend is a cancel that never reaches a panel (§5.3)
    EXPECT_FALSE(chosen.has_value());
    EXPECT_EQ(headless::file_dialog_count(), 0U);
}

TEST(FileDialog, ADestroyedParentIsRefusedWithoutShowingAPanel) {
    // arrange a headless application whose parent window is destroyed before the call (§5.2)
    HeadlessScope scope;
    headless::reset_file_dialogs();
    Application application(std::make_unique<headless::HeadlessEventLoop>());
    WindowOptions window_options;
    window_options.visible = false;
    const WindowHandle window = application.create_window(window_options);
    ASSERT_TRUE(window.valid());
    EXPECT_TRUE(application.destroy_window(window));
    OpenFileOptions options;
    options.parent = window;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = application.open_file(options);
    // assert the stale handle is refused, because a panel would otherwise be unowned (§5.2)
    EXPECT_FALSE(chosen.has_value());
    EXPECT_EQ(headless::file_dialog_count(), 0U);
}
