#include "platform/macos/platform_window.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstddef>
#include <memory>
#include <string>

// the delegate the native window consults before it closes, which is the veto point of §5.2
@interface AlborWindowDelegate : NSObject <NSWindowDelegate>

// the framework listener the close request is forwarded to, which is null once the framework window is gone (§4.2)
@property(nonatomic, assign) albor::PlatformWindowListener* listener;

@end

@implementation AlborWindowDelegate

// the native close request asks the framework first, which may veto it (§5.2)
- (BOOL)windowShouldClose:(NSWindow*)sender {
    // a listener that is gone means the framework window is already destroyed, so the close proceeds (§4.2)
    if (self.listener == nullptr)
        return YES;

    return self.listener->on_platform_close_request() ? YES : NO;
}

// the native window reports that it closed, which is the Closed notification of §5.2
- (void)windowWillClose:(NSWindow*)sender {
    // the sender is only the trigger, so it is not carried into the framework (§5.3)
    (void)sender;
    // a listener that is gone means the framework window is already destroyed, so nobody is left to notify (§4.2)
    if (self.listener == nullptr)
        return;

    self.listener->on_platform_closed();
}

@end

// the content view that translates native events into normalized ones, which is the input path of §8.3
@interface AlborContentView : NSView

// the framework listener the events are forwarded to, which is null once the framework window is gone (§4.2)
@property(nonatomic, assign) albor::PlatformWindowListener* listener;

@end

namespace
{
    // the portable key each ansi letter key code names, which the letters occupy as one contiguous range (§18.5)
    constexpr albor::Key LETTER_KEYS[] = {albor::Key::A, albor::Key::S, albor::Key::D, albor::Key::F, albor::Key::H, albor::Key::G, albor::Key::Z, albor::Key::X, albor::Key::C, albor::Key::V, albor::Key::None, albor::Key::B, albor::Key::Q, albor::Key::W, albor::Key::E, albor::Key::R, albor::Key::Y, albor::Key::T};

    // the portable key each ansi digit key code names, where two codes are punctuation and name nothing (§18.5)
    constexpr albor::Key DIGIT_KEYS[] = {albor::Key::One, albor::Key::Two, albor::Key::Three, albor::Key::Four, albor::Key::Six, albor::Key::Five, albor::Key::None, albor::Key::Nine, albor::Key::Seven, albor::Key::None, albor::Key::Eight, albor::Key::Zero};

    albor::Key key_from_nsevent(NSEvent* event) {
        // the key code is narrowed once, because every table below and the switch both index on it (§8.3)
        const int code = static_cast<int>(event.keyCode);
        // the ansi letters occupy the first contiguous range, so one table names every one of them (§18.5)
        if (code >= 0 && code <= 17)
            return LETTER_KEYS[static_cast<std::size_t>(code)];
        // the ansi digits occupy the range right after them, where two codes are punctuation (§18.5)
        if (code >= 18 && code <= 29)
            return DIGIT_KEYS[static_cast<std::size_t>(code - 18)];
        // the named keys map one platform key code onto one portable name (§8.3)
        switch (code) {
        // tab
        case 48:
            return albor::Key::Tab;
        // return and the keypad enter both activate
        case 36:
        case 76:
            return albor::Key::Return;
        // escape
        case 53:
            return albor::Key::Escape;
        // the left arrow
        case 123:
            return albor::Key::ArrowLeft;
        // the right arrow
        case 124:
            return albor::Key::ArrowRight;
        // the down arrow
        case 125:
            return albor::Key::ArrowDown;
        // the up arrow
        case 126:
            return albor::Key::ArrowUp;
        // the space bar
        case 49:
            return albor::Key::Space;
        // the backspace and the forward delete both remove
        case 51:
        case 117:
            return albor::Key::Delete;
        // the home key, which a scroll viewport answers with its start (§8.3)
        case 115:
            return albor::Key::Home;
        // the page up key, which scrolls back one viewport (§8.3)
        case 116:
            return albor::Key::PageUp;
        // the end key, which a scroll viewport answers with its end (§8.3)
        case 119:
            return albor::Key::End;
        // the page down key, which scrolls forward one viewport (§8.3)
        case 121:
            return albor::Key::PageDown;
        // the first function key
        case 122:
            return albor::Key::F1;
        // the second function key
        case 120:
            return albor::Key::F2;
        // the third function key
        case 99:
            return albor::Key::F3;
        // the fourth function key
        case 118:
            return albor::Key::F4;
        // the fifth function key
        case 96:
            return albor::Key::F5;
        // the sixth function key
        case 97:
            return albor::Key::F6;
        // the seventh function key
        case 98:
            return albor::Key::F7;
        // the eighth function key
        case 100:
            return albor::Key::F8;
        // the ninth function key
        case 105:
            return albor::Key::F9;
        // the tenth function key
        case 109:
            return albor::Key::F10;
        // the eleventh function key
        case 103:
            return albor::Key::F11;
        // the twelfth function key
        case 111:
            return albor::Key::F12;
        // every other key carries no portable name
        default:
            return albor::Key::None;
        }
    }

