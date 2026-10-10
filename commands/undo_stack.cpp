#include "commands/undo_stack.h"

#include <utility>

#include "commands/command_invocation.h"

namespace albor
{
    namespace
    {
        // the label a stack reports while it retains nothing, which is built once on first use (§18.4)
        const std::string& empty_label() {
            // a function local constant is initialized on the first call and never torn down (§18.4)
            static const std::string value;
            return value;
        }
    } // namespace

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
        return UndoGroup(this);
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
                // a step with no reversal body contributes nothing, so the group skips it rather than aborting (§18.4)
                if (entry->undo)
                    entry->undo(carried);
            }
        };
        folded.redo = [steps](const CommandInvocation& invocation_ignored) {
            for (const UndoEntry& entry : steps) {
                // a step with no replay body contributes nothing, so the group skips it rather than aborting (§18.4)
                if (entry.redo)
                    entry.redo(invocation_ignored);
            }
        };
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
        return !m_undo.empty();
    }

    bool UndoStack::can_redo() const {
        // a pending group is still forming one action, so nothing is reappliable until it closes (§18.4)
        if (m_group_open)
            return false;
        return !m_redo.empty();
    }

    bool UndoStack::undo() {
        // a group still forming is a partial action, which reversing would leave the caller half way (§18.4)
        if (m_group_open)
            return false;
        // a stack that retains nothing cannot reverse, which is what the undo command publishes (§18.4)
        if (m_undo.empty())
            return false;
        // the newest step leaves the retained direction for the reapplied direction (§18.4)
        UndoEntry entry = std::move(m_undo.back());
        m_undo.pop_back();
        // the step is copied into the reapplied direction, because the body below still has to run (§18.4)
        m_redo.push_back(entry);
        // recording is suspended for the body, because a reversal must not push a step of its own (§18.4)
        m_applying = true;
        // the body receives a reversal invocation, which a command recorded entry rebinds to what actually ran (§18.4)
        const CommandInvocation carried{.command = invalid_command_id(), .source = InvocationSource::UndoRedo};
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
        const CommandInvocation carried{.command = invalid_command_id(), .source = InvocationSource::UndoRedo};
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

    std::size_t UndoStack::depth() const { return m_undo.size(); }

    void UndoStack::set_depth_limit(std::size_t limit) {
        m_depth_limit = limit;
        // the cap holds immediately, so lowering it releases the steps it no longer permits (§18.4)
        trim();
    }

    std::size_t UndoStack::depth_limit() const { return m_depth_limit; }

    const std::string& UndoStack::top_label() const {
        // a stack that retains nothing presents no label, which is the empty one (§18.4)
        if (m_undo.empty())
            return empty_label();
        return m_undo.back().label;
    }

    std::size_t UndoStack::applied_count() const { return m_applied; }

    void UndoStack::add_change_handler(ChangeHandler handler) { m_changed.push_back(std::move(handler)); }

    void UndoStack::drop_redo() { m_redo.clear(); }

    void UndoStack::trim() {
        // the oldest steps leave first, so the newest history is what the cap keeps (§18.4)
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
        m_stack(other.m_stack) {
        // the scope that was moved from no longer closes the group, because the survivor does (§18.4)
        other.m_stack = nullptr;
    }

    UndoGroup& UndoGroup::operator=(UndoGroup&& other) noexcept {
        // a scope that already held a group closes it first, because the label and steps belong to it (§18.4)
        if (m_stack != nullptr)
            m_stack->close_group();
        // the survivor adopts the group it was moved from (§18.4)
        m_stack = other.m_stack;
        // the scope that was moved from no longer closes the group, because the survivor does (§18.4)
        other.m_stack = nullptr;
        return *this;
    }

    UndoGroup::~UndoGroup() {
        // a scope built by default closes nothing, which is what a refused nested group hands back (§18.4)
        if (m_stack == nullptr)
            return;
        // leaving the scope folds the pending steps into one entry, whatever path left it (§18.4)
        m_stack->close_group();
        // the scope no longer owns the group, so a second destruction is harmless (§18.4)
        m_stack = nullptr;
    }

    bool UndoGroup::open() const { return m_stack != nullptr; }
} // namespace albor
