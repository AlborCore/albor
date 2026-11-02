#pragma once

#include <cstddef>
#include <functional>

namespace albor
{
    // the half open range a selection scoped action runs over, which is a value and never a view into a widget (§18.3)
    struct SelectionSpan
    {
        // the index the range starts at, which is the first selected item (§18.3)
        std::size_t first = 0;

        // the index one past the last selected item, which is what makes the range half open (§18.3)
        std::size_t last = 0;

        // report whether the range selects nothing, which a command that needs a selection refuses (§18.3)
        [[nodiscard]] bool empty() const {
            // an inverted or empty range selects nothing, which is the only shape that means no selection (§18.3)
            return first >= last;
        }

        // hand out how many items the range covers, which is what a handler iterates (§18.3)
        [[nodiscard]] std::size_t count() const {
            // an empty range covers nothing rather than a negative count (§18.3)
            return empty() ? 0 : last - first;
        }

        // compare two spans field by field, which the invocation tests assert on
        friend bool operator==(const SelectionSpan& lhs, const SelectionSpan& rhs) = default;
    };

    // where a command reads the current selection from, which the application installs once per view (§18.3)
    using SelectionSource = std::function<SelectionSpan()>;

    // the empty range a command falls back to while no selection source is installed (§18.3)
    [[nodiscard]] inline SelectionSpan empty_selection() {
        // an empty selection is the zero span, which selects nothing rather than naming an item (§18.3)
        return {};
    }
} // namespace albor
