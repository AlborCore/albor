#include "prototypes/a2/platform.h"

#import "prototypes/a2/native_surface_mac.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <chrono>
#include <mutex>

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
        NSWindow* window = nil;
        CAMetalLayer* layer = nil;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLTexture> depth_stencil = nil;
        id display_link = nil;
        NSObject* link_target = nil;

        // the frame callback the benchmark installed
        std::function<void(const FrameRecord&)> on_frame;

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

        // the last observed backing scale, to detect a scale change (§9.1)
        double last_scale = 1.0;

        // the application object that owns the run loop
        NSApplication* application = nil;

        // stop the loop once the window closes
        std::atomic<bool> running{false};
    };

} // namespace albor::a2

// the object the display link calls back into, because a display link targets a selector
@interface AlborFrameSource : NSObject
@property(nonatomic, assign) albor::a2::PlatformWindowState* state;
- (void)on_display_link:(id)link;
@end

@implementation AlborFrameSource

// present one frame through the metal layer and report what the platform observed
- (void)on_display_link:(id)link {
    static_cast<void>(link);

    // resolve the window state the frame source belongs to
    albor::a2::PlatformWindowState& state = *self.state;

    // read the bounds the window reports, which are authoritative for the metrics (§9.1)
    const NSRect bounds = state.window.contentView ? state.window.contentView.bounds : NSMakeRect(0, 0, 0, 0);
    const double scale = state.window.backingScaleFactor;

    {
        // observe a backing scale change before drawing so the frame carries the new scale
        const std::lock_guard<std::mutex> lock(state.mutex);
        if (scale != state.last_scale) {
            state.last_scale = scale;
            state.scale_changes++;
        }
    }

    // keep the layer in step with the window size and scale, which is the resize contract (§7.4)
    state.layer.frame = bounds;
    state.layer.contentsScale = scale;
    state.layer.drawableSize = CGSizeMake(bounds.size.width * scale, bounds.size.height * scale);

    // acquire the drawable the platform hands us for this frame
    id<CAMetalDrawable> drawable = [state.layer nextDrawable];
    albor::a2::FrameRecord record;

    // build the record even without a drawable, so a dropped frame is still observable
    const albor::a2::Clock::time_point now = albor::a2::Clock::now();

    if (drawable != nil && state.queue != nil) {
        // describe the single attachment this prototype clears
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        MTLRenderPassColorAttachmentDescriptor* color = pass.colorAttachments[0];
        color.texture = drawable.texture;
        color.loadAction = MTLLoadActionClear;
        color.storeAction = MTLStoreActionStore;
        color.clearColor = MTLClearColorMake(0.05, 0.06, 0.08, 1.0);

        // encode and present one clear, which is enough to exercise presentation
        id<MTLCommandBuffer> commands = [state.queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        encoder.label = @"albor-a2-clear";
        [encoder endEncoding];
        [commands presentDrawable:drawable];
        [commands commit];
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

        record.logical_width = static_cast<std::size_t>(bounds.size.width);
        record.logical_height = static_cast<std::size_t>(bounds.size.height);
        record.pixel_width = static_cast<std::size_t>(bounds.size.width * scale);
        record.pixel_height = static_cast<std::size_t>(bounds.size.height * scale);
        record.scale_factor = scale;
        record.resizing = state.resizing != 0;

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

    // hand the frame to the benchmark outside the lock
    if (state.on_frame) {
        state.on_frame(record);
    }
}

@end

namespace albor::a2
{

    PlatformWindow::PlatformWindow() :
        m_state(new PlatformWindowState()) {}

    PlatformWindow::~PlatformWindow() {
        // stop the pacing before the window goes away
        if (m_state->display_link != nil) {
            [(CADisplayLink*)m_state->display_link invalidate];
        }

        // release the state object
        delete m_state;
        m_state = nullptr;
    }

    bool PlatformWindow::create(const std::string& title, std::size_t logical_width, std::size_t logical_height) {
        // reach the shared application and activate it, because the display link only ticks for an active application
        m_state->application = [NSApplication sharedApplication];
        [m_state->application setActivationPolicy:NSApplicationActivationPolicyRegular];
        [m_state->application finishLaunching];
        [m_state->application activateIgnoringOtherApps:YES];

        // create the resizable window, which is what makes a live resize loop possible (§7.4)
        const NSRect frame = NSMakeRect(120, 120, static_cast<double>(logical_width), static_cast<double>(logical_height));
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

        // start from the backing scale the window reports (§9.1)
        m_state->last_scale = m_state->window.backingScaleFactor;

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
                        }];
        [center addObserverForName:NSWindowDidChangeBackingPropertiesNotification
                            object:m_state->window
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification*) {
                          const std::lock_guard<std::mutex> lock(m_state->mutex);
                          m_state->scale_changes++;
                        }];

        // create the frame source that the display link calls back into
        AlborFrameSource* source = [[AlborFrameSource alloc] init];
        source.state = m_state;
        m_state->link_target = source;

        // create the display link that paces the frames (§10.3)
        m_state->display_link = [NSScreen.mainScreen displayLinkWithTarget:source selector:@selector(on_display_link:)];

        // a display link is required to answer the pacing question of this slice
        return m_state->display_link != nil;
    }

    void PlatformWindow::show() {
        // put the window on screen
        [m_state->window makeKeyAndOrderFront:nil];
        m_state->running = true;
    }

    void PlatformWindow::run(const std::function<void(const FrameRecord&)>& on_frame) {
        // remember the callback the display link hands its frames to
        m_state->on_frame = on_frame;

        // add the frame source to the common run loop modes, which is what keeps it alive inside a nested loop (§7.4)
        CADisplayLink* link = (CADisplayLink*)m_state->display_link;
        [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];

        // run the application loop until the window closes
        while (m_state->running) {
            @autoreleasepool {
                // give the run loop a short deadline in the future so it actually services its sources,
                // which is what lets the display link callback fire (§10.3)
                NSEvent* event = [m_state->application nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.002] inMode:NSDefaultRunLoopMode dequeue:YES];

                // forward the event to the application when one arrived
                if (event != nil) {
                    [m_state->application sendEvent:event];
                }
            }
        }
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
        [m_state->window setFrame:NSMakeRect(120, 120, static_cast<double>(logical_width), static_cast<double>(logical_height)) display:YES];
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
