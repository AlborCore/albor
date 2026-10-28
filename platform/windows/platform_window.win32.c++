#include "platform/windows/platform_window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>

#include <imm.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "platform/windows/text_encoding.h"

namespace albor
{
    // the win32 encodes the window shares with the clipboard surface, which the calls below name unqualified (§5.3)
    using windows::utf8_from_units;
    using windows::wide_from_utf8;

    // the opaque win32 state, defined here so that no platform type reaches a public header (§3.5, AD-05)
    struct Win32WindowState
    {
        // the framework listener the native window raises events into (§5.3)
        PlatformWindowListener* listener = nullptr;

        // the native window itself, which is null from creation until the window is destroyed
        HWND window = nullptr;

        // whether a frame was requested and not yet drawn, which is per window pacing (AD-18b)
        bool frame_requested = false;

        // whether the window is currently on screen, which win32 does not track for the framework (§5.1)
        bool visible = false;

        // whether a left press is held, which tells a popup shown from that press to leave the capture alone (§5.1)
        bool mouse_down = false;

        // whether the window is a popup, which the show path and the dismissal monitor consult (§5.1)
        bool popup = false;

        // the owner a transient surface follows, which the anchor test reads (§5.1)
        HWND owner = nullptr;

        // the trigger rectangle the popup is anchored to, in the owner's logical client space (§5.1)
        Rect anchor{};

        // the logical extent the popup presents, which every placement scales into the frame (§5.1)
        Size popup_size{};

        // the caret rect the framework pushed, which the candidate window is placed by (§8.3)
        Rect text_anchor;

        // whether an anchor was pushed at all, which is what the exclusion is only meaningful under (§8.3)
        bool anchor_valid = false;

        // the document selection the framework pushed, which a reconversion re-checks (§8.3)
        TextInputRange text_selection;

        // whether an input method composition is open, which the end message closes (§8.3)
        bool composing = false;

        // the high surrogate a split utf16 pair waits to complete, where zero marks no pending half (§8.3)
        wchar_t pending_surrogate = 0;

        // the style the window was created with, which a later resize adjusts the frame against (§5.1)
        DWORD style = 0;
    };

    namespace
    {
        // the name the window class is registered under, which is one per process (§5.3)
        constexpr const wchar_t* WINDOW_CLASS = L"AlborWindow";

        // the scale factor the window reports, which separates logical pixels from physical ones (AD-27)
        float scale_of(HWND window) {
            // the dpi the window sits under is what the per monitor awareness hands out (AD-27)
            const UINT dpi = GetDpiForWindow(window);
            // a window that reports no dpi falls back to the identity scale, which headless metrics also use (AD-27)
            if (dpi == 0)
                return 1.0f;
            return static_cast<float>(dpi) / 96.0f;
        }

        // the metrics a window reports, which the platform is the authority for (§5.3, AD-27)
        WindowMetrics metrics_of(HWND window) {
            // the client rect is the drawable area, which is what the options and the layout size (§5.3)
            RECT client{};
            // a window that cannot be measured has no metrics to report, which the invalid default carries (§5.3)
            if (!GetClientRect(window, &client))
                return WindowMetrics{};
            // the scale is read once, because the logical size and the metrics both divide by it (AD-27)
            const float scale = scale_of(window);
            // the client size arrives in physical pixels, while the layout measures logical ones (AD-27)
            const Size pixel{static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top)};
            return WindowMetrics::from_logical(Size{pixel.width / scale, pixel.height / scale}, scale);
        }

        // the portable key each virtual key names, where an unmapped key carries none (§8.3)
        Key key_from_wparam(WPARAM wparam) {
            // the virtual key is narrowed once, because every range and switch below reads it (§8.3)
            const int code = static_cast<int>(wparam);
            // the letters occupy one contiguous range of virtual keys, so one span names every one of them (§18.5)
            if (code >= 'A' && code <= 'Z')
                return static_cast<Key>(static_cast<int>(Key::A) + (code - 'A'));
            // the digits occupy one contiguous range right after the letters (§18.5)
            if (code >= '0' && code <= '9')
                return static_cast<Key>(static_cast<int>(Key::Zero) + (code - '0'));
            // the named keys map one virtual key onto one portable name (§8.3)
            switch (code) {
            case VK_TAB:
                return Key::Tab;
            // the return key and the keypad enter both activate
            case VK_RETURN:
                return Key::Return;
            case VK_ESCAPE:
                return Key::Escape;
            case VK_LEFT:
                return Key::ArrowLeft;
            case VK_RIGHT:
                return Key::ArrowRight;
            case VK_DOWN:
                return Key::ArrowDown;
            case VK_UP:
                return Key::ArrowUp;
            case VK_SPACE:
                return Key::Space;
            // the backspace key removes before the caret, and the forward delete removes at it (§8.3)
            case VK_BACK:
                return Key::Backspace;
            case VK_DELETE:
                return Key::Delete;
            case VK_HOME:
                return Key::Home;
            case VK_PRIOR:
                return Key::PageUp;
            case VK_END:
                return Key::End;
            case VK_NEXT:
                return Key::PageDown;
            case VK_F1:
                return Key::F1;
            case VK_F2:
                return Key::F2;
            case VK_F3:
                return Key::F3;
            case VK_F4:
                return Key::F4;
            case VK_F5:
                return Key::F5;
            case VK_F6:
                return Key::F6;
            case VK_F7:
                return Key::F7;
            case VK_F8:
                return Key::F8;
            case VK_F9:
                return Key::F9;
            case VK_F10:
                return Key::F10;
            case VK_F11:
                return Key::F11;
            case VK_F12:
                return Key::F12;
            // every other key carries no portable name
            default:
                return Key::None;
            }
        }

