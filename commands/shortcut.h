#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "commands/command_id.h"
#include "ui/ui_event.h"

namespace albor
{
    // one normalized accelerator, which is a key plus the modifiers it requires (§18.5)
    struct Shortcut
    {
        // the key the accelerator names, which is the extended portable set of §8.3 (§18.5)
        Key key = Key::None;

        // the modifiers the accelerator requires, which distinguishes cmd S from plain S (§18.5)
        Modifiers modifiers{};

        // report whether the accelerator carries no modifier, which routes it after the focused node (§18.5)
        [[nodiscard]] bool plain() const { return modifiers.none(); }

        // compare two accelerators field by field, which the table keys on
        friend bool operator==(const Shortcut& lhs, const Shortcut& rhs) = default;
    };

    // fold an accelerator into one value, which is the whole key the table indexes on (§18.5)
    [[nodiscard]] constexpr std::uint64_t shortcut_key(Key key, Modifiers modifiers) {
        // the four modifier flags fold into the low nibble, so the whole accelerator is one comparable value (§18.5)
        const std::uint64_t flags = (static_cast<std::uint64_t>(modifiers.shift) << 3) | (static_cast<std::uint64_t>(modifiers.control) << 2) | (static_cast<std::uint64_t>(modifiers.option) << 1) | static_cast<std::uint64_t>(modifiers.command);
        // the key occupies the bits above the flags, so two accelerators can never collide (§18.5)
        return (static_cast<std::uint64_t>(key) << 4) | flags;
    }

    // the table a window routes keys through, which maps an accelerator onto the command it invokes (AD-15, §18.5)
    class ShortcutTable
    {
    public:
        // bind an accelerator to a command, which replaces any binding the same accelerator already had (§18.5)
        void bind(Shortcut shortcut, CommandId command);

        // remove an accelerator binding, which reports whether one was there to remove (§18.5)
        [[nodiscard]] bool unbind(Shortcut shortcut);

        // report whether an accelerator is bound at all, which a routing test asserts on (§18.5)
        [[nodiscard]] bool bound(Shortcut shortcut) const;

        // hand out the command an accelerator names, where zero means it names none (§18.5)
        [[nodiscard]] CommandId lookup(Shortcut shortcut) const;

        // hand out every accelerator bound to a command, which is what a menu presents beside its label (§18.5)
        [[nodiscard]] std::vector<Shortcut> for_command(CommandId command) const;

        // hand out the text an accelerator presents, which is a portable spelling a platform may replace (§18.5)
        [[nodiscard]] std::string display(Shortcut shortcut) const;

        // hand out how many accelerators are bound, which is a routing test asserts on (§18.5)
        [[nodiscard]] std::size_t size() const;

        // drop every binding, which is what a window teardown requires (§18.5)
        void clear();

    private:
        // the command each bound accelerator names, keyed on the folded accelerator (§18.5)
        std::map<std::uint64_t, CommandId> m_by_key;

        // the accelerators each command owns, which is the reverse index a menu presentation walks (§18.5)
        std::map<CommandId, std::vector<Shortcut>> m_by_command;
    };

    // hand out the portable spelling of a key, which the accelerator text is built from (§18.5)
    [[nodiscard]] std::string key_label(Key key);

    // hand out the portable spelling of one modifier, which the accelerator text is built from (§18.5)
    [[nodiscard]] std::string modifier_label(Modifiers modifiers);
} // namespace albor
