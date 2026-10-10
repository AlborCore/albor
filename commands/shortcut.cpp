#include "commands/shortcut.h"

#include <utility>

namespace albor
{
    namespace
    {
        // the letter the alphabet block of the key enum starts at, which is what an offset is measured from (§18.5)
        constexpr int LETTER_BASE = static_cast<int>(Key::A);

        // the digit the numeric block of the key enum starts at, which is what an offset is measured from (§18.5)
        constexpr int DIGIT_BASE = static_cast<int>(Key::Zero);

        // the function key the numbered block of the key enum starts at, which is what an offset is measured from (§18.5)
        constexpr int FUNCTION_BASE = static_cast<int>(Key::F1);

        // the separator between the parts of an accelerator, which is the portable spelling of the platform glyph (§18.5)
        constexpr const char* SEPARATOR = "+";
    } // namespace

    void ShortcutTable::bind(Shortcut shortcut, CommandId command) {
        // an accelerator that names no key binds nothing, because no key can ever match it (§18.5)
        if (shortcut.key == Key::None)
            return;
        // an accelerator that names no command binds nothing, because routing it would reach no action (§18.5)
        if (!is_valid(command))
            return;
        // a rebinding of the same accelerator releases its previous owner, which a menu would otherwise still show (§18.5)
        (void)unbind(shortcut);
        // the accelerator is indexed by its folded value, which is the whole lookup key (§18.5)
        m_by_key.insert_or_assign(shortcut_key(shortcut.key, shortcut.modifiers), command);
        // the reverse index records the accelerator against its new owner, which is what a menu walks (§18.5)
        m_by_command[command].push_back(shortcut);
    }

    bool ShortcutTable::unbind(Shortcut shortcut) {
        // the accelerator is looked up by its folded value, which is the whole lookup key (§18.5)
        const auto found = m_by_key.find(shortcut_key(shortcut.key, shortcut.modifiers));
        // an accelerator that binds nothing reports that there was nothing to remove (§18.5)
        if (found == m_by_key.end())
            return false;
        // the previous owner is taken from the forward index, which is where the command lives (§18.5)
        const CommandId command = found->second;
        m_by_key.erase(found);
        // the reverse index entry for that owner is located, so the accelerator leaves it too (§18.5)
        const auto owned = m_by_command.find(command);
        if (owned == m_by_command.end())
            return true;
        // the accelerator is removed from the owner's list in place, because a menu presents them in bind order (§18.5)
        for (auto entry = owned->second.begin(); entry != owned->second.end(); ++entry) {
            if (*entry == shortcut) {
                owned->second.erase(entry);
                break;
            }
        }
        // an owner left with no accelerator is dropped, so the reverse index never reports an empty owner (§18.5)
        if (owned->second.empty())
            m_by_command.erase(owned);
        return true;
    }

    bool ShortcutTable::bound(Shortcut shortcut) const { return m_by_key.find(shortcut_key(shortcut.key, shortcut.modifiers)) != m_by_key.end(); }

    CommandId ShortcutTable::lookup(Shortcut shortcut) const {
        const auto found = m_by_key.find(shortcut_key(shortcut.key, shortcut.modifiers));
        // an accelerator that binds nothing names no command, which is the sentinel rather than a default (§18.5)
        if (found == m_by_key.end())
            return invalid_command_id();
        return found->second;
    }

    std::vector<Shortcut> ShortcutTable::for_command(CommandId command) const {
        const auto found = m_by_command.find(command);
        // a command with no accelerator presents none, which is an empty list rather than a failure (§18.5)
        if (found == m_by_command.end())
            return {};
        return found->second;
    }

    std::string ShortcutTable::display(Shortcut shortcut) const {
        // an accelerator that names no key presents nothing, because there is nothing to spell (§18.5)
        if (shortcut.key == Key::None)
            return {};
        // the modifiers come first, which is the order every platform convention writes them in (§18.5)
        std::string text = modifier_label(shortcut.modifiers);
        // an accelerator with no modifiers starts with the key, so no separator is written first (§18.5)
        if (!text.empty())
            text += SEPARATOR;
        // the key spelling closes the accelerator, which is what a menu presents beside its label (§18.5)
        text += key_label(shortcut.key);
        return text;
    }

    std::size_t ShortcutTable::size() const { return m_by_key.size(); }

    void ShortcutTable::clear() {
        // the forward index is emptied, which is what stops any accelerator from routing (§18.5)
        m_by_key.clear();
        // the reverse index is emptied with it, which is what stops a menu presenting a dead accelerator (§18.5)
        m_by_command.clear();
    }

    std::string key_label(Key key) {
        // the letters run consecutively in the enum, so the spelling is the first letter plus the offset (§18.5)
        if (key >= Key::A && key <= Key::Z) {
            const int offset = static_cast<int>(key) - LETTER_BASE;
            return std::string(1, static_cast<char>('A' + offset));
        }
        // the digits run consecutively too, so the spelling is the first digit plus the offset (§18.5)
        if (key >= Key::Zero && key <= Key::Nine) {
            const int offset = static_cast<int>(key) - DIGIT_BASE;
            return std::string(1, static_cast<char>('0' + offset));
        }
        // the function keys run consecutively, so the spelling is the prefix plus the offset (§18.5)
        if (key >= Key::F1 && key <= Key::F12) {
            const int offset = static_cast<int>(key) - FUNCTION_BASE + 1;
            return std::string("F") + std::to_string(offset);
        }
        switch (key) {
        case Key::Tab:
            return "Tab";
        case Key::Return:
            return "Return";
        case Key::Escape:
            return "Esc";
        case Key::ArrowLeft:
            return "Left";
        case Key::ArrowRight:
            return "Right";
        case Key::ArrowUp:
            return "Up";
        case Key::ArrowDown:
            return "Down";
        case Key::Space:
            return "Space";
        case Key::Delete:
            return "Delete";
        // an accelerator never names no key, so the sentinel has no spelling (§18.5)
        case Key::None:
        default:
            return {};
        }
    }

    std::string modifier_label(Modifiers modifiers) {
        // the parts accumulate in the order every platform convention writes them in (§18.5)
        std::vector<std::string> parts;
        // each held modifier contributes one part, so an unmodified accelerator has none (§18.5)
        if (modifiers.control)
            parts.emplace_back("Ctrl");
        if (modifiers.option)
            parts.emplace_back("Alt");
        if (modifiers.shift)
            parts.emplace_back("Shift");
        if (modifiers.command)
            parts.emplace_back("Cmd");
        // the parts are joined by the separator, which is what a consumer writes between them (§18.5)
        std::string text;
        for (const std::string& part : parts) {
            // a separator only appears between two parts, never before the first or after the last (§18.5)
            if (!text.empty())
                text += SEPARATOR;
            text += part;
        }
        return text;
    }
} // namespace albor