    albor::Modifiers modifiers_from_nsevent(NSEvent* event) {
        // the modifier flags map onto the portable set, one flag per key (§8.3)
        albor::Modifiers modifiers;
        const NSEventModifierFlags flags = event.modifierFlags;
        modifiers.shift = (flags & NSEventModifierFlagShift) != 0;
        modifiers.control = (flags & NSEventModifierFlagControl) != 0;
        modifiers.option = (flags & NSEventModifierFlagOption) != 0;
        modifiers.command = (flags & NSEventModifierFlagCommand) != 0;
        return modifiers;
    }
} // namespace

@implementation AlborContentView

// the view owns the caret, so it accepts first responder for the key events to reach it (§8.3)
- (BOOL)acceptsFirstResponder {
    return YES;
}

// a click on an inactive window reaches the view immediately, which is what a fast press needs (§8.3)
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}

// turn a native pointer position into the logical point of §8.3, flipping the y axis to the tree origin
- (albor::Point)logicalPointOf:(NSEvent*)event {
    const NSPoint in_view = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat height = self.bounds.size.height;
    return albor::Point{static_cast<float>(in_view.x), static_cast<float>(height - in_view.y)};
}

- (void)mouseDown:(NSEvent*)event {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::PointerDown;
    message.position = [self logicalPointOf:event];
    self.listener->on_platform_event(message);
}

- (void)mouseDragged:(NSEvent*)event {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::PointerMove;
    message.position = [self logicalPointOf:event];
    self.listener->on_platform_event(message);
}

- (void)mouseUp:(NSEvent*)event {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::PointerUp;
    message.position = [self logicalPointOf:event];
    self.listener->on_platform_event(message);
}

- (void)scrollWheel:(NSEvent*)event {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::Wheel;
    message.position = [self logicalPointOf:event];
    message.scroll = static_cast<float>(event.scrollingDeltaY);
    // the unit travels with the delta, because a notched wheel counts notches and a trackpad counts points (§8.3)
    message.precision = event.hasPreciseScrollingDeltas ? albor::ScrollPrecision::Point : albor::ScrollPrecision::Line;
    self.listener->on_platform_event(message);
}

- (void)keyDown:(NSEvent*)event {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::KeyDown;
    message.key = key_from_nsevent(event);
    message.modifiers = modifiers_from_nsevent(event);
    self.listener->on_platform_event(message);
    // the interpret path gives the input method a chance to produce committed text (§8.3)
    [self interpretKeyEvents:@[ event ]];
}

- (void)keyUp:(NSEvent*)event {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::KeyUp;
    message.key = key_from_nsevent(event);
    message.modifiers = modifiers_from_nsevent(event);
    self.listener->on_platform_event(message);
}

