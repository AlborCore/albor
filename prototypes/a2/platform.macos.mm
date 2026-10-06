#include "prototypes/a2/platform.h"

#import "prototypes/a2/native_surface.macos.h"

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <chrono>
#include <mutex>

// CVDisplayLink is deprecated in macOS 15 in favour of the NSView/NSWindow display links, but Prototype A2's
// recorded finding disqualified those (whole-process stalls, prototypes/a2_macos/PACING.md), so the deprecation
// is expected rather than a defect (plan.md revision 2)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace albor::a2
{

    namespace
    {

        // the clock the prototype measures with, monotonic like the framework timer (§7.1)
        using Clock = std::chrono::steady_clock;

        // convert a duration into milliseconds
        double to_ms(Clock::duration elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); }

    } // namespace

    // the opaque platform state, so that no appkit type reaches the interface (§3.5, AD-05)
    struct PlatformWindowState
    {
        // a self reference, because a queued frame delivery must keep this state alive after the window is gone
        std::shared_ptr<PlatformWindowState> self;

        // the frame record waiting for the run loop to deliver it, because a callback entered from a dispatch
        // block cannot start a modal session (§7.4, revision 2)
        FrameRecord pending_record;
        bool has_pending_record = false;

        NSWindow* window = nil;
        CAMetalLayer* layer = nil;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLTexture> depth_stencil = nil;
        CVDisplayLinkRef cv_link = nullptr;

        // the frame callback the benchmark installed
        std::function<void(const FrameRecord&)> on_frame;

        // the window facts the pacing thread reads, mirrored because an appkit property may not be read from
        // core video's own thread; the main queue refreshes them on every resize and backing change (§9.1)
        CGSize mirrored_bounds = CGSizeZero;
        double last_scale = 1.0;
        double framed_scale = 0.0;

        // protects every counter the display link and the main thread share
        std::mutex mutex;

        // counters that describe what the platform layer observed
        std::size_t frames_presented = 0;
        std::size_t frames_presented_while_resizing = 0;
        std::size_t resize_events = 0;
        std::size_t scale_changes = 0;
        std::size_t posted_tasks_total = 0;
        std::size_t posted_tasks_during_nested_loop = 0;
        std::size_t close_after_frames = 0;
        std::size_t nested_loops_running = 0;
        std::size_t resizing = 0;

        // whether stage 2 presents from the ui thread, which is what prototype A2 measured
        std::size_t present_on_ui_thread = 1;

        // the native surface handed to a render thread (§10.2)
        NativeSurface surface;

        // the timestamp of the previous presented frame, for the interval metric
        Clock::time_point last_frame{};
        bool has_last_frame = false;

        // the timestamp of the resize request, for the input to present latency
        Clock::time_point resize_request{};
        bool has_resize_request = false;

        // where the window sits on the screen, so a live resize keeps its corner (§7.4)
        double origin_x = 120.0;
        double origin_y = 120.0;

        // the application object that owns the run loop
        NSApplication* application = nil;

        // stop the loop once the window closes
        std::atomic<bool> running{false};
    };

} // namespace albor::a2

namespace albor::a2
{

