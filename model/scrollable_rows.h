#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/scrollable_content.h"
#include "model/item_key.h"
#include "ui/ui_node.h"

namespace albor::model
{
    // the rows a scroll container drives, which builds only the window and recycles the rest (§8.2, AD-13)
    template <typename TItem>
    class ScrollableRows final : public UiNode, public ScrollableContent
    {
    public:
        // the signature of a row builder, which materializes one row from its item (§18.1)
        using Builder = std::unique_ptr<UiNode> (*)(void* context, const TItem& item, std::size_t index);

        // the row updater writes an existing row from the item it now shows, and is plain too (§18.1)
        using Updater = void (*)(void* context, UiNode& row, const TItem& item, std::size_t index);

        // the signature of a key reader, which reads the identity an item carries (§18.1, AD-13)
        using KeyReader = ItemKey (*)(void* context, const TItem& item);

        // the rows are owned by the view, so it is not copyable (§8.1, §18.1)
        ScrollableRows(const ScrollableRows&) = delete;
        ScrollableRows& operator=(const ScrollableRows&) = delete;
        ScrollableRows(ScrollableRows&&) = delete;
        ScrollableRows& operator=(ScrollableRows&&) = delete;
        ~ScrollableRows() = default;

        // build a view over uniform rows of the given height, which is what makes an index a position (§8.2, AD-13)
        ScrollableRows(float row_extent, std::size_t recycle_limit) :
            m_row_extent(row_extent), m_recycle_limit(recycle_limit) {}

        // report the extent the whole content would take, which is what the scrollbar measures against (§8.2)
        [[nodiscard]] Size content_extent() const override {
            // a row height of zero has no extent at all, which is what an undeclared height means (§8.2)
            if (m_row_extent <= 0.0f)
                return Size{m_measured_size.width, 0.0f};
            // the content is as tall as the count of rows the items name (§8.2, AD-13)
            const std::size_t count = m_items == nullptr ? 0 : m_items->size();
            return Size{m_measured_size.width, static_cast<float>(count) * m_row_extent};
        }

        // build only the rows the window intersects, which keeps a long list cheap (§8.2, AD-13)
        void set_scroll_viewport(const ScrollViewport& viewport) override {
            // the window is stored first, so the reconcile walks a view that already knows it (§8.2)
            m_window = viewport;
            // the rows are rebuilt now, because the offset is what decides which ones exist (§8.2, AD-13)
            rebuild();
        }

        // measure the whole content rather than the built rows, because a scroll range needs the whole extent (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the content takes the width it is offered and its full row count for the height (§8.2)
            const Size content{.width = constraints.maximum.width, .height = content_extent().height};
            // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
            m_measured_size = constraints.constrain(content);
            // the parent reads the whole extent, which is what a scroll container needs (§8.2)
            out_size = m_measured_size;
        }

        void arrange(const Rect& bounds) override {
            // the assigned bounds replace the previous ones, while the measured size stands (§8.2)
            m_bounds = bounds;
            // the rows and their slots are walked together, because each needs its own index (§8.2, AD-13)
            for (std::size_t built = 0; built < child_count() && built < m_slots.size(); built++) {
                // the row sits at its own place in the content, which the viewport offset already moved (§8.2)
                const std::size_t index = m_slots[built].index;
                // the row fills the content width, because a log row spans the viewport (§8.2)
                const Rect row = Rect::from_origin_size(Point{.x = bounds.x, .y = bounds.y + static_cast<float>(index) * m_row_extent}, Size{.width = bounds.width, .height = m_row_extent});
                // the row is placed only when it asked for a pass or its place moved (§8.2)
                arrange_if_changed(*child_at(built), row);
            }
        }

        // override: take the height the container settled on, which is what the window divides by (§8.2, AD-13)
        void set_row_extent(float row_extent) override {
            // an unchanged height lays the same rows out, so nothing is rebuilt (§8.2)
            if (m_row_extent == row_extent)
                return;
            // the height is stored first, so the reconcile walks a view that already knows it (§8.2)
            m_row_extent = row_extent;
            // the content grew or shrank, so the whole window is decided again (§8.2, AD-13)
            rebuild();
        }

        // hand out the height one row takes, which is what the window divides by (§8.2)
        [[nodiscard]] float row_extent() const { return m_row_extent; }

        // take the items the rows are built from, which are borrowed rather than copied (§8.2)
        void set_items(const std::vector<TItem>& items) {
            // the items are borrowed, because the owner outlives the rows it feeds (§8.2)
            m_items = &items;
            // the item set changed, so every row is decided again; the address cannot decide it (§8.2, AD-13)
            rebuild();
        }

