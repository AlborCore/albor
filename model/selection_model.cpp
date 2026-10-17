#include "model/selection_model.h"

#include <utility>

namespace albor::model
{
    void SelectionModel::select(ItemKey key, std::size_t index) {
        // an unchanged selection is not a change, so nothing is reported (§18.3)
        if (!empty() && m_index == index && m_key == key)
            return;
        // the identity is stored first, because the notification reads it (§18.1, AD-11)
        m_key = std::move(key);
        // the position travels with the identity, because a list reads both (§18.3)
        m_index = index;
        // the change is reported once, which is what a no-op select avoids (§18.3)
        notify();
    }

    void SelectionModel::clear() {
        // clearing an empty model is not a change, so nothing is reported (§18.3)
        if (empty())
            return;
        // the identity is dropped first, because the notification reads it (§18.1, AD-11)
        m_key = ItemKey{};
        // the position follows the identity, because neither stands alone (§18.3)
        m_index = NO_INDEX;
        // the change is reported once, which is what a repeated clear avoids (§18.3)
        notify();
    }

    bool SelectionModel::empty() const {
        // the sentinel index is what names the empty selection, so it alone decides (§18.3)
        return m_index == NO_INDEX;
    }

    const ItemKey& SelectionModel::selected_key() const {
        // the selected identity is the empty key while nothing is selected (AD-11)
        return m_key;
    }

    std::size_t SelectionModel::selected_index() const {
        // the sentinel is what a caller compares against while nothing is selected (§18.3)
        return m_index;
    }

    bool SelectionModel::is_selected(const ItemKey& key) const {
        // nothing matches while nothing is selected, because there is no identity to compare (§18.1)
        return !empty() && m_key == key;
    }

    void SelectionModel::set_on_change(std::function<void()> handler) {
        // the work is borrowed, because the owner outlives the model it reports through (§18.3)
        m_on_change = std::move(handler);
    }

    void SelectionModel::notify() {
        // only an installed handler is told, because a list without a signal installs none (§18.3)
        if (m_on_change)
            m_on_change();
    }
} // namespace albor::model
