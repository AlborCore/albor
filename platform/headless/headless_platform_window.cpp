#include "platform/headless/headless_platform_window.h"

#include "window/window_metrics.h"
#include "window/window_options.h"

namespace albor::headless
{
    std::unique_ptr<HeadlessPlatformWindow> HeadlessPlatformWindow::create(const WindowOptions& options) {
        // an invalid option set is refused before anything is built, as every platform adapter does (§5.1)
        if (!options.valid())
            return nullptr;
        // the window is returned as the hal, so no concrete type escapes this module (§3.5, AD-05)
        return std::unique_ptr<HeadlessPlatformWindow>(new HeadlessPlatformWindow(options));
    }

    HeadlessPlatformWindow::HeadlessPlatformWindow(const WindowOptions& options) {
        // the metrics start from the requested size at a unit scale, because there is no display to report one (§5.3)
        m_metrics = WindowMetrics::from_logical(options.size, 1.0f);
        // the title is kept because a headless report reads it and nothing else would (§5.3)
        m_title = options.title;
        // the window starts on screen when the options asked for it (§5.3)
        m_visible = options.visible;
    }

    HeadlessPlatformWindow::~HeadlessPlatformWindow() = default;

    void HeadlessPlatformWindow::set_listener(PlatformWindowListener* listener) {
        // the listener is borrowed, because the window does not own the application (§5.3)
        m_listener = listener;
    }

    void HeadlessPlatformWindow::show() {
        // an already visible window records nothing new, because a repeated show has no effect (§5.3)
        if (m_visible)
            return;
        // there is no screen to appear on, so the flag is the whole of showing (§5.3)
        m_visible = true;
    }

    void HeadlessPlatformWindow::hide() {
        // an already hidden window records nothing new, because a repeated hide has no effect (§5.3)
        if (!m_visible)
            return;
        // there is no screen to disappear from, so the flag is the whole of hiding (§5.3)
        m_visible = false;
    }

    void HeadlessPlatformWindow::set_title(std::string_view title) {
        // the title is stored, because a headless run reads it back out of a report (§5.3)
        m_title = std::string(title);
    }

    void HeadlessPlatformWindow::set_size(Size size) {
        // metrics with no area cannot be presented, so the previous size is kept (§6.2)
        if (size.width <= 0.0f || size.height <= 0.0f)
            return;
        // the scale is unchanged, because a headless window has no display to change it (§5.3, AD-27)
        raise_resize(WindowMetrics::from_logical(size, m_metrics.scale_factor));
    }

    WindowMetrics HeadlessPlatformWindow::metrics() const {
        // the metrics are the synthetic ones this window owns, which is what a test asserts on (§5.3)
        return m_metrics;
    }

    NativeWindowHandle HeadlessPlatformWindow::native_handle() const {
        // there is no native object, so the handle is empty rather than a fabricated pointer (§3.5, AD-05)
        return NativeWindowHandle{};
    }

    void HeadlessPlatformWindow::request_frame() {
        // the request is recorded rather than scheduled, because there is no display link to schedule on (§4.3, AD-18b)
        m_frame_requested = true;
    }

    bool HeadlessPlatformWindow::frame_requested() const {
        // the flag is the whole per window pacing state, because nothing else tracks it here (§4.3, AD-18b)
        return m_frame_requested;
    }

    bool HeadlessPlatformWindow::visible() const {
        // the flag is what show and hide record, because there is no screen to ask (§5.3)
        return m_visible;
    }

    SystemAppearance HeadlessPlatformWindow::system_appearance() const {
        // the appearance is the synthetic one a test set, or the portable default before that (§5.3, §8.4)
        return m_appearance;
    }

    void HeadlessPlatformWindow::raise_appearance_change(const SystemAppearance& appearance) {
        // the appearance is stored first, so a later query reads what was announced (§8.4)
        m_appearance = appearance;
        // a window with no listener has nobody to tell, which is how a detached window flips (§8.4)
        if (m_listener == nullptr)
            return;
        // the change is raised on the thread that called this, which is the ui thread in every real path (§8.4)
        m_listener->on_platform_appearance_changed(appearance);
    }

    void HeadlessPlatformWindow::close() {
        // a close is a request, because the application may veto it (§5.2)
        raise_close_request();
    }

    void HeadlessPlatformWindow::raise_resize(const WindowMetrics& metrics) {
        // metrics with no area cannot be presented, so the request is ignored (§6.2)
        if (!metrics.valid())
            return;
        // the metrics move first, because a listener reads them while handling the resize (§5.3, D9.1)
        m_metrics = metrics;
        // the pending flag is what a test synchronizes on before it draws (§5.3)
        m_resize_pending = true;
        // a window with no listener has nobody to tell, which is how a detached window resizes (§5.3)
        if (m_listener == nullptr)
            return;
        // the resize is raised on the thread that called this, which is the ui thread in every real path (§5.3, D9.1)
        m_listener->on_platform_resize(m_metrics);
    }

    void HeadlessPlatformWindow::raise_event(const Event& event) {
        // a window with no listener has nobody to route into, so the event is dropped (§5.3, §8.3)
        if (m_listener == nullptr)
            return;
        // the event is handed over as raised, because the adapter has already normalized it (§8.3)
        m_listener->on_platform_event(event);
    }

    void HeadlessPlatformWindow::raise_close_request() {
        // a window with no listener has nobody to ask, so the request cannot proceed (§5.2)
        if (m_listener == nullptr)
            return;
        // the application vetoes or grants, and only a grant closes (§5.2)
        if (!m_listener->on_platform_close_request())
            return;
        // the close is reported after the grant, so the window is gone by the time the application hears (§5.2)
        m_visible = false;
        m_listener->on_platform_closed();
    }

    void HeadlessPlatformWindow::pump_frame() {
        // only a requested frame is drawn, because an unrequested frame would break the pacing contract (§4.3, AD-18b)
        if (!m_frame_requested)
            return;
        // the request clears before the draw, so a handler that requests again is not lost (§4.3)
        m_frame_requested = false;
        // a window with no listener has no ui thread to tick (§4.3)
        if (m_listener == nullptr)
            return;
        // the pending resize is answered by this frame, so a test that waits sees it settle (§5.3, D9.1)
        m_resize_pending = false;
        // the tick is what a display link would raise on a real platform (§4.3, AD-16)
        m_listener->on_platform_frame();
    }

    const std::string& HeadlessPlatformWindow::title() const {
        // the title is stored, so a headless report can assert on what was set (§5.3)
        return m_title;
    }

    bool HeadlessPlatformWindow::resize_pending() const {
        // the flag is what a test waits on, because there is no window manager to tell it (§5.3)
        return m_resize_pending;
    }
} // namespace albor::headless