- (void)insertText:(id)string {
    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
    if (self.listener == nullptr)
        return;
    // an attributed string hands its plain text over, which is what a key event spells (§8.3)
    NSString* text = [string isKindOfClass:[NSString class]] ? (NSString*)string : [string string];
    if (text.length == 0)
        return;
    const char* utf8 = text.UTF8String;
    if (utf8 == nullptr)
        return;
    albor::Event message;
    message.kind = albor::EventKind::TextInput;
    message.text = std::string(utf8);
    self.listener->on_platform_event(message);
}

@end

namespace albor
{
    // the opaque appkit state, defined here so that no appkit type reaches a public header (§3.5, AD-05)
    struct MacosWindowState
    {
        // the framework listener the native window raises events into (§5.3)
        PlatformWindowListener* listener = nullptr;

        // the native window itself, which is nil until the window is created
        NSWindow* window = nil;

        // the metal layer the renderer backend attaches to, which is the macos presentation path of AD-16
        CAMetalLayer* layer = nil;

        // the delegate that turns a native close request into a framework close request (§5.2)
        id window_delegate = nil;

        // the content view that translates the native events of §8.3 into normalized ones (§5.3, §8.3)
        id content_view = nil;

        // the resize observer, which is removed before the state is released (§4.2)
        id resize_observer = nil;

        // the backing scale observer, which is removed before the state is released (§4.2)
        id backing_observer = nil;

        // whether a frame was requested and not yet drawn, which is per window pacing (AD-18b)
        bool frame_requested = false;

        // whether the window is currently on screen, which appkit does not report directly
        bool visible = false;
    };

    namespace
    {
        NSWindowStyleMask style_mask_for(const WindowOptions& options) {
            // every albor window is titled and closable, which is what the documented role mask asks for (§5.3)
            NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
            // a window the user may not resize simply omits the resizable bit (§5.1)
            if (options.resizable)
                mask |= NSWindowStyleMaskResizable;
            // a dialog is a secondary window, which appkit marks as miniaturizable (§5.1)
            if (options.role == WindowRole::Dialog)
                mask |= NSWindowStyleMaskMiniaturizable;
            return mask;
        }

        WindowMetrics metrics_of(NSWindow* window) {
            // the window a resize arrived on is measured, and one with no content view has no area to report (§5.3)
            NSView* content = window.contentView;
            if (content == nil)
                return WindowMetrics{};
            // the content view bounds are in logical points, while the drawable is in physical pixels (AD-27)
            const NSRect bounds = content.bounds;
            // the backing scale factor is what separates the logical size from the pixel size (AD-27)
            const CGFloat scale = window.backingScaleFactor;
            return WindowMetrics::from_logical(Size{static_cast<float>(bounds.size.width), static_cast<float>(bounds.size.height)}, static_cast<float>(scale));
        }
    } // namespace