        // the modifier keys the keyboard holds, which the key state answers at the moment of the event (§8.3)
        Modifiers modifiers_from_keyboard() {
            // the modifier flags map onto the portable set, one flag per key (§8.3)
            Modifiers modifiers;
            modifiers.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            modifiers.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            // the alt key is the option, and the windows key is the command, which the portable names carry (§8.3)
            modifiers.option = (GetKeyState(VK_MENU) & 0x8000) != 0;
            modifiers.command = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0;
            return modifiers;
        }

        // the logical point a client position names, which divides the pixels by the window scale (§8.3, AD-27)
        Point logical_from_client(HWND window, int x, int y) {
            const float scale = scale_of(window);
            return Point{static_cast<float>(x) / scale, static_cast<float>(y) / scale};
        }

        // the dword a registry value carries, which a missing one leaves the fallback untouched for (§8.4)
        bool registry_dword(const wchar_t* key, const wchar_t* value, DWORD& read) {
            // the machine word the converter fills is measured once, because the api takes its size (§8.4)
            DWORD bytes = sizeof(read);
            // the value is read straight into the machine word the theme is named by (§8.4)
            return RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_DWORD, nullptr, &read, &bytes) == ERROR_SUCCESS;
        }

        // read the appearance the system renders with, which is what a theme follows (§5.3, §8.4)
        SystemAppearance system_appearance_report() {
            SystemAppearance report;
            // the scheme is what the user picked for applications, which the personalize key names (§8.4)
            DWORD light = 1;
            // a key that is not there leaves the portable default in place, which names dark (§8.4)
            if (registry_dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", light))
                report.scheme = light != 0 ? ColorScheme::Light : ColorScheme::Dark;
            // the accent colour is the one the system highlights controls with, in the srgb space (§6.3)
            DWORD colorization = 0;
            // a colour the desktop composition did not pick leaves the portable accent in place (§8.4)
            if (registry_dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\DWM", L"ColorizationColor", colorization)) {
                // the high byte is the intensity the balancer joins the gradient with, which colour ignores (§6.3)
                colorization &= 0xFFFFFFu;
                // the low byte is red, the middle one green and the third blue, which a colourref reads as (§6.3)
                report.accent = Color::rgba(static_cast<float>(colorization & 0xFFu) / 255.0f, static_cast<float>((colorization >> 8) & 0xFFu) / 255.0f, static_cast<float>((colorization >> 16) & 0xFFu) / 255.0f, 1.0f);
            }
            return report;
        }

        // the extended styles each role adds, which a popup floats the composition layer by (§5.1)
        DWORD ex_style_for(const WindowOptions& options) {
            // a popup floats above the ordinary windows and never takes the foreground of its own show (§5.1)
            if (options.role == WindowRole::Popup)
                return WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
            return 0;
        }

        // place a popup under its owner's anchor and keep it inside the monitor work area (§5.1)
        void position_popup(HWND popup, HWND owner, Rect anchor, Size size);

        // reposition every shown popup an owner anchors, because a moved owner must not leave its list behind (§5.1)
        void reposition_owned_popups(HWND owner);

        // report whether a client point names a spot outside the window frame, which capture delivers (§5.1)
        bool point_is_outside(HWND window, int x, int y) {
            // the point is opened up to the screen space the frame is measured in (§5.1)
            POINT point{x, y};
            ClientToScreen(window, &point);
            // the frame is the outside the click falls into, which the client rect never bounds (§5.1)
            RECT frame{};
            GetWindowRect(window, &frame);
            return PtInRect(&frame, point) == FALSE;
        }

        // report whether a screen point lands on the trigger an owned popup hangs from (§5.1)
        bool point_in_anchor(const Win32WindowState* state, POINT screen) {
            // a popup without an owner has no anchor, so the point is never inside one (§5.1)
            if (state->owner == nullptr)
                return false;
            // the screen point is converted to the owner's client space, where the anchor was recorded (§5.1)
            POINT client = screen;
            if (!ScreenToClient(state->owner, &client))
                return false;
            // the owner dpi turns the logical anchor into the physical coordinates of that client space (§5.3, AD-27)
            const float scale = static_cast<float>(GetDpiForWindow(state->owner)) / 96.0f;
            // the anchor is half open, which is the rectangle the control itself hit tests (§8.3)
            const LONG left = static_cast<LONG>(std::lround(static_cast<double>(state->anchor.x) * scale));
            const LONG top = static_cast<LONG>(std::lround(static_cast<double>(state->anchor.y) * scale));
            const LONG right = static_cast<LONG>(std::lround(static_cast<double>(state->anchor.x + state->anchor.width) * scale));
            const LONG bottom = static_cast<LONG>(std::lround(static_cast<double>(state->anchor.y + state->anchor.height) * scale));
            // the point is inside when it falls in the half open trigger rectangle (§5.1)
            return client.x >= left && client.x < right && client.y >= top && client.y < bottom;
        }

        // the dismissal a popup shares between its outside click monitor and its activation loss (§5.1, §8.4)
        void dismiss_popup(Win32WindowState* state) {
            // an already hidden popup is not dismissed, so its hook never fires a second time (§5.1, §5.2)
            if (state->window == nullptr || !state->visible)
                return;
            // the capture is released first, because it is what holds the clicks outside the popup (§5.1)
            if (GetCapture() == state->window)
                ReleaseCapture();
            // the popup is hidden, which is the whole surface a dismissal takes off screen (§5.1)
            ShowWindow(state->window, SW_HIDE);
            state->visible = false;
            // the framework is told the popup closed, so its owner can re-arm and tear it down (§5.2)
            if (state->listener != nullptr)
                state->listener->on_platform_closed();
        }

        // the popup windows the process holds, which a deactivation is forwarded across (§5.1, §8.4)
        std::vector<std::weak_ptr<Win32WindowState>>& popup_registry() {
            // the registry is process wide, because one deactivation dismisses every popup (§5.1)
            static std::vector<std::weak_ptr<Win32WindowState>> popups;
            return popups;
        }

        // the deactivation of the application, which every shown popup is dismissed through (§5.1, §8.4)
        void dismiss_registered_popups() {
            // the registry is pruned first, because a popup whose framework window is gone is not dismissed (§5.1)
            auto& popups = popup_registry();
            popups.erase(std::remove_if(popups.begin(), popups.end(), [](const std::weak_ptr<Win32WindowState>& weak) { return weak.expired(); }), popups.end());
            // every remaining popup is dismissed through the shared path, so its hook fires exactly once (§5.1, §5.2)
            for (const std::weak_ptr<Win32WindowState>& weak : popups) {
                // a locked state names a live popup, which the shared pointer keeps alive across the hook (§4.2)
                if (std::shared_ptr<Win32WindowState> locked = weak.lock()) {
                    // the dismissal is the shared one, which the outside click also takes (§5.1)
                    dismiss_popup(locked.get());
                }
            }
        }

        // hand the capture to a shown popup after the opening press ends, so outside clicks still dismiss it (§5.1)
        void arm_owned_popup_capture(HWND owner) {
            // the registry is pruned first, because a popup whose framework window is gone holds no capture (§5.1)
            auto& popups = popup_registry();
            popups.erase(std::remove_if(popups.begin(), popups.end(), [](const std::weak_ptr<Win32WindowState>& weak) { return weak.expired(); }), popups.end());
            // the newest shown popup an owner anchors is the surface that receives the next outside press (§5.1)
            for (auto entry = popups.rbegin(); entry != popups.rend(); ++entry) {
                if (std::shared_ptr<Win32WindowState> locked = entry->lock(); locked && locked->popup && locked->visible && locked->owner == owner && locked->window != nullptr) {
                    // the popup takes the capture after the owner received the release, not before it (§5.1)
                    SetCapture(locked->window);
                    return;
                }
            }
        }

        // reposition every shown popup an owner anchors, because a moved owner must not leave its list behind (§5.1)
        void reposition_owned_popups(HWND owner) {
            // the registry is pruned first, because a popup whose framework window is gone is not moved (§5.1)
            auto& popups = popup_registry();
            popups.erase(std::remove_if(popups.begin(), popups.end(), [](const std::weak_ptr<Win32WindowState>& weak) { return weak.expired(); }), popups.end());
            // only a shown popup anchored to this owner follows it, because a hidden surface is placed on show (§5.1)
            for (const std::weak_ptr<Win32WindowState>& weak : popups) {
                if (std::shared_ptr<Win32WindowState> locked = weak.lock(); locked && locked->popup && locked->visible && locked->owner == owner)
                    position_popup(locked->window, owner, locked->anchor, locked->popup_size);
            }
        }

        // the press the dismissal swallowed is forwarded, because the click still reaches what the user clicked (§5.1)
        void forward_press(const POINT& screen, UINT code, WPARAM wparam, LPARAM lparam) {
            // the window the press names sits under the screen point, which the capture had held (§5.1)
            const HWND target = WindowFromPoint(screen);
            // a lookup that named nothing has no receiver, which is the quiet drop (§5.1)
            if (target == nullptr)
                return;
            // the forward reaches a window of this process, which is the only one a capture can take (§5.1)
            DWORD target_process = 0;
            GetWindowThreadProcessId(target, &target_process);
            // a press on another application is offered activation, which is the click-through macOS gives (§5.1)
            if (target_process != GetCurrentProcessId()) {
                // the top level window is what activation names, because the click may land on a child (§5.1)
                const HWND root = GetAncestor(target, GA_ROOT);
                // the system may refuse a forced activation, which is why the click is only offered (§5.1)
                SetForegroundWindow(root != nullptr ? root : target);
                return;
            }
            // the point is translated into the target's client space, because the popup and the target differ (§5.1)
            POINT target_point = screen;
            if (!ScreenToClient(target, &target_point))
                return;
            // the translated point is what the target's hit test reads, because the original was popup space (§5.1)
            const LPARAM translated = MAKELPARAM(target_point.x, target_point.y);
            // the message is posted, because a synchronous cross process send could block this thread (§5.1)
            (void)PostMessageW(target, code, wparam, translated);
        }

        // forward a click on the trigger instead of dismissing, so the trigger can toggle its own surface (§5.1)
        bool forward_anchor_press(Win32WindowState* state, HWND popup, const POINT& screen, UINT code, WPARAM wparam, LPARAM lparam) {
            // only a shown popup with a matching anchor takes this path (§5.1)
            if (!state->popup || !state->visible || !point_in_anchor(state, screen))
                return false;
            // the capture is released first, because the owner must receive the forwarded press (§5.1)
            if (GetCapture() == popup)
                ReleaseCapture();
            // the press is posted to the trigger, which is the control that owns the open list (§5.1)
            forward_press(screen, code, wparam, lparam);
            return true;
        }

        // the utf8 of one composition string the context carries, empty when it names none (§8.3)
        std::string composition_string(HIMC context, DWORD kind) {
            // the length is measured once in bytes, because the api answers the measure before the copy (§8.3)
            const LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
            // a string that is not there carries none, which the empty read reports (§8.3)
            if (bytes <= 0)
                return {};
            // the units are read into the measured span, because the marshaller keeps the utf16 form (§8.3)
            std::wstring wide(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(context, kind, wide.data(), bytes);
            return utf8_from_units(wide.data(), static_cast<int>(wide.size()));
        }

        // the window procedure that turns native messages into normalized events, which every window shares (§5.3)
        LRESULT CALLBACK window_proc(HWND window, UINT code, WPARAM wparam, LPARAM lparam) {
            // the create call carries the state through its creation struct, which is the only path to it (§5.3)
            if (code == WM_NCCREATE) {
                auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
                return DefWindowProcW(window, code, wparam, lparam);
            }
            // the state is the window's own, which the create message stored and every later message reads (§5.3)
            auto* state = reinterpret_cast<Win32WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            // a window with no state or no listener is past the framework, so the default window handles it (§4.2)
            if (state == nullptr || state->listener == nullptr)
                return DefWindowProcW(window, code, wparam, lparam);
            // the platform events are normalized below, one native message family at a time (§5.3, §8.3)
            switch (code) {
            case WM_WINDOWPOSCHANGED: {
                // a moved owner moves its transient surfaces, which win32 does not do for an owned window (§5.1)
                if (!state->popup)
                    reposition_owned_popups(window);
                // the default handling is kept, because the system still owns the window position state (§5.3)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_SIZE: {
                // a minimized window reports a zero client area, which is not a metrics change to raise (§5.3)
                if (wparam == SIZE_MINIMIZED)
                    return 0;
                // the framework receives the metrics the platform now reports (§5.3)
                state->listener->on_platform_resize(metrics_of(window));
                return 0;
            }
            case WM_DPICHANGED: {
                // the suggested rect is what the system sized the window for under the new dpi (AD-27)
                const auto* suggested = reinterpret_cast<const RECT*>(lparam);
                // the window is moved and resized in one call, because two of them would flash a frame (AD-27)
                SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
                // the scale change is a metrics change, because the pixel size moved with it (AD-27)
                state->listener->on_platform_resize(metrics_of(window));
                return 0;
            }
            case WM_CLOSE: {
                // the native close request asks the framework first, which may veto it (§5.2)
                if (state->listener->on_platform_close_request())
                    DestroyWindow(window);
                return 0;
            }
            case WM_DESTROY: {
                // the handle is dropped first, because a destroy of a recycled handle would hit another window (§4.2)
                state->window = nullptr;
                // the native window reports that it closed, which is the closed notification of §5.2
                state->listener->on_platform_closed();
                return 0;
            }
            case WM_MOUSEACTIVATE: {
                // a popup never takes activation from its owner, so the trigger stays focused (§5.1)
                if (state->popup)
                    return MA_NOACTIVATE;
                // every ordinary window keeps the system activation policy (§5.3)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_LBUTTONDOWN: {
                // a press a shown popup receives dismisses it, which the click outside the frame is (§5.1)
                if (state->popup && state->visible && point_is_outside(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) {
                    // the screen point is opened before the dismissal, which may tear the window down (§5.1)
                    POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                    ClientToScreen(window, &screen);
                    // a click on the trigger belongs to the control, which toggles the popup itself (§5.1)
                    if (forward_anchor_press(state, window, screen, code, wparam, lparam))
                        return 0;
                    // the popups the click missed are dismissed, which is what every monitor of the reference takes (§5.1)
                    dismiss_registered_popups();
                    // the press is forwarded, because the click still reaches whatever the user actually clicked (§5.1)
                    forward_press(screen, code, wparam, lparam);
                    return 0;
                }
                // the capture keeps a drag alive outside the window, which a press started inside continues (§8.3)
                SetCapture(window);
                // a popup opened from this press must not steal the capture before the matching release (§5.1)
                state->mouse_down = true;
                Event message;
                message.kind = EventKind::PointerDown;
                message.position = logical_from_client(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                state->listener->on_platform_event(message);
                return 0;
            }
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN: {
                // the other buttons dismiss through the same monitor, which is the outside click of §5.1
                if (state->popup && state->visible && point_is_outside(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) {
                    // the screen point is opened before the dismissal, which may tear the window down (§5.1)
                    POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                    ClientToScreen(window, &screen);
                    // the popups the click missed are dismissed, which is what every monitor of the reference takes (§5.1)
                    dismiss_registered_popups();
                    // the press is forwarded, because the click still reaches whatever the user actually clicked (§5.1)
                    forward_press(screen, code, wparam, lparam);
                    return 0;
                }
                // the press is answered with the default, because the framework maps a context menu elsewhere (§8.3)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_LBUTTONUP: {
                // the press is over before the event runs, so a popup opened during it may take the capture next (§5.1)
                state->mouse_down = false;
                // the capture is released on the button that took it, because no drag is left to continue (§8.3)
                if (GetCapture() == window)
                    ReleaseCapture();
                Event message;
                message.kind = EventKind::PointerUp;
                message.position = logical_from_client(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                state->listener->on_platform_event(message);
                // an owner that opened a popup on this press hands the capture to it after the release is delivered (§5.1)
                if (!state->popup)
                    arm_owned_popup_capture(window);
                return 0;
            }
            case WM_MOUSEMOVE: {
                Event message;
                message.kind = EventKind::PointerMove;
                message.position = logical_from_client(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                state->listener->on_platform_event(message);
                return 0;
            }
            case WM_MOUSEWHEEL: {
                // the wheel arrives in screen coordinates, which are converted to the client area it names (§8.3)
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &point);
                // the delta counts multiples of a notch, where a smaller one is a precision wheel's points (§8.3)
                const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                Event message;
                message.kind = EventKind::Wheel;
                message.position = logical_from_client(window, point.x, point.y);
                // a whole notch count is lines and anything finer is points, which the unit travels as (§8.3)
                message.precision = delta % WHEEL_DELTA == 0 ? ScrollPrecision::Line : ScrollPrecision::Point;
                // the scroll is notches for a line wheel and raw points for a precision one (§8.3)
                message.scroll = message.precision == ScrollPrecision::Line ? static_cast<float>(delta / WHEEL_DELTA) : static_cast<float>(delta);
                state->listener->on_platform_event(message);
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                Event message;
                message.kind = EventKind::KeyDown;
                message.key = key_from_wparam(wparam);
                message.modifiers = modifiers_from_keyboard();
                state->listener->on_platform_event(message);
                return 0;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                Event message;
                message.kind = EventKind::KeyUp;
                message.key = key_from_wparam(wparam);
                message.modifiers = modifiers_from_keyboard();
                state->listener->on_platform_event(message);
                return 0;
            }
            case WM_ACTIVATEAPP: {
                // losing activation is announced to the window that had it, and every popup dismisses on (§5.1, §8.4)
                if (wparam == FALSE)
                    dismiss_registered_popups();
                // the activation flow needs no other answer, which the default procedure carries (§5.3)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_IME_SETCONTEXT: {
                // the system composition window is off, the framework paints its own mark, the candidate stays (§8.3)
                lparam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
                // the other input ui stays, which is what the anchor the framework pushed drives (§8.3)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_IME_STARTCOMPOSITION: {
                // the composition opens the mark the composing events carry, which the end message closes (§8.3)
                state->composing = true;
                return 0;
            }
            case WM_IME_COMPOSITION: {
                // the input method is reached through the window, which the composition strings are read from (§8.3)
                HIMC context = ImmGetContext(window);
                // a window with no input method shapes nothing, so a composition without one is dropped (§8.3)
                if (context == nullptr)
                    return 0;
                // the committed string is what a finalised mark became, which a commit replaces the mark by (§8.3)
                if ((lparam & GCS_RESULTSTR) != 0) {
                    Event message;
                    message.kind = EventKind::TextInput;
                    message.text = composition_string(context, GCS_RESULTSTR);
                    // the framework is told, which a field inserts at its own caret (§8.3)
                    state->listener->on_platform_event(message);
                }
                // the in flight string is what a mark holds, which is what the field paints as it is (§8.3)
                if ((lparam & GCS_COMPSTR) != 0) {
                    Event message;
                    message.kind = EventKind::Composing;
                    message.text = composition_string(context, GCS_COMPSTR);
                    // the framework is told, which a field paints the mark its own way (§8.3)
                    state->listener->on_platform_event(message);
                }
                // the context is released, because the window holds it only for this call (§8.3)
                ImmReleaseContext(window, context);
                return 0;
            }
            case WM_IME_ENDCOMPOSITION: {
                // a composition that ended without a commit renounces its mark, which is reported empty (§8.3)
                if (state->composing) {
                    Event message;
                    message.kind = EventKind::Composing;
                    message.text = std::string();
                    state->listener->on_platform_event(message);
                }
                // the composition is closed, because the mark it carried is gone either way (§8.3)
                state->composing = false;
                return 0;
            }
            case WM_DWMCOLORIZATIONCOLORCHANGED: {
                // the accent or the scheme changing is an appearance change, which the framework is told of (§8.4)
                state->listener->on_platform_appearance_changed(system_appearance_report());
                // the colour needs no acknowledgement, so the default procedure carries the flow (§8.4)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_SETTINGCHANGE: {
                // a theme change arrives as a settings change the immersive colour set names (§8.4)
                const auto* names = lparam != 0 ? reinterpret_cast<const wchar_t*>(lparam) : nullptr;
                // the framework is told only of a real theme change, because an appearance event is one (§8.4)
                if (names != nullptr && wcscmp(names, L"ImmersiveColorSet") == 0)
                    state->listener->on_platform_appearance_changed(system_appearance_report());
                // the settings need no acknowledgement, which the default procedure carries (§5.3)
                return DefWindowProcW(window, code, wparam, lparam);
            }
            case WM_CHAR:
            case WM_SYSCHAR: {
                // the utf16 unit arrives split when a character needs a surrogate pair, so the halves join here (§8.3)
                const wchar_t unit = static_cast<wchar_t>(wparam);
                // a high surrogate waits for its low half, because half a character is not text (§8.3)
                if (unit >= 0xD800 && unit <= 0xDBFF) {
                    state->pending_surrogate = unit;
                    return 0;
                }
                // only a low surrogate completes the pending high half, because any other unit makes it a lone one (§8.3)
                const bool pair = state->pending_surrogate != 0 && unit >= 0xDC00 && unit <= 0xDFFF;
                wchar_t units[2] = {pair ? state->pending_surrogate : unit, pair ? unit : L'\0'};
                const int count = pair ? 2 : 1;
                // the pending half is spent either way, because a low surrogate or a plain unit ends the wait (§8.3)
                state->pending_surrogate = 0;
                Event message;
                message.kind = EventKind::TextInput;
                message.text = utf8_from_units(units, count);
                // units that convert to nothing carry no text, which is not an event a receiver can read (§8.3)
                if (message.text.empty())
                    return 0;
                state->listener->on_platform_event(message);
                return 0;
            }
            default:
                return DefWindowProcW(window, code, wparam, lparam);
            }
        }

        // register the window class once per process, because a second registration is refused (§5.3)
        bool register_window_class() {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            // the class redraws on either axis resize, because every resize repaints the whole surface anyway (§5.3)
            window_class.style = CS_HREDRAW | CS_VREDRAW;
            window_class.lpfnWndProc = &window_proc;
            // the arrow is the ordinary cursor, cast wide because this file builds without the unicode define (§5.3)
            window_class.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
            // the system window colour brushes the surface before a renderer presents over it (§5.3)
            window_class.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
            window_class.lpszClassName = WINDOW_CLASS;
            // a class that did not register leaves every window refused, which the factory reports as null (§5.3)
            if (RegisterClassExW(&window_class) != 0)
                return true;
            // a class already registered is the same one this process registered before, so it proceeds (§5.3)
            return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }

        // the window style the options describe, which mirrors the documented appkit mask one bit at a time (§5.1)
        DWORD style_for(const WindowOptions& options) {
            // a popup is a borderless transient surface, which win32 draws with no chrome at all (§5.1)
            if (options.role == WindowRole::Popup)
                return WS_OVERLAPPED | WS_POPUP;
            // every albor window is titled and closable, which is what the documented role mask asks for (§5.3)
            DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
            // a window the user may resize adds the thick frame and the maximize box (§5.1)
            if (options.resizable)
                style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            // a dialog is a secondary window, which win32 marks as minimizable for the same reason appkit does (§5.1)
            if (options.role == WindowRole::Dialog)
                style |= WS_MINIMIZEBOX;
            return style;
        }

        // the dpi a window is created under, which the system screen reports before any window exists (AD-27)
        UINT initial_dpi() {
            // the system dpi is the best answer before a window exists, which the first scale change corrects (AD-27)
            const UINT dpi = GetDpiForSystem();
            return dpi == 0 ? 96 : dpi;
        }
    } // namespace

    namespace
    {
        // place a popup under its owner's anchor and keep it inside the monitor work area (§5.1)
        void position_popup(HWND popup, HWND owner, Rect anchor_rect, Size size) {
            // an owner that is gone cannot place a child, so the cascade position stands (§5.1)
            if (owner == nullptr)
                return;
            // the owner dpi turns the logical anchor and popup extent into the physical frame (§5.3, AD-27)
            const float scale = static_cast<float>(GetDpiForWindow(owner)) / 96.0f;
            // the anchor is a client point, which the screen conversion needs before the popup is placed (§5.1)
            POINT anchor{static_cast<LONG>(std::lround(static_cast<double>(anchor_rect.x) * scale)), static_cast<LONG>(std::lround(static_cast<double>(anchor_rect.y) * scale))};
            // a failed conversion keeps the cascade position, which is a refused placement (§5.1)
            if (!ClientToScreen(owner, &anchor))
                return;
            // the popup extent follows the owner scale, because the options size is logical (§5.3, AD-27)
            const int width = static_cast<int>(std::lround(static_cast<double>(size.width) * scale));
            const int height = static_cast<int>(std::lround(static_cast<double>(size.height) * scale));
            // the monitor the owner occupies is the work area the popup must stay inside (§5.1)
            MONITORINFO monitor{};
            monitor.cbSize = sizeof monitor;
            if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor))
                return;
            // the popup hangs below the anchor first, which is the conventional dropdown position (§5.1)
            int x = static_cast<int>(anchor.x);
            int y = static_cast<int>(anchor.y) + static_cast<int>(std::lround(static_cast<double>(anchor_rect.height) * scale));
            // the popup flips above the anchor when the work area has no room below (§5.1)
            if (y + height > monitor.rcWork.bottom)
                y = static_cast<int>(anchor.y) - height;
            // the popup is clamped into the work area, so a trigger at an edge still shows the list (§5.1)
            x = std::clamp(x, static_cast<int>(monitor.rcWork.left), std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - width));
            y = std::clamp(y, static_cast<int>(monitor.rcWork.top), std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - height));
            // the frame is applied without activation, which keeps the owner focused (§5.1)
            SetWindowPos(popup, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
        }
    } // namespace

    std::unique_ptr<Win32PlatformWindow> Win32PlatformWindow::create(const WindowOptions& options) {
        // an option set the framework refuses is never handed to win32 (§5.1)
        if (!options.valid())
            return nullptr;
        // the window class is what every window is created from, and it exists once the first window is (§5.3)
        static const bool registered = register_window_class();
        if (!registered)
            return nullptr;
        // the initial size is logical pixels grown by the scale the screen reports (AD-27)
        const float scale = static_cast<float>(initial_dpi()) / 96.0f;
        RECT frame{0, 0, static_cast<LONG>(options.size.width * scale), static_cast<LONG>(options.size.height * scale)};
        // the frame is adjusted for the borders and title bar, because the options size the client area (§5.1)
        const DWORD style = style_for(options);
        AdjustWindowRectEx(&frame, style, FALSE, 0);
        // the state is shared with the window procedure, which reaches it through the window's own user data (§4.2)
        std::shared_ptr<Win32WindowState> state = std::make_shared<Win32WindowState>();
        // the creation style is kept, because a later resize has to adjust against the very frame this window has (§5.1)
        state->style = style;
        // the popup flag is kept, which is what the show path and the dismissal consult (§5.1)
        state->popup = options.role == WindowRole::Popup;
        // the owner and anchor are kept, because every dismissal needs to tell the trigger from the outside (§5.1)
        if (state->popup) {
            // the owner is the hwnd the options named, which is also passed as the create owner (§5.1)
            state->owner = options.native_parent.valid() ? static_cast<HWND>(const_cast<void*>(options.native_parent.object())) : nullptr;
            // the anchor is logical client space, which the dpi conversion reads on use (§5.1, AD-27)
            state->anchor = options.anchor;
            // the popup extent is logical too, because every placement scales it by the owner dpi (§5.3, AD-27)
            state->popup_size = options.size;
        }
        // a popup joins the registered set, which the deactivation message is forwarded across (§5.1, §8.4)
        if (state->popup)
            popup_registry().push_back(state);
        // the extended styles name the popup layer, which the composition floats over the ordinary windows (§5.1)
        const DWORD ex_style = ex_style_for(options);
        // a popup is owned by its trigger window, while an ordinary window keeps the system cascade (§5.1)
        HWND owner = options.native_parent.valid() ? static_cast<HWND>(const_cast<void*>(options.native_parent.object())) : nullptr;
        // the window is created at the system cascade, and a popup is placed against its anchor below (§5.3)
        HWND window = CreateWindowExW(ex_style, WINDOW_CLASS, wide_from_utf8(options.title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top, owner, nullptr, GetModuleHandleW(nullptr), state.get());
        // a window win32 refused leaves the factory reporting no window, which is the quiet refusal of §5.1
        if (window == nullptr)
            return nullptr;
        // the state now names its window, which is what the metrics and the handle read (§5.3)
        state->window = window;
        // a popup with an owner is placed under the anchor before it is ever shown (§5.1)
        if (state->popup && owner != nullptr)
            position_popup(window, owner, state->anchor, state->popup_size);
        // the framework shows the window itself, because the registry binds the listener before it appears (§5.3)
        return std::unique_ptr<Win32PlatformWindow>(new Win32PlatformWindow(std::move(state)));
    }

    Win32PlatformWindow::~Win32PlatformWindow() {
        // the listener is detached first, because a teardown message would otherwise reach a half dead object (§4.2)
        m_state->listener = nullptr;
        // the capture a popup took at show is released, because the teardown of the window ends it (§5.1)
        if (m_state->window != nullptr && GetCapture() == m_state->window)
            ReleaseCapture();
        // the native window is destroyed last, so its window procedure never sees a dead framework window (§4.2)
        if (m_state->window != nullptr) {
            DestroyWindow(m_state->window);
            m_state->window = nullptr;
        }
        m_state->frame_requested = false;
        m_state->visible = false;
    }

    void Win32PlatformWindow::set_listener(PlatformWindowListener* listener) {
        // the window procedure reads the listener through the state, which every message already shares (§5.3)
        m_state->listener = listener;
    }

    void Win32PlatformWindow::show() {
        // a window that was destroyed has nothing to show (§5.3)
        if (m_state->window == nullptr)
            return;
        // a popup is non activating, which is what the appkit show also avoids (§5.1)
        if (m_state->popup) {
            // the owner may have moved since the last show, so the anchor is placed again (§5.1)
            position_popup(m_state->window, m_state->owner, m_state->anchor, m_state->popup_size);
            ShowWindow(m_state->window, SW_SHOWNOACTIVATE);
            // the owner state tells whether this show lands inside the press that opened the popup (§5.1)
            bool owner_pressed = false;
            if (m_state->owner != nullptr) {
                const auto* owner_state = reinterpret_cast<Win32WindowState*>(GetWindowLongPtrW(m_state->owner, GWLP_USERDATA));
                owner_pressed = owner_state != nullptr && owner_state->mouse_down;
            }
            // a popup opened by a press waits for the owner release, while a keyboard open takes the capture now (§5.1)
            if (!owner_pressed)
                SetCapture(m_state->window);
        }
        else {
            ShowWindow(m_state->window, SW_SHOW);
            // the window is brought forward and focused, which is the foreground activation the appkit show does (§5.3)
            SetForegroundWindow(m_state->window);
            SetFocus(m_state->window);
        }
        m_state->visible = true;
    }

    void Win32PlatformWindow::hide() {
        // a window that was destroyed was never on screen (§5.3)
        if (m_state->window == nullptr)
            return;
        // the capture a popup took at show is released, because the window taken off screen holds nothing (§5.1)
        if (GetCapture() == m_state->window)
            ReleaseCapture();
        ShowWindow(m_state->window, SW_HIDE);
        m_state->visible = false;
    }

    void Win32PlatformWindow::set_title(std::string_view title) {
        // a window that was destroyed has no title bar to write into (§5.3)
        if (m_state->window == nullptr)
            return;
        SetWindowTextW(m_state->window, wide_from_utf8(title).c_str());
    }

    void Win32PlatformWindow::set_size(Size size) {
        // a window that was destroyed cannot be resized (§5.3)
        if (m_state->window == nullptr)
            return;
        // the requested size is the client area in logical pixels, which the frame grows around (§5.1, AD-27)
        const float scale = scale_of(m_state->window);
        RECT frame{0, 0, static_cast<LONG>(size.width * scale), static_cast<LONG>(size.height * scale)};
        // the frame is adjusted against the creation style, because that is the frame this window actually has (§5.1)
        AdjustWindowRectEx(&frame, m_state->style, FALSE, 0);
        // the origin is kept and no other window moves, so the window grows from the corner the user placed it at (§7.4)
        SetWindowPos(m_state->window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    WindowMetrics Win32PlatformWindow::metrics() const {
        // a window that was destroyed has no metrics to report (§5.3)
        if (m_state->window == nullptr)
            return WindowMetrics{};
        return metrics_of(m_state->window);
    }

    NativeWindowHandle Win32PlatformWindow::native_window_handle() const {
        // a window that was destroyed hands out the invalid handle, which a popup refuses (§5.1)
        if (m_state->window == nullptr)
            return NativeWindowHandle{};
        // the hwnd crosses as an opaque pointer, which no public header names (§3.5, AD-05)
        return NativeWindowHandle::from_object(reinterpret_cast<const void*>(m_state->window));
    }

    NativeWindowHandle Win32PlatformWindow::native_handle() const {
        // a window that was destroyed hands out the invalid handle, which a backend refuses (§3.5)
        if (m_state->window == nullptr)
            return NativeWindowHandle{};
        // the window crosses into the framework as an opaque pointer, which no win32 type may name (§3.5, AD-05)
        return NativeWindowHandle::from_object(reinterpret_cast<const void*>(m_state->window));
    }

    SystemAppearance Win32PlatformWindow::system_appearance() const {
        // the appearance is a system value, so it is read fresh on every ask rather than cached (§5.3, §8.4)
        return system_appearance_report();
    }

    void Win32PlatformWindow::request_frame() {
        // the pacing source is owned by the event loop, so a window only records that it wants a frame (§4.3)
        m_state->frame_requested = true;
    }

    void Win32PlatformWindow::set_text_anchor(Rect anchor) {
        // a window that was destroyed has no input method to anchor (§8.3)
        if (m_state->window == nullptr)
            return;
        // the anchor is stored, which is what the exclusion is only handled through (§8.3)
        m_state->text_anchor = anchor;
        m_state->anchor_valid = true;
        // the input method is the one the window holds, which a session without one is the portable no-op under (§8.3)
        HIMC context = ImmGetContext(m_state->window);
        if (context == nullptr)
            return;
        // the anchor is in logical pixels, which the client area is measured in physical ones (AD-27)
        const float scale = scale_of(m_state->window);
        // the exclusion frame is the physical rect the composition window must not cover (§8.3)
        CANDIDATEFORM form{};
        form.dwIndex = 0;
        form.dwStyle = CFS_EXCLUDE;
        form.ptCurrentPos.x = static_cast<LONG>(anchor.x * scale);
        form.ptCurrentPos.y = static_cast<LONG>(anchor.y * scale);
        form.rcArea.left = form.ptCurrentPos.x;
        form.rcArea.top = form.ptCurrentPos.y;
        form.rcArea.right = form.rcArea.left + static_cast<LONG>(anchor.width * scale);
        form.rcArea.bottom = form.rcArea.top + static_cast<LONG>(anchor.height * scale);
        // the composition window is placed away from the caret rect, which is the anchor's whole meaning (§8.3)
        ImmSetCandidateWindow(context, &form);
        // the context is released, because the window owns it only for this call (§8.3)
        ImmReleaseContext(m_state->window, context);
    }

    void Win32PlatformWindow::set_text_selection(TextInputRange selection) {
        // a window that was destroyed has no input method to address a document through (§8.3)
        if (m_state->window == nullptr)
            return;
        // the selection is stored, which is what a reconversion re-checks before it replaces a span (§8.3)
        m_state->text_selection = selection;
    }

    bool Win32PlatformWindow::frame_requested() const {
        // the flag is what the next pacing tick consults to decide whether to draw (§4.3)
        return m_state->frame_requested;
    }

    bool Win32PlatformWindow::visible() const {
        // the flag is the framework's record of what it asked win32 to do (§5.1)
        return m_state->visible;
    }

    void Win32PlatformWindow::close() {
        // a window that was never created has nothing to close (§5.2)
        if (m_state->window == nullptr)
            return;
        // the listener is detached first, because closing raises messages the framework is already past (§4.2)
        m_state->listener = nullptr;
        // the capture a popup took at show is released, because the closing of the window ends it (§5.1)
        if (GetCapture() == m_state->window)
            ReleaseCapture();
        // the native window is destroyed without asking the framework, because the framework initiated this (§5.2)
        DestroyWindow(m_state->window);
        m_state->window = nullptr;
        // a closed window has nothing left to draw (§4.3)
        m_state->frame_requested = false;
        m_state->visible = false;
    }

    Win32PlatformWindow::Win32PlatformWindow(std::shared_ptr<Win32WindowState> state) :
        m_state(std::move(state)) {}
} // namespace albor
