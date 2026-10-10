#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "model/item_key.h"
#include "ui/ui_node.h"

namespace albor::model
{
    // the row view a repeater owns, which materializes one node per item and reconciles them by identity (§18
    template <typename TItem>
    class KeyedView
    {
    public:
        // the signature of a row builder, which materializes one row from its item (§13
        using Builder = std::unique_ptr<UiNode> (*)(void* context, const TItem& item, std::size_t index);
        // the row updater writes an existing row from the item it now shows, and is plain too (§13
        using Updater = void (*)(void* context, UiNode& row, const TItem& item, std::size_t index);

        // the signature of a key reader, which reads the identity an item carries (§18.1, AD-13)
        using KeyReader = ItemKey (*)(void* context, const TItem& item);

        // build a view whose rows live inside the given parent at the given offset (§18.1, AD-13)
        KeyedView(UiNode* parent, std::size_t offset) :
            m_parent(parent), m_offset(offset) {}

        // the rows are owned by the parent node, so the view is not copyable (§8.1, §18.1)
        KeyedView(const KeyedView&) = delete;
        KeyedView& operator=(const KeyedView&) = delete;
        KeyedView(KeyedView&&) = delete;
        KeyedView& operator=(KeyedView&&) = delete;
        ~KeyedView() = default;

        // reconcile the rows against the given items, which matches by key and touches only what differs (§18.1, AD-13)
        void reconcile(void* context, std::span<const TItem> items, Builder builder, Updater updater, KeyReader key_reader) {
            // the surviving rows are kept alive, because a match must not destroy the row it matches (§18.1, AD-13)
            std::unordered_map<ItemKey, std::unique_ptr<UiNode>> survivors;
            // every row is detached, because the parent order is rebuilt from the item order (§8.1, §18.1)
            for (const Row& row : m_rows) {
                // the parent hands ownership back, so the row stays alive while the reconcile runs (§8.1, §18.1)
                std::unique_ptr<UiNode> detached = m_parent->remove_child(row.node);
                // the row is indexed under its key, so the item that carries it finds it (§18.1, AD-13)
                survivors.emplace(row.key, std::move(detached));
            }
            // the order is rebuilt in item order, which is the order the view shows (§18.1)
            std::vector<Row> next;
            next.reserve(items.size());
            // each item is placed, because the parent order follows the item order (§8.1, §18.1)
            for (std::size_t index = 0; index < items.size(); index++) {
                // the item is the one this row shows, which is what the builder and the updater both read (§18.1)
                const TItem& item = items[index];
                // the identity decides whether the row is reused, which is the whole point of the key (§18.1, AD-13)
                const ItemKey key = key_reader(context, item);
                // the surviving rows are searched, because a match is what keeps the row alive (§18.1, AD-13)
                const auto found = survivors.find(key);
                // a matched row is updated rather than rebuilt, so its state survives the reconcile (§18.1, AD-13)
                if (found != survivors.end()) {
                    // the row is updated in place, which is what makes a move a move rather than a rebuild (§18.1, AD-13)
                    updater(context, *found->second, item, index);
                    // the row joins the fresh order, and its owner stays the survivor map until adoption (§18.1)
                    next.push_back(Row{.key = key, .node = found->second.get()});
                    continue;
                }
                // an unmatched item has no row, so one is materialized for it (§18.1)
                std::unique_ptr<UiNode> built = builder(context, item, index);
                // the new row joins the fresh order, and the fresh map keeps it alive (§18.1)
                next.push_back(Row{.key = key, .node = built.get()});
                fresh_rows.emplace(key, std::move(built));
            }
            // the fresh order replaces the old one, which is what a reconcile leaves behind (§18.1)
            m_rows = std::move(next);
            // the rows are adopted in item order, which is where the ownership moves to the parent (§8.1, §18.1)
            for (std::size_t index = 0; index < m_rows.size(); index++) {
                // the row's owner is found by key, because a surviving row lives in the survivor map (§18.1, AD-13)
                const auto survivor = survivors.find(m_rows.at(index).key);
                // a row the map holds is adopted from it (§8.1, §18.1)
                if (survivor != survivors.end()) {
                    m_parent->insert_child(m_offset + index, std::move(survivor->second));
                    // the survivor is consumed, because one item owns one row (§18.1, AD-13)
                    survivors.erase(survivor);
                    continue;
                }
                // a row the map does not hold is a fresh one, which the fresh map owns (§18.1)
                const auto built = fresh_rows.find(m_rows.at(index).key);
                // the fresh row is adopted from it (§8.1, §18.1)
                m_parent->insert_child(m_offset + index, std::move(built->second));
                // the fresh row is consumed, because one item owns one row (§18.1)
                fresh_rows.erase(built);
            }
            // whatever the items no longer name is destroyed here, which is what removes a row (§18.1, AD-13)
            survivors.clear();
        }

        // hand out how many rows the view holds, which is a count of items rather than of positions (§18.1)
        [[nodiscard]] std::size_t size() const { return m_rows.size(); }

        // report whether the view holds no row, which a container asks before it lays out (§8.2, §18.1)
        [[nodiscard]] bool empty() const { return m_rows.empty(); }

        // hand out the row at the given position, which is how a host reads a row back (§18.1)
        [[nodiscard]] UiNode* row_at(std::size_t index) const { return m_rows.at(index).node; }

        // hand out the identity of the row at the given position, which a test asserts against (§18.1, AD-13)
        [[nodiscard]] const ItemKey& key_at(std::size_t index) const { return m_rows.at(index).key; }

    private:
        // one materialized row and the identity it was built for, which is what a match compares (§18.1, AD-13)
        struct Row
        {
            // the identity this row shows, which is what makes the row the same row across a reconcile (§18.1, AD-13)
            ItemKey key;

            // the node, which the parent owns while the view is at rest (§8.1, §18.1)
            UiNode* node = nullptr;
        };

        // the parent the rows hang from, because a repeater is a child of the container that holds it (§8.1, §18.1)
        UiNode* m_parent = nullptr;

        // the position the first row occupies, because rows keep the place their repeat was written (§8.1, §18.1)
        std::size_t m_offset = 0;

        // the rows in item order, which is the order the parent shows (§18.1)
        std::vector<Row> m_rows;

        // the rows a reconcile built, which are kept alive until the parent adopts them (§18.1)
        std::unordered_map<ItemKey, std::unique_ptr<UiNode>> fresh_rows;
    };
} // namespace albor::model
