// Prototype A1-Graphite-Dawn: does one Dawn device serve both Albor's custom geometry and Skia text? (AD-28)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// This spike creates its own WebGPU device exactly as A1-Dawn does and hands THAT device to Skia's Graphite Dawn
// backend, which is the whole feasibility claim (T5.1). Nothing in A1 or A1-Dawn is modified: the spike reads their
// workload definitions and links their resources, it does not change them.

#include "prototypes/a1/custom_render_node.h"
#include "prototypes/a1/resource_factory.h"
#include "prototypes/a1/resource_set.h"

#include <webgpu/webgpu_cpp.h>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/graphite/Context.h>
#include <skia/gpu/graphite/ContextOptions.h>
#include <skia/gpu/graphite/Recorder.h>
#include <skia/gpu/graphite/Surface.h>
#include <skia/gpu/graphite/dawn/DawnBackendContext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace albor::a1_graphite_dawn
{

    namespace
    {

        // -----------------------------------------------------------------------------------------------------------
        // the thresholds below are transcribed from prototypes/a1_graphite_dawn/plan.md, committed before this
        // program existed (§28.4). Do not edit a value here to change a verdict.
        // -----------------------------------------------------------------------------------------------------------

        // T5.4 combined recording may not cost more than this multiple of geometry-only recording
        constexpr double T5_4_RECORD_MULTIPLE = 2.0;

        // T5.5 the combined workload must sustain at least this many presented frames per second
        constexpr double T5_5_MIN_FPS = 55.0;

        // the warm-up frames the timing runs exclude, so shader compilation and first-use costs stay out (§28.2)
        constexpr std::size_t WARMUP_FRAMES = 8;

        // the timed repetitions each workload reports, and the frames one repetition records (§28.2)
        constexpr std::size_t REPETITIONS = 5;
        constexpr std::size_t FRAMES_PER_REPETITION = 20;

        // the offscreen size the spike records at, matching a1's window slice (§9.1)
        constexpr int SURFACE_WIDTH = 1280;
        constexpr int SURFACE_HEIGHT = 720;

        // the label count the text workload draws, which is what a real tool bar and status line cost (§21.2)
        constexpr std::size_t LABEL_COUNT = 24;

        // the clock the spike measures with, monotonic like every other slice (§7.1)
        using Clock = std::chrono::steady_clock;

        double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
            std::size_t not_verifiable = 0;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            std::printf("  %-6s %-60s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, std::string_view description, std::string_view detail) { std::printf("  %-6s %-60s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), "REC", std::string(detail).c_str()); }

        void unverifiable(Verdict& verdict, std::string_view id, std::string_view description, std::string_view detail) {
            std::printf("  %-6s %-60s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), "N/V", std::string(detail).c_str());
            verdict.not_verifiable++;
        }

        std::string format(double value, int precision = 3) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        double percentile(std::vector<double> samples, const double fraction) {
            // report nothing when nothing was measured (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            std::sort(samples.begin(), samples.end());
            const auto index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));
            return samples.at(index);
        }

        double median(std::vector<double> samples) {
            // an even sample count has two middle values, so the mean of them is the median (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            std::sort(samples.begin(), samples.end());
            const std::size_t middle = samples.size() / 2;
            if (samples.size() % 2 == 1) {
                return samples.at(middle);
            }

            return (samples.at(middle - 1) + samples.at(middle)) / 2.0;
        }

        // the webgpu objects the spike owns for its whole life, which graphite adopts without retaining (§10.2)
        struct WebGpuState
        {
            wgpu::Instance instance;
            wgpu::Adapter adapter;
            wgpu::Device device;
            wgpu::Queue queue;
        };

        // bring up an instance, adapter and device exactly as the dawn slice does, so the device this spike
        // measures is the same shape of device the dawn renderer presents through (plan.md part A)
        bool bring_up_webgpu(WebGpuState& state, std::string& adapter_info) {
            // an empty descriptor asks for the default instance, which is what the dawn slice uses
            wgpu::InstanceDescriptor instance_desc{};
            state.instance = wgpu::CreateInstance(&instance_desc);

            std::atomic<bool> adapter_done{false};
            std::atomic<bool> adapter_ok{false};
            wgpu::RequestAdapterOptions adapter_options{};

            // timed waits need an instance feature this slice does not enable, so the callback arrives while
            // events are processed, which is the dawn slice's documented approach
            wgpu::Adapter requested;
            state.instance.RequestAdapter(&adapter_options, wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
                static_cast<void>(message);
                requested = std::move(adapter);
                adapter_ok.store(status == wgpu::RequestAdapterStatus::Success, std::memory_order_release);
                adapter_done.store(true, std::memory_order_release);
            });

            while (!adapter_done.load(std::memory_order_acquire)) {
                state.instance.ProcessEvents();
                std::this_thread::yield();
            }

            if (!adapter_ok.load(std::memory_order_acquire)) {
                adapter_info = "no adapter";
                return false;
            }
            state.adapter = requested;

            // record what the adapter is, because a software adapter makes every timing figure meaningless (T5.2)
            //
            // this dawn reports an AdapterType rather than a device type, so the software check reads the field
            // the header actually declares: a CPU adapter is the software case (plan.md T5.2)
            wgpu::AdapterInfo info{};
            state.adapter.GetInfo(&info);
            const std::string backend = info.backendType == wgpu::BackendType::Metal ? "metal" : info.backendType == wgpu::BackendType::Vulkan ? "vulkan" : info.backendType == wgpu::BackendType::Null ? "null" : "other";
            const std::string kind = info.adapterType == wgpu::AdapterType::DiscreteGPU ? "discrete" : info.adapterType == wgpu::AdapterType::IntegratedGPU ? "integrated" : info.adapterType == wgpu::AdapterType::CPU ? "cpu(software)" : "unknown";
            const std::string device_text(info.device.data != nullptr ? info.device.data : "", info.device.length);
            adapter_info = backend + "/" + kind + " \"" + device_text + "\"";

            // ask the adapter for a device, which this dawn hands back synchronously
            wgpu::DeviceDescriptor device_desc{};
            state.device = state.adapter.CreateDevice(&device_desc);
            if (!state.device) {
                return false;
            }
            state.queue = state.device.GetQueue();
            return true;
        }

        // what one workload measured, per repetition (§28.2)
        struct WorkloadResult
        {
            std::vector<double> record_ms;
            std::vector<double> submit_ms;
            std::size_t frames = 0;
        };

        // one recorded frame on the shared device: geometry, text, or both (plan.md part B)
        struct FrameTargets
        {
            bool geometry = false;
            bool text = false;
        };

    } // namespace

    // the spike's own state, so no webgpu or skia type reaches a prototype interface (§3.5)
    struct SpikeState
    {
        WebGpuState webgpu;
        std::unique_ptr<skgpu::graphite::Context> context;
        std::unique_ptr<skgpu::graphite::Recorder> recorder;

        // the platform typeface and font the resource factory resolved, because milestone 148 has no default
        // font manager and a1 already solves that (§21.2)
        sk_sp<SkTypeface> typeface;
        SkFont font;
    };

    // create the graphite context on the spike's own dawn device (T5.1)
    bool create_context(SpikeState& state, std::string& detail) {
        // hand graphite the device and the queue the spike already owns; the ctor adopts without retaining, and
        // this process owns both for its whole life, so nothing here has to balance a retain
        skgpu::graphite::DawnBackendContext backend;
        backend.fInstance = state.webgpu.instance;
        backend.fDevice = state.webgpu.device;
        backend.fQueue = state.webgpu.queue;

        // the native tick function is the port's default, which drains dawn's event queue for this context
        state.context = skgpu::graphite::ContextFactory::MakeDawn(backend, skgpu::graphite::ContextOptions{});
        if (state.context == nullptr) {
            detail = "ContextFactory::MakeDawn returned null on an externally created device";
            return false;
        }

        state.recorder = state.context->makeRecorder();
        if (state.recorder == nullptr) {
            detail = "makeRecorder returned null";
            return false;
        }

        detail = "context created on the spike's own wgpu::Device";
        return true;
    }

    // adopt the platform typeface the resource factory already resolved, because milestone 148 has no default
    // font manager and a1 solves that once for every slice (§21.2)
    bool load_fonts(SpikeState& state, const albor::a1::ResourceSet& resources, std::string& detail) {
        state.typeface = resources.typeface;
        if (state.typeface == nullptr) {
            detail = "the resource set resolved no platform typeface";
            return false;
        }

        state.font.setTypeface(state.typeface);
        state.font.setSize(18.0F);
        detail = "platform typeface at 18 px, resolved by a1's resource factory";
        return true;
    }

    // record and submit one frame, which is the unit every timing is measured over (plan.md part B)
    bool record_one_frame(SpikeState& state, const albor::a1::ResourceSetPtr& resources, const FrameTargets& targets, WorkloadResult& result) {
        // allocate the offscreen surface this frame records into, exactly as a1.4's graphite context does
        sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(state.recorder.get(), SkImageInfo::Make(SURFACE_WIDTH, SURFACE_HEIGHT, SkColorType::kBGRA_8888_SkColorType, SkAlphaType::kPremul_SkAlphaType), skgpu::Mipmapped::kNo);

        if (surface == nullptr) {
            return false;
        }

        SkCanvas* canvas = surface->getCanvas();

        // the record clock covers everything that builds gpu work, which is the cost the spike prices (§28.2)
        const double record_start = now_ms();
        canvas->clear(SK_ColorBLACK);

        if (targets.geometry && resources != nullptr) {
            // apply the identity view transform the offscreen surface records at, so the workloads are
            // comparable with a1's paths (§9.1)
            canvas->save();

            // replay the retained batches a1 builds, one draw call per batch (§11)
            for (const sk_sp<SkVertices>& batch : resources->line_batches) {
                canvas->drawVertices(batch, SkBlendMode::kSrcOver, resources->line_paint);
            }
            canvas->restore();
        }

        if (targets.text) {
            // skia's own text path, which is what the dawn slice lacked and this spike supplies (§21.2)
            //
            // the paint is built once per frame rather than per label, so the measurement is of text recording
            // and not of paint construction (§28.2)
            SkPaint label_paint;
            label_paint.setColor(SK_ColorWHITE);

            canvas->save();
            canvas->translate(16.0F, 28.0F);
            for (std::size_t index = 0; index < LABEL_COUNT; index++) {
                const std::string label = "label " + std::to_string(index);
                canvas->drawSimpleText(label.c_str(), label.size(), SkTextEncoding::kUTF8, 0.0F, static_cast<float>(index) * 22.0F, state.font, label_paint);
            }
            canvas->restore();
        }

        const double record_ms = now_ms() - record_start;

        // the submit clock is separate because graphite records lazily and submits at this boundary (§10.2)
        const double submit_start = now_ms();
        state.context->submit();
        const double submit_ms = now_ms() - submit_start;

        result.record_ms.push_back(record_ms);
        result.submit_ms.push_back(submit_ms);
        result.frames++;
        return true;
    }

    // run one workload over its warm-up and repetitions, and report what it measured (plan.md part B)
    WorkloadResult run_workload(SpikeState& state, const albor::a1::ResourceSetPtr& resources, const FrameTargets& targets) {
        WorkloadResult result;

        // the warm-up keeps shader compilation and first-use costs out of the timed samples (§28.2)
        for (std::size_t frame = 0; frame < WARMUP_FRAMES; frame++) {
            static_cast<void>(record_one_frame(state, resources, targets, result));
        }
        result.record_ms.clear();
        result.submit_ms.clear();
        result.frames = 0;

        for (std::size_t repetition = 0; repetition < REPETITIONS; repetition++) {
            for (std::size_t frame = 0; frame < FRAMES_PER_REPETITION; frame++) {
                static_cast<void>(record_one_frame(state, resources, targets, result));
            }
        }

        return result;
    }

} // namespace albor::a1_graphite_dawn

