#include "automation/command_commands.h"

#include <memory>
#include <optional>
#include <sstream>
#include <string_view>

namespace albor::automation
{
    namespace
    {
        // the family every command command sits under, which keeps the allow list readable (§10.3)
        constexpr std::string_view FAMILY = "command.";

        // find the command carrying an author assigned id, which is the only selector automation offers (AD-11)
        Command* command_by_id(CommandRegistry& commands, const std::string& id) {
            // the lookup is exact, because a prefix or a case fold would select the wrong action (§18.1, AD-11)
            Command* command = commands.find(id);
            // a command the author never named is refused, which is what a selector requires (§18.1, AD-11)
            if (command == nullptr)
                return nullptr;
            // a command with no author id is unaddressable, because automation selects by identity (AD-11)
            if (!command->identified())
                return nullptr;
            return command;
        }

        // the argument a command needs, which is refused rather than defaulted (§10.3)
        std::optional<std::string> argument_at(const AutomationRequest& request, std::size_t index) {
            // an index past the end names no argument, which is a client mistake (§10.3)
            if (index >= request.arguments.size())
                return std::nullopt;
            return request.arguments[index];
        }

        // why an invocation did not run, which is stated rather than hidden, because a client drives states (§18.3)
        std::string refusal_for(InvocationOutcome outcome) {
            switch (outcome) {
            case InvocationOutcome::Invoked:
                return {};
            case InvocationOutcome::Disabled:
                return "the command is not permitted right now";
            case InvocationOutcome::Hidden:
                return "the command does not apply right now";
            case InvocationOutcome::NoSelection:
                return "the command needs a selection and none was resolved";
            case InvocationOutcome::NoAction:
                return "the command was declared without an action";
            }
            // an outcome outside the enumeration is refused rather than reported as a success (§18.3)
            return "the command did not run";
        }
    } // namespace

    std::vector<std::string> command_command_names() {
        // the names are fixed rather than discovered, because a host allow-lists before any registry exists (§10.3, AD-10)
        return {std::string(FAMILY) + "list", std::string(FAMILY) + "get", std::string(FAMILY) + "invoke", std::string(FAMILY) + "can_undo", std::string(FAMILY) + "undo", std::string(FAMILY) + "redo"};
    }

    void register_command_commands(AutomationRegistry& registry, CommandSnapshot snapshot) {
        // the dump reports every command with its state, which is what a client asserts against (§18.2)
        registry.register_command(std::string(FAMILY) + "list", [snapshot](const AutomationRequest&) {
            // a host that supplied no registry cannot answer, which is refused rather than answered empty (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no command registry");
            // the registry is taken once, because the application may declare more between lines (§10.3)
            CommandRegistry& commands = *snapshot();
            // the dump accumulates line by line, which is what a client parses (§10.3)
            std::ostringstream out;
            // every command contributes one line, in declaration order (§18.1)
            for (const Command* command : commands.commands()) {
                // an unnamed command is dumped with a dash, because a reader reaches it though a client cannot (AD-11)
                const std::string id = command->identified() ? command->explicit_id() : std::string("-");
                // the two states are words rather than numbers, so a client cannot mistake one for the other (§18.2)
                const CommandState& state = command->state();
                // the line carries id, enablement, applicability and label, which is what a consumer shows (§18.2)
                out << id << ' ' << (state.enabled ? "enabled" : "disabled") << ' ' << (state.visible ? "visible" : "hidden") << ' ' << state.label << '\n';
            }
            return AutomationResult::answered(out.str());
        });

        // the lookup reports one command's state, which is how a client asserts a single action (§10.3)
        registry.register_command(std::string(FAMILY) + "get", [snapshot](const AutomationRequest& request) {
            // a host with no registry cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no command registry");
            // the id is required, because a command is selected by identity and never by position (§18.1, AD-11)
            const std::optional<std::string> id = argument_at(request, 0);
            if (!id)
                return AutomationResult::refused("command.get needs an id");
            // the command is resolved against the registry, which owns every declared action (§18.1)
            Command* command = command_by_id(*snapshot(), *id);
            // an id no command carries is refused, which names the reason rather than reporting an empty answer (§10.3)
            if (command == nullptr)
                return AutomationResult::refused("no command with id: " + *id);
            // the answer carries both published states and the label, which is what every consumer presents (§18.2)
            const CommandState& state = command->state();
            std::ostringstream out;
            out << (state.enabled ? "enabled" : "disabled") << ' ' << (state.visible ? "visible" : "hidden") << ' ' << state.label;
            return AutomationResult::answered(out.str());
        });

        // the invocation runs the same action a control runs, and observes the same refusals (§18.3)
        registry.register_command(std::string(FAMILY) + "invoke", [snapshot](const AutomationRequest& request) {
            // a host with no registry cannot invoke, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no command registry");
            // the id is required, because a command is selected by identity and never by position (§18.1, AD-11)
            const std::optional<std::string> id = argument_at(request, 0);
            if (!id)
                return AutomationResult::refused("command.invoke needs an id");
            // the command is resolved against the registry, which owns every declared action (§18.1)
            Command* command = command_by_id(*snapshot(), *id);
            // an id no command carries is refused, which names the reason rather than reporting an empty answer (§10.3)
            if (command == nullptr)
                return AutomationResult::refused("no command with id: " + *id);
            // the argument is optional, because most actions take nothing (§18.3)
            const std::optional<std::string> argument = argument_at(request, 1);
            CommandInvocation invocation{.command = command->id(), .source = InvocationSource::Automation};
            // an argument the client supplied travels with the invocation, so the action reads it (§18.3)
            if (argument)
                invocation.argument = *argument;
            // the command decides for itself, so automation observes exactly the enablement a user would (§18.3, AD-10)
            const InvocationOutcome outcome = command->invoke(invocation);
            // a refusal is reported as one, because a client must not read an empty answer as a success (§10.3)
            if (outcome != InvocationOutcome::Invoked)
                return AutomationResult::refused(refusal_for(outcome));
            // a selection scoped action reports the range it ran over, which is what a client asserts against (§18.4)
            if (command->selection_required())
                return AutomationResult::answered(std::to_string(command->resolve_selection().count()));
            return AutomationResult::answered("");
        });

        // the history questions are answered by the stack itself, which is the one place the answer lives (§18.4)
        registry.register_command(std::string(FAMILY) + "can_undo", [snapshot](const AutomationRequest&) {
            // a host with no registry cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no command registry");
            // the stack answers, which is the same answer the undo command publishes (§18.4)
            return AutomationResult::answered(snapshot()->undo_stack().can_undo() ? "yes" : "no");
        });

        // the reversal runs through the shared stack, so a pointer and a key contribute one entry (§18.4)
        registry.register_command(std::string(FAMILY) + "undo", [snapshot](const AutomationRequest&) {
            // a host with no registry cannot reverse, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no command registry");
            // an empty history refuses, rather than reporting an empty answer a client would read as a reversal (§10.3)
            if (!snapshot()->undo_stack().undo())
                return AutomationResult::refused("there is nothing to undo");
            return AutomationResult::answered("");
        });

        // the replay runs through the shared stack, which is what the redo command runs as well (§18.4)
        registry.register_command(std::string(FAMILY) + "redo", [snapshot](const AutomationRequest&) {
            // a host with no registry cannot replay, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no command registry");
            // an empty reversed history refuses, for the same reason a reversal would (§10.3)
            if (!snapshot()->undo_stack().redo())
                return AutomationResult::refused("there is nothing to redo");
            return AutomationResult::answered("");
        });
    }
} // namespace albor::automation
