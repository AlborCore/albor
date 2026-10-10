#include "commands/command_registry.h"

#include <utility>

namespace albor
{
    CommandRegistry::CommandRegistry() {
        // the stack notifies this registry, which is what keeps the undo and redo commands true without polling (§18.4)
        m_undo_stack.add_change_handler([this] { refresh_history_commands(); });
    }

    CommandRegistry::~CommandRegistry() {
        // the history commands are released before the stack, because each of them observes the stack (§18.4)
        m_redo_command.reset();
        // the undo command is released for the same reason, because it observes the stack as well (§18.4)
        m_undo_command.reset();
        // the commands go before the stack, because a command records its reversal on it (§18.4)
        m_commands.clear();
    }

    Command& CommandRegistry::declare(std::string explicit_id, std::string label) {
        // the command is built first, because its identity is what the index below records (§18.1)
        auto command = std::make_unique<Command>(std::move(explicit_id), std::move(label));
        // the reversal is recorded on this registry's history, which is what makes one invocation one entry (§18.4)
        command->bind_undo_stack(&m_undo_stack);
        // the current selection is resolved through this registry's source, which no other command shares (§18.3)
        command->bind_selection_source(&m_selection_source);
        // the command is owned by the registry, so an application never holds the ownership (§18.1)
        Command& reference = *command;
        // an author assigned id is the only selector automation may use, so it alone enters the index (§18.1, AD-11)
        if (reference.identified())
            m_by_explicit_id.insert_or_assign(reference.explicit_id(), reference.id());
        // the command joins the declared set in declaration order, which is the order a menu presents (§18.1)
        m_commands.push_back(std::move(command));
        return reference;
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
        for (const std::unique_ptr<Command>& command : m_commands) {
            // the runtime identity is a correlation hint, so a match here is a lookup and never a selector (§18.1, AD-11)
            if (command->id() == id)
                return command.get();
        }
        return nullptr;
    }

    Command* CommandRegistry::find_shortcut(const Shortcut& shortcut) {
        // the accelerator is folded to the identity the table indexed, which is where the route ends (§18.5)
        return find(m_shortcuts.lookup(shortcut));
    }

    const std::vector<std::unique_ptr<Command>>& CommandRegistry::commands() const { return m_commands; }

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