    // present one frame through the metal layer and report what the platform observed
    //
    // this is a free function over the window state rather than an objc method because the pacing source is now
    // core video's callback, which is a c function running on core video's own thread (plan.md revision)
    void present_one_frame(PlatformWindowState& state) {
        // read the mirrored window facts, because an appkit property may not be read from core video's thread
        // (plan.md revision); the main queue refreshes them on every resize and backing change (§9.1)
        CGSize bounds = CGSizeZero;
        double scale = 1.0;

        {
            const std::lock_guard<std::mutex> lock(state.mutex);
            bounds = state.mirrored_bounds;
            scale = state.last_scale;

            // observe a backing scale change before drawing so the frame carries the new scale
            if (scale != state.framed_scale) {
                state.framed_scale = scale;
                state.scale_changes++;
            }
        }

        // keep the layer in step with the window size and scale, which is the resize contract (§7.4)
        state.layer.frame = CGRectMake(0.0, 0.0, bounds.width, bounds.height);
        state.layer.contentsScale = scale;
        state.layer.drawableSize = CGSizeMake(bounds.width * scale, bounds.height * scale);

        // acquire the drawable the platform hands us for this frame
        // acquire the drawable only when this thread presents, because acquiring one blocks (§10.2)
        const albor::a2::Clock::time_point acquire_start = albor::a2::Clock::now();
        id<CAMetalDrawable> drawable = state.present_on_ui_thread != 0 ? [state.layer nextDrawable] : nil;
        const double acquire_ms = albor::a2::to_ms(albor::a2::Clock::now() - acquire_start);

        albor::a2::FrameRecord record;
        double encode_submit_ms = 0.0;

        // build the record even without a drawable, so a dropped frame is still observable
        const albor::a2::Clock::time_point now = albor::a2::Clock::now();

        if (drawable != nil && state.queue != nil && state.present_on_ui_thread != 0) {
            // describe the single attachment this prototype clears
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            MTLRenderPassColorAttachmentDescriptor* color = pass.colorAttachments[0];
            color.texture = drawable.texture;
            color.loadAction = MTLLoadActionClear;
            color.storeAction = MTLStoreActionStore;
            color.clearColor = MTLClearColorMake(0.05, 0.06, 0.08, 1.0);

            // encode and present one clear, which is enough to exercise presentation
            const albor::a2::Clock::time_point encode_start = albor::a2::Clock::now();
            id<MTLCommandBuffer> commands = [state.queue commandBuffer];
            id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
            encoder.label = @"albor-a2-clear";
            [encoder endEncoding];
            [commands presentDrawable:drawable];
            [commands commit];
            encode_submit_ms = albor::a2::to_ms(albor::a2::Clock::now() - encode_start);
        }

        {
            // fill the frame record with what the platform measured
            const std::lock_guard<std::mutex> lock(state.mutex);
            record.index = state.frames_presented;
            record.interval_ms = state.has_last_frame ? albor::a2::to_ms(now - state.last_frame) : 0.0;
            record.input_to_present_ms = state.has_resize_request ? albor::a2::to_ms(now - state.resize_request) : 0.0;
            state.last_frame = now;
            state.has_last_frame = true;

            // clear the pending resize request once it has been answered
            state.has_resize_request = false;

            record.logical_width = static_cast<std::size_t>(bounds.width);
            record.logical_height = static_cast<std::size_t>(bounds.height);
            record.pixel_width = static_cast<std::size_t>(bounds.width * scale);
            record.pixel_height = static_cast<std::size_t>(bounds.height * scale);
            record.scale_factor = scale;
            record.resizing = state.resizing != 0;
            record.acquire_ms = acquire_ms;
            record.encode_submit_ms = encode_submit_ms;

            // count the presented frame and whether it answered a resize request (§7.4)
            state.frames_presented++;
            state.frames_presented_while_resizing += state.resizing != 0 ? 1 : 0;

            // clear the resize mark now that the request has been answered
            state.resizing = 0;

            // count the display link as one framework event source for the nested loop measurement
            state.posted_tasks_total++;
            state.posted_tasks_during_nested_loop += state.nested_loops_running != 0 ? 1 : 0;

            // stop the loop once the requested number of frames has been presented
            if (state.close_after_frames != 0 && state.frames_presented >= state.close_after_frames) {
                state.running = false;
            }
        }

        // hand the frame to the benchmark on the main queue, because the callback drives appkit, which is
        // main-thread only, and core video's thread is not it (the crash report recorded in revision 2's results)
        //
        // the pacing source is what moved to core video; the delivery stays on the ui thread, so the off-ui-thread
        // presentation question stays a separate package (plan.md revision 2)
        // a record is only worth handing over when the window still has a callback, so a detached window's pacing
        // thread stops queueing (§7.4)
        {
            const std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.on_frame) {
                return;
            }

            state.pending_record = record;
            state.has_pending_record = true;
        }
    }

    // deliver one frame record on the ui thread, called by the run loop rather than from the pacing callback
    //
    // this is the same place revision 1 invoked the callback from - inside the run loop's own iteration - so the
    // benchmark's workload, including the nested modal session, runs in the same context as before (§7.4)
    void deliver_pending_frame(PlatformWindowState& state) {
        std::function<void(const FrameRecord&)> callback;
        FrameRecord record;

        {
            const std::lock_guard<std::mutex> lock(state.mutex);

            // nothing to deliver when no record is waiting, which is the common case between frames
            if (!state.has_pending_record) {
                return;
            }

            callback = state.on_frame;
            record = state.pending_record;
            state.has_pending_record = false;
        }

        if (callback) {
            callback(record);
        }
    }

    // the core video callback presents one frame on core video's own thread, which is the mechanism the pacing
    // triage measured as uniformly reliable (prototypes/a2_macos/PACING.md)
    CVReturn on_cv_display_link(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags, CVOptionFlags*, void* user) {
        present_one_frame(*static_cast<PlatformWindowState*>(user));
        return kCVReturnSuccess;
    }

} // namespace albor::a2

