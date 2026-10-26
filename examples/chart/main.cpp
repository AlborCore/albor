#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include "application/application.h"
#include "examples/chart/waveform_widget.h"

int main() {
    // the application owns the native loop, the window registry and the task dispatcher (§4.1)
    albor::Application& application = albor::Application::instance();
    // the window opens with the example title and the documented 800 by 600 size (§5.1)
    albor::WindowOptions options;
    options.title = "Albor \xE2\x80\x94 Chart";
    options.size = albor::Size{800.0f, 600.0f};
    // the manifest glue generated the facade, which the hand written widget implements (§11.2, AD-17)
    albor::WaveformWidget* widget = nullptr;
    const albor::WindowHandle handle = application.create_window(options, widget);
    // a window that could not be created leaves nothing to present (§5.1)
    if (!handle.valid() || widget == nullptr) {
        std::fprintf(stderr, "chart: the window could not be created\n");
        return 1;
    }
    // the generated signal slot is wired here, which is how a host reacts to a selection (§11.2)
    widget->set_on_selection_changed([](float time_pos) { std::printf("chart: selection at %.1fs\n", time_pos); });
    // a timer pushes a new snapshot and raises the selection signal, which the generated facade publishes (§4.2, §11.2)
    int tick = 0;
    const albor::TimerHandle ticker = application.timers().schedule_repeating(
        [widget, &tick] {
            // the samples describe one swipe of the synthetic controller, which is the data slot contract (§13.2)
            std::vector<float> samples;
            for (int index = 0; index < 64; ++index)
                samples.push_back(static_cast<float>(std::sin(static_cast<double>(tick) / 4.0 + static_cast<double>(index) / 8.0)));
            // the immutable snapshot crosses into the widget, which is how the data type travels (§13.2)
            widget->set_data(std::make_shared<const albor::WaveformData>(albor::WaveformData{.samples = std::move(samples)}));
            // the selection lands at the current time position, which the wired slot prints (§11.2)
            widget->raise_selection(static_cast<float>(tick) / 2.0f);
            ++tick;
        },
        std::chrono::milliseconds(500));
    // the handle is what a shutdown would cancel, so it is kept rather than dropped (§4.2)
    if (!application.timers().scheduled(ticker))
        std::fprintf(stderr, "chart: the ticker was not registered\n");
    std::printf("chart: running, a new waveform and selection arrive every 500ms\n");
    std::printf("chart: WaveformWidget generated from chart.ui-manifest, implemented by waveform_widget.cpp (§11.2, AD-17)\n");
    // the loop blocks until the window closes, which is what ends the application (§4.1)
    return application.run();
}
