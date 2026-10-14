#include "platform/macos/clipboard.h"

#import <AppKit/AppKit.h>

#include <memory>
#include <string>
#include <string_view>

#include "core/clipboard.h"

namespace albor::macos
{
    namespace
    {
        // the pasteboard backed clipboard, which reaches appkit and so lives behind the portable surface (§3.5, AD-05)
        class PasteboardClipboard final : public Clipboard
        {
        public:
            // the system pasteboard is looked up on each call, because the framework owns no appkit object here (§5.3)
            [[nodiscard]] NSPasteboard* board() const {
                // a headless process has no pasteboard, which is what a null board reports (§5.3)
                return [NSPasteboard generalPasteboard];
            }

            // override: replace what the pasteboard carries with the text given, which is what a copy is (§3.5)
            void set_text(std::string_view text) override {
                // a board that is gone cannot be written to, so a copy is dropped rather than crashing (§5.3)
                NSPasteboard* pasteboard = board();
                if (pasteboard == nil)
                    return;
                // the pasteboard is cleared first, because a copy replaces rather than adds (§3.5)
                [pasteboard clearContents];
                // the string flavour is the one every application agrees on, so a copy leaves it readable (§3.5)
                NSString* value = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
                // a string the utf8 could not encode carries nothing, so the pasteboard is left empty (§3.5)
                if (value == nil)
                    return;
                [pasteboard setString:value forType:NSPasteboardTypeString];
            }

            // override: hand out the text the pasteboard carries, which is empty when it carries none (§3.5)
            [[nodiscard]] std::string text() const override {
                // a board that is gone carries nothing, which is what an empty string reports (§5.3)
                NSPasteboard* pasteboard = board();
                if (pasteboard == nil)
                    return {};
                // the string flavour is read directly, because asking for a string of any type may find nothing (§3.5)
                NSString* value = [pasteboard stringForType:NSPasteboardTypeString];
                // a pasteboard with no string has nothing to hand out (§3.5)
                if (value == nil)
                    return {};
                // the utf8 bytes are read first, because a string may carry an encoding the framework cannot (§3.5)
                const char* utf8 = value.UTF8String;
                // bytes that are absent carry nothing, which an empty string reports rather than a crash (§3.5)
                if (utf8 == nullptr)
                    return {};
                // the bytes become the framework's own string, because the appkit object does not outlive the call (§3.5)
                return std::string(utf8);
            }

            // override: report whether the pasteboard currently carries text, which a paste checks first (§3.5)
            [[nodiscard]] bool has_text() const override {
                // the string flavour is read directly, because an empty string is not the same as no string (§3.5)
                NSPasteboard* pasteboard = board();
                if (pasteboard == nil)
                    return false;
                return [pasteboard stringForType:NSPasteboardTypeString] != nil;
            }

            // override: report whether the surface can reach a clipboard, which a caller checks before copying (§3.5)
            [[nodiscard]] bool ready() const override {
                // the general pasteboard is the one the process writes through, so its absence is the whole answer (§5.3)
                return board() != nil;
            }
        };
    } // namespace

    void install_macos_clipboard() {
        // the factory builds the pasteboard surface, so linking the adapter is the selection (§3.5, AD-16)
        set_clipboard_factory([] { return std::make_unique<PasteboardClipboard>(); });
    }
} // namespace albor::macos