namespace albor::a2
{

    PlatformWindow::PlatformWindow() :
        m_state(std::make_shared<PlatformWindowState>()) {
        // let a queued frame delivery keep the state alive (§7.4)
        m_state->self = m_state;
    }

    PlatformWindow::~PlatformWindow() {
        // stop and release the pacing source before the window goes away (§10.3)
        if (m_state->cv_link != nullptr) {
            CVDisplayLinkStop(m_state->cv_link);
            CFRelease(m_state->cv_link);
            m_state->cv_link = nullptr;
        }

        // the state outlives this object when a frame delivery is still queued, and the shared owner decides
        // when it goes (§7.4)
        m_state->self.reset();
        m_state.reset();
    }

    bool PlatformWindow::create(const std::string& title, std::size_t logical_width, std::size_t logical_height) {
        // reach the shared application and activate it, because the display link only ticks for an active application
        m_state->application = [NSApplication sharedApplication];
        [m_state->application setActivationPolicy:NSApplicationActivationPolicyRegular];

        // launch the application only for the first window, because finishLaunching may only be called once
        static std::atomic<std::size_t> window_ordinal{0};
        const std::size_t ordinal = window_ordinal.fetch_add(1);
        if (ordinal == 0) {
            [m_state->application finishLaunching];
        }
        [m_state->application activateIgnoringOtherApps:YES];

        // cascade the window origins so a second window does not hide behind the first one
        m_state->origin_x = 120.0 + 80.0 * static_cast<double>(ordinal);
        m_state->origin_y = 120.0 + 60.0 * static_cast<double>(ordinal);

        // create the resizable window, which is what makes a live resize loop possible (§7.4)
        const NSRect frame = NSMakeRect(m_state->origin_x, m_state->origin_y, static_cast<double>(logical_width), static_cast<double>(logical_height));
        m_state->window = [[NSWindow alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        if (m_state->window == nil) {
            return false;
        }

        m_state->window.title = [NSString stringWithUTF8String:title.c_str()];

        // back the content view with a metal layer, which is the macOS presentation path of AD-16
        m_state->window.contentView.wantsLayer = YES;
        m_state->layer = [CAMetalLayer layer];
        m_state->window.contentView.layer = m_state->layer;

        // resolve the metal device and its queue
        m_state->device = MTLCreateSystemDefaultDevice();
        if (m_state->device == nil) {
            return false;
        }

        m_state->layer.device = m_state->device;
        m_state->queue = [m_state->device newCommandQueue];

        // start from the backing scale and the bounds the window reports, mirrored because the pacing
        // thread may not read appkit properties (§9.1)
        m_state->last_scale = m_state->window.backingScaleFactor;
        m_state->mirrored_bounds = m_state->window.contentView ? m_state->window.contentView.bounds.size : CGSizeZero;

        // publish the native surface for a render thread (§10.2)
        m_state->surface.layer = m_state->layer;
        m_state->surface.device = m_state->device;
        m_state->surface.queue = m_state->queue;

        // install the hooks that count resize and scale changes (§7.4, §23.1)
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center addObserverForName:NSWindowDidResizeNotification
                            object:m_state->window
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification*) {
                          const std::lock_guard<std::mutex> lock(m_state->mutex);
                          m_state->resize_events++;

                          // mirror the new bounds, because the pacing thread may not read appkit (§7.4)
                          m_state->mirrored_bounds = m_state->window.contentView ? m_state->window.contentView.bounds.size : CGSizeZero;
                        }];
        [center addObserverForName:NSWindowDidChangeBackingPropertiesNotification
                            object:m_state->window
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification*) {
                          const std::lock_guard<std::mutex> lock(m_state->mutex);
                          m_state->last_scale = m_state->window.backingScaleFactor;
                          m_state->mirrored_bounds = m_state->window.contentView ? m_state->window.contentView.bounds.size : CGSizeZero;
                        }];

