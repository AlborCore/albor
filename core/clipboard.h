#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace albor
{
    // the clipboard a control copies and pastes through, which the platform adapter implements (§3.5, AD-05)
    class Clipboard
    {
    public:
        virtual ~Clipboard() = default;

        // replace what the clipboard carries with the text given, which is what a copy is (§3.5)
        virtual void set_text(std::string_view text) = 0;

        // hand out the text the clipboard carries, which is empty when it carries none (§3.5)
        [[nodiscard]] virtual std::string text() const = 0;

        // report whether the clipboard currently carries text, which a paste checks before it replaces a selection (§3.5)
        [[nodiscard]] virtual bool has_text() const = 0;

        // report whether the surface can reach a clipboard at all, which a caller checks before it offers copy (§3.5)
        [[nodiscard]] virtual bool ready() const = 0;
    };

    // the signature a clipboard implements to build itself (§3.5)
    using ClipboardFactory = std::function<std::unique_ptr<Clipboard>()>;

    // the state the installed factory lives in, heap held behind a constant initialized pointer (§3.5)
    struct ClipboardFactoryState
    {
        // the installed factory, which is empty until a platform adapter installs one (§3.5)
        ClipboardFactory factory;

        // the clipboard the process copies through, which is built once from the factory (§3.5)
        std::unique_ptr<Clipboard> held;
    };

    // install the clipboard the framework copies through, which a platform adapter calls once at load (§3.5)
    void set_clipboard_factory(ClipboardFactory factory);

    // hand out the installed factory, which is empty until a platform adapter installs one (§3.5)
    [[nodiscard]] const ClipboardFactory& clipboard_factory();

    // build the installed clipboard, returning null when no platform adapter is installed (§3.5)
    [[nodiscard]] std::unique_ptr<Clipboard> create_clipboard();

    // hand out the process clipboard, which is what a control copies through by default (§3.5)
    [[nodiscard]] Clipboard* process_clipboard();
} // namespace albor
