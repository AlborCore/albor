#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "commands/command_id.h"
#include "commands/command_invocation.h"
#include "commands/command_state.h"
#include "commands/selection.h"
#include "commands/undo_stack.h"

namespace albor
{
    class CommandRegistry;

    // the shared action object that buttons, shortcuts, menus and automation bind to (AD-15, §18.1)
    class Command
    {
    public:
        // the shape a consumer binds as, which is a plain function pointer over its own context (§13.3)
        using ObserverFn = void (*)(void* context, const CommandState& state);

        // build a command over the author assigned id, which is the only selector automation may use (§18.1, AD-11)
        explicit Command(std::string explicit_id, std::string label = {});

        Command(const Command&) = delete;
        Command& operator=(const Command&) = delete;
        Command(Command&&) = delete;
        Command& operator=(Command&&) = delete;
        ~Command();

        // hand out the runtime identity, which is the correlation hint and never a selector (§18.1, AD-11)
        [[nodiscard]] CommandId id() const;

        // hand out the author assigned id, which is empty when the author named nothing (§18.1, AD-11)
        [[nodiscard]] const std::string& explicit_id() const;

        // report whether the command carries an author assigned identity, which automation requires (§18.1, AD-11)
        [[nodiscard]] bool identified() const;

        // hand out the published state, which every consumer reads from this one object (§18.2)
        [[nodiscard]] const CommandState& state() const;

        // replace the whole state, which publishes one pair so that a consumer never observes a torn state (§18.2)
        void set_state(CommandState state);

        // replace the label every consumer presents, which publishes like any other state change (§18.2)
        void set_label(std::string label);

        // withhold or restore permission to invoke, which leaves the consumer in place (§18.2)
        void set_enabled(bool enabled);

        // withdraw or restore applicability, which removes the consumer from layout and focus traversal (§18.2)
        void set_visible(bool visible);

        // bind a consumer, which is handed the current state at once so that a late binder is never stale (§18.1)
        [[nodiscard]] ObserverHandle bind_static(ObserverFn observer, void* context);

        // remove a bound consumer, which is what a detached control calls so it stops receiving (§18.1)
        [[nodiscard]] bool unbind(ObserverHandle handle);

        // hand out how many consumers are bound, which is what a detachment test asserts on (§18.1)
        [[nodiscard]] std::size_t observer_count() const;

        // invoke the action, which refuses rather than degrades so that no route can drive a refused state (§18.3)
        [[nodiscard]] InvocationOutcome invoke(CommandInvocation invocation);

        // invoke an action that takes no argument and no range, which is what a plain press performs (§18.3)
        [[nodiscard]] InvocationOutcome invoke();

        // replace the work the action performs, which is the body an author writes once (§18.3)
        void set_action(CommandAction action);

        // declare that the action needs the current selection, which refuses an empty range instead of running (§18.3)
        void set_selection_required(bool required);

        // report whether the action declared that it needs the current selection (§18.3)
        [[nodiscard]] bool selection_required() const;

        // resolve the range an invocation would use now, which is empty while no source is installed (§18.3)
        [[nodiscard]] SelectionSpan resolve_selection() const;

        // install where the current selection is read from, which the registry owns (§18.3)
        void bind_selection_source(const SelectionSource* source);

        // declare the reversal this command contributes to history, which runs one entry per invocation (§18.4)
        void set_undo_entry(UndoEntry entry);

        // install the stack the reversal is recorded on, which the registry owns (§18.4)
        void bind_undo_stack(UndoStack* stack);

    private:
        // the registry that indexed this command, which it clears when the registry goes away first (§18.1)
        friend class CommandRegistry;

        // one bound consumer, which is a callback and the context it was given (§18.1)
        struct Binding
        {
            // the handle unbind is given, which the command assigns when the consumer bound (§18.1)
            ObserverHandle handle = 0;

            // the callback the publication fans the state out to (§18.1)
            ObserverFn observer = nullptr;

            // the object the callback reads and writes through, which the consumer owns (§18.1)
            void* context = nullptr;
        };

        // publish the current state to every bound consumer in the order they bound (§18.2)
        void publish();

        // record one history entry for an invocation, which is what makes three routes one undo step (§18.4)
        void record_undo(const CommandInvocation& invocation);

        // the published state, which every consumer reads from this one object (§18.2)
        CommandState m_state;

        // the author assigned id, which is empty when the author named nothing (§18.1, AD-11)
        std::string m_explicit_id;

        // the runtime identity, which is the correlation hint and never a selector (§18.1, AD-11)
        CommandId m_id = invalid_command_id();

        // the bound consumers, which are notified in registration order (§18.2)
        std::vector<Binding> m_bindings;

        // the next handle to hand out, because zero binds nothing (§18.1)
        ObserverHandle m_next_handle = 1;

        // the work the action performs, which is empty until an author sets it (§18.3)
        CommandAction m_action;

        // the reversal this command records, which an empty body means the action is not undoable (§18.4)
        UndoEntry m_undo_entry;

        // the stack the reversal is recorded on, which is null while no registry installed one (§18.4)
        UndoStack* m_undo_stack = nullptr;

        // where the current selection is read from, which is null while no registry installed one (§18.3)
        const SelectionSource* m_selection_source = nullptr;

        // whether the action declared that it needs the current selection (§18.3)
        bool m_selection_required = false;

        // the registry that indexed this command, which leaves rather than owns it and is null while unindexed (§18.1)
        CommandRegistry* m_registry = nullptr;
    };
} // namespace albor
