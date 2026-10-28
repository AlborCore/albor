#include "commands/undo_stack.h"

#include <algorithm>
#include <utility>

#include "commands/command_invocation.h"

namespace albor
{
    UndoOwner::~UndoOwner() {
        // a destroyed token revokes itself, because no entry it left behind can reach the object it named (§18.4)
        revoke();
    }

    void UndoOwner::revoke() {
        // the owner stops being alive, which every stored entry observes before it runs (§18.4)
        m_alive = false;
    }

    bool UndoOwner::alive() const {
        // the flag is the whole state, because a revoked owner never returns (§18.4)
        return m_alive;
    }

    namespace
    {
        // the label a stack reports while it retains nothing, which is built once on first use (§18.4)
        const std::string& empty_label() {
            // a function local constant is initialized on the first call and never torn down (§18.4)
            static const std::string value;
            return value;
        }

        // report whether an entry may run, which a revoked owner or a dead folded group makes false (§18.4)
        bool entry_alive(const UndoEntry& entry) {
            // a carried owner decides first, because its revocation means the captured object is gone (§18.4)
            if (entry.owner != nullptr && !entry.owner->alive())
                return false;
            // a folded group decides next, because every step it copied may have lost its owner (§18.4)
            if (entry.live != nullptr && !entry.live())
                return false;
            // an entry that carries neither lives with the stack, which is the ordinary command case (§18.4)
            return true;
        }
    } // namespace

    UndoStack::~UndoStack() {
        // the token dies before the history it guarded, so a field that outlives the stack never calls it (§18.4)
        *m_lifetime = false;
    }

    std::shared_ptr<const bool> UndoStack::lifetime() const {
        // the shared flag is what a borrowed field holds, because a raw pointer is not a liveness proof (§18.4)
        return m_lifetime;
    }

    void UndoStack::push(UndoEntry entry) {
        // a step body that pushes creates no history, because a reversal must not be undoable itself (§18.4)
        if (m_applying)
            return;
        // an open group folds the step in, because the caller declared the steps to be one action (§18.4)
        if (m_group_open) {
            m_group.push_back(std::move(entry));
            return;
        }
        // a fresh step invalidates what came after it, so the reapplied steps go (§18.4)
        drop_redo();
        // the step is retained in the order it was pushed, which is the order a reversal walks (§18.4)
        m_undo.push_back(std::move(entry));
        // the retention cap holds after every push, which is what bounds a long session (§18.4)
        trim();
        // the observers learn the stack moved, which is what refreshes the undo and redo commands (§18.4)
        notify();
    }

    UndoGroup UndoStack::open_group(std::string label) {
        // a group already open is not nested, because a fold order the caller never wrote is not a fold (§18.4)
        if (m_group_open)
            return {};
        // the steps of the previous group are history already, so nothing is pending here (§18.4)
        m_group.clear();
        // the new group starts with no steps of its own (§18.4)
        m_group_label = std::move(label);
        // the group is open, which is what routes the next push into it (§18.4)
        m_group_open = true;
        // the scope closes the group on the way out, whatever path leaves it (§18.4)
        return UndoGroup(this, lifetime());
    }

    void UndoStack::close_group() {
        // closing with no group open is what a scope that was never handed one does, and it changes nothing (§18.4)
        if (!m_group_open)
            return;
        // the group stops being open before it is folded, so a body that pushes cannot re-enter the fold (§18.4)
        m_group_open = false;
        // a group that folded nothing records nothing, because there is no action a reversal could undo (§18.4)
        if (m_group.empty()) {
            m_group_label.clear();
            return;
        }
        // the folded steps are copied, because one body walks them back and the other walks them forward (§18.4)
        const std::vector<UndoEntry> steps = m_group;
        UndoEntry folded;
        folded.label = m_group_label;
        folded.undo = [steps](const CommandInvocation& carried) {
            for (auto entry = steps.rbegin(); entry != steps.rend(); ++entry) {
                // a step whose owner was revoked is skipped, because its captured object no longer exists (§18.4)
                if (!entry_alive(*entry))
                    continue;
                // a step with no reversal body contributes nothing, so the group skips it rather than aborting (§18.4)
                if (entry->undo)
                    entry->undo(carried);
            }
        };
        folded.redo = [steps](const CommandInvocation& invocation_ignored) {
            for (const UndoEntry& entry : steps) {
                // a step whose owner was revoked is skipped, because its captured object no longer exists (§18.4)
                if (!entry_alive(entry))
                    continue;
                // a step with no replay body contributes nothing, so the group skips it rather than aborting (§18.4)
                if (entry.redo)
                    entry.redo(invocation_ignored);
            }
        };
        // the folded group lives while any captured step lives, because a revoked owner must not publish a no-op (§18.4)
        folded.live = [steps] {
            // one live step keeps the group reversible, because its body still reaches a live object (§18.4)
            return std::any_of(steps.begin(), steps.end(), entry_alive);
        };
        // a group whose captured steps all died records nothing, because no reversal could reach a live object (§18.4)
        if (!folded.live()) {
            // the pending steps are released, because they no longer form an action (§18.4)
            m_group.clear();
            // the label is released with the steps it named (§18.4)
            m_group_label.clear();
            return;
        }
        // the pending steps are released, because they now live inside the folded entry (§18.4)
        m_group.clear();
        // the label is released with the steps it named (§18.4)
        m_group_label.clear();
        // a new entry invalidates what came after it, so the reapplied steps go (§18.4)
        drop_redo();
        // the folded entry joins the history as exactly one step (§18.4)
        m_undo.push_back(std::move(folded));
        // the retention cap holds after every fold (§18.4)
        trim();
        // the observers learn the stack moved, which is what refreshes the undo and redo commands (§18.4)
        notify();
    }

