#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace albor
{
    // the invocation an undo step carries, which is declared in the invocation vocabulary of §18.3
    struct CommandInvocation;

    // the lifetime token an entry carries, which a destroyed owner revokes before its history can run (§18.4)
    class UndoOwner
    {
    public:
        UndoOwner() = default;
        UndoOwner(const UndoOwner&) = delete;
        UndoOwner& operator=(const UndoOwner&) = delete;
        UndoOwner(UndoOwner&&) = delete;
        UndoOwner& operator=(UndoOwner&&) = delete;

        // the destructor revokes, because an owner that is gone can never run its entries (§18.4)
        ~UndoOwner();

        // revoke the owner, which makes every entry that carries the token unreachable (§18.4)
        void revoke();

        // report whether the owner still exists, which a stored entry checks before it runs (§18.4)
        [[nodiscard]] bool alive() const;

    private:
        // whether the owner still exists, which a destruction clears (§18.4)
        bool m_alive = true;
    };

    // one reversible step, which the stack owns until it is undone or dropped (§18.4)
    struct UndoEntry
    {
        // the text the undo and redo commands present for this step (§18.4)
        std::string label;

        // the work that reverses the step, which receives the invocation it reverses (§18.4)
        std::function<void(const CommandInvocation&)> undo{};

        // the work that reapplies the step, which receives the invocation it replays and may be empty (§18.4)
        std::function<void(const CommandInvocation&)> redo{};

        // the token whose revocation makes the step unreachable, which is empty for an entry that owns no object (§18.4)
        std::shared_ptr<UndoOwner> owner{};

        // a predicate a folded group uses to report whether any captured step still lives, empty otherwise (§18.4)
        std::function<bool()> live{};
    };

    // the undo stack behind the shared command, which one invocation contributes exactly one entry to (§18.4)
    class UndoStack;

    // the scope that folds the entries pushed while it is open into one step, whatever path leaves it (§18.4)
    class UndoGroup
    {
    public:
        UndoGroup() = default;
        UndoGroup(const UndoGroup&) = delete;
        UndoGroup& operator=(const UndoGroup&) = delete;
        UndoGroup(UndoGroup&& other) noexcept;
        UndoGroup& operator=(UndoGroup&& other) noexcept;
        ~UndoGroup();

        // report whether the scope closed a group, which a move assignment leaves false (§18.4)
        [[nodiscard]] bool open() const;

    private:
        friend class UndoStack;

        // build a scope over a stack, which the stack alone hands out (§18.4)
        explicit UndoGroup(UndoStack* stack, std::shared_ptr<const bool> lifetime) :
            m_stack(stack), m_lifetime(std::move(lifetime)) {}

        // the stack whose group this scope closes, which is null once the scope has been moved from (§18.4)
        UndoStack* m_stack = nullptr;

        // the token that proves the borrowed stack still exists when the scope closes (§18.4)
        std::shared_ptr<const bool> m_lifetime;
    };

    // the undo stack behind the shared command, which one invocation contributes exactly one entry to (§18.4)
    class UndoStack
    {
    public:
        // the notification a mutation raises, which the undo and redo commands refresh themselves through (§18.4)
        using ChangeHandler = std::function<void()>;

        UndoStack() = default;
        UndoStack(const UndoStack&) = delete;
        UndoStack& operator=(const UndoStack&) = delete;
        UndoStack(UndoStack&&) = delete;
        UndoStack& operator=(UndoStack&&) = delete;
        ~UndoStack();

        // hand out the token a borrowed field checks before it calls back, which dies with the stack (§18.4)
        [[nodiscard]] std::shared_ptr<const bool> lifetime() const;

        // push one step, which folds into an open group and otherwise becomes its own entry (§18.4)
        void push(UndoEntry entry);

        // open a coalescing group, which folds every step pushed while it is open into one entry (§18.4)
        UndoGroup open_group(std::string label);

        // close the open group, which is what a scope does on the way out and a direct caller may do early (§18.4)
        void close_group();

        // report whether a group is open, which a step pushed outside one wants to know (§18.4)
        [[nodiscard]] bool group_open() const;

        // report whether a step can be reversed, which is what the undo command publishes as its state (§18.4)
        [[nodiscard]] bool can_undo() const;

        // report whether a reversed step can be reapplied, which is what the redo command publishes (§18.4)
        [[nodiscard]] bool can_redo() const;

        // reverse the newest step, which runs its body with history recording suspended (§18.4)
        bool undo();

        // reapply the newest reversed step, which runs its body with history recording suspended (§18.4)
        bool redo();

        // forget every step in both directions, which is what a document close requires (§18.4)
        void clear();

        // revoke an owner and drop every step it recorded, which is what its destruction requires (§18.4)
        void remove_owner(const std::shared_ptr<UndoOwner>& owner);

        // hand out how many steps are retained, which is what the depth limit bounds (§18.4)
        [[nodiscard]] std::size_t depth() const;

        // cap the retained steps and drop the oldest, which is what keeps a long session bounded (§18.4)
        void set_depth_limit(std::size_t limit);

        // hand out the cap, which is the retained maximum and never the current depth (§18.4)
        [[nodiscard]] std::size_t depth_limit() const;

        // hand out the label of the newest step, which is empty when nothing is retained (§18.4)
        [[nodiscard]] const std::string& top_label() const;

        // hand out how many step bodies have run, which is what proves a group folded several into one (§18.4)
        [[nodiscard]] std::size_t applied_count() const;

        // install a notification a mutation raises, which the undo and redo commands refresh themselves through (§18.4)
        void add_change_handler(ChangeHandler handler);

    private:
        friend class UndoGroup;

        // drop the reapplied steps, which a new step does because it invalidates what came after it (§18.4)
        void drop_redo();

        // drop the oldest steps until the retention cap holds (§18.4)
        void trim();

        // announce a mutation, which is how the undo and redo commands learn the stack moved (§18.4)
        void notify();

        // the retained steps in the direction they were pushed (§18.4)
        std::vector<UndoEntry> m_undo;

        // the reversed steps, which a new step discards because the history after it no longer applies (§18.4)
        std::vector<UndoEntry> m_redo;

        // the steps an open group is folding, which are not history until the group closes (§18.4)
        std::vector<UndoEntry> m_group;

        // the label the open group presents for the step it will fold into (§18.4)
        std::string m_group_label;

        // whether a group is open, which is what routes a push into the pending steps (§18.4)
        bool m_group_open = false;

        // the retained maximum, which a depth of zero keeps nothing (§18.4)
        std::size_t m_depth_limit = 256;

        // whether this stack is still alive, which a borrowed field reads before it calls back (§18.4)
        std::shared_ptr<bool> m_lifetime = std::make_shared<bool>(true);

        // how many step bodies have run, which the group folding tests assert against (§18.4)
        std::size_t m_applied = 0;

        // whether a step body is running, which suspends recording so a reversal creates no history (§18.4)
        bool m_applying = false;

        // the notifications a mutation raises, which are empty while nothing observes the stack (§18.4)
        std::vector<ChangeHandler> m_changed;
    };
} // namespace albor
