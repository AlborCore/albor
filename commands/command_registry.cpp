#include "commands/command_registry.h"

#include <algorithm>
#include <utility>

namespace albor
{
    CommandRegistry::CommandRegistry() {
        // the stack notifies this registry, which is what keeps the undo and redo commands true without polling (§18.4)
        m_undo_stack.add_change_handler([this] { refresh_history_commands(); });
    }

    CommandRegistry::~CommandRegistry() {
        // borrowed commands are detached first, so none calls back into a registry already gone (§18.1)
        for (Command* command : m_commands)
            command->m_registry = nullptr;
        // the history commands are released before the stack, because each of them observes the stack (§18.4)
        m_redo_command.reset();
        // the undo command is released for the same reason, because it observes the stack as well (§18.4)
        m_undo_command.reset();
        // the index is dropped, because the commands it held belong to their callers (§18.1)
        m_commands.clear();
    }

    std::unique_ptr<Command> CommandRegistry::declare(std::string explicit_id, std::string label) {
        // the command is built first, because joining it is what puts it in the index (§18.1)
        auto command = std::make_unique<Command>(std::move(explicit_id), std::move(label));
        // the join binds the history and the selection and records the borrowed pointer (§18.1, §18.4)
        join(*command);
        // ownership hands back to the caller, which decides how long the command lives (§18.1)
        return command;
    }

    void CommandRegistry::join(Command& command) {
        // a command already indexed here is left alone, because a second entry would present it twice (§18.1)
        if (command.m_registry == this)
            return;
        // a command indexed somewhere else leaves that registry first, so it is never in two (§18.1)
        if (command.m_registry != nullptr)
            command.m_registry->leave(command);
        // the reversal is recorded on this registry's history, which is what makes one invocation one entry (§18.4)
        command.bind_undo_stack(&m_undo_stack);
        // the current selection is resolved through this registry's source, which no other command shares (§18.3)
        command.bind_selection_source(&m_selection_source);
        // the command points back so its own destructor can leave the index (§18.1)
        command.m_registry = this;
        // an author assigned id is the only selector automation may use, so it alone enters the id index (§18.1, AD-11)
        if (command.identified())
            m_by_explicit_id.insert_or_assign(command.explicit_id(), command.id());
        // the command joins the indexed set in join order, which is the order a menu presents (§18.1)
        m_commands.push_back(&command);
    }

    void CommandRegistry::leave(Command& command) {
        // the borrowed pointer is dropped, which is what stops a destroyed command being handed out (§18.1)
        m_commands.erase(std::remove(m_commands.begin(), m_commands.end(), &command), m_commands.end());
        // the id index is cleared too, but only for a command that carried an author assigned id (§18.1, AD-11)
        if (command.identified())
            m_by_explicit_id.erase(command.explicit_id());
        // the back reference is cleared, so a later join to another registry starts clean (§18.1)
        command.m_registry = nullptr;
    }

    Command* CommandRegistry::find(std::string_view explicit_id) {
        // the author assigned id is looked up exactly, because a prefix or a case fold is not a selector (§18.1, AD-11)
        const auto found = m_by_explicit_id.find(explicit_id);
        // an id the author never assigned names no command, which a refusal states rather than guessing (§18.1, AD-11)
        if (found == m_by_explicit_id.end())
            return nullptr;
        return find(found->second);
    }

    Command* CommandRegistry::find(CommandId id) {
        for (Command* command : m_commands) {
            // the runtime identity is a correlation hint, so a match here is a lookup and never a selector (§18.1, AD-11)
            if (command->id() == id)
                return command;
        }
        return nullptr;
    }

    Command* CommandRegistry::find_shortcut(const Shortcut& shortcut) {
        // the accelerator is folded to the identity the table indexed, which is where the route ends (§18.5)
        return find(m_shortcuts.lookup(shortcut));
    }

    const std::vector<Command*>& CommandRegistry::commands() const { return m_commands; }

    std::size_t CommandRegistry::size() const { return m_commands.size(); }

    UndoStack& CommandRegistry::undo_stack() { return m_undo_stack; }

    UndoCommand& CommandRegistry::undo_command() {
        // the command is declared once, because a second one would publish a second answer (§18.4)
        if (!m_undo_command.has_value())
            m_undo_command.emplace(m_undo_stack);
        return *m_undo_command;
    }

    RedoCommand& CommandRegistry::redo_command() {
        // the command is declared once, because a second one would publish a second answer (§18.4)
        if (!m_redo_command.has_value())
            m_redo_command.emplace(m_undo_stack);
        return *m_redo_command;
    }

    ShortcutTable& CommandRegistry::shortcuts() { return m_shortcuts; }

    void CommandRegistry::set_selection_source(SelectionSource source) { m_selection_source = std::move(source); }

    SelectionSpan CommandRegistry::selection() const {
        // a registry with no installed source has no selection, which is the empty span (§18.3)
        if (!m_selection_source)
            return empty_selection();
        return m_selection_source();
    }

    void CommandRegistry::refresh_history_commands() {
        // the undo command is refreshed only once it exists, because an application may declare no undo (§18.4)
        if (m_undo_command.has_value())
            m_undo_command->refresh();
        // the redo command is refreshed only once it exists, for the same reason (§18.4)
        if (m_redo_command.has_value())
            m_redo_command->refresh();
    }
} // namespace albor