    bool UndoStack::group_open() const { return m_group_open; }

    bool UndoStack::can_undo() const {
        // a pending group is still forming one action, so nothing is reversible until it closes (§18.4)
        if (m_group_open)
            return false;
        // an entry whose owner was revoked cannot run, so it does not publish a reversible step (§18.4)
        return std::any_of(m_undo.begin(), m_undo.end(), entry_alive);
    }

    bool UndoStack::can_redo() const {
        // a pending group is still forming one action, so nothing is reappliable until it closes (§18.4)
        if (m_group_open)
            return false;
        // an entry whose owner was revoked cannot run, so it does not publish a reappliable step (§18.4)
        return std::any_of(m_redo.begin(), m_redo.end(), entry_alive);
    }

    bool UndoStack::undo() {
        // a group still forming is a partial action, which reversing would leave the caller half way (§18.4)
        if (m_group_open)
            return false;
        // the newest live step is found by dropping revoked entries, which can never run again (§18.4)
        while (!m_undo.empty() && !entry_alive(m_undo.back()))
            m_undo.pop_back();
        // a stack that retains nothing live cannot reverse, which is what the undo command publishes (§18.4)
        if (m_undo.empty())
            return false;
        // the newest step leaves the retained direction for the reapplied direction (§18.4)
        UndoEntry entry = std::move(m_undo.back());
        m_undo.pop_back();
        // the step is copied into the reapplied direction, because the body below still has to run (§18.4)
        m_redo.push_back(entry);
        // recording is suspended for the body, because a reversal must not push a step of its own (§18.4)
        m_applying = true;
        // the body receives a reversal invocation, which a command recorded entry rebinds (§18.4)
        const CommandInvocation carried{.command = invalid_command_id(), .argument = {}, .selection = {}, .source = InvocationSource::UndoRedo};
        // a step with no reversal body runs nothing, and the count stays as it was (§18.4)
        if (entry.undo) {
            entry.undo(carried);
            m_applied++;
        }
        // the body has run, so recording resumes (§18.4)
        m_applying = false;
        // the observers learn the stack moved, which is what refreshes the undo and redo commands (§18.4)
        notify();
        return true;
    }

    bool UndoStack::redo() {
        // a group still forming is a partial action, which replaying would double (§18.4)
        if (m_group_open)
            return false;
        // the newest live step is found by dropping revoked entries, which can never run again (§18.4)
        while (!m_redo.empty() && !entry_alive(m_redo.back()))
            m_redo.pop_back();
        // a stack with nothing reappliable cannot replay, which is what the redo command publishes (§18.4)
        if (m_redo.empty())
            return false;
        // the newest reversed step leaves the reapplied direction for the retained direction (§18.4)
        UndoEntry entry = std::move(m_redo.back());
        m_redo.pop_back();
        // the step is copied into the retained direction, because the body below still has to run (§18.4)
        m_undo.push_back(entry);
        // recording is suspended for the body, because a replay must not push a step of its own (§18.4)
        m_applying = true;
        // the body receives a replay invocation, which a command recorded entry rebinds to what actually ran (§18.4)
        const CommandInvocation carried{.command = invalid_command_id(), .argument = {}, .selection = {}, .source = InvocationSource::UndoRedo};
        // a step with no replay body runs nothing, and the count stays as it was (§18.4)
        if (entry.redo) {
            entry.redo(carried);
            m_applied++;
        }
        // the body has run, so recording resumes (§18.4)
        m_applying = false;
        // the observers learn the stack moved, which is what refreshes the undo and redo commands (§18.4)
        notify();
        return true;
    }

    void UndoStack::clear() {
        // a pending group never became history, so it is discarded along with the retained steps (§18.4)
        m_group.clear();
        // the group stops being open, which is what a clear means for a scope still in flight (§18.4)
        m_group_open = false;
        // the label is released with the steps it named (§18.4)
        m_group_label.clear();
        // the retained direction is emptied (§18.4)
        m_undo.clear();
        // the reapplied direction is emptied, because nothing is left to reapply (§18.4)
        m_redo.clear();
        // the observers learn the stack moved, which is what refreshes the undo and redo commands (§18.4)
        notify();
    }

