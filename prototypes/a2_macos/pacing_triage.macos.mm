// Pacing triage for the recorded Platform pacing finding (§30, AD-18b).
//
// This program reproduces the claim that a display link with a CAMetalLayer
// attached runs at 58 Hz or at exactly 0 Hz "apparently at random", and rules out
// one condition at a time. It deliberately holds no albor code: the finding was
// made by a standalone program, so a reproduction must also be standalone.
//
// Measurement code, not a runtime module (AGENTS.md).

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// CVDisplayLink is deprecated in macOS 15 in favour of the NSView/NSWindow/NSScreen
// display links, but the plan compares it as a distinct mechanism, so its
// deprecation is expected rather than a defect (§28.2).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@class PacingDriver;

namespace
{

    // the display-link mechanisms under comparison, because the mechanism may be the variable (§28.2)
    enum class Mechanism
    {
        CoreAnimation, // CADisplayLink, the one A1 and A2 use today (§10.3)
        CoreVideo, // CVDisplayLink
        Metal // CAMetalDisplayLink, tied to a CAMetalLayer
    };

    const char* mechanism_name(Mechanism mechanism) {
        switch (mechanism) {
        case Mechanism::CoreAnimation:
            return "CADisplayLink";
        case Mechanism::CoreVideo:
            return "CVDisplayLink";
        case Mechanism::Metal:
            return "CAMetalDisplayLink";
        }
        return "unknown";
    }

    // everything one run reports, so a stalled run can be explained rather than only counted (§28.2)
    struct RunReport
    {
        Mechanism mechanism = Mechanism::CoreAnimation;
        double seconds = 0.0;
        std::size_t callbacks = 0;
        double hz = 0.0;
        double interval_p50_ms = 0.0;
        double interval_max_ms = 0.0;
        double gpu_ms_p50 = 0.0;
        std::size_t gpu_samples = 0;
        std::size_t drawables = 0;
        double presented_p50_ms = 0.0;
        std::size_t presented_samples = 0;
        uint64_t occlusion_state = 0;
        bool application_active = false;
        long policy = -1;
        std::string bundle_id = "<none>";
        bool display_asleep = false;
        bool display_active = false;
        uint32_t display_id = 0;
        double refresh_hz = 0.0;
        std::size_t display_count = 0;
        std::size_t heartbeats = 0;
        bool window_visible = false;
        bool application_running = false;
        long window_level = 0;
        std::string device_name = "<none>";
        std::string condition = "as_found";
    };

    // the live state one mechanism drives, kept in file-scope state because the
    // display-link callbacks are C functions and ObjC methods (§28.2)
    struct Triage
    {
        NSWindow* window = nil;
        CAMetalLayer* layer = nil;
        CADisplayLink* ca_link = nil;
        CVDisplayLinkRef cv_link = nullptr;
        CAMetalDisplayLink* metal_link = nil;
        id driver = nil;

        std::chrono::steady_clock::time_point start;
        double start_media = 0.0;
        std::size_t heartbeats = 0;
        void* heartbeat_timer = nullptr;
        std::vector<double> intervals_ms;
        std::vector<double> gpu_ms;
        std::vector<double> presented_ms;
        std::size_t drawables = 0;
        double last_callback_ms = -1.0;
        std::size_t callbacks = 0;

        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        bool floating = false;
    };

    Triage g_triage;

    double now_ms() {
        // report milliseconds on a monotonic clock, so a wall-clock change cannot inflate an interval (§28.2)
        const auto delta = std::chrono::steady_clock::now() - g_triage.start;
        return std::chrono::duration<double, std::milli>(delta).count();
    }

    void record_callback() {
        // count the callback and the interval since the previous one (§28.2)
        const double stamp = now_ms();
        if (g_triage.last_callback_ms >= 0.0) {
            g_triage.intervals_ms.push_back(stamp - g_triage.last_callback_ms);
        }
        g_triage.last_callback_ms = stamp;
        g_triage.callbacks++;
    }

    // the presentation target a mechanism asked for, on the media clock (§28.2)
    void record_presentation_target(double target_timestamp) {
        // a target before the window started is not a sample (§28.4)
        if (target_timestamp <= 0.0) {
            return;
        }

        g_triage.presented_ms.push_back((target_timestamp - g_triage.start_media) * 1000.0);
    }