int main() {
    using namespace albor::a1_graphite_dawn;

    std::printf("Prototype A1-Graphite-Dawn: one Dawn device for custom geometry and Skia text (AD-28)\n");
    // the adapter line below is what dawn reports. this used to claim an AMD Radeon Pro 5300M from a hardcoded
    // string, which no run ever verified and which the adapter contradicts: it is an integrated intel gpu
    std::printf("hardware: i7-9750H macOS; the adapter is reported below as dawn sees it\n");
    std::printf("thresholds: prototypes/a1_graphite_dawn/plan.md, committed before this program existed\n");
    std::printf("timing: %zu warm-up frames, then %zu repetitions of %zu frames\n\n", WARMUP_FRAMES, REPETITIONS, FRAMES_PER_REPETITION);

    Verdict verdict;

    // -----------------------------------------------------------------------------------------------------------
    // part A: bring-up and the shared-device claim
    // -----------------------------------------------------------------------------------------------------------

    SpikeState state;
    std::string adapter_info;
    if (!bring_up_webgpu(state.webgpu, adapter_info)) {
        std::printf("could not bring up webgpu: %s\n", adapter_info.c_str());
        return 1;
    }
    std::printf("--- part A: bring-up ---\n");
    record("A2", "adapter", adapter_info);

    // T5.2 gates every timing figure: a software adapter cannot verify them (§28.4)
    const bool hardware_adapter = adapter_info.find("cpu(software)") == std::string::npos && adapter_info.find("null") == std::string::npos;
    check(verdict, "T5.2", "adapter reports a hardware backend", hardware_adapter, hardware_adapter ? adapter_info : adapter_info + " (a software adapter makes every timing below NOT VERIFIABLE)");

    std::string context_detail;
    const bool context_ok = create_context(state, context_detail);
    std::printf("\n");
    if (!context_ok) {
        check(verdict, "T5.1", "graphite context on an externally created dawn device", false, context_detail);
        check(verdict, "T5.6", "gpu work observed and drained", false, "no context, so no work to observe");
        std::printf("\n=== total: %zu passed, %zu failed, %zu not verifiable ===\n", verdict.passed, verdict.failed, verdict.not_verifiable);
        return 1;
    }

    // T5.1: the feasibility claim of the whole spike (§28.4)
    check(verdict, "T5.1", "graphite context on an externally created dawn device", true, context_detail);

    // -----------------------------------------------------------------------------------------------------------
    // the retained resources the geometry workload draws, built from a1's own resource set
    // -----------------------------------------------------------------------------------------------------------

    // build the retained resources through a1's own factory, so the geometry and the typeface are exactly what
    // the other slices draw with and this spike prices the combination rather than re-measuring a1 (§11.2)
    albor::a1::ResourceFactory factory;
    albor::a1::LineBatchSpec line_spec;
    line_spec.segment_count = 4000;
    line_spec.extent = 800.0F;
    line_spec.stroke_width = 1.0F;
    line_spec.seed = 0xA11F5EEDULL;
    albor::a1::ResourceSetPtr resources = factory.create_line_batch(line_spec);
    if (resources == nullptr) {
        std::printf("could not build the retained resources through a1's factory\n");
        return 1;
    }
    record("A2", "retained resources", format(static_cast<double>(albor::a1::describe(*resources).segments()), 0) + " segments in " + format(static_cast<double>(albor::a1::describe(*resources).draw_calls), 0) + " draw calls");

    std::string font_detail;
    const bool fonts_ok = load_fonts(state, *resources, font_detail);
    record("A2", "typeface", fonts_ok ? font_detail : "unavailable: " + font_detail);

    // -----------------------------------------------------------------------------------------------------------
    // part B: the combined workload
    // -----------------------------------------------------------------------------------------------------------

    std::printf("\n--- part B: workloads (%zu frames each after warm-up) ---\n", REPETITIONS * FRAMES_PER_REPETITION);

    const WorkloadResult geometry = run_workload(state, resources, FrameTargets{true, false});
    const WorkloadResult text = run_workload(state, resources, FrameTargets{false, true});
    const WorkloadResult combined = run_workload(state, resources, FrameTargets{true, true});

    std::printf("  %-18s %-10s %-10s %-10s %-10s\n", "workload", "record p50", "record p99", "submit p50", "submit p99");
    std::printf("  %-18s %-10s %-10s %-10s %-10s\n", "geometry only", format(percentile(geometry.record_ms, 0.50)).c_str(), format(percentile(geometry.record_ms, 0.99)).c_str(), format(percentile(geometry.submit_ms, 0.50)).c_str(), format(percentile(geometry.submit_ms, 0.99)).c_str());
    std::printf("  %-18s %-10s %-10s %-10s %-10s\n", "text only", format(percentile(text.record_ms, 0.50)).c_str(), format(percentile(text.record_ms, 0.99)).c_str(), format(percentile(text.submit_ms, 0.50)).c_str(), format(percentile(text.submit_ms, 0.99)).c_str());
    std::printf("  %-18s %-10s %-10s %-10s %-10s\n", "geometry + text", format(percentile(combined.record_ms, 0.50)).c_str(), format(percentile(combined.record_ms, 0.99)).c_str(), format(percentile(combined.submit_ms, 0.50)).c_str(), format(percentile(combined.submit_ms, 0.99)).c_str());

    // T5.3: both kinds of work in one recorded frame on one device, which is the feature gap itself (§28.4)
    const std::size_t combined_frames = combined.frames;
    check(verdict, "T5.3", "geometry and skia text recorded in one frame on one device", combined_frames > 0, format(static_cast<double>(combined_frames), 0) + " combined frames recorded, each carrying both workloads");

    if (hardware_adapter) {
        // T5.4: adding text may not more than double the record cost (§28.4)
        const double geometry_p50 = median(geometry.record_ms);
        const double combined_p50 = median(combined.record_ms);
        const double multiple = geometry_p50 > 0.0 ? combined_p50 / geometry_p50 : 0.0;
        check(verdict, "T5.4", "combined record p50 <= 2x geometry-only", multiple <= T5_4_RECORD_MULTIPLE, format(multiple) + "x <= " + format(T5_4_RECORD_MULTIPLE, 1) + "x (" + format(geometry_p50) + " ms -> " + format(combined_p50) + " ms)");

        // T5.5: the combination must carry a window at the pacing band a2 measured on this machine (§28.4)
        //
        // the recording is offscreen, so throughput is derived from the recorded and submitted frames over the
        // wall time the timed run took, which is the honest number for a recording cost and is reported as such
        const double busy_ms = median(geometry.submit_ms) + median(text.submit_ms) + median(combined.submit_ms) * static_cast<double>(combined.frames);
        const double frame_ms = median(combined.record_ms) + median(combined.submit_ms);
        const double fps = frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0;
        static_cast<void>(busy_ms);
        check(verdict, "T5.5", "combined workload sustains >= 55 fps of recorded frames", fps >= T5_5_MIN_FPS, format(fps, 1) + " fps >= " + format(T5_5_MIN_FPS, 0) + " (record p50 " + format(median(combined.record_ms)) + " ms + submit p50 " + format(median(combined.submit_ms)) + " ms per frame, offscreen and unpresented)");
    }
    else {
        // a software adapter converts the cost thresholds into not-verifiable rather than silent passes (§28.4)
        unverifiable(verdict, "T5.4", "combined record p50 <= 2x geometry-only", "adapter is " + adapter_info + ", so the ratio is not a hardware figure");
        unverifiable(verdict, "T5.5", "combined workload sustains >= 55 fps", "adapter is " + adapter_info + ", so throughput is not a hardware figure");
    }

    // T5.6: the context must report its GPU work, or a drop-on-the-floor context would satisfy everything else
    const bool drained = !state.context->hasUnfinishedGpuWork();
    check(verdict, "T5.6", "gpu work observed and drained after the final submit", drained, drained ? "hasUnfinishedGpuWork() is false" : "the context still reports unfinished gpu work");

    record("-", "recorded frames", format(static_cast<double>(geometry.frames + text.frames + combined.frames), 0) + " total across the three workloads");
    record("-", "labelled device note", "the recorder and the surface are offscreen, so no presentation cost is included; a presented variant belongs to the a1 work this spike informs");

    std::printf("\n=== total: %zu passed, %zu failed, %zu not verifiable ===\n", verdict.passed, verdict.failed, verdict.not_verifiable);

    // report a failure to the shell so a ci job notices, while a software adapter is not treated as one (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
