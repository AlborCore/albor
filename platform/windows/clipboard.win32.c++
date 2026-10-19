#include "platform/windows/clipboard.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "core/clipboard.h"
#include "platform/windows/text_encoding.h"

namespace albor::windows
{
    namespace
    {
        // the host window class the clipboard is owned through, which is registered once per process (§5.3)
        constexpr const wchar_t* HOST_CLASS = L"AlborClipboardHost";

        // the window procedure the host window carries, which the clipboard api drives nothing through (§5.3)
        LRESULT CALLBACK host_proc(HWND window, UINT code, WPARAM wparam, LPARAM lparam) {
            // every message is handled by the default procedure, because the surface only carries ownership (§5.3)
            return DefWindowProcW(window, code, wparam, lparam);
        }

        // the message only window the clipboard is opened over, which is retried while it refuses (§5.3)
        HWND clipboard_host() {
            // a host that came up is the one every call reuses, because the clipboard is opened over it (§5.3)
            static HWND host = nullptr;
            if (host != nullptr)
                return host;
            // the class exists before the window does, and a refusal means the first registration already made it (§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = &host_proc;
            window_class.lpszClassName = HOST_CLASS;
            // a class that did not register is checked against the refusal that names an existing one (§5.3)
            if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return nullptr;
            // the window is message only, and a failure stays uncached, so a later call retries (§5.3)
            host = CreateWindowExW(0, HOST_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
            return host;
        }

        // the clipboard surface a copy and a paste reach, which reaches win32 behind the portable api (§3.5, AD-05)
        class Win32Clipboard final : public Clipboard
        {
        public:
            // override: replace what the clipboard carries with the text given, which is what a copy is (§3.5)
            void set_text(std::string_view text) override {
                // a host the process could not create cannot own the clipboard, so a copy is dropped (§5.3)
                const HWND host = clipboard_host();
                if (host == nullptr)
                    return;
                // the clipboard is opened before it is written, because win32 serializes the access (§3.5)
                if (!OpenClipboard(host))
                    return;
                // the clipboard is emptied first, because a copy replaces rather than adds (§3.5)
                EmptyClipboard();
                // the text becomes the wide form the clipboard stores, which is the format every app agrees on (§3.5)
                const std::wstring wide = wide_from_utf8(text);
                // units that did not convert leave the clipboard empty, which is what a refused copy is (§3.5)
                if (wide.empty()) {
                    CloseClipboard();
                    return;
                }
                // the block is allocated moveable, which is the form the clipboard takes ownership of (§3.5)
                HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
                // an allocation that failed leaves the clipboard empty, because the handover carries nothing (§3.5)
                if (block == nullptr) {
                    CloseClipboard();
                    return;
                }
                // the units are written while the block is locked, which is what the handover carries (§3.5)
                if (auto* units = static_cast<wchar_t*>(GlobalLock(block)); units != nullptr) {
                    (void)memcpy(units, wide.data(), wide.size() * sizeof(wchar_t));
                    units[wide.size()] = L'\0';
                    (void)GlobalUnlock(block);
                }
                // a handover that failed frees the block again, because the ownership never moved (§3.5)
                if (SetClipboardData(CF_UNICODETEXT, block) == nullptr)
                    GlobalFree(block);
                // the clipboard is closed on every path, because one left open blocks every other copy (§3.5)
                CloseClipboard();
            }

            // override: hand out the text the clipboard carries, which is empty when it carries none (§3.5)
            [[nodiscard]] std::string text() const override {
                // the format check answers before the clipboard is opened, which skips the open bracket (§3.5)
                if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
                    return {};
                // a host the process could not create cannot reach the clipboard, which an empty read reports (§5.3)
                const HWND host = clipboard_host();
                if (host == nullptr || !OpenClipboard(host))
                    return {};
                // the data handle is read while the clipboard is open, because it weakens outside the bracket (§3.5)
                const HGLOBAL block = static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
                // a shift between the check and the read can empty the clipboard, which reads as nothing (§3.5)
                if (block == nullptr) {
                    CloseClipboard();
                    return {};
                }
                // the units are locked while the copy is made, because the system may free what was locked (§3.5)
                const auto* units = static_cast<const wchar_t*>(GlobalLock(block));
                // a block that would not lock carries nothing, which an empty read reports (§3.5)
                if (units == nullptr) {
                    CloseClipboard();
                    return {};
                }
                // length excludes the terminator, which the clipboard stores after the units it hands over (§3.5)
                std::size_t length = GlobalSize(block) / sizeof(wchar_t);
                // a terminator shortens the count, because the units end where it stands (§3.5)
                while (length > 0 && units[length - 1] == L'\0')
                    length--;
                // the bytes are converted before the unlock, because the block caps the range the read walks (§3.5)
                const std::string text = utf8_from_units(units, static_cast<int>(length));
                (void)GlobalUnlock(block);
                // the clipboard is closed on every path, because one left open blocks every other copy (§3.5)
                CloseClipboard();
                return text;
            }

            // override: report whether the clipboard currently carries text, which a paste checks first (§3.5)
            [[nodiscard]] bool has_text() const override {
                // the format is readable without opening, which is what the availability call is for (§3.5)
                return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
            }

            // override: report whether the surface can reach a clipboard, which a caller checks before copying (§3.5)
            [[nodiscard]] bool ready() const override {
                // the host window is the whole answer, because it is what the clipboard is opened over (§5.3)
                return clipboard_host() != nullptr;
            }
        };
    } // namespace

    void install_windows_clipboard() {
        // the factory builds the win32 surface, so linking the adapter is the selection (§3.5, AD-16)
        set_clipboard_factory([] { return std::make_unique<Win32Clipboard>(); });
    }
} // namespace albor::windows