        // install the three plain functions the rows are built and matched through (§13.3)
        void set_view(Builder builder, Updater updater, KeyReader key_reader, void* context) {
            // a changed builder or updater cannot read rows the previous one made (§13.3, §18.1)
            const bool incompatible = m_builder != builder || m_updater != updater;
            // the built rows are released first, because the new updater would cast them wrongly (§18.1)
            if (incompatible)
                release_rows();
            // the view is swapped in place, so a window after this point uses the new functions (§13.3)
            m_builder = builder;
            m_updater = updater;
            m_key_reader = key_reader;
            // the context is what the plain functions read, because they cannot capture one (§13.3)
            m_context = context;
            // the rows were built by the previous view, so they are decided again (§18.1)
            rebuild();
        }

        // hand out how many rows are built, which is the window rather than the item count (§8.2, AD-13)
        [[nodiscard]] std::size_t materialized() const { return child_count(); }

        // hand out the first item index the window covers, which is where the built rows begin (§8.2, AD-13)
        [[nodiscard]] std::size_t first_index() const { return m_first; }

        // hand out how many rows left the window and are held ready to be reused (§8.2, AD-13)
        [[nodiscard]] std::size_t recycled() const { return m_pool.size(); }

    private:
        // what one built row stands for, which is what places it in the content after a rebuild (§8.2, AD-13)
        struct Slot
        {
            // the identity this row was built for, which is what a later window matches on (§18.1, AD-13)
            ItemKey key;

            // the item index this row shows, which is its place in the content (§8.2, AD-13)
            std::size_t index = 0;
        };

        // release every built row and every pooled one, which is what an incompatible view change needs (§18.1, AD-13)
        void release_rows() {
            // the built rows leave the tree first, because the tree owns what it walks (§8.1)
            while (child_count() > 0)
                (void)remove_child(child_at(0));
            // the slots go with them, because no row stands behind a slot any more (§18.1)
            m_slots.clear();
            // the pool is released too, because a pooled row carries the old updater's expectations (§18.1)
            m_pool.clear();
        }

