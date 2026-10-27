#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace albor
{
    // the invocation an undo step carries, which is declared in the invocation vocabulary of §18.3
    struct CommandInvocation;

    // one reversible step, which the stack owns until it is undone or dropped (§18.4)
    struct UndoEntry
    {
        // the text the undo and redo commands present for this step (§18.4)
        std::string label;

        // the work that reverses the step, which receives the invocation it reverses (§18.4)
        std::function<void(const CommandInvocation&)> undo{};

        // the work that reapplies the step, which receives the invocation it replays and may be empty (§18.4)
        std::function<void(const CommandInvocation&)> redo{};
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
        explicit UndoGroup(UndoStack* stack) :
            m_stack(stack) {}

        // the stack whose group this scope closes, which is null once the scope has been moved from (§18.4)
        UndoStack* m_stack = nullptr;
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
        ~UndoStack() = default;

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

        // how many step bodies have run, which the group folding tests assert against (§18.4)
        std::size_t m_applied = 0;

        // whether a step body is running, which suspends recording so a reversal creates no history (§18.4)
        bool m_applying = false;

        // the notifications a mutation raises, which are empty while nothing observes the stack (§18.4)
        std::vector<ChangeHandler> m_changed;
    };
} // namespace albor
