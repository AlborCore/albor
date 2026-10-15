#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "commands/shortcut.h"
#include "commands/undo_command.h"
#include "commands/undo_stack.h"

namespace albor
{
    // the services an application's commands share, which indexes them without keeping any alive (§18.1)
    class CommandRegistry
    {
    public:
        // build an empty registry, which an application fills by declaring commands (§18.1)
        CommandRegistry();

        CommandRegistry(const CommandRegistry&) = delete;
        CommandRegistry& operator=(const CommandRegistry&) = delete;
        CommandRegistry(CommandRegistry&&) = delete;
        CommandRegistry& operator=(CommandRegistry&&) = delete;
        ~CommandRegistry();

        // declare a command, which the caller owns and the registry only indexes for lookup (§18.1)
        [[nodiscard]] std::unique_ptr<Command> declare(std::string explicit_id, std::string label = {});

        // index a command the caller already owns, which binds it to this registry's history and selection (§18.1)
        void join(Command& command);

        // drop a command from the index, which its own destructor calls so no entry is ever left dangling (§18.1)
        void leave(Command& command);

        // find a command by the author assigned id, which is the only selector automation may use (§18.1, AD-11)
        [[nodiscard]] Command* find(std::string_view explicit_id);

        // find a command by its runtime identity, which the accelerator table indexes on (§18.1)
        [[nodiscard]] Command* find(CommandId id);

        // find the command an accelerator names, which is where a key press is routed (§18.5)
        [[nodiscard]] Command* find_shortcut(const Shortcut& shortcut);

        // hand out every indexed command in join order, which is what a menu presents (§18.1)
        [[nodiscard]] const std::vector<Command*>& commands() const;

        // hand out how many commands are declared, which a registry test asserts on (§18.1)
        [[nodiscard]] std::size_t size() const;

        // hand out the history every reversal is recorded on, which is one per application (§18.4)
        [[nodiscard]] UndoStack& undo_stack();

        // hand out the undo command, which is declared on first use and publishes the stack's own answer (§18.4)
        [[nodiscard]] UndoCommand& undo_command();

        // hand out the redo command, which is declared on first use and publishes the stack's own answer (§18.4)
        [[nodiscard]] RedoCommand& redo_command();

        // hand out the accelerator table, which a window routes keys through (§18.5)
        [[nodiscard]] ShortcutTable& shortcuts();

        // install where the current selection is read from, which a view owns and the registry lends out (§18.3)
        void set_selection_source(SelectionSource source);

        // resolve the current selection, which is the empty span while no source is installed (§18.3)
        [[nodiscard]] SelectionSpan selection() const;

    private:
        // refresh the undo and redo commands, which is what the stack calls whenever it moves (§18.4)
        void refresh_history_commands();

        // the indexed commands, which the caller owns and the registry only borrows for lookup (§18.1)
        std::vector<Command*> m_commands;

        // the author assigned ids, which are the only selectors automation is allowed to use (§18.1, AD-11)
        std::map<std::string, CommandId, std::less<>> m_by_explicit_id;

        // the history every reversal is recorded on, which the undo and redo commands read (§18.4)
        UndoStack m_undo_stack;

        // the undo command, which does not exist until an application asks for it (§18.4)
        std::optional<UndoCommand> m_undo_command;

        // the redo command, which does not exist until an application asks for it (§18.4)
        std::optional<RedoCommand> m_redo_command;

        // the accelerator table, which a window routes keys through (§18.5)
        ShortcutTable m_shortcuts;

        // where the current selection is read from, which the registry lends to every selection scoped command (§18.3)
        SelectionSource m_selection_source;
    };
} // namespace albor