    std::unique_ptr<MacosPlatformWindow> MacosPlatformWindow::create(const WindowOptions& options) {
        // an option set the framework refuses is never handed to appkit (§5.1)
        if (!options.valid())
            return nullptr;
        // the shared application has to exist before a window can be ordered front (§4.1, §5.3)
        NSApplication* application = [NSApplication sharedApplication];
        // a regular policy is what gives the window a menu bar and a dock tile (§4.1)
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        // the cascade offset keeps a second window from hiding behind the first one (§5.3)
        static std::size_t window_ordinal = 0;
        const double origin_x = 120.0 + 80.0 * static_cast<double>(window_ordinal);
        const double origin_y = 120.0 + 60.0 * static_cast<double>(window_ordinal);
        const NSRect frame = NSMakeRect(origin_x, origin_y, static_cast<double>(options.size.width), static_cast<double>(options.size.height));
        // the native window is created with the documented titled, closable and resizable mask (§5.3)
        NSWindow* window = [[NSWindow alloc] initWithContentRect:frame styleMask:style_mask_for(options) backing:NSBackingStoreBuffered defer:NO];
        if (window == nil)
            return nullptr;
        window.title = [NSString stringWithUTF8String:options.title.c_str()];
        // the content view carries the metal layer and translates the native events of §8.3
        AlborContentView* content = [[AlborContentView alloc] initWithFrame:window.contentView.frame];
        window.contentView = content;
        content.wantsLayer = YES;
        content.layer = [CAMetalLayer layer];
        // the content view takes the caret, so the key events reach it once the window is key (§8.3)
        window.initialFirstResponder = content;
        [window makeFirstResponder:content];
        // the state is shared because the notification blocks outlive this function and capture it (§4.2)
        std::shared_ptr<MacosWindowState> state = std::make_shared<MacosWindowState>();
        state->window = window;
        state->content_view = content;
        state->layer = static_cast<CAMetalLayer*>(content.layer);
        // the delegate is the veto point of a native close request, which the framework answers (§5.2)
        AlborWindowDelegate* delegate = [[AlborWindowDelegate alloc] init];
        state->window_delegate = delegate;
        window.delegate = delegate;
        // the notifications arrive on the main queue, so a resize or a scale change is observed on the ui thread (§4.1)
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        NSOperationQueue* main_queue = [NSOperationQueue mainQueue];
        // a resize reaches the framework as new metrics, because the platform is their authority (§9.1)
        state->resize_observer = [center addObserverForName:NSWindowDidResizeNotification
                                                     object:window
                                                      queue:main_queue
                                                 usingBlock:^(NSNotification*) {
                                                   // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
                                                   if (state->listener == nullptr)
                                                       return;
                                                   // the framework receives the metrics the platform now reports (§9.1)
                                                   state->listener->on_platform_resize(metrics_of(state->window));
                                                 }];
        // a backing scale change is a metrics change as well, because the pixel size moves with it (AD-27)
        state->backing_observer = [center addObserverForName:NSWindowDidChangeBackingPropertiesNotification
                                                      object:window
                                                       queue:main_queue
                                                  usingBlock:^(NSNotification*) {
                                                    // a listener that is gone means the framework window is already destroyed, so the event is dropped (§4.2)
                                                    if (state->listener == nullptr)
                                                        return;
                                                    // the framework receives the metrics the platform now reports (§9.1)
                                                    state->listener->on_platform_resize(metrics_of(state->window));
                                                  }];
        // a new window moves the cascade along, so a later window does not stack on this one (§5.3)
        window_ordinal++;
        // the framework window keeps the shared state alive, because the observers reference it (§4.2)
        return std::unique_ptr<MacosPlatformWindow>(new MacosPlatformWindow(std::move(state)));
    }

    MacosPlatformWindow::~MacosPlatformWindow() {
        // the listeners are detached first, because a teardown notification would otherwise reach a half dead object (§4.2)
        m_state->listener = nullptr;
        m_state->window.delegate = nil;
        // the content view keeps no listener, because the framework window is already gone (§4.2)
        AlborContentView* content = static_cast<AlborContentView*>(m_state->content_view);
        if (content != nil)
            content.listener = nil;
        // the observers are removed before the state is released, because their blocks captured it (§4.2)
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        if (m_state->resize_observer != nil) {
            [center removeObserver:m_state->resize_observer];
            m_state->resize_observer = nil;
        }
        if (m_state->backing_observer != nil) {
            [center removeObserver:m_state->backing_observer];
            m_state->backing_observer = nil;
        }
        // the native window is closed last, so the layer and its drawables die with it (§6.3)
        [m_state->window close];
        // the native objects are dropped with the state, which releases them under arc (§5.3)
        m_state->window = nil;
        m_state->window_delegate = nil;
        m_state->content_view = nil;
        m_state->layer = nil;
    }

