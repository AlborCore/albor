#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "albor/application/application.h"
#include "albor/platform/headless/headless_event_loop.h"
#include "albor/render/render-software/software_renderer_backend.h"
#include "albor/text/text_engine.h"
#include "albor/window/platform_window_factory.h"
#include "todo/todo.h"
#include "widget_form.h"

namespace
{
    // the fixed advance double gives the sample controls a measurable area (§9.1)
    class TestTextEngine final : public albor::TextEngine
    {
    public:
        // one byte contributes this advance in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // every synthesized run reports this height (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // measure forwards to the explicit overload with the default size (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view text, float max_width) const override { return measure(text, max_width, DEFAULT_FONT_SIZE); }

        // shape one glyph per byte, which is enough to lay out the sample (§9.1)
        [[nodiscard]] albor::TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            if (text.empty())
                return albor::TextLayout{};
            albor::ShapedRun::Line line;
            line.origin = albor::Point{.x = 0.0f, .y = HEIGHT * 0.8f};
            line.start_byte = 0;
            line.end_byte = text.size();
            line.ascent = HEIGHT * 0.8f;
            line.descent = HEIGHT * 0.2f;
            for (const char character : text)
                line.glyphs.push_back(albor::ShapedGlyph{.id = static_cast<std::uint32_t>(character), .position = albor::Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}, .cluster = static_cast<std::uint32_t>(line.glyphs.size())});
            const float advanced = static_cast<float>(text.size()) * ADVANCE;
            line.width = advanced;
            return albor::TextLayout{.run = albor::ShapedRun::make({std::move(line)}, font_size), .size = albor::Size{.width = max_width > 0.0f ? std::min(advanced, max_width) : advanced, .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        // the application checks readiness before laying out a tree (§9.1)
        [[nodiscard]] bool ready() const override { return true; }
    };

    // the scope installs the portable double stack for this process (§5.3, §10.3)
    class InstalledScope
    {
    public:
        InstalledScope() {
            albor::set_text_engine_factory([] { return std::make_unique<TestTextEngine>(); });
            albor::software::install_software_renderer_backend();
            albor::headless::install_headless_platform();
        }

        ~InstalledScope() {
            albor::set_text_engine_factory(nullptr);
            albor::set_renderer_backend_factory(nullptr);
            albor::set_platform_window_factory(nullptr);
            albor::set_platform_event_loop_factory(nullptr);
        }

        InstalledScope(const InstalledScope&) = delete;
        InstalledScope& operator=(const InstalledScope&) = delete;
    };
} // namespace

int main(int argc, char** argv) {
    // the scope keeps every factory alive for the whole process (§5.3)
    InstalledScope scope;
    // the application owns the loop, the window registry and the host timer (§4.1, §10.3)
    albor::Application& application = albor::Application::instance();
    // the generated form is the tree the reference compatibility flow addresses (§11.3, §16)
    albor::WidgetForm* form = nullptr;
    // the window size matches the form the in-process suite builds (§8.2)
    albor::WindowOptions options;
    options.title = "Albor Automation Reference";
    options.size = albor::Size{420.0f, 840.0f};
    // create_window binds the tree to the automation host through the dev hook (§10.3, §12.1)
    const albor::WindowHandle handle = application.create_window(options, form);
    if (!handle.valid() || form == nullptr) {
        std::fprintf(stderr, "reference: the window could not be created\n");
        return 1;
    }
    // the sample todos give the form the rows the documented flow reads (§16)
    form->set_todos(std::vector<albor::Todo>{albor::Todo{.key = "t01", .title = "Buy groceries"}, albor::Todo{.key = "t02", .title = "Walk the dog"}});
    // the first frame arranges the tree and binds the window to the host (§6.1, §10.3)
    application.pump();
    // the stop file lets the client end the process and remove the host directory cleanly (§10.3)
    const std::filesystem::path stop = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path();
    while (true) {
        // a stop file ends the loop after the current frame (§4.1)
        if (!stop.empty() && std::filesystem::exists(stop))
            break;
        // each pump fires the host's repeating serve timer on the ui thread (§6.1, §10.3)
        application.pump();
        // the headless loop has no display link, so the wait is explicit (§4.3)
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    // destroying the window unbinds the tree and stops the host endpoint (§5.2, §10.3)
    application.destroy_window(handle);
    // one last pump applies the deferred destruction (§4.2)
    application.pump();
    return 0;
}
