#include "devrt/reload_adapter.h"

#include <algorithm>
#include <optional>

#include "runtime/binding_engine.h"
#include "text/text_engine.h"
#include "ui/ui_node.h"

namespace albor::devrt
{
    namespace
    {
        // find the slot of the child carrying an author assigned id, which is how an edit is matched (§12.1, AD-11)
        std::optional<std::size_t> slot_with_id(UiNode& parent, const std::string& id) {
            // the children are walked, because only an identified node can be matched by name (§12.1, AD-11)
            for (std::size_t index = 0; index < parent.child_count(); index++) {
                // the child is read, because its identity is what is compared (§12.1, AD-11)
                UiNode* child = parent.child_at(index);
                // a child with no identity cannot be matched by name (§18.1, AD-11)
                if (child == nullptr || !child->identity().identified())
                    continue;
                // the ids must match exactly, because a prefix would claim the wrong node (§18.1, AD-11)
                if (child->identity().explicit_id == id)
                    return index;
            }
            // no child carries that id, which the caller treats as an addition (§12.1)
            return std::nullopt;
        }

        // forget every child the definition did not cover, which is how a removal happens (§12.1)
        void remove_uncovered(UiNode& parent, const std::vector<UiNode*>& covered, ReloadReport& report) {
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
                report.removed++;
            }
        }

        // apply the definition's property assignments to a node, which makes an edited value visible (§12.1)
        void apply_properties(UiNode& node, const IrNode& definition) {
            // the assignments are walked, because a node declares a handful of them (§11.1)
            for (const IrProperty& property : definition.properties) {
                // the name resolves against the element, so a value only lands on the node that declares it (§11.1)
                static_cast<void>(apply_property(node, definition.element, property.name, property.value));
            }
            // a property write repaints the node, which is what makes the new value reach the next frame (§6.1)
            node.invalidate();
        }

        // reconcile one node's children against the definition's, which is the whole of a reload (§12.1)
        void reconcile(UiNode& parent, const IrNode& definition, const ReloadContext& context, ReloadReport& report) {
            // the children the definition covered, which is what tells a removal from a keep (§12.1)
            std::vector<UiNode*> covered;
            // the definition is walked, because each entry claims or adds one child (§12.1)
            for (const IrNode& node : definition.children) {
                // an identified entry is matched by its id, which is the identity the author declared (§12.1, AD-11)
                std::optional<std::size_t> slot;
                // only an identified entry may be matched by name (§18.1, AD-11)
                if (!node.id.empty())
                    slot = slot_with_id(parent, node.id);
                // an unidentified entry falls back to the first child nobody has covered (§18.1, AD-11)
                if (!slot && node.id.empty()) {
                    // the children are walked for the first uncovered slot (§12.1, AD-11)
                    for (std::size_t index = 0; index < parent.child_count(); index++) {
                        // the child at that slot is the candidate (§12.1, AD-11)
                        UiNode* candidate = parent.child_at(index);
                        // a candidate nobody covered is the match (§12.1, AD-11)
                        if (candidate != nullptr && std::find(covered.begin(), covered.end(), candidate) == covered.end()) {
                            // the slot becomes the match, because a rebuild needs the position too (§12.1)
                            slot = index;
                            // the search stops at the first uncovered child (§12.1)
                            break;
                        }
                    }
                }
                // no match means the definition adds a node, which only a factory can build (§12.1, AD-02)
                if (!slot) {
                    // without a factory nothing can be built, and the entry is skipped rather than faked (§12.1, AD-02)
                    if (!context.factory || context.engine == nullptr)
                        continue;
                    // the node is built by name over the engines the window owns (§12.1)
                    std::unique_ptr<UiNode> built = context.factory(node.element, *context.engine, *context.text);
                    // a factory that builds nothing leaves the tree as it was (§12.1)
                    if (built == nullptr)
                        continue;
                    // the id is assigned, because identity is what a later edit will match (§12.1, AD-11)
                    if (!node.id.empty())
                        built->set_explicit_id(node.id);
                    // the properties land before the node is shown, so its first frame already carries them (§12.1)
                    apply_properties(*built, node);
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
                    reconcile(*added, node, context, report);
                    // the entry is consumed (§12.1)
                    continue;
                }
                // the matched node is read at its slot, which the identity or the position found (§12.1)
                UiNode* match = parent.child_at(*slot);
                // a matched node whose element changed is a different class, so its instance cannot be kept (§12.1)
                if (context.element_of && context.factory && context.engine != nullptr && context.element_of(*match) != node.element) {
                    // the replacement is built before anything is torn down, so a refusal leaves the old node (§12.1)
                    std::unique_ptr<UiNode> replacement = context.factory(node.element, *context.engine, *context.text);
                    // a factory that builds nothing keeps the node it had, because a refusal changes nothing (§12.1)
                    if (replacement != nullptr) {
                        // the id carries across, because the identity is the author's and not the class's (§12.1, AD-11)
                        if (!node.id.empty())
                            replacement->set_explicit_id(node.id);
                        // the properties land before the node is shown, so its first frame already carries them (§12.1)
                        apply_properties(*replacement, node);
                        // the old instance goes, which is the state this rebuild loses (§12.1, §12.2)
                        static_cast<void>(parent.remove_child(match));
                        // the replacement takes that slot, which keeps the order the definition declared (§12.1)
                        parent.insert_child(*slot, std::move(replacement));
                        // the inserted node is what the coverage below must protect (§12.1)
                        match = parent.child_at(*slot);
                        // the rebuild is reported, which is what a developer watches when an element changed (§12.1)
                        report.replaced++;
                        // the replacement's own children are reconciled, which is the whole recursion (§12.1)
                        reconcile(*match, node, context, report);
                        // the replacement is covered too, or the removal below would delete it (§12.1)
                        covered.push_back(match);
                        // the entry is consumed (§12.1)
                        continue;
                    }
                }
                // the match is covered, so a later entry cannot take the same child (§12.1, AD-11)
                covered.push_back(match);
                // the matched node keeps its instance, which is the state a reload preserves (§12.1, §12.2)
                report.preserved++;
                // its own properties are re-applied, because an edited value is what this reload is for (§12.1)
                apply_properties(*match, node);
                // its own children are reconciled, which is the whole recursion (§12.1)
                reconcile(*match, node, context, report);
            }
            // every child the definition did not cover is gone from the tree (§12.1)
            remove_uncovered(parent, covered, report);
        }
    } // namespace

    ReloadReport apply_reload(UiNode& root, const IrNode& definition, const ReloadContext& context) {
        // the report starts empty, because a reload is a single outcome (§12.1)
        ReloadReport report;
        // the root is reconciled against the definition, because that is the one entry point a reload has (§12.1)
        reconcile(root, definition, context, report);
        // the root's own id is applied when the definition names one, because a reload may name it (§12.1, AD-11)
        if (!definition.id.empty() && root.identity().explicit_id != definition.id)
            root.set_explicit_id(definition.id);
        // the root's own properties are applied last, because the tree is already the shape the file declares (§12.1)
        apply_properties(root, definition);
        // the report is returned whole, which is what the hook reports (§12.1)
        return report;
    }
} // namespace albor::devrt