        // hand back the index range the window intersects, which is what virtualization decides (§8.2, AD-13)
        [[nodiscard]] std::pair<std::size_t, std::size_t> window_range() const {
            // without items or a row height there is no window to compute, so nothing is built (§8.2)
            if (m_items == nullptr || m_row_extent <= 0.0f)
                return {0, 0};
            // the top reaches back past the offset by the overscan, which is what hides a rebuild at the edge (§8.2)
            const float top = m_window.offset - m_window.overscan;
            // a negative top clamps to the first row, because there is nothing above the content (§8.2)
            const std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(top / m_row_extent)));
            // a window that starts past the end covers nothing, which is what scrolling past the last row does (§8.2)
            if (first >= m_items->size())
                return {m_items->size(), m_items->size()};
            // the bottom reaches past the viewport by the same overscan (§8.2)
            const float bottom = std::max(0.0f, m_window.offset + m_window.viewport.height + m_window.overscan);
            // the reach is how many rows reach that far, which is what the window covers (§8.2, AD-13)
            const std::size_t reach = static_cast<std::size_t>(std::ceil(bottom / m_row_extent));
            // the end is clamped to what the items hold, because there is no row past the last item (§8.2)
            return {first, std::min(m_items->size(), std::max(first, reach))};
        }

        // rebuild the window, keeping the rows that survived and recycling the rest (§8.2, AD-13)
        void rebuild() {
            // the previous slots are copied first, because the detach below empties the children they describe (§8.2)
            const std::vector<Slot> previous = m_slots;
            // every built row is detached back into this view, which is the only owner a row ever has (§8.1)
            std::vector<std::unique_ptr<UiNode>> detached;
            // the detach runs until no child is left, because the window is rebuilt from nothing (§8.1)
            while (child_count() > 0)
                detached.push_back(remove_child(child_at(0)));
            // the detached rows are indexed by the identity they were built for, so a match is not rebuilt (§18.1, AD-13)
            std::unordered_map<ItemKey, std::unique_ptr<UiNode>> survivors;
            // each detached row joins under its own key, which is what a later window matches against (§18.1, AD-13)
            for (std::size_t built = 0; built < detached.size() && built < previous.size(); built++)
                if (detached[built] != nullptr)
                    survivors.emplace(previous[built].key, std::move(detached[built]));
            // the window decides which items are built, which is the whole of virtualization (§8.2, AD-13)
            const auto [first, last] = window_range();
            // the first index is remembered, because a row's place in the content follows from it (§8.2)
            m_first = first;
            // the fresh slots replace the old ones, which is what a rebuild leaves behind (§18.1)
            std::vector<Slot> slots;
            // without the three plain functions no row can be built, so the window stays empty (§13.3)
            if (m_items == nullptr || m_builder == nullptr || m_updater == nullptr || m_key_reader == nullptr) {
                // the slots are emptied, because no row stands behind them any more (§18.1)
                m_slots.clear();
                return;
            }
            // the identities the window wants, which is what decides which detached rows survive (§18.1, AD-13)
            std::vector<ItemKey> wanted;
            // each item in the window names the row it would show, which is what the match reads (§18.1, AD-13)
            for (std::size_t index = first; index < last; index++)
                wanted.push_back(m_key_reader(m_context, (*m_items)[index]));
            // the survivors the window does not name are recycled rather than destroyed (§8.2, AD-13)
            for (auto entry = survivors.begin(); entry != survivors.end();) {
                // a row the window wants stays in the map, because a match is not rebuilt (§18.1, AD-13)
                if (std::find(wanted.begin(), wanted.end(), entry->first) != wanted.end()) {
                    ++entry;
                    continue;
                }
                // a row the pool can take is held ready rather than rebuilt (§8.2, AD-13)
                if (m_pool.size() < m_recycle_limit)
                    m_pool.push_back(std::move(entry->second));
                // the entry leaves the map either way, because it is no longer a match (§18.1)
                entry = survivors.erase(entry);
            }
            // every item in the window is placed, in item order, which is the order the content shows (§8.1, §18.1)
            for (std::size_t index = first; index < last; index++) {
                // the item is the one this row shows (§18.1)
                const TItem& item = (*m_items)[index];
                // the identity was decided in the wanted pass, and the row is read again to look it up (§18.1, AD-13)
                const ItemKey key = wanted[index - first];
                // a surviving row is looked up by its key, because a match must not rebuild what it matched (§18.1, AD-13)
                const auto found = survivors.find(key);
                // a matched row is the one kept, and a recycled one is the next held ready (§8.2, AD-13)
                std::unique_ptr<UiNode> row;
                // a matched row survives, so its state is not lost to a scroll that did not change its item (§18.1, AD-13)
                if (found != survivors.end()) {
                    // the matched row is taken out of the survivor map, which is where it was kept (§18.1)
                    row = std::move(found->second);
                    // the survivor is consumed, because one item owns one row (§18.1, AD-13)
                    survivors.erase(found);
                }
                // an unmatched row with nothing pooled is the only allocation a scroll makes (§8.2, AD-13)
                else if (!m_pool.empty()) {
                    // the pooled row is reused, which is what stops a scroll allocating per row (§8.2, AD-13)
                    row = std::move(m_pool.back());
                    // the pool shrinks by the row that took one (§8.2)
                    m_pool.pop_back();
                }
                // a builder with nothing to reuse is the only path that allocates a node (§8.2, AD-13)
                else
                    row = m_builder(m_context, item, index);
                // a builder that returned nothing has no row to place, so the item goes unshown (§18.1)
                if (row == nullptr)
                    continue;
                // the row is rewritten from the item it now shows, which is all a recycled row needs (§18.1, AD-13)
                m_updater(m_context, *row, item, index);
                // the row joins the fresh order, which is what the arrange pass walks (§8.1)
                slots.push_back(Slot{.key = key, .index = index});
                // the row is read before it moves, because add_child hands back the parent (§8.1)
                UiNode* adopted = row.get();
                // the view adopts the row, because the tree owns what it walks (§8.1)
                add_child(std::move(row));
                // the row is measured here, because the frame that built it has already passed its measure pass (§8.2)
                Size measured{};
                adopted->measure(SizeConstraints::tight(Size{.width = m_window.viewport.width, .height = m_row_extent}), measured);
            }
            // the slots are the ones the adopted rows stand for, which is what the arrange pass reads (§8.2)
            m_slots = std::move(slots);
        }

        // the height one row takes, which is what turns an index into a position (§8.2, AD-13)
        float m_row_extent = 0.0f;

        // how many rows may be held ready to be reused, which bounds what a long scroll costs (§8.2, AD-13)
        std::size_t m_recycle_limit = 32;

        // the items the rows are built from, which are borrowed because their owner outlives them (§8.2)
        const std::vector<TItem>* m_items = nullptr;

        // the window the scroll container is showing, which is what decides the rows (§8.2)
        ScrollViewport m_window;

        // the first item index the built rows cover, which is what places them in the content (§8.2, AD-13)
        std::size_t m_first = 0;

        // what each built row stands for, in the order the children were adopted (§8.2, AD-13)
        std::vector<Slot> m_slots;

        // the rows that left the window and are held ready to be written rather than rebuilt (§8.2, AD-13)
        std::vector<std::unique_ptr<UiNode>> m_pool;

        // the builder that materializes one row, which is a plain function over the context (§13.3)
        Builder m_builder = nullptr;

        // the updater that rewrites one row in place, which is a plain function over the context (§13.3)
        Updater m_updater = nullptr;

        // the reader that names the identity an item carries, which is a plain function over the context (§13.3)
        KeyReader m_key_reader = nullptr;

        // the context the three plain functions read, which they cannot capture on their own (§13.3)
        void* m_context = nullptr;
    };
} // namespace albor::model
