#include "window/window.h"

namespace albor
{
    Window::Window(WindowHandle handle, WindowOptions options, std::unique_ptr<PlatformWindow> platform) :
        m_handle(handle), m_options(std::move(options)), m_platform(std::move(platform)) {
        // a window without a native window is still a valid framework window, but it has no metrics yet (§5.1)
        if (m_platform) {
            // the listener has to point at this window before any native event can arrive (§5.3)
            m_platform->set_listener(this);
            // the metrics start from what the native window already reports (§9.1)
            m_metrics = m_platform->metrics();
        }
    }

    Window::~Window() {
        // the native window holds a raw pointer to this listener, so it must stop raising events first (§4.2)
        if (m_platform)
            m_platform->set_listener(nullptr);
    }

    bool Window::alive() const {
        // a window is alive exactly while it owns a native window (§5.2)
        return m_platform != nullptr;
    }

    WindowHandle Window::handle() const {
        // the handle names this window for the rest of its life (§5.2)
        return m_handle;
    }

    const WindowOptions& Window::options() const {
        // the options are kept whole, because a role is immutable after creation (§5.1)
        return m_options;
    }

    WindowMetrics Window::metrics() const {
        // the platform is the authority on metrics, so the window only caches what it last reported (§9.1)
        return m_metrics;
    }

    void Window::show() {
        // a window without a native window has nothing to put on screen (§5.1)
        if (!m_platform)
            return;
        m_platform->show();
        m_visible = true;
    }

    void Window::hide() {
        // a window without a native window was never on screen (§5.1)
        if (!m_platform)
            return;
        m_platform->hide();
        m_visible = false;
    }

    void Window::set_title(std::string_view title) {
        // a window without a native window has no title bar to write into (§5.1)
        if (!m_platform)
            return;
        m_platform->set_title(title);
    }

    void Window::set_size(Size size) {
        // a window without a native window cannot be resized (§5.1)
        if (!m_platform)
            return;
        m_platform->set_size(size);
    }

    void Window::request_frame() {
        // a window without a platform has nothing to pace (§6.1)
        if (!m_platform)
            return;
        m_platform->request_frame();
    }

    void Window::set_text_anchor(Rect anchor) {
        // a window without a platform has no input method to anchor (§8.3)
        if (!m_platform)
            return;
        // the anchor is handed through whole, which the platform places (§8.3)
        m_platform->set_text_anchor(anchor);
    }

    void Window::set_text_selection(TextInputRange selection) {
        // a window without a platform has no input method to address a document through (§8.3)
        if (!m_platform)
            return;
        // the range is handed through whole, which the platform stores in its own units (§8.3)
        m_platform->set_text_selection(selection);
    }

    NativeWindowHandle Window::native_handle() const {
        // a window without a native window hands out the invalid handle, which a backend refuses (§3.5)
        if (!m_platform)
            return NativeWindowHandle{};
        return m_platform->native_handle();
    }

    SystemAppearance Window::system_appearance() const {
        // a window without a native window has no system to ask, so it reports the portable default (§5.3)
        if (!m_platform)
            return SystemAppearance{};
        // the platform is the authority on the system appearance, so the window only asks it (§5.3, §8.4)
        return m_platform->system_appearance();
    }

    void Window::set_appearance_handler(std::function<void(const SystemAppearance&)> handler) {
        // the handler receives every appearance change the platform raises (§8.4)
        m_appearance_handler = std::move(handler);
    }

    void Window::set_close_handler(std::function<bool()> handler) {
        // the handler is consulted on every close request, so replacing it changes the next verdict (§5.2)
        m_close_handler = std::move(handler);
    }

    void Window::set_closed_handler(std::function<void()> handler) {
        // the handler receives the Closed notification of §5.2, which only a native close raises (§5.2)
        m_closed_handler = std::move(handler);
    }

    void Window::set_resize_handler(std::function<void(const WindowMetrics&)> handler) {
        // the handler receives the metrics the platform just reported (§9.1)
        m_resize_handler = std::move(handler);
    }

    void Window::set_frame_handler(std::function<void()> handler) {
        // the handler receives every pacing tick the platform raises (§4.3)
        m_frame_handler = std::move(handler);
    }

    void Window::set_input_handler(std::function<void(const Event&)> handler) {
        // the handler receives the normalized events the platform raises (§8.3)
        m_input_handler = std::move(handler);
    }

    void Window::close() {
        // closing a window that never existed is a no operation rather than an error (§5.2)
        if (!m_platform) {
            // reset flag
            m_close_pending = false;
            // exit
            return;
        }
        // a framework initiated close is remembered, so the native notification that follows it is not repeated (§5.2)
        m_close_initiated = true;
        // the native window goes away first, so that no event reaches a half torn down object (§4.2)
        m_platform->close();
        // the framework window is no longer on screen once its native window is gone
        m_visible = false;
        // a granted close leaves nothing pending behind (§5.2)
        m_close_pending = false;
    }

    bool Window::close_pending() const {
        // a pending close is the record of a veto, which the next request may retry (§5.2)
        return m_close_pending;
    }

    bool Window::visible() const {
        // visibility is the framework's record of what it asked the native window to do (§5.1)
        return m_visible;
    }

    void Window::on_platform_resize(const WindowMetrics& metrics) {
        // the platform is the authority on metrics, so the cached copy follows it (§9.1)
        m_metrics = metrics;
        // an application that listens for resizes sees the new metrics (§6.1)
        if (m_resize_handler)
            m_resize_handler(m_metrics);
    }

    bool Window::on_platform_close_request() {
        // without a handler the framework grants the close, which is what a plain window wants (§5.2)
        if (!m_close_handler)
            return true;
        // a vetoing handler leaves the close pending so that the next request may retry (§5.2)
        m_close_pending = !m_close_handler();
        // the platform may only close the native window when the application granted it (§5.2)
        return !m_close_pending;
    }

    void Window::on_platform_closed() {
        // a close the framework initiated is already tearing down, so the notification is not raised twice (§5.2)
        if (m_close_initiated)
            return;
        // the native window is gone, so the framework window is no longer on screen (§5.1)
        m_visible = false;
        // the window is closed, so a previously vetoed request no longer pends (§5.2)
        m_close_pending = false;
        // the Closed notification reaches the application, which is the second step of §5.2
        if (m_closed_handler)
            m_closed_handler();
    }

    void Window::on_platform_frame() {
        // a window without a frame handler presents nothing (§4.3)
        if (!m_frame_handler)
            return;
        m_frame_handler();
    }

    void Window::on_platform_event(const Event& event) {
        // a window without an input handler drops the event, which is the quiet default of §8.3
        if (!m_input_handler)
            return;
        m_input_handler(event);
    }

    void Window::on_platform_appearance_changed(const SystemAppearance& appearance) {
        // a window without an appearance handler records nothing, which is the quiet default of §8.4
        if (!m_appearance_handler)
            return;
        m_appearance_handler(appearance);
    }
} // namespace albor