        // create the core video pacing source, which the triage measured as the only uniformly reliable
        // mechanism on this machine (§10.3, prototypes/a2_macos/PACING.md)
        if (CVDisplayLinkCreateWithActiveCGDisplays(&m_state->cv_link) != kCVReturnSuccess) {
            return false;
        }

        // hand the callback the window state it presents through, which core video keeps as an opaque pointer
        CVDisplayLinkSetOutputCallback(m_state->cv_link, &albor::a2::on_cv_display_link, m_state.get());

        // a pacing source is required to answer the pacing question of this slice
        return m_state->cv_link != nullptr;
    }

    void PlatformWindow::show() {
        // put the window on screen
        [m_state->window makeKeyAndOrderFront:nil];

        // start core video, which delivers the callback on its own thread and therefore keeps ticking inside a
        // nested loop without any run loop registration (§7.4)
        CVDisplayLinkStart(m_state->cv_link);

        m_state->running = true;
    }

    void PlatformWindow::run(const std::function<void(const FrameRecord&)>& on_frame) {
        // remember the callback the display link hands its frames to
        m_state->on_frame = on_frame;

        // run the application loop until the window closes
        while (m_state->running) {
            @autoreleasepool {
                // deliver the record core video produced, from inside the run loop's own iteration, so the
                // workload may start a modal session (§7.4)
                deliver_pending_frame(*m_state);

                // give the run loop a short deadline in the future so it actually services its sources,
                // which is what lets the display link callback fire (§10.3)
                // poll with a sub-millisecond deadline: the pacing thread now hands records over rather than
                // waking the loop itself, so the poll interval is part of the frame's latency budget (§10.3)
                NSEvent* event = [m_state->application nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0002] inMode:NSDefaultRunLoopMode dequeue:YES];

                // forward the event to the application when one arrived
                if (event != nil) {
                    [m_state->application sendEvent:event];
                }
            }
        }

        // detach the frame source before the caller destroys what the callback captured: a display link that
        // stayed attached would fire a lambda whose references no longer exist. show() attaches it again, so
        // the same window can drive a later workload.
        detach_frame_source();
    }

    void PlatformWindow::set_frame_callback(const std::function<void(const FrameRecord&)>& on_frame) {
        // remember the callback without entering a loop, so a second window can be driven by the first one
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->on_frame = on_frame;
    }

    void PlatformWindow::detach_frame_source() {
        // stop core video first, which blocks until an in-flight callback has returned, so no further frame can
        // be delivered at all (§7.4)
        if (m_state->cv_link != nullptr) {
            CVDisplayLinkStop(m_state->cv_link);
        }

        // then drop the callback, so a frame delivered a moment ago cannot reach captures that are going away
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->on_frame = nullptr;
        m_state->running = false;
    }

    void PlatformWindow::request_resize(std::size_t logical_width, std::size_t logical_height) {
        {
            // mark the window as resizing until a frame answers the request, which is the live resize attribution of §7.4
            const std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->resizing = 1;
            m_state->resize_request = Clock::now();
            m_state->has_resize_request = true;
        }

        // ask appkit to resize, which is the live resize entry point
        [m_state->window setFrame:NSMakeRect(m_state->origin_x, m_state->origin_y, static_cast<double>(logical_width), static_cast<double>(logical_height)) display:YES];
    }

    void PlatformWindow::run_nested_loop_for_ms(std::size_t duration_ms) {
        // mark that a nested native loop is running, so the frame source can be attributed (§7.4)
        {
            const std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->nested_loops_running++;
        }

        // create the window that owns the modal session
        NSWindow* panel = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 200, 120) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        panel.title = @"nested modal session";

        // end the modal session after the requested time, on the main queue rather than in a run loop timer,
        // because a default mode timer is not serviced while a modal session owns the loop (§7.4)
        //
        // the block carries the deadline as a captured value rather than reading a shared flag, because with
        // core video driving the pacing the main queue carries a continuous frame backlog and a flag poll would
        // have to win that race; a dispatched block runs in order, once the backlog in front of it has drained
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(NSEC_PER_MSEC * duration_ms)), dispatch_get_main_queue(), ^{
          // leave the nested loop with a normal response code
          [NSApp stopModalWithCode:NSModalResponseOK];
        });

        // enter the nested native loop, which is the re-entrancy case of §7.4
        [NSApp runModalForWindow:panel];

        // clear the nested mark once the session ends
        {
            const std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->nested_loops_running--;
        }
    }

    void PlatformWindow::close_after_frames(std::size_t frames) {
        // remember how many frames may be presented before the window closes
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->close_after_frames = frames;
    }

    void PlatformWindow::set_present_on_ui_thread(bool present) {
        // remember whether stage 2 presents from the ui thread
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->present_on_ui_thread = present ? 1 : 0;
    }

    NativeSurface* PlatformWindow::native_surface() const { return &m_state->surface; }

    Metrics PlatformWindow::metrics() const {
        // read the authoritative metrics from the window (§9.1)
        const NSRect bounds = m_state->window.contentView.bounds;
        const double scale = m_state->window.backingScaleFactor;

        Metrics metrics;
        metrics.logical_width = static_cast<std::size_t>(bounds.size.width);
        metrics.logical_height = static_cast<std::size_t>(bounds.size.height);
        metrics.pixel_width = static_cast<std::size_t>(bounds.size.width * scale);
        metrics.pixel_height = static_cast<std::size_t>(bounds.size.height * scale);
        metrics.scale_factor = scale;
        metrics.refresh_hz = static_cast<double>(NSScreen.mainScreen.maximumFramesPerSecond);
        return metrics;
    }

    std::size_t PlatformWindow::frames_presented() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_presented;
    }

    std::size_t PlatformWindow::resize_events() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->resize_events;
    }

    std::size_t PlatformWindow::scale_changes() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->scale_changes;
    }

    std::size_t PlatformWindow::frames_presented_while_resizing() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->frames_presented_while_resizing;
    }

    std::size_t PlatformWindow::posted_tasks_during_nested_loop() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->posted_tasks_during_nested_loop;
    }

    std::size_t PlatformWindow::posted_tasks_total() const {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        return m_state->posted_tasks_total;
    }

} // namespace albor::a2

#pragma clang diagnostic pop