    void MacosPlatformWindow::set_listener(PlatformWindowListener* listener) {
        m_state->listener = listener;
        // the delegate holds the same listener, because the close veto travels through it (§5.2)
        AlborWindowDelegate* delegate = static_cast<AlborWindowDelegate*>(m_state->window_delegate);
        if (delegate != nil)
            delegate.listener = listener;
        // the content view holds the same listener, because the input events travel through it (§8.3)
        AlborContentView* content = static_cast<AlborContentView*>(m_state->content_view);
        if (content != nil)
            content.listener = listener;
    }

    void MacosPlatformWindow::show() {
        // a window with no native window cannot be shown (§5.3)
        if (m_state->window == nil)
            return;
        [m_state->window makeKeyAndOrderFront:nil];
        // the application is activated so that the window takes focus and the pacing source ticks (§4.1)
        [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
        m_state->visible = true;
    }

    void MacosPlatformWindow::hide() {
        // a window with no native window was never on screen (§5.3)
        if (m_state->window == nil)
            return;
        [m_state->window orderOut:nil];
        m_state->visible = false;
    }

    void MacosPlatformWindow::set_title(std::string_view title) {
        // a window with no native window has no title bar to write into (§5.3)
        if (m_state->window == nil)
            return;
        m_state->window.title = [[NSString alloc] initWithBytes:title.data() length:title.size() encoding:NSUTF8StringEncoding];
    }

    void MacosPlatformWindow::set_size(Size size) {
        // a window with no native window cannot be resized (§5.3)
        if (m_state->window == nil)
            return;
        // the origin is kept, so the window grows from the corner the user placed it at (§7.4)
        const NSRect frame = m_state->window.frame;
        [m_state->window setFrame:NSMakeRect(frame.origin.x, frame.origin.y, static_cast<double>(size.width), static_cast<double>(size.height)) display:YES];
    }

    WindowMetrics MacosPlatformWindow::metrics() const {
        // a window with no native window has no metrics to report (§5.3)
        if (m_state->window == nil)
            return WindowMetrics{};
        return metrics_of(m_state->window);
    }

    NativeWindowHandle MacosPlatformWindow::native_handle() const {
        // a window with no layer hands out the invalid handle, which a backend refuses (§3.5)
        if (m_state->layer == nil)
            return NativeWindowHandle{};
        // the layer crosses into the framework as an opaque pointer, which no appkit type may name (§3.5, AD-05)
        return NativeWindowHandle::from_object((__bridge const void*)m_state->layer);
    }

    void MacosPlatformWindow::request_frame() {
        // the pacing source is owned by the event loop, so a window only records that it wants a frame (§4.3)
        m_state->frame_requested = true;
    }

    bool MacosPlatformWindow::frame_requested() const {
        // the flag is what the next pacing tick consults to decide whether to draw (§4.3)
        return m_state->frame_requested;
    }

    bool MacosPlatformWindow::visible() const {
        // the flag is the framework's record of what it asked appkit to do (§5.1)
        return m_state->visible;
    }

    void MacosPlatformWindow::close() {
        // a window that was never created has nothing to close (§5.2)
        if (m_state->window == nil)
            return;
        // the listener is detached first, because closing raises notifications the framework is already past (§4.2)
        m_state->listener = nullptr;
        m_state->window.delegate = nil;
        // the content view keeps no listener, because the framework is already past the window (§4.2)
        AlborContentView* content = static_cast<AlborContentView*>(m_state->content_view);
        if (content != nil)
            content.listener = nil;
        // the native window is closed without asking the framework, because the framework initiated this (§5.2)
        [m_state->window close];
        // a closed window has nothing left to draw (§4.3)
        m_state->frame_requested = false;
        m_state->visible = false;
    }

    MacosPlatformWindow::MacosPlatformWindow(std::shared_ptr<MacosWindowState> state) :
        m_state(std::move(state)) {}
} // namespace albor
