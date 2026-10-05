#include "platform/macos/event_loop.h"

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>

#include "platform/macos/platform_window.h"
#include "window/platform_window_factory.h"

// the display link is deprecated in macOS 15 in favour of the appkit display links, but prototype A2 recorded whole
// process stalls for those, so the deprecation is expected rather than a defect (prototypes/a2_macos/PACING.md)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace albor::macos
{
    // the opaque appkit and core video state, so that no platform type reaches this header (§3.5, AD-05)
    struct MacosEventLoop::State
    {
        // the shared appkit application the loop drives (§4.1)
        NSApplication* application = nil;

        // the core video display link that paces frames, which runs on its own thread (§4.3, AD-16)
        CVDisplayLinkRef display_link = nullptr;

        // the refresh rate the display reports, which is the pacing target (§4.3)
        double refresh_hz = 60.0;
    };

    namespace
    {
        CVReturn on_display_link(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags, CVOptionFlags* flags, void* context) {
            // the out parameter is nulled, which core video reads as the signal that the frame was handled (§4.3)
            if (flags != nullptr)
                *flags = 0;
            // the context is the loop that installed the display link (§4.3)
            MacosEventLoop* loop = static_cast<MacosEventLoop*>(context);
            // the loop may already be gone, because a teardown does not wait for core video (§4.2)
            if (loop == nullptr)
                return kCVReturnSuccess;
            // the frame is handed to the main queue, because the pump draws and may start a nested loop (§4.3, AD-21)
            dispatch_async(dispatch_get_main_queue(), ^{
              // the loop owns the delivery, so a torn down loop drops the frame rather than pumping (§4.2)
              loop->deliver_on_ui_thread();
            });
            return kCVReturnSuccess;
        }
    } // namespace

    MacosEventLoop::MacosEventLoop() :
        m_state(std::make_unique<State>()) {
        // the shared application is what the windows attach to, so the loop owns its activation (§4.1)
        m_state->application = [NSApplication sharedApplication];

        // a regular policy is what gives the process a menu bar and a dock tile (§4.1)
        [m_state->application setActivationPolicy:NSApplicationActivationPolicyRegular];

        // the launch may only happen once per process, because appkit refuses a second launch (§4.1)
        static std::atomic<bool> launched{false};
        if (!launched.exchange(true))
            [m_state->application finishLaunching];

        // the screen refresh rate is the pacing target of the display link (§4.3)
        NSScreen* screen = [NSScreen mainScreen];
        if (screen != nil)
            m_state->refresh_hz = static_cast<double>(screen.maximumFramesPerSecond);

        // the display link is created up front, so starting the pacing source cannot fail later (§4.3)
        if (CVDisplayLinkCreateWithActiveCGDisplays(&m_state->display_link) != kCVReturnSuccess)
            m_state->display_link = nullptr;
    }

    MacosEventLoop::~MacosEventLoop() {
        // the display link is stopped before anything else, so no further frame can arrive (§4.3)
        stop_frame_pacing();
        // a link that was created is released, because the loop owns it as a core foundation object (§4.3)
        if (m_state->display_link != nullptr) {
            CFRelease(m_state->display_link);
            m_state->display_link = nullptr;
        }
        // the application reference is dropped with the state, so appkit outlives every window (§4.1)
        m_state->application = nil;
    }

    void MacosEventLoop::set_pump_handler(Task pump) {
        // the handler is what the display link invokes on the ui thread (§4.1)
        m_pump = std::move(pump);
    }

    void MacosEventLoop::run() {
        m_running = true;
        // the process is activated so that the window takes focus and the display link ticks (§4.1)
        [m_state->application activateIgnoringOtherApps:YES];
        // the appkit loop is entered, which is what delivers native events until it stops (§4.1)
        while (m_running) {
            // one event is pulled with a short deadline, so a posted task is answered without waiting for a native event (§4.1)
            @autoreleasepool {
                NSEvent* event = [m_state->application nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0005] inMode:NSDefaultRunLoopMode dequeue:YES];
                // an event that arrived is handed to appkit, which is the ordinary native path (§4.1)
                if (event != nil)
                    [m_state->application sendEvent:event];
            }
        }
    }

    void MacosEventLoop::quit() {
        // the flag is atomic, so a worker thread may terminate the application (§4.1)
        m_running = false;
        // the pacing source is stopped as well, because the loop is gone (§4.3)
        stop_frame_pacing();
    }

    bool MacosEventLoop::running() const {
        // the display link consults this before it hands a frame over, so it must be atomic (§4.3)
        return m_running.load();
    }

    void MacosEventLoop::start_frame_pacing() {
        // a link that could not be created leaves the loop running without pacing rather than crashing (§4.3)
        if (m_state->display_link == nullptr)
            return;
        // the callback receives the loop, which it marshals its frames onto (§4.3)
        CVDisplayLinkSetOutputCallback(m_state->display_link, &on_display_link, this);
        // the loop is started once, because a second start is an error in core video (§4.3)
        CVDisplayLinkStart(m_state->display_link);
    }

    void MacosEventLoop::stop_frame_pacing() {
        // a link that was never created has nothing to stop (§4.3)
        if (m_state->display_link == nullptr)
            return;
        // stopping blocks until an in flight callback returned, so no frame can arrive afterwards (§4.3)
        CVDisplayLinkStop(m_state->display_link);
    }

    std::uint64_t MacosEventLoop::frame_ticks() const {
        // the counter is atomic, because the display link thread increments it (§4.3)
        return m_frame_ticks.load();
    }

    double MacosEventLoop::refresh_hz() const {
        // the rate is what the screen reported when the loop was built (§4.3)
        return m_state->refresh_hz;
    }

    void MacosEventLoop::deliver_on_ui_thread() {
        // the loop may have been torn down while the block waited on the main queue (§4.2)
        if (!m_running.load())
            return;
        // the tick is counted so that the example can report the cadence it reached (§4.3)
        m_frame_ticks.fetch_add(1);
        // the pump runs on the ui thread, which is where posted tasks and timers belong (§4.1)
        if (!m_pump)
            return;
        m_pump();
    }

    void install_macos_platform() {
        // the loop factory builds the appkit backed loop, which an application runs on (§5.3, AD-16)
        set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new MacosEventLoop()); });
        // the window factory builds the appkit window that carries the metal layer (§5.3, AD-16)
        set_platform_window_factory([](const WindowOptions& options) -> std::unique_ptr<PlatformWindow> {
            // the factory returns the concrete window as the hal, so no appkit type escapes the module (§3.5)
            return MacosPlatformWindow::create(options);
        });
    }
} // namespace albor::macos

#pragma clang diagnostic pop
