#pragma once

#include <cstddef>
#include <functional>

#include "model/item_key.h"

namespace albor::model
{
    // the single selection a list owns, which stores the chosen identity and position and reports change (§18.3)
    class SelectionModel
    {
    public:
        // the index nothing is selected at, which a reader compares the reported index against (§18.3)
        static constexpr std::size_t NO_INDEX = static_cast<std::size_t>(-1);

        // build an empty model, which is the state a list starts in before anything is chosen (§18.3)
        SelectionModel() = default;

        // select one item by identity and position, which replaces any previous selection (§18.1, AD-11)
        void select(ItemKey key, std::size_t index);

        // drop the selection, leaving no item selected (§18.3)
        void clear();

        // report whether nothing is selected, which is what a bare model answers (§18.3)
        [[nodiscard]] bool empty() const;

        // hand out the selected identity, which is the empty key while nothing is selected (AD-11)
        [[nodiscard]] const ItemKey& selected_key() const;

        // hand out the selected index, which is NO_INDEX while nothing is selected (§18.3)
        [[nodiscard]] std::size_t selected_index() const;

        // report whether the given identity is the selected one, which a list reads per row (§18.1, AD-11)
        [[nodiscard]] bool is_selected(const ItemKey& key) const;

        // install the work told about every change of the selection, which a list forwards as its signal (§18.3)
        void set_on_change(std::function<void()> handler);

    private:
        // tell the installed work that the selection changed, if any is installed (§18.3)
        void notify();

        // the selected identity, which is empty while nothing is selected (§18.1, AD-11)
        ItemKey m_key;

        // the selected index, which is NO_INDEX while nothing is selected (§18.3)
        std::size_t m_index = NO_INDEX;

        // the work told about every change, which a list forwards as its signal (§18.3)
        std::function<void()> m_on_change;
    };
} // namespace albor::model
