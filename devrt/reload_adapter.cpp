#include "devrt/reload_adapter.h"

#include <algorithm>
#include <utility>

namespace albor::devrt
{
    namespace
    {
        // find the child carrying an author assigned id, which is how an edit is matched (§12.1, AD-11)
        UiNode* child_with_id(UiNode& parent, const std::string& id) {
            // the children are walked, because only an identified node can be matched by name (§12.1, AD-11)
            for (std::size_t index = 0; index < parent.child_count(); index++) {
                // the child is read, because its identity is what is compared (§12.1, AD-11)
                UiNode* child = parent.child_at(index);
                // a child with no identity cannot be matched by name (§18.1, AD-11)
                if (child == nullptr || !child->identity().identified())
                    continue;
                // the ids must match exactly, because a prefix would claim the wrong node (§18.1, AD-11)
                if (child->identity().explicit_id == id)
                    return child;
            }
            // no child carries that id, which the caller treats as an addition (§12.1)
            return nullptr;
        }

        // forget every child the definition did not cover, which is how a removal happens (§12.1)
        std::size_t remove_uncovered(UiNode& parent, const std::vector<UiNode*>& covered) {
            // the count is what the report states (§12.1)
            std::size_t removed = 0;
            // the children are walked backwards, because removing shifts the ones after it (§12.1)
            for (std::size_t index = parent.child_count(); index > 0; index--) {
                // the position is one before the loop value, because the indices are zero based (§12.1)
                const std::size_t at = index - 1;
                // the child is read, because a pointer is what coverage was recorded as (§12.1)
                UiNode* child = parent.child_at(at);
                // a child the definition covered stays, because it still belongs (§12.1)
                if (child != nullptr && std::find(covered.begin(), covered.end(), child) != covered.end())
                    continue;
                // the child is removed, which hands it back to the caller to destroy (§8.1)
                const std::unique_ptr<UiNode> removed_child = parent.remove_child(child);
                // a removal that produced nothing was never a child (§8.1)
                if (removed_child == nullptr)
                    continue;
                // the count follows the removal, which is what the report states (§12.1)
                removed++;
            }
            // the count is returned whole (§12.1)
            return removed;
        }

        // reconcile one node's children against the definition's, which is the whole of a reload (§12.1)
        ReloadReport reconcile(UiNode& parent, const std::vector<IrNode>& wanted, ElementFactory factory) {
            // the report starts empty, because a node contributes only its own changes (§12.1)
            ReloadReport report;
            // the children the definition covered, which is what tells a removal from a keep (§12.1)
            std::vector<UiNode*> covered;
            // the definition is walked, because each entry claims or adds one child (§12.1)
            for (const IrNode& node : wanted) {
                // an identified entry is matched by its id, which is the identity the author declared (§12.1, AD-11)
                UiNode* match = nullptr;
                // only an identified entry may be matched by name (§18.1, AD-11)
                if (!node.id.empty()) {
                    // the existing children are searched for that identity (§12.1, AD-11)
                    match = child_with_id(parent, node.id);
                }
                // an unidentified entry falls back to the first child nobody has covered (§18.1, AD-11)
                if (match == nullptr && node.id.empty()) {
                    // the children are walked for the first uncovered slot (§12.1, AD-11)
                    for (std::size_t index = 0; index < parent.child_count(); index++) {
                        // the child at that slot is the candidate (§12.1, AD-11)
                        UiNode* candidate = parent.child_at(index);
                        // a candidate nobody covered is the match (§12.1, AD-11)
                        if (candidate != nullptr && std::find(covered.begin(), covered.end(), candidate) == covered.end()) {
                            // the candidate becomes the match (§12.1, AD-11)
                            match = candidate;
                            // the search stops at the first uncovered child (§12.1)
                            break;
                        }
                    }
                }
                // no match means the definition adds a node, which only a factory can build (§12.1, AD-02)
                if (match == nullptr) {
                    // without a factory nothing can be built, and the entry is skipped rather than faked (§12.1, AD-02)
                    if (factory == nullptr)
                        continue;
                    // the node is built by name, which is what the definition declared (§12.1)
                    std::unique_ptr<UiNode> built = factory(node.element);
                    // a factory that builds nothing leaves the tree as it was (§12.1)
                    if (built == nullptr)
                        continue;
                    // the id is assigned, because identity is what a later edit will match (§12.1, AD-11)
                    if (!node.id.empty())
                        built->set_explicit_id(node.id);
                    // the node is adopted, which takes ownership (§8.1)
                    parent.add_child(std::move(built));
                    // add_child hands back the parent, so the appended child is read from the end (§8.1)
                    UiNode* added = parent.child_at(parent.child_count() - 1);
                    // an adoption that stored nothing leaves nothing to cover (§12.1)
                    if (added == nullptr)
                        continue;
                    // the addition is covered too, or the removal below would delete what was just added (§12.1)
                    covered.push_back(added);
                    // the addition is reported, which is what a developer watches while typing (§12.1)
                    report.added++;
                    // the added node's own children are reconciled, which is the whole recursion (§12.1)
                    const ReloadReport inner = reconcile(*added, node.children, factory);
                    // the inner counts join the outer one, because a reload is a single outcome (§12.1)
                    report.added += inner.added;
                    // the removals join (§12.1)
                    report.removed += inner.removed;
                    // the preserved join (§12.1)
                    report.preserved += inner.preserved;
                    // the entry is consumed (§12.1)
                    continue;
                }
                // the match is covered, so a later entry cannot take the same child (§12.1, AD-11)
                covered.push_back(match);
                // the matched node keeps its instance, which is the state a reload preserves (§12.1, §12.2)
                report.preserved++;
                // its own children are reconciled, which is the whole recursion (§12.1)
                const ReloadReport inner = reconcile(*match, node.children, factory);
                // the inner additions join (§12.1)
                report.added += inner.added;
                // the inner removals join (§12.1)
                report.removed += inner.removed;
                // the inner preserved join (§12.1)
                report.preserved += inner.preserved;
            }
            // every child the definition did not cover is gone from the tree (§12.1)
            report.removed += remove_uncovered(parent, covered);
            // the report is returned whole (§12.1)
            return report;
        }
    } // namespace

    ReloadReport apply_reload(UiNode& root, const IrNode& definition, ElementFactory factory) {
        // the root is reconciled against the definition, because that is the one entry point a reload has (§12.1)
        ReloadReport report = reconcile(root, definition.children, factory);
        // the root's own id is applied when the definition names one, because a reload may name it (§12.1, AD-11)
        if (!definition.id.empty() && root.identity().explicit_id != definition.id)
            root.set_explicit_id(definition.id);
        // the report is returned whole, which is what the hook reports (§12.1)
        return report;
    }
} // namespace albor::devrt
