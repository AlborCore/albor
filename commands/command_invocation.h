#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <variant>

#include "commands/command_id.h"
#include "commands/selection.h"

namespace albor
{
    // the value an action carries when it is bound to many rows, which is a truth, an index, a value or a key (§18.3)
    using CommandArgument = std::variant<std::monostate, bool, std::uint64_t, double, std::string>;

    // where an invocation came from, which is what a handler branches on and what automation reports (§18.3)
    enum class InvocationSource
    {
        // a press on a control in the tree, which is the pointer route (§8.3)
        Pointer,

        // a key that matched a bound accelerator, which is the keyboard route (§18.5)
        Keyboard,

        // a menu item that a surface presented, which is the menu route (§18.1)
        Menu,

        // an out of process client that was allowed to name the command, which is the automation route (§19.6)
        Automation,

        // assistive technology acting on the semantic tree, which is the accessibility route (§10.1)
        Accessibility,

        // the undo or redo command replaying history, which is the reversal route (§18.4)
        UndoRedo,

        // a .ui handler body that fired the command itself, which is the declarative route (§11.1, §18.3)
        Handler
    };

    // why an invocation did or did not run, which automation reports and a consumer may ignore (§18.3)
    enum class InvocationOutcome
    {
        // the action ran, which is the only outcome that changed anything (§18.3)
        Invoked,

        // the command is not permitted right now, which is the refusal a disabled control gives (§18.2)
        Disabled,

        // the command does not apply right now, which is the refusal a hidden control gives (§18.2)
        Hidden,

        // the command needs a selection and none was resolved, which refuses rather than running over nothing (§18.3)
        NoSelection,

        // the command was declared without an action, which refuses rather than pretending to have run (§18.3)
        NoAction
    };

    // one invocation, which carries whatever the action needs at the moment it runs (§18.3)
    struct CommandInvocation
    {
        // the command the invocation ran on, which the handler reads rather than capturing (§18.3)
        CommandId command = invalid_command_id();

        // the row identity or index the invocation carried, which most actions leave empty (§18.3)
        CommandArgument argument{};

        // the range the invocation resolved to, which only a selection scoped action reads (§18.3)
        SelectionSpan selection{};

        // the route the invocation arrived by, which every handler may branch on (§18.3)
        InvocationSource source = InvocationSource::Pointer;
    };

    // the work one command performs, which receives everything the invocation carried (§18.3)
    using CommandAction = std::function<void(const CommandInvocation&)>;
} // namespace albor
