#include <cstdio>
#include <memory>
#include <vector>

#include "application/application.h"
#include "render/frame_scene.h"
#include "text/text_engine.h"

namespace
{
    // the background the window is cleared with every frame (§6.1)
    const albor::Color BACKGROUND = albor::Color::rgb(0.06f, 0.07f, 0.09f);

    // the scene the example draws, which is the same one hello-blend2d draws so the two can be compared (§6.3, AD-26)
    std::shared_ptr<const albor::FrameScene> build_frame(const albor::WindowMetrics& metrics, std::uint64_t index, const albor::TextLayout& greeting) {
        // the shapes are gathered in paint order, which is the order the scene replays them (§6.1)
        std::vector<albor::DrawCommand> commands;
        // the inscribed band grows with the window, which is what makes a resize visible (§6.1)
        const float band_width = metrics.logical_size.width * 0.6f;
        const float band_height = metrics.logical_size.height * 0.25f;
        const float origin_x = (metrics.logical_size.width - band_width) * 0.5f;
        const float origin_y = (metrics.logical_size.height - band_height) * 0.5f;
        // the band paints first, so the block below it draws over it (§6.1)
        const albor::Rect band = albor::Rect::from_origin_size(albor::Point{origin_x, origin_y}, albor::Size{band_width, band_height});
        commands.push_back(albor::DrawCommand::rounded_rectangle(band, band_height * 0.2f, albor::Paint{.fill = albor::Color::rgb(0.14f, 0.17f, 0.22f)}));
        // the sliding block proves that the frame advances rather than presenting a still image (§6.1)
        const float slide_width = band_width * 0.2f;
        const float travel = band_width - slide_width;
        // the phase is the fraction of the travel the current frame stands at, which the slide below reads (§6.1)
        const float phase = travel > 0.0f ? static_cast<float>(index % 120u) / 120.0f * travel : 0.0f;
        // the block sits inside the band, which keeps its corners on the surface at every window size (§6.1)
        const albor::Rect block = albor::Rect::from_origin_size(albor::Point{origin_x + phase, origin_y + band_height * 0.25f}, albor::Size{slide_width, band_height * 0.5f});
        commands.push_back(albor::DrawCommand::rounded_rectangle(block, slide_width * 0.25f, albor::Paint{.fill = albor::Color::rgb(0.36f, 0.62f, 0.86f)}));
        // the greeting is centred on the block as a box, which is how a button centres its own label (§6.1, §9.1)
        const albor::Point greeting_origin{.x = block.x + (block.width - greeting.size.width) * 0.5f, .y = block.y + (block.height - greeting.size.height) * 0.5f};
        commands.push_back(albor::DrawCommand::text(greeting, greeting_origin, albor::Paint{.fill = albor::Color::rgb(0.96f, 0.97f, 0.99f)}));
        // the scene is built here and handed over immutable, because the render thread consumes it (§6.1, §10.5)
        std::shared_ptr<albor::FrameScene> scene = std::make_shared<albor::FrameScene>(metrics, BACKGROUND);
        // the shapes join the scene in paint order, which is what the replay walks (§6.1)
        for (albor::DrawCommand& command : commands)
            scene->add_command(std::move(command));
        // the scene is complete, so nothing mutates it after this point (§6.1, AD-18)
        return scene;
    }
} // namespace

int main() {
    // the greeting is measured before the window opens, because a scene carries a shaped run (§9.1, §6.2)
    albor::TextLayout greeting;
    // the engine is scoped to the measurement, because the run carries the font bytes it draws through (§9.1, AD-34)
    if (const std::unique_ptr<albor::TextEngine> text = albor::create_text_engine())
        greeting = text->measure("Hello", albor::TextEngine::UNBOUNDED_WIDTH, 24.0f);
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the documented title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Hello render-blend2d";
    options.size = albor::Size{800.0f, 600.0f};
    // this example owns its scene rather than a tree, which is what the scene source is for (§6.1)
    albor::MainWindowConfig config;
    config.background = BACKGROUND;
    config.scene_source = [greeting](const albor::WindowMetrics& metrics, std::uint64_t index) { return build_frame(metrics, index, greeting); };
    // the blend2d backend installed itself at load, so linking chose it rather than code (§6.3, AD-26)
    const albor::WindowHandle handle = application.create_window(options, albor::RootBuilder{}, config);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid()) {
        std::fprintf(stderr, "hello-render-blend2d: the window could not be created\n");
        return 1;
    }
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
