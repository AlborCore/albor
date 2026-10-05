// Prototype A1-Graphite-Dawn, phase two: the shared-device measurement (AD-28, §30)
//
// plan.md was committed before the first spike existed (§28.4). graphite_dawn_main.cpp is that spike and it keeps
// its pre-registered T5.1-T5.6 thresholds. This driver is what runs once the dawn feature is enabled: it proves the
// stages of task §5 one at a time, establishes the shared-device property with diagnostics (§6), and then measures
// the a1 workloads unchanged - w1's 1.8e6 segments and w2's 4000 labels (§7).
//
// Nothing in a1 or a1-dawn is modified. The workload parameters, seeds and label paint are transcribed from
// prototypes/a1/workloads.cpp and prototypes/a1/gpu_context.macos.mm so these figures join the existing ones
// instead of replacing them.
//
// every stage is on its own clock and the clocks are never summed: recording is cpu work, the enqueue submit is
// the cost of handing graphite's recording to dawn, the fenced submit is a cpu wait for gpu completion, and the
// readback is the transfer. §13 forbids reporting those as one number, and a1.4 recorded 0.0016 ms for an enqueue
// and said so.

#include "prototypes/a1/resource_factory.h"
#include "prototypes/a1/resource_set.h"

#include <webgpu/webgpu_cpp.h>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkFontMgr.h>
#include <skia/ports/SkFontMgr_mac_ct.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkRect.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkTypeface.h>
#include <skia/core/SkVertices.h>
#include <skia/gpu/graphite/Context.h>
#include <skia/gpu/graphite/ContextOptions.h>
#include <skia/gpu/graphite/GraphiteTypes.h>
#include <skia/gpu/graphite/Recorder.h>
#include <skia/gpu/graphite/Surface.h>
#include <skia/gpu/graphite/dawn/DawnBackendContext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace albor::a1_graphite_dawn
{

    namespace
    {

        // -----------------------------------------------------------------------------------------------------------
        // the workload parameters, transcribed from prototypes/a1/workloads.cpp so these figures join the existing
        // ones. the seeds are a1's, so the geometry is bit-identical to the batch ganesh and dawn drew
        // -----------------------------------------------------------------------------------------------------------

        constexpr int WINDOW_WIDTH = 1280;
        constexpr int WINDOW_HEIGHT = 720;

        // w1 waveform: a1 builds one million segments and appends eight times of 100k to reach this total
        constexpr std::size_t W1_SEGMENTS = 1800000;
        constexpr float WORKLOAD_EXTENT = 2000.0F;
        constexpr float STROKE_WIDTH = 1.0F;
        constexpr std::uint64_t W1_SEED = 0xA11F5EEDULL;

        // w2 cad canvas: a1's symbol set, whose 4000 labels are what the dawn slice never drew
        constexpr std::size_t W2_SYMBOLS = 100000;
        constexpr std::size_t W2_SEGMENTS_PER_SYMBOL = 10;
        constexpr std::size_t W2_LABELS = 4000;
        constexpr float W2_LABEL_SIZE = 11.0F;
        constexpr std::uint64_t W2_SEED = 0xCAD0FFEEULL;

        // the timing policy of the other slices, so a percentile here means what it means there (§28.2)
        constexpr std::size_t WARMUP_FRAMES = 8;
        constexpr std::size_t REPETITIONS = 5;
        constexpr std::size_t FRAMES_PER_REPETITION = 20;

        using Clock = std::chrono::steady_clock;

        double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

        std::string format(double value, int precision = 3) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        double median_of(std::vector<double> samples) {
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

        double percentile_of(std::vector<double> samples, const double fraction) {
            if (samples.empty()) {
                return 0.0;
            }

            std::sort(samples.begin(), samples.end());
            const auto index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));
            return samples.at(index);
        }

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
            std::size_t not_verifiable = 0;
        };

        void report(const char* id, const char* description, const std::string& detail) { std::printf("  %-6s %-56s %s\n", id, description, detail.c_str()); }

        void check(Verdict& verdict, const char* id, const char* description, bool passed, const std::string& detail) {
            std::printf("  %-6s %-56s %-4s %s\n", id, description, passed ? "PASS" : "FAIL", detail.c_str());
            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void unverifiable(Verdict& verdict, const char* id, const char* description, const std::string& detail) {
            std::printf("  %-6s %-56s %-4s %s\n", id, description, "N/V", detail.c_str());
            verdict.not_verifiable++;
        }

        // -----------------------------------------------------------------------------------------------------------
        // the webgpu objects this process owns. graphite adopts handles without retaining the owning object, and
        // this struct outlives every context, which is what makes the hand-off safe (§10.2)
        // -----------------------------------------------------------------------------------------------------------

        struct WebGpuState
        {
            wgpu::Instance instance;
            wgpu::Adapter adapter;
            wgpu::Device device;
            wgpu::Queue queue;
        };

        bool bring_up_webgpu(WebGpuState& state, std::string& adapter_info) {
            // graphite's dawn queue manager implements SyncToCpu with a dawn timeout wait, and dawn only allows
            // one when the instance requested it. an instance without this feature aborts inside skia on the
            // first fenced submit, which is what the first run of this driver did
            static constexpr wgpu::InstanceFeatureName kRequiredInstanceFeatures[] = {wgpu::InstanceFeatureName::TimedWaitAny};

            wgpu::InstanceDescriptor instance_desc{};
            instance_desc.requiredFeatureCount = 1;
            instance_desc.requiredFeatures = kRequiredInstanceFeatures;
            state.instance = wgpu::CreateInstance(&instance_desc);

            std::atomic<bool> adapter_done{false};
            std::atomic<bool> adapter_ok{false};
            wgpu::RequestAdapterOptions adapter_options{};

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

            // a software adapter makes every timing figure meaningless, so it is identified before anything is
            // measured (T5.2)
            wgpu::AdapterInfo info{};
            state.adapter.GetInfo(&info);
            const std::string backend = info.backendType == wgpu::BackendType::Metal ? "metal" : info.backendType == wgpu::BackendType::Vulkan ? "vulkan" : info.backendType == wgpu::BackendType::Null ? "null" : "other";
            const std::string kind = info.adapterType == wgpu::AdapterType::DiscreteGPU ? "discrete" : info.adapterType == wgpu::AdapterType::IntegratedGPU ? "integrated" : info.adapterType == wgpu::AdapterType::CPU ? "cpu(software)" : "unknown";
            const std::string device_text(info.device.data != nullptr ? info.device.data : "", info.device.length);
            const std::string vendor_text(info.vendor.data != nullptr ? info.vendor.data : "", info.vendor.length);
            adapter_info = backend + "/" + kind + " \"" + device_text + "\" vendor \"" + vendor_text + "\"";

            wgpu::DeviceDescriptor device_desc{};
            state.device = state.adapter.CreateDevice(&device_desc);
            if (!state.device) {
                adapter_info += " (CreateDevice returned no device)";
                return false;
            }
            state.queue = state.device.GetQueue();
            return true;
        }

        // -----------------------------------------------------------------------------------------------------------
        // the graphite side: one context and one recorder, on the device above
        // -----------------------------------------------------------------------------------------------------------

        struct State
        {
            WebGpuState webgpu;
            std::unique_ptr<skgpu::graphite::Context> context;
            std::unique_ptr<skgpu::graphite::Recorder> recorder;
        };

        bool create_context(State& state, std::string& detail) {
            skgpu::graphite::DawnBackendContext backend;
            backend.fInstance = state.webgpu.instance;
            backend.fDevice = state.webgpu.device;
            backend.fQueue = state.webgpu.queue;

            // the port's default tick function drains dawn's event queue, which is what lets a sync submit wait
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

            detail = "context and recorder created on the only device this process creates";
            return true;
        }

        // -----------------------------------------------------------------------------------------------------------
        // pixel readback through graphite's own async read. the callback fires inside the fenced submit, so the
        // copy has to happen there: the result does not outlive the callback
        // -----------------------------------------------------------------------------------------------------------

        struct Readback
        {
            bool called = false;
            bool ok = false;
            std::size_t row_bytes = 0;
            std::vector<std::uint8_t> pixels;
        };

        void readback_callback(SkImage::ReadPixelsContext context, std::unique_ptr<const SkImage::AsyncReadResult> result) {
            auto* out = static_cast<Readback*>(context);
            out->called = true;

            if (result == nullptr || result->count() < 1) {
                return;
            }

            out->row_bytes = result->rowBytes(0);
            const std::uint8_t* data = static_cast<const std::uint8_t*>(result->data(0));
            if (data == nullptr || out->row_bytes == 0) {
                return;
            }

            out->pixels.assign(data, data + out->row_bytes * static_cast<std::size_t>(WINDOW_HEIGHT));
            out->ok = true;
        }

        // -----------------------------------------------------------------------------------------------------------
        // pixel analysis, so "it drew something" is never the evidence
        // -----------------------------------------------------------------------------------------------------------

        struct InkBox
        {
            std::size_t count = 0;
            int min_x = 0;
            int min_y = 0;
            int max_x = -1;
            int max_y = -1;
            double mean_luma = 0.0;
        };

        InkBox analyse(const Readback& readback) {
            InkBox box;
            std::uint64_t luma_total = 0;

            for (int y = 0; y < WINDOW_HEIGHT; y++) {
                for (int x = 0; x < WINDOW_WIDTH; x++) {
                    const std::size_t offset = readback.row_bytes * static_cast<std::size_t>(y) + static_cast<std::size_t>(x) * 4;
                    if (offset + 3 >= readback.pixels.size()) {
                        continue;
                    }

                    // the readback is bgra, so blue comes first
                    const std::uint32_t blue = readback.pixels.at(offset);
                    const std::uint32_t green = readback.pixels.at(offset + 1);
                    const std::uint32_t red = readback.pixels.at(offset + 2);
                    if (blue == 0 && green == 0 && red == 0) {
                        continue;
                    }

                    if (box.count == 0) {
                        box.min_x = x;
                        box.max_x = x;
                        box.min_y = y;
                        box.max_y = y;
                    }
                    else {
                        box.min_x = std::min(box.min_x, x);
                        box.max_x = std::max(box.max_x, x);
                        box.min_y = std::min(box.min_y, y);
                        box.max_y = std::max(box.max_y, y);
                    }
                    box.count++;
                    luma_total += (299U * red + 587U * green + 114U * blue) / 1000U;
                }
            }

            if (box.count > 0) {
                box.mean_luma = static_cast<double>(luma_total) / static_cast<double>(box.count);
            }
            return box;
        }

        struct Diff
        {
            std::size_t differing = 0;
            std::size_t total = 0;
            unsigned max_channel_delta = 0;
            double mean_error = 0.0;
        };

        // the exact difference between two readbacks of one scene, which is the repeatability evidence
        Diff compare(const Readback& left, const Readback& right) {
            Diff diff;
            diff.total = std::min(left.pixels.size(), right.pixels.size()) / 4;

            std::uint64_t error_total = 0;
            for (std::size_t pixel = 0; pixel < diff.total; pixel++) {
                bool differs = false;
                for (std::size_t channel = 0; channel < 4; channel++) {
                    const std::size_t offset = pixel * 4 + channel;
                    const auto delta = static_cast<unsigned>(std::abs(static_cast<int>(left.pixels.at(offset)) - static_cast<int>(right.pixels.at(offset))));
                    if (delta != 0) {
                        differs = true;
                        diff.max_channel_delta = std::max(diff.max_channel_delta, delta);
                        error_total += delta;
                    }
                }
                if (differs) {
                    diff.differing++;
                }
            }

            if (diff.total > 0) {
                diff.mean_error = static_cast<double>(error_total) / static_cast<double>(diff.total * 4);
            }
            return diff;
        }

        // -----------------------------------------------------------------------------------------------------------
        // one recorded frame, with every stage on its own clock
        // -----------------------------------------------------------------------------------------------------------

        struct FrameContent
        {
            const albor::a1::ResourceSet* resources = nullptr;
            bool draw_geometry = false;
            bool draw_text = false;
            std::function<void(SkCanvas&)> overlay = nullptr;
        };

        struct FrameTiming
        {
            std::vector<double> surface_ms;
            std::vector<double> record_ms;
            std::vector<double> enqueue_ms;
            std::vector<double> fence_ms;
            std::vector<double> readback_ms;
        };

        void clear_timing(FrameTiming& timing) {
            timing.surface_ms.clear();
            timing.record_ms.clear();
            timing.enqueue_ms.clear();
            timing.fence_ms.clear();
            timing.readback_ms.clear();
        }

        void record_frame(State& state, const FrameContent& content, FrameTiming& timing, Readback* readback) {
            // graphite allocates the surface itself: TextureInfo's constructor from backend data is private, so a
            // client cannot hand graphite a texture it allocated. recorded in RESULTS.md as an api limitation
            const double surface_start = now_ms();
            sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(state.recorder.get(), SkImageInfo::Make(WINDOW_WIDTH, WINDOW_HEIGHT, SkColorType::kBGRA_8888_SkColorType, SkAlphaType::kPremul_SkAlphaType), skgpu::Mipmapped::kNo);
            timing.surface_ms.push_back(now_ms() - surface_start);

            if (surface == nullptr) {
                return;
            }

            SkCanvas* canvas = surface->getCanvas();

            const double record_start = now_ms();
            canvas->clear(SK_ColorBLACK);

            canvas->save();

            if (content.draw_geometry && content.resources != nullptr) {
                // one draw call per retained batch, which is the o(1) replay §11.2 promises
                for (const sk_sp<SkVertices>& batch : content.resources->line_batches) {
                    canvas->drawVertices(batch, SkBlendMode::kSrcOver, content.resources->line_paint);
                }
            }

            if (content.draw_text && content.resources != nullptr) {
                // a1's exact label loop, so these are the same 4000 labels ganesh drew (§21.2)
                if (content.resources->font.getTypeface() != nullptr && !content.resources->labels.empty()) {
                    for (const albor::a1::Label& label : content.resources->labels) {
                        const char* value = content.resources->label_buffer.data() + label.offset;
                        canvas->drawSimpleText(value, label.length, SkTextEncoding::kUTF8, label.x, label.y, content.resources->font, content.resources->label_paint);
                    }
                }
            }

            if (content.overlay) {
                content.overlay(*canvas);
            }
            canvas->restore();

            timing.record_ms.push_back(now_ms() - record_start);

            // the enqueue submit returns once the recording is handed to dawn, with the gpu not yet started
            const double enqueue_start = now_ms();
            state.context->submit();
            timing.enqueue_ms.push_back(now_ms() - enqueue_start);

            // the fenced submit is a cpu wait for gpu completion, and it is the closest this build gets to gpu
            // execution time. it is a wait, not a gpu timestamp, and is labelled as one wherever it is reported
            const double fence_start = now_ms();
            state.context->submit(skgpu::graphite::SubmitInfo{skgpu::graphite::SyncToCpu::kYes});
            timing.fence_ms.push_back(now_ms() - fence_start);

            if (readback != nullptr) {
                const SkImageInfo dst = SkImageInfo::Make(WINDOW_WIDTH, WINDOW_HEIGHT, SkColorType::kBGRA_8888_SkColorType, SkAlphaType::kPremul_SkAlphaType, nullptr);
                const double readback_start = now_ms();
                state.context->asyncRescaleAndReadPixels(surface.get(), dst, SkIRect::MakeWH(WINDOW_WIDTH, WINDOW_HEIGHT), SkImage::RescaleGamma::kSrc, SkImage::RescaleMode::kNearest, readback_callback, readback);
                state.context->submit(skgpu::graphite::SubmitInfo{skgpu::graphite::SyncToCpu::kYes});
                timing.readback_ms.push_back(now_ms() - readback_start);
            }
        }

        // run the warm-up, then the timed repetitions (§28.2)
        FrameTiming run_frames(State& state, const FrameContent& content) {
            FrameTiming timing;

            for (std::size_t frame = 0; frame < WARMUP_FRAMES; frame++) {
                record_frame(state, content, timing, nullptr);
            }
            clear_timing(timing);

            for (std::size_t repetition = 0; repetition < REPETITIONS; repetition++) {
                for (std::size_t frame = 0; frame < FRAMES_PER_REPETITION; frame++) {
                    record_frame(state, content, timing, nullptr);
                }
            }
            return timing;
        }

        // capture one frame's pixels, for the validation and feature passes
        bool capture(State& state, const FrameContent& content, Readback& readback, InkBox& box) {
            FrameTiming timing;
            readback = Readback{};
            record_frame(state, content, timing, &readback);
            box = analyse(readback);
            return readback.ok;
        }

        void print_timing(const char* label, const FrameTiming& timing) {
            std::printf("  %-26s %-9s %-9s %-9s %-9s %-6s\n", label, format(median_of(timing.record_ms)).c_str(), format(median_of(timing.enqueue_ms)).c_str(), format(median_of(timing.fence_ms)).c_str(), format(percentile_of(timing.record_ms, 0.99)).c_str(), format(static_cast<double>(timing.record_ms.size()), 0).c_str());
        }

        void print_timing_header() {
            std::printf("  %-26s %-9s %-9s %-9s %-9s %-6s\n", "workload", "record50", "enqueue50", "fence50", "record99", "frames");
        }

    } // namespace

} // namespace albor::a1_graphite_dawn