    void UndoStack::remove_owner(const std::shared_ptr<UndoOwner>& owner) {
        // a null owner names no entry, so every direction is left as it stands (§18.4)
        if (owner == nullptr)
            return;
        // the owner is revoked first, because a folded group checks the captured token, not the folded entry (§18.4)
        owner->revoke();
        // the retained steps are filtered, because a revoked owner and a dead folded group can never run again (§18.4)
        std::erase_if(m_undo, [&owner](const UndoEntry& entry) { return entry.owner == owner || !entry_alive(entry); });
        // the reapplied steps are filtered for the same reason (§18.4)
        std::erase_if(m_redo, [&owner](const UndoEntry& entry) { return entry.owner == owner || !entry_alive(entry); });
        // the pending group is filtered before it folds, because it is not history yet (§18.4)
        std::erase_if(m_group, [&owner](const UndoEntry& entry) { return entry.owner == owner; });
        // the observers learn the stack moved, which is what refreshes the undo and redo commands (§18.4)
        notify();
    }

    std::size_t UndoStack::depth() const {
        // a dead step is not retained history, because no reversal can reach the object it captured (§18.4)
        return static_cast<std::size_t>(std::count_if(m_undo.begin(), m_undo.end(), entry_alive));
    }

    void UndoStack::set_depth_limit(std::size_t limit) {
        m_depth_limit = limit;
        // the cap holds immediately, so lowering it releases the steps it no longer permits (§18.4)
        trim();
        // the observers learn the history moved, because the cap may have dropped a step (§18.4)
        notify();
    }

    std::size_t UndoStack::depth_limit() const { return m_depth_limit; }

    const std::string& UndoStack::top_label() const {
        // the newest live step is found from the end, because a revoked owner never presents a label (§18.4)
        for (auto entry = m_undo.rbegin(); entry != m_undo.rend(); ++entry)
            if (entry_alive(*entry))
                return entry->label;
        // a stack that retains nothing live presents no label, which is the empty one (§18.4)
        return empty_label();
    }

    std::size_t UndoStack::applied_count() const { return m_applied; }

    void UndoStack::add_change_handler(ChangeHandler handler) { m_changed.push_back(std::move(handler)); }

    void UndoStack::drop_redo() { m_redo.clear(); }

    void UndoStack::trim() {
        // a revoked entry is not retained history, because no reversal can reach the object it captured (§18.4)
        std::erase_if(m_undo, [](const UndoEntry& entry) { return !entry_alive(entry); });
        // the reapplied direction drops its dead entries for the same reason (§18.4)
        std::erase_if(m_redo, [](const UndoEntry& entry) { return !entry_alive(entry); });
        // the oldest live steps leave first, so the newest history is what the cap keeps (§18.4)
        while (m_undo.size() > m_depth_limit)
            m_undo.erase(m_undo.begin());
        // the reapplied direction is bounded by the same cap, because it never outlives the retained one (§18.4)
        while (m_redo.size() > m_depth_limit)
            m_redo.erase(m_redo.begin());
    }

    void UndoStack::notify() {
        // a stack nobody observes raises nothing, which is what an application without undo commands does (§18.4)
        for (const ChangeHandler& handler : m_changed) {
            // every observer is told, because more than one may be watching the same stack (§18.4)
            if (handler)
                handler();
        }
    }

    UndoGroup::UndoGroup(UndoGroup&& other) noexcept :
        m_stack(other.m_stack), m_lifetime(std::move(other.m_lifetime)) {
        // the scope that was moved from no longer closes the group, because the survivor does (§18.4)
        other.m_stack = nullptr;
    }

    UndoGroup& UndoGroup::operator=(UndoGroup&& other) noexcept {
        // a scope that already held a group closes it first, because the label and steps belong to it (§18.4)
        if (m_stack != nullptr && m_lifetime != nullptr && *m_lifetime)
            m_stack->close_group();
        // the survivor adopts the group it was moved from (§18.4)
        m_stack = other.m_stack;
        // the lifetime token moves with the borrowed stack, because it proves the pointer is still callable (§18.4)
        m_lifetime = std::move(other.m_lifetime);
        // the scope that was moved from no longer closes the group, because the survivor does (§18.4)
        other.m_stack = nullptr;
        return *this;
    }

    UndoGroup::~UndoGroup() {
        // a scope built by default closes nothing, which is what a refused nested group hands back (§18.4)
        if (m_stack == nullptr)
            return;
        // a closed stack is not called, because its address is no longer a live object (§18.4)
        if (m_lifetime != nullptr && *m_lifetime)
            m_stack->close_group();
        // the scope no longer owns the group, so a second destruction is harmless (§18.4)
        m_stack = nullptr;
    }

    bool UndoGroup::open() const { return m_stack != nullptr && m_lifetime != nullptr && *m_lifetime; }
} // namespace albor
