#include "commands/command.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "commands/command_registry.h"

namespace albor
{
    namespace
    {
        // the sequence that hands out runtime identities, because a command is built before a registry sees it (§18.1)
        std::atomic<std::uint64_t> g_next_command_id{1};

        // hand out the identity a newly built command carries (§18.1)
        std::uint64_t take_command_id() {
            // the sequence starts at one, so the zero identity never names a real command (§18.1)
            return g_next_command_id.fetch_add(1, std::memory_order_relaxed);
        }
    } // namespace

    Command::Command(std::string explicit_id, std::string label) :
        m_state(CommandState{.enabled = true, .visible = true, .label = std::move(label)}), m_explicit_id(std::move(explicit_id)), m_id(take_command_id()) {}

    Command::~Command() {
        // the registry only indexes this command, so the leaving command removes itself rather than being owned (§18.1)
        if (m_registry != nullptr)
            m_registry->leave(*this);
    }

    CommandId Command::id() const { return m_id; }

    const std::string& Command::explicit_id() const { return m_explicit_id; }

    bool Command::identified() const { return !m_explicit_id.empty(); }

    const CommandState& Command::state() const { return m_state; }

    void Command::set_state(CommandState state) {
        // an unchanged state publishes nothing, so a consumer is never invalidated by a write that did nothing (§18.2)
        if (state == m_state)
            return;
        // the whole state is replaced at once, so no consumer observes a torn pair of the two published states (§18.2)
        m_state = std::move(state);
        // the consumers are told, which is what makes enablement observed rather than copied (§18.2)
        publish();
    }

    void Command::set_label(std::string label) {
        CommandState next = m_state;
        next.label = std::move(label);
        set_state(std::move(next));
    }

    void Command::set_enabled(bool enabled) {
        CommandState next = m_state;
        next.enabled = enabled;
        set_state(std::move(next));
    }

    void Command::set_visible(bool visible) {
        CommandState next = m_state;
        next.visible = visible;
        set_state(std::move(next));
    }

    ObserverHandle Command::bind_static(ObserverFn observer, void* context) {
        // a null observer binds nothing, because there is no callback to publish to (§18.1)
        if (observer == nullptr)
            return 0;
        // the handle is taken before the consumer is appended, because the binding records it (§18.1)
        const ObserverHandle handle = m_next_handle++;
        // the consumer joins the end of the notification order, which is the order it bound in (§18.1)
        m_bindings.push_back(Binding{.handle = handle, .observer = observer, .context = context});
        // the consumer is handed the current state at once, because a late binder must not render stale (§18.1)
        observer(context, m_state);
        return handle;
    }

    bool Command::unbind(ObserverHandle handle) {
        for (auto binding = m_bindings.begin(); binding != m_bindings.end(); ++binding) {
            // the binding the handle names is the one that is removed (§18.1)
            if (binding->handle == handle) {
                m_bindings.erase(binding);
                return true;
            }
        }
        // a handle that binds nothing is reported as unremoved rather than silently accepted (§18.1)
        return false;
    }

    std::size_t Command::observer_count() const { return m_bindings.size(); }

    InvocationOutcome Command::invoke(CommandInvocation invocation) {
        // a command that does not apply right now refuses, which is the gate every route reads (§18.2)
        if (!m_state.visible)
            return InvocationOutcome::Hidden;
        // a command that is not permitted right now refuses, which is the refusal a disabled control gives (§18.2)
        if (!m_state.enabled)
            return InvocationOutcome::Disabled;
        // the invocation names the command it ran on, which the handler reads instead of capturing (§18.3)
        invocation.command = m_id;
        // a selection scoped action resolves its range now, because one command serves many controls (§18.3)
        if (m_selection_required)
            invocation.selection = resolve_selection();
        // a selection scoped action refuses an empty range rather than running over nothing (§18.3)
        if (m_selection_required && invocation.selection.empty())
            return InvocationOutcome::NoSelection;
        // a command declared without an action refuses, which is better than pretending something ran (§18.3)
        if (!m_action)
            return InvocationOutcome::NoAction;
        // the history entry is recorded before the action runs, so a reversal sees the state the action left (§18.4)
        record_undo(invocation);
        // the action runs on the ui thread, which is the only thread a command is ever invoked from (§18.3)
        m_action(invocation);
        return InvocationOutcome::Invoked;
    }

    InvocationOutcome Command::invoke() {
        // the bare overload is the case that carries neither an argument nor a range (§18.3)
        return invoke(CommandInvocation{});
    }

    void Command::set_action(CommandAction action) { m_action = std::move(action); }

    void Command::set_selection_required(bool required) { m_selection_required = required; }

    bool Command::selection_required() const { return m_selection_required; }

    SelectionSpan Command::resolve_selection() const {
        // a command with no installed source has no selection, which is the empty span (§18.3)
        if (m_selection_source == nullptr)
            return empty_selection();
        return (*m_selection_source)();
    }

    void Command::bind_selection_source(const SelectionSource* source) { m_selection_source = source; }

    void Command::set_undo_entry(UndoEntry entry) { m_undo_entry = std::move(entry); }

    void Command::bind_undo_stack(UndoStack* stack) { m_undo_stack = stack; }

    void Command::publish() {
        // the bindings are copied because a consumer may bind or unbind from inside its own callback (§18.2)
        const std::vector<Binding> bindings = m_bindings;
        for (const Binding& binding : bindings) {
            // a consumer that was removed by an earlier callback is skipped, because its copy outlived the removal (§18.2)
            const bool still_bound = std::any_of(m_bindings.begin(), m_bindings.end(), [&binding](const Binding& current) { return current.handle == binding.handle; });
            if (!still_bound)
                continue;
            if (binding.observer != nullptr)
                binding.observer(binding.context, m_state);
        }
    }

    void Command::record_undo(const CommandInvocation& invocation) {
        // a command with no stack or no reversal body contributes nothing to the history (§18.4)
        if (m_undo_stack == nullptr || !m_undo_entry.undo)
            return;
        // the template is copied because the history outlives the call frame that recorded it (§18.4)
        const UndoEntry entry = m_undo_entry;
        // the recorded step is bound to the invocation it reverses, so history says what actually ran (§18.4)
        m_undo_stack->push(UndoEntry{
            .label = entry.label.empty() ? m_state.label : entry.label,
            .undo = [entry, invocation](const CommandInvocation&) { entry.undo(invocation); },
            .redo = [entry, invocation](const CommandInvocation&) { entry.redo(invocation); },
            .owner = entry.owner,
            .live = entry.live,
        });
    }
} // namespace albor