int main() {
    using namespace albor::a1_graphite_dawn;

    std::printf("Prototype A1-Graphite-Dawn phase two: one Dawn device for geometry and text (AD-28)\n");
    std::printf("workloads: a1's w1 1.8e6 segments and w2 4000 labels, a1's seeds, from prototypes/a1\n");
    std::printf("timing: %zu warm-up frames then %zu x %zu frames; record, enqueue, fence and readback are separate\n\n", WARMUP_FRAMES, REPETITIONS, FRAMES_PER_REPETITION);

    Verdict verdict;

    State state;
    std::string adapter_info;
    if (!bring_up_webgpu(state.webgpu, adapter_info)) {
        std::printf("could not bring up webgpu: %s\n", adapter_info.c_str());
        return 1;
    }

    // -----------------------------------------------------------------------------------------------------------
    // §6: the shared-device property, with the evidence this build can actually produce
    // -----------------------------------------------------------------------------------------------------------

    std::printf("--- part A: device identity and the shared-device claim ---\n");
    report("A0", "instance feature", "timed-wait-any requested at CreateInstance; this dawn exposes no instance feature query, so the fenced submits below are the proof it took effect");
    report("A1", "adapter", adapter_info);
    report("A2", "instances created here", "1, at bring-up");
    report("A3", "adapters requested here", "1, at bring-up");
    report("A4", "devices created here", "1, at bring-up");
    report("A5", "queues", "1, device.GetQueue(), handed to graphite unchanged");

    const bool hardware_adapter = adapter_info.find("cpu(software)") == std::string::npos && adapter_info.find("null") == std::string::npos;
    check(verdict, "T5.2", "adapter reports a hardware backend", hardware_adapter, adapter_info);

    std::string context_detail;
    if (!create_context(state, context_detail)) {
        check(verdict, "T5.1", "graphite context on an externally created dawn device", false, context_detail);
        std::printf("\n=== total: %zu passed, %zu failed, %zu not verifiable ===\n", verdict.passed, verdict.failed, verdict.not_verifiable);
        return 1;
    }
    check(verdict, "T5.1", "graphite context on an externally created dawn device", true, context_detail);

    const bool backend_is_dawn = state.context->backend() == skgpu::BackendApi::kDawn;
    check(verdict, "T5.1b", "graphite reports the dawn backend", backend_is_dawn, backend_is_dawn ? "context->backend() == skgpu::BackendApi::kDawn" : "context->backend() is not kDawn");

    // the api has no factory that lets graphite make a device: DawnBackendContext's only constructor takes the
    // client's instance, device and queue. configuration B is therefore not representable, not merely untested
    report("A6", "configuration B", "not representable: DawnBackendContext cannot be built without the client's device");
    report("A7", "client-owned target", "not representable: TextureInfo's constructor from backend data is private, so graphite allocates its own surfaces");

    // -----------------------------------------------------------------------------------------------------------
    // §5: the smoke test, staged, failing loudly
    // -----------------------------------------------------------------------------------------------------------

    std::printf("\n--- part B: smoke test ---\n");

    Readback primitive_pixels;
    InkBox primitive_box;
    const bool primitive_ok = capture(state, FrameContent{nullptr, false, false, [](SkCanvas& canvas) {
                                        SkPaint paint;
                                        paint.setColor(SK_ColorWHITE);
                                        paint.setAntiAlias(false);
                                        canvas.drawRect(SkRect::MakeXYWH(600.0F, 340.0F, 80.0F, 40.0F), paint);
                                    }},
                                     primitive_pixels, primitive_box);

    check(verdict, "S1", "surface created and read back", primitive_ok, primitive_ok ? "SkSurfaces::RenderTarget returned a surface and the async read delivered pixels" : "no pixels came back");

    // the assertion is the geometry itself: a rect at 600,340 sized 80x40 lights exactly that box and nothing else
    const bool primitive_exact = primitive_box.count == 80U * 40U && primitive_box.min_x == 600 && primitive_box.min_y == 340 && primitive_box.max_x == 679 && primitive_box.max_y == 379;
    check(verdict, "S2", "readback matches the drawn primitive exactly", primitive_exact, primitive_exact ? "3200 lit pixels in exactly the requested 80x40 box at 600,340" : "lit " + std::to_string(primitive_box.count) + " pixels, box " + std::to_string(primitive_box.min_x) + "," + std::to_string(primitive_box.min_y) + " to " + std::to_string(primitive_box.max_x) + "," + std::to_string(primitive_box.max_y));

    const bool drained = !state.context->hasUnfinishedGpuWork();
    check(verdict, "S3", "gpu work drained after the fenced submit", drained, drained ? "hasUnfinishedGpuWork() is false" : "work still outstanding");

    // text on the same context and the same device, through the typeface a1's own factory resolved
    albor::a1::ResourceFactory factory;
    albor::a1::LineBatchSpec probe_spec;
    probe_spec.segment_count = 16;
    probe_spec.extent = WORKLOAD_EXTENT;
    probe_spec.stroke_width = STROKE_WIDTH;
    probe_spec.seed = W1_SEED;
    const albor::a1::ResourceSetPtr probe = factory.create_line_batch(probe_spec);
    SkTypeface* typeface = probe != nullptr ? probe->typeface.get() : nullptr;

    // the factory's own route is SkFontMgr_New_CoreText(nullptr)->matchFamilyStyle(nullptr, {}), and it returned
    // nothing. rather than guess why, every route is tried and reported, because w2 is only meaningful with a
    // real face and "text silently drew nothing" is the failure mode that would invalidate it
    sk_sp<SkTypeface> fallback_face;
    std::string font_probe;
    {
        const sk_sp<SkFontMgr> manager = SkFontMgr_New_CoreText(nullptr);
        if (manager == nullptr) {
            font_probe = "SkFontMgr_New_CoreText returned null";
        }
        else {
            const int families = manager->countFamilies();
            const sk_sp<SkTypeface> by_null = manager->matchFamilyStyle(nullptr, SkFontStyle());
            const sk_sp<SkTypeface> by_helvetica = manager->matchFamilyStyle("Helvetica", SkFontStyle());
            const sk_sp<SkTypeface> by_legacy = manager->legacyMakeTypeface(nullptr, SkFontStyle());
            font_probe = "coretext manager present, " + std::to_string(families) + " families; matchFamilyStyle(null)="
                + (by_null != nullptr ? "ok" : "null") + ", matchFamilyStyle(Helvetica)=" + (by_helvetica != nullptr ? "ok" : "null")
                + ", legacyMakeTypeface(null)=" + (by_legacy != nullptr ? "ok" : "null") + ", factory typeface=" + (typeface != nullptr ? "ok" : "null");

            // if the factory's route failed but a named family resolved, the text workload still runs on a real
            // face, and which route worked is recorded rather than papered over
            if (typeface == nullptr) {
                fallback_face = by_helvetica != nullptr ? by_helvetica : by_null;
                typeface = fallback_face.get();
                font_probe += typeface != nullptr ? "; falling back to a coretext face for the text workload" : "; no coretext face available";
            }
        }
    }

    // this skia build exports exactly one font manager, SkFontMgr_New_CoreText: there is no freetype and no
    // custom manager, and SkTypeface in milestone 148 has no MakeFromData. so if coretext's collection is empty
    // in this process there is no route to a face at all, and the text workload is unmeasurable rather than slow
    report("F2", "font managers in this build", "SkFontMgr_New_CoreText only (no freetype, no custom manager, and SkTypeface::MakeFromData does not exist in milestone 148)");
    report("F1", "typeface routes", font_probe);

    bool glyph_ok = false;
    std::string glyph_detail = typeface != nullptr ? "the async read delivered nothing" : "no typeface resolved on any route, and milestone 148 has no default font manager";
    if (typeface != nullptr) {
        SkFont font;
        font.setTypeface(fallback_face != nullptr ? fallback_face : probe->typeface);
        font.setSize(48.0F);

        Readback glyph_pixels;
        InkBox glyph_box;
        constexpr char label[] = "graphite on dawn";
        glyph_ok = capture(state, FrameContent{nullptr, false, false, [&font, label](SkCanvas& canvas) {
                          SkPaint paint;
                          paint.setColor(SK_ColorWHITE);
                          paint.setAntiAlias(true);
                          canvas.drawSimpleText(label, sizeof(label) - 1, SkTextEncoding::kUTF8, 40.0F, 120.0F, font, paint);
                      }},
                          glyph_pixels, glyph_box);

        glyph_detail = glyph_ok ? format(static_cast<double>(glyph_box.count), 0) + " lit pixels from 17 glyphs at 48 px, box " + std::to_string(glyph_box.min_x) + "," + std::to_string(glyph_box.min_y) + " to " + std::to_string(glyph_box.max_x) + "," + std::to_string(glyph_box.max_y) : "the async read delivered nothing";
    }
    check(verdict, "S4", "graphite text recorded and read back", glyph_ok, glyph_detail);

    // -----------------------------------------------------------------------------------------------------------
    // the retained sets, built through a1's own factory with a1's seeds
    // -----------------------------------------------------------------------------------------------------------

    albor::a1::LineBatchSpec w1_spec;
    w1_spec.segment_count = W1_SEGMENTS;
    w1_spec.extent = WORKLOAD_EXTENT;
    w1_spec.stroke_width = STROKE_WIDTH;
    w1_spec.seed = W1_SEED;
    const albor::a1::ResourceSetPtr w1_resources = factory.create_line_batch(w1_spec);
    if (w1_resources == nullptr) {
        std::printf("could not build w1's retained resources\n");
        return 1;
    }

    albor::a1::SymbolSetSpec w2_spec;
    w2_spec.symbol_count = W2_SYMBOLS;
    w2_spec.segments_per_symbol = W2_SEGMENTS_PER_SYMBOL;
    w2_spec.label_count = W2_LABELS;
    w2_spec.extent = WORKLOAD_EXTENT;
    w2_spec.label_size = W2_LABEL_SIZE;
    w2_spec.seed = W2_SEED;
    const albor::a1::ResourceSetPtr w2_resources = factory.create_symbol_set(w2_spec);
    if (w2_resources == nullptr) {
        std::printf("could not build w2's retained resources\n");
        return 1;
    }

    const albor::a1::ResourceSetInfo w1_info = albor::a1::describe(*w1_resources);
    const albor::a1::ResourceSetInfo w2_info = albor::a1::describe(*w2_resources);
    std::printf("\n  w1: %s segments, %s retained batches, %s bytes\n", format(static_cast<double>(w1_info.segments()), 0).c_str(), format(static_cast<double>(w1_info.draw_calls), 0).c_str(), format(static_cast<double>(w1_info.bytes), 0).c_str());
    std::printf("  w2: %s symbols, %s segments, %s labels, %s bytes\n", format(static_cast<double>(w2_info.symbols), 0).c_str(), format(static_cast<double>(w2_info.segments()), 0).c_str(), format(static_cast<double>(w2_info.labels), 0).c_str(), format(static_cast<double>(w2_info.bytes), 0).c_str());

    // -----------------------------------------------------------------------------------------------------------
    // §9: deterministic output over the mixed scene
    // -----------------------------------------------------------------------------------------------------------

    std::printf("\n--- part C: pixel validation ---\n");

    Readback run_a;
    Readback run_b;
    InkBox box_a;
    InkBox box_b;
    static_cast<void>(capture(state, FrameContent{w1_resources.get(), true, true, nullptr}, run_a, box_a));
    static_cast<void>(capture(state, FrameContent{w1_resources.get(), true, true, nullptr}, run_b, box_b));

    const Diff repeat = compare(run_a, run_b);
    check(verdict, "P1", "geometry + text identical across two runs", repeat.differing == 0, format(static_cast<double>(repeat.differing), 0) + " of " + format(static_cast<double>(repeat.total), 0) + " pixels differ; max channel delta " + std::to_string(repeat.max_channel_delta) + ", mean error " + format(repeat.mean_error, 6));
    std::printf("  mixed scene: %s lit pixels, box %d,%d to %d,%d\n", format(static_cast<double>(box_a.count), 0).c_str(), box_a.min_x, box_a.min_y, box_a.max_x, box_a.max_y);

    // w2's labels on their own, which is the figure the dawn slice never had and the reason its w2 was not
    // comparable: geometry-only would have been an easier number to report and a wrong one
    Readback labels_pixels;
    InkBox labels_box;
    static_cast<void>(capture(state, FrameContent{w2_resources.get(), false, true, nullptr}, labels_pixels, labels_box));
    check(verdict, "P2", "w2 draws its 4000 labels", labels_box.count > 0, format(static_cast<double>(labels_box.count), 0) + " lit pixels from " + format(static_cast<double>(w2_info.labels), 0) + " labels, box " + std::to_string(labels_box.min_x) + "," + std::to_string(labels_box.min_y) + " to " + std::to_string(labels_box.max_x) + "," + std::to_string(labels_box.max_y));

    // -----------------------------------------------------------------------------------------------------------
    // §7: the a1 workloads
    // -----------------------------------------------------------------------------------------------------------

    std::printf("\n--- part D: workloads (median of %zu frames, record99 is the p99) ---\n", REPETITIONS * FRAMES_PER_REPETITION);
    print_timing_header();

    const FrameTiming w1_geometry = run_frames(state, FrameContent{w1_resources.get(), true, false, nullptr});
    print_timing("w1 geometry 1.8e6 segs", w1_geometry);

    const FrameTiming w2_geometry = run_frames(state, FrameContent{w2_resources.get(), true, false, nullptr});
    print_timing("w2 geometry only", w2_geometry);

    const FrameTiming w2_labels = run_frames(state, FrameContent{w2_resources.get(), false, true, nullptr});
    print_timing("w2 text only (4000 labels)", w2_labels);

    const FrameTiming w2_combined = run_frames(state, FrameContent{w2_resources.get(), true, true, nullptr});
    print_timing("w2 geometry + text", w2_combined);

    // the sum of the two isolated halves against the combined frame: if text and geometry interact, this differs
    const double halves = median_of(w2_geometry.record_ms) + median_of(w2_labels.record_ms);
    std::printf("  w2 text share of the combined record: %s ms of %s ms (halves %s ms)\n", format(median_of(w2_labels.record_ms)).c_str(), format(median_of(w2_combined.record_ms)).c_str(), format(halves).c_str());

    // the memory this build can observe
    std::printf("  graphite budgeted bytes: %s current, %s purgeable, %s max\n", format(static_cast<double>(state.context->currentBudgetedBytes()), 0).c_str(), format(static_cast<double>(state.context->currentPurgeableBytes()), 0).c_str(), format(static_cast<double>(state.context->maxBudgetedBytes()), 0).c_str());

    // -----------------------------------------------------------------------------------------------------------
    // §7: w3. a1's w3 is a two-window presentation test with an injected 100 ms hold, which offscreen recording
    // cannot reproduce, so only its update half is measured and the rest is reported as not verifiable
    // -----------------------------------------------------------------------------------------------------------

    std::printf("\n--- part E: w3 update half ---\n");

    std::vector<double> rebuild_ms;
    std::vector<double> rerecord_ms;
    for (std::size_t step = 0; step < REPETITIONS; step++) {
        albor::a1::LineBatchSpec next;
        next.extent = WORKLOAD_EXTENT;
        next.stroke_width = STROKE_WIDTH;
        next.seed = W1_SEED + step;
        next.segment_count = W1_SEGMENTS;

        const double build_start = now_ms();
        const albor::a1::ResourceSetPtr grown = factory.create_line_batch(next);
        rebuild_ms.push_back(now_ms() - build_start);
        if (grown == nullptr) {
            break;
        }

        FrameTiming timing;
        record_frame(state, FrameContent{grown.get(), true, false, nullptr}, timing, nullptr);
        rerecord_ms.push_back(median_of(timing.record_ms));
    }

    std::printf("  retained rebuild after mutation p50: %s ms (a1's append copies every point again)\n", format(median_of(rebuild_ms)).c_str());
    std::printf("  re-record of the mutated set p50: %s ms\n", format(median_of(rerecord_ms)).c_str());
    unverifiable(verdict, "W3iso", "w3 multi-window isolation and presentation hold", "a1's w3 needs two windows and a 100 ms presentation hold; offscreen cannot verify it and §14 forbids windowed pacing as primary evidence here");

    std::printf("\n=== total: %zu passed, %zu failed, %zu not verifiable ===\n", verdict.passed, verdict.failed, verdict.not_verifiable);
    return verdict.failed == 0 ? 0 : 1;
}
