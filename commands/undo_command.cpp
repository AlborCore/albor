#include "commands/undo_command.h"

#include <utility>

namespace albor
{
    namespace
    {
        // publish the state a history command reads off a stack, which is one rule for both directions (§18.4)
        void publish_history_state(Command& command, const UndoStack& stack, bool enabled) {
            // the state is copied first, because publishing the pair is what keeps both fields consistent (§18.2)
            CommandState state = command.state();
            // the enablement is the stack's answer, which is the one place the question is asked (§18.4)
            state.enabled = enabled;
            // the label follows the newest step, so a menu presents what would actually be reversed (§18.4)
            const std::string& label = stack.top_label();
            if (!label.empty())
                state.label = label;
            // the state is published, which is what makes the button, the menu item and the accelerator agree (§18.4)
            command.set_state(std::move(state));
        }
    } // namespace

    UndoCommand::UndoCommand(UndoStack& stack, std::string explicit_id, std::string label) :
        m_stack(stack), m_command(std::move(explicit_id), std::move(label)) {
        // the command asks the stack to reverse, which is the only body it owns (§18.4)
        m_command.set_action([this](const CommandInvocation&) { (void)m_stack.undo(); });
        // the stack notifies this command whenever it moves, so no consumer ever polls for the answer (§18.4)
        m_stack.add_change_handler([this] { refresh(); });
        // the command starts from what the stack already holds, so it never publishes a state that disagrees (§18.4)
        refresh();
    }

    Command& UndoCommand::command() { return m_command; }

    void UndoCommand::refresh() { publish_history_state(m_command, m_stack, m_stack.can_undo()); }

    RedoCommand::RedoCommand(UndoStack& stack, std::string explicit_id, std::string label) :
        m_stack(stack), m_command(std::move(explicit_id), std::move(label)) {
        // the command asks the stack to replay, which is the only body it owns (§18.4)
        m_command.set_action([this](const CommandInvocation&) { (void)m_stack.redo(); });
        // the stack notifies this command whenever it moves, so no consumer ever polls for the answer (§18.4)
        m_stack.add_change_handler([this] { refresh(); });
        // the command starts from what the stack already holds, so it never publishes a state that disagrees (§18.4)
        refresh();
    }

    Command& RedoCommand::command() { return m_command; }

    void RedoCommand::refresh() { publish_history_state(m_command, m_stack, m_stack.can_redo()); }
} // namespace albor