    // encode and present one frame, so the layer is genuinely attached and consuming a drawable (§28.2)
    void render_one_frame(id<CAMetalDrawable> supplied) {
        // nothing to draw without a device, which the report says rather than hiding (§28.4)
        if (g_triage.device == nil || g_triage.queue == nil) {
            return;
        }

        // the layer must be sized before it can vend a drawable (§9.1)
        g_triage.layer.drawableSize = CGSizeMake(64.0, 64.0);
        id<CAMetalDrawable> drawable = supplied != nil ? supplied : [g_triage.layer nextDrawable];
        if (drawable == nil) {
            return;
        }

        g_triage.drawables++;

        // one clear, which is enough to occupy the GPU for the timestamp comparison (§28.2)
        id<MTLCommandBuffer> command = [g_triage.queue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);

        id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
        [encoder endEncoding];
        [command presentDrawable:drawable];

        // record the GPU cost when the buffer completes; the handler must be added
        // before commit, which Metal asserts (§28.2)
        [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
          const double begin = finished.GPUStartTime;
          const double end = finished.GPUEndTime;

          // a buffer that never reached the GPU has no interval to report (§28.4)
          if (begin > 0.0 && end >= begin) {
              g_triage.gpu_ms.push_back(end - begin);
          }
        }];

        [command commit];
    }

    CVReturn on_core_video_link(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags, CVOptionFlags*, void*) {
        record_callback();
        render_one_frame(nil);
        return kCVReturnSuccess;
    }

    double percentile(std::vector<double> samples, double fraction) {
        // report nothing when a run produced no sample (§28.2)
        if (samples.empty()) {
            return 0.0;
        }

        std::sort(samples.begin(), samples.end());
        const std::size_t index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));
        return samples.at(index);
    }

    double median_of(std::vector<double> samples) {
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

    // the display the window is on, which is the one whose refresh rate matters (§10.3)
    CGDirectDisplayID primary_display() {
        // the main display is what an unattached window lands on (§9.1)
        return CGMainDisplayID();
    }

    void collect_display_facts(RunReport& report) {
        // the refresh rate of the display under test, so a 58 Hz reading can be told from a 60 Hz one (§28.2)
        report.display_id = primary_display();
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(report.display_id);
        if (mode != nullptr) {
            const double refresh = CGDisplayModeGetRefreshRate(mode);
            report.refresh_hz = refresh > 0.0 ? refresh : 0.0;
            CGDisplayModeRelease(mode);
        }

        // asleep means the display is off, which is one of the conditions under test (§28.2)
        report.display_asleep = CGDisplayIsAsleep(report.display_id) != 0;
        report.display_active = CGDisplayIsActive(report.display_id) != 0;

        // how many displays are attached, so an external one is recorded rather than assumed absent (§28.2)
        uint32_t count = 0;
        CGGetActiveDisplayList(0, nullptr, &count);
        report.display_count = count;

        // the Metal device, which names the GPU the frames ran on (§28.2)
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device != nil) {
            report.device_name = std::string([[device name] UTF8String]);
        }
    }

    void collect_process_facts(RunReport& report) {
        // the activation state, which decides whether the app is foreground (§13.2)
        report.application_active = NSApp != nil && [NSApp isActive];

        // the policy, which a non-bundled process cannot hold as Regular (§28.2)
        report.policy = NSApp != nil ? static_cast<long>([NSApp activationPolicy]) : -1;

        // whether the process carries a bundle, which is condition C3 (§28.2)
        NSString* identifier = [[NSBundle mainBundle] bundleIdentifier];
        report.bundle_id = identifier != nil ? std::string([identifier UTF8String]) : std::string("<none>");

        // the window's occlusion state, which separates condition C4 from C5 (§28.2)
        report.occlusion_state = g_triage.window != nil ? static_cast<uint64_t>([g_triage.window occlusionState]) : 0;

        // on-screen visibility, because an occlusion bitmask alone does not say whether
        // the window is actually presented (§28.2)
        report.window_visible = g_triage.window != nil && [g_triage.window isVisible];
        report.application_running = NSApp != nil && [NSApp isRunning];
        report.window_level = g_triage.window != nil ? static_cast<long>([g_triage.window level]) : 0;
    }

    // build the window and layer the finding describes: a real window with a CAMetalLayer attached (§28.2)
    void build_window(bool on_screen, bool frontmost) {
        // an on-screen window is placed on the main display; an off-screen one is far
        // enough away to be occluded without being closed (§28.2)
        NSRect frame = on_screen ? NSMakeRect(200, 200, 320, 240) : NSMakeRect(-20000, -20000, 320, 240);
        g_triage.window = [[NSWindow alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
        g_triage.device = MTLCreateSystemDefaultDevice();
        g_triage.queue = [g_triage.device newCommandQueue];

        g_triage.layer = [CAMetalLayer layer];
        g_triage.layer.device = g_triage.device;
        g_triage.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_triage.layer.contentsScale = 2.0;
        g_triage.layer.drawableSize = CGSizeMake(64.0, 64.0);
        g_triage.window.contentView.wantsLayer = YES;
        g_triage.window.contentView.layer = g_triage.layer;

        // ordering front is what makes the run attended (§28.2)
        if (frontmost) {
            // the floating probe pins the window above everything, so the stacking
            // race with other apps cannot occlude it; exploratory, not a committed
            // condition (§28.4)
            if (g_triage.floating) {
                [g_triage.window setLevel:NSFloatingWindowLevel];
            }
            [g_triage.window orderFrontRegardless];
        }
    }

    void heartbeat(void* timer) {
        // count run-loop deliveries that are not display links, so a dead run loop can
        // be told apart from a link that never fires (§28.4)
        if (timer == g_triage.heartbeat_timer) {
            g_triage.heartbeats++;
        }
    }

    void teardown() {
        // every mechanism must be invalidated or the next run inherits the previous one's callbacks (§28.4)
        if (g_triage.ca_link != nil) {
            [g_triage.ca_link invalidate];
            g_triage.ca_link = nil;
        }

        if (g_triage.cv_link != nullptr) {
            CVDisplayLinkStop(g_triage.cv_link);
            CFRelease(g_triage.cv_link);
            g_triage.cv_link = nullptr;
        }

        if (g_triage.metal_link != nil) {
            [g_triage.metal_link invalidate];
            g_triage.metal_link = nil;
        }

        if (g_triage.heartbeat_timer != nullptr) {
            [(NSTimer*)g_triage.heartbeat_timer invalidate];
            g_triage.heartbeat_timer = nullptr;
        }

        if (g_triage.window != nil) {
            [g_triage.window orderOut:nil];
            g_triage.window = nil;
        }

        g_triage.layer = nil;
        g_triage.queue = nil;
        g_triage.device = nil;
    }

    // run one cell of the matrix: one mechanism, one condition, one repetition (§28.2)
    RunReport run_once(Mechanism mechanism, bool on_screen, bool frontmost, double seconds) {
        // the condition is fixed by the caller; the run itself never changes it mid-window (§28.2)
        // start from a clean slate so no run inherits another's callbacks (§28.4)
        teardown();
        g_triage.intervals_ms.clear();
        g_triage.gpu_ms.clear();
        g_triage.presented_ms.clear();
        g_triage.drawables = 0;
        g_triage.callbacks = 0;
        g_triage.last_callback_ms = -1.0;

        @autoreleasepool {
            build_window(on_screen, frontmost);

            RunReport report;
            report.mechanism = mechanism;
            report.seconds = seconds;

            // attach the requested mechanism to the layer or the run loop (§28.2)
            if (mechanism == Mechanism::CoreAnimation) {
                std::printf("DIAG mainScreen=%p\n", (void*)NSScreen.mainScreen);
                std::fflush(stdout);
                // macOS has no +displayLinkWithTarget:selector:, so the screen is what
                // creates the link; this is the same mechanism A2 uses today (§10.3)
                id screen_link = [NSScreen.mainScreen displayLinkWithTarget:g_triage.driver selector:@selector(on_core_animation_link:)];
                g_triage.ca_link = (CADisplayLink*)screen_link;

                // a screen-created link is not registered until it is added to a run loop,
                // and without this it is never called at all (§10.3)
                [g_triage.ca_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
            }
            else if (mechanism == Mechanism::CoreVideo) {
                CVDisplayLinkCreateWithActiveCGDisplays(&g_triage.cv_link);
                CVDisplayLinkSetOutputCallback(g_triage.cv_link, &on_core_video_link, nullptr);
                CVDisplayLinkStart(g_triage.cv_link);
            }
            else {
                g_triage.metal_link = [[CAMetalDisplayLink alloc] initWithMetalLayer:g_triage.layer];
                [g_triage.metal_link setDelegate:(id<CAMetalDisplayLinkDelegate>)g_triage.driver];
                [g_triage.metal_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
            }

            // record the facts that explain the run, before the wait rather than after (§28.2)
            collect_process_facts(report);
            collect_display_facts(report);

            // a plain timer at 1 Hz proves the run loop delivers, independently of the
            // display link under test (§28.4)
            NSTimer* heartbeat = [NSTimer timerWithTimeInterval:1.0 target:g_triage.driver selector:@selector(on_heartbeat:) userInfo:nil repeats:YES];
            [[NSRunLoop mainRunLoop] addTimer:heartbeat forMode:NSRunLoopCommonModes];
            g_triage.heartbeat_timer = (__bridge void*)heartbeat;

            g_triage.start = std::chrono::steady_clock::now();
            g_triage.start_media = CACurrentMediaTime();

            // let the run loop spin for the measurement window, which is what a display link needs (§28.2)
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];

            // fill in the measured fields after the window (§28.2)
            report.callbacks = g_triage.callbacks;
            report.hz = seconds > 0.0 ? static_cast<double>(g_triage.callbacks) / seconds : 0.0;
            report.interval_p50_ms = percentile(g_triage.intervals_ms, 0.50);
            report.interval_max_ms = g_triage.intervals_ms.empty() ? 0.0 : *std::max_element(g_triage.intervals_ms.begin(), g_triage.intervals_ms.end());
            report.gpu_ms_p50 = percentile(g_triage.gpu_ms, 0.50);
            report.gpu_samples = g_triage.gpu_ms.size();
            report.presented_p50_ms = percentile(g_triage.presented_ms, 0.50);
            report.presented_samples = g_triage.presented_ms.size();
            report.drawables = g_triage.drawables;
            report.heartbeats = g_triage.heartbeats;

            // the facts may have changed during the window, so they are read again (§28.2)
            collect_process_facts(report);
            collect_display_facts(report);

            teardown();
            return report;
        }
    }

    void print_report(const RunReport& report) {
        // one line per run, so a table can be read without re-running anything (§28.2)
        std::printf("mech=%-18s cond=%-14s hz=%6.1f cb=%5zu ivl_p50=%6.2f ivl_max=%8.2f gpu_p50=%6.3f/%zu presented_p50=%9.3f/%zu drawables=%4zu occl=%llu active=%d policy=%ld bundle=%-18s asleep=%d disp_active=%d displays=%zu refresh=%.1f hb=%zu visible=%d running=%d level=%ld gpu=%s\n", mechanism_name(report.mechanism), report.condition.c_str(), report.hz, report.callbacks, report.interval_p50_ms, report.interval_max_ms, report.gpu_ms_p50, report.gpu_samples, report.presented_p50_ms, report.presented_samples, report.drawables, static_cast<unsigned long long>(report.occlusion_state), report.application_active ? 1 : 0, report.policy, report.bundle_id.c_str(), report.display_asleep ? 1 : 0, report.display_active ? 1 : 0, report.display_count, report.refresh_hz, report.heartbeats, report.window_visible ? 1 : 0, report.application_running ? 1 : 0, report.window_level, report.device_name.c_str());
        std::fflush(stdout);
    }

    // the machine-readable line, so a script can gate on a threshold without parsing prose (§28.4)
    void print_verdict_line(const RunReport& report) {
        std::printf("T3.2 line mechanism=%s condition=%s callbacks=%zu hz=%.1f\n", mechanism_name(report.mechanism), report.condition.c_str(), report.callbacks, report.hz);
        std::fflush(stdout);
    }

    // a launch condition: where the window goes and whether the app is activated (§28.2)
    struct Condition
    {
        const char* name;
        bool on_screen;
        bool frontmost;
        bool activate;
    };

    // map the command line's mechanism name onto the mechanism it selects (§28.2)
    bool mechanism_from_name(const std::string& name, Mechanism& mechanism) {
        if (name == "cadisplaylink") {
            mechanism = Mechanism::CoreAnimation;
            return true;
        }

        if (name == "cvdisplaylink") {
            mechanism = Mechanism::CoreVideo;
            return true;
        }

        if (name == "metaldisplaylink") {
            mechanism = Mechanism::Metal;
            return true;
        }

        return false;
    }

    // map the command line's condition name onto the launch it selects (§28.2)
    bool condition_from_name(const std::string& name, bool& on_screen, bool& frontmost, bool& activate) {
        if (name == "as_found") {
            on_screen = true;
            frontmost = false;
            activate = false;
            return true;
        }

        if (name == "attended") {
            on_screen = true;
            frontmost = true;
            activate = true;
            return true;
        }

        if (name == "unattended") {
            // the window is off screen but still ordered front, so it is a real
            // surface; the app is never activated (§28.2)
            on_screen = false;
            frontmost = true;
            activate = false;
            return true;
        }

        return false;
    }

    // one process, one condition, all three mechanisms: app activation state is
    // sticky, so it cannot be toggled per repetition inside one process; the
    // driver launches one process per condition instead (§28.2)
    int run_mechanisms(bool on_screen, bool frontmost, bool activate, const std::string& condition_text, int runs, double seconds) {
        // the plan names the conditions C0, C4 and C5; the launch conditions are
        // as_found, attended and unattended (§28.2)
        const std::string condition = condition_text == "as_found" ? "C0_as_found" : condition_text == "attended" ? "C4_attended" : "C5_unattended";
        const Mechanism mechanisms[] = {Mechanism::CoreAnimation, Mechanism::CoreVideo, Mechanism::Metal};

        if (activate) {
            [NSApp activateIgnoringOtherApps:YES];
        }
        else if (condition == "unattended") {
            [NSApp hide:nil];
        }

        for (const Mechanism mechanism : mechanisms) {
            std::vector<double> hz_samples;
            std::vector<double> interval_samples;

            for (int repetition = 0; repetition < runs; repetition++) {
                RunReport report = run_once(mechanism, on_screen, frontmost, seconds);
                report.condition = condition;
                print_report(report);
                print_verdict_line(report);
                hz_samples.push_back(report.hz);
                interval_samples.push_back(report.interval_p50_ms);
            }

            std::printf("CELL mechanism=%s condition=%s runs=%d hz_median=%.1f hz_min=%.1f hz_max=%.1f ivl_p50_median=%.2f\n", mechanism_name(mechanism), condition.c_str(), runs, median_of(hz_samples), *std::min_element(hz_samples.begin(), hz_samples.end()), *std::max_element(hz_samples.begin(), hz_samples.end()), median_of(interval_samples));
            std::fflush(stdout);
        }

        teardown();
        return 0;
    }

} // namespace

#pragma clang diagnostic pop

@interface PacingDriver : NSObject <CAMetalDisplayLinkDelegate>
- (void)on_core_animation_link:(CADisplayLink*)link;
- (void)on_heartbeat:(NSTimer*)timer;
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update;
@end

@implementation PacingDriver

- (void)on_core_animation_link:(CADisplayLink*)link {
    record_callback();
    record_presentation_target(link.targetTimestamp);
    render_one_frame(nil);
}

- (void)on_heartbeat:(NSTimer*)timer {
    heartbeat((__bridge void*)timer);
}

- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
    record_callback();
    record_presentation_target(update.targetPresentationTimestamp);
    render_one_frame(update.drawable);
}

@end

int main(int argc, char** argv) {
    // the windowed behaviour needs a real application, not a command-line process (§28.2)
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];

        // condition C3 asks for an activated regular app; the policy is set either way and reported (§28.2)
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        bool finish_launching = true;
        for (int index = 1; index < argc; index++) {
            if (std::strcmp(argv[index], "--no-finish-launching") == 0) {
                // exploratory probe: reproduces the bring-up state that produced 0 Hz
                finish_launching = false;
            }
            else if (std::strcmp(argv[index], "--floating") == 0) {
                // exploratory probe: pins the window above every other app
                g_triage.floating = true;
            }
        }

        // recorded per run, so a probe figure can never be mistaken for a matrix
        // figure (§28.4)
        std::printf("finishLaunching=%s\n", finish_launching ? "yes" : "no");
        if (finish_launching) {
            [application finishLaunching];
        }

        int runs = 5;
        double seconds = 5.0;
        bool matrix = false;
        std::string mechanism_text;
        std::string condition_text = "as_found";

        for (int index = 1; index < argc; index++) {
            if (std::strcmp(argv[index], "--matrix") == 0) {
                matrix = true;
            }
            else if (std::strcmp(argv[index], "--runs") == 0 && index + 1 < argc) {
                runs = std::atoi(argv[++index]);
            }
            else if (std::strcmp(argv[index], "--seconds") == 0 && index + 1 < argc) {
                seconds = std::atof(argv[++index]);
            }
            else if (std::strcmp(argv[index], "--mechanism") == 0 && index + 1 < argc) {
                mechanism_text = argv[++index];
            }
            else if (std::strcmp(argv[index], "--condition") == 0 && index + 1 < argc) {
                condition_text = argv[++index];
            }
        }

        // the exploratory probe is recorded per run, so a figure from it can never
        // be mistaken for one from the committed matrix (§28.4)
        std::printf("finishLaunching=%s\n", finish_launching ? "yes" : "no");
        if (finish_launching) {
            [application finishLaunching];
        }

        NSString* identifier = [[NSBundle mainBundle] bundleIdentifier];
        RunReport environment;
        collect_display_facts(environment);

        std::printf("Prototype A2 macOS pacing triage (AD-18b, §30)\n");
        std::printf("macos_version=%s runs=%d seconds=%.1f\n", [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String], runs, seconds);
        const char* bundle_text = identifier != nil ? [identifier UTF8String] : "<none>";
        std::printf("bundle=%s policy=%ld\n", bundle_text, (long)[application activationPolicy]);
        std::printf("display_id=%u refresh=%.1f asleep=%d active=%d count=%zu gpu=%s\n", environment.display_id, environment.refresh_hz, environment.display_asleep ? 1 : 0, environment.display_active ? 1 : 0, environment.display_count, environment.device_name.c_str());
        std::printf("conditions needing a human (C2 unlocked session, C7 GPU pinning, C6 external display) are not automated here\n\n");

        // a driver object owns the display-link callbacks, which must be ObjC methods (§28.2)
        PacingDriver* driver = [[PacingDriver alloc] init];
        g_triage.driver = driver;

        bool on_screen = true;
        bool frontmost = false;
        bool activate = false;
        if (!condition_from_name(condition_text, on_screen, frontmost, activate)) {
            std::printf("unknown condition: %s (expected as_found, attended or unattended)\n", condition_text.c_str());
            return 2;
        }

        if (matrix) {
            // one condition per process; all three mechanisms inside it (§28.2)
            return run_mechanisms(on_screen, frontmost, activate, condition_text, runs, seconds);
        }

        // a single run, so one cell can be inspected in isolation (§28.2)
        Mechanism mechanism = Mechanism::CoreAnimation;
        if (!mechanism_text.empty() && !mechanism_from_name(mechanism_text, mechanism)) {
            std::printf("unknown mechanism: %s (expected cadisplaylink, cvdisplaylink or metaldisplaylink)\n", mechanism_text.c_str());
            return 2;
        }

        // the launch condition is applied exactly as named: attended activates,
        // unattended hides, as-found does neither (§28.2)
        if (activate) {
            [application activateIgnoringOtherApps:YES];
        }
        else if (condition_text == "unattended") {
            [application hide:nil];
        }

        RunReport report = run_once(mechanism, on_screen, frontmost, seconds);
        report.condition = condition_text;
        print_report(report);
        print_verdict_line(report);
        return 0;
    }
}
