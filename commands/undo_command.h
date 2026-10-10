#pragma once

#include <string>
#include <utility>

#include "commands/command.h"
#include "commands/undo_stack.h"

namespace albor
{
    // the command that reverses the newest step, whose enablement is the stack's own answer (§18.4)
    class UndoCommand
    {
    public:
        // build the command over a stack, which the caller keeps alive for as long as the command is bound (§18.4)
        explicit UndoCommand(UndoStack& stack, std::string explicit_id = "edit.undo", std::string label = "Undo");

        UndoCommand(const UndoCommand&) = delete;
        UndoCommand& operator=(const UndoCommand&) = delete;
        UndoCommand(UndoCommand&&) = delete;
        UndoCommand& operator=(UndoCommand&&) = delete;
        ~UndoCommand() = default;

        // hand out the command a consumer binds, which is the object the shared state lives on (§18.4)
        [[nodiscard]] Command& command();

        // re-read the stack and publish, which is what the stack calls whenever it moves (§18.4)
        void refresh();

    private:
        // the stack whose answer the command publishes, which the caller owns (§18.4)
        UndoStack& m_stack;

        // the command itself, which is a shared object like any other (§18.1)
        Command m_command;
    };

    // the command that replays the newest reversed step, whose enablement is the stack's own answer (§18.4)
    class RedoCommand
    {
    public:
        // build the command over a stack, which the caller keeps alive for as long as the command is bound (§18.4)
        explicit RedoCommand(UndoStack& stack, std::string explicit_id = "edit.redo", std::string label = "Redo");

        RedoCommand(const RedoCommand&) = delete;
        RedoCommand& operator=(const RedoCommand&) = delete;
        RedoCommand(RedoCommand&&) = delete;
        RedoCommand& operator=(RedoCommand&&) = delete;
        ~RedoCommand() = default;

        // hand out the command a consumer binds, which is the object the shared state lives on (§18.4)
        [[nodiscard]] Command& command();

        // re-read the stack and publish, which is what the stack calls whenever it moves (§18.4)
        void refresh();

    private:
        // the stack whose answer the command publishes, which the caller owns (§18.4)
        UndoStack& m_stack;

        // the command itself, which is a shared object like any other (§18.1)
        Command m_command;
    };
} // namespace albor
