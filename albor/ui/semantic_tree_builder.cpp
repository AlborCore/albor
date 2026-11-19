#include "albor/ui/semantic_tree_builder.h"

#include <memory>
#include <utility>

namespace albor
{
    namespace
    {
        // the provider that walks a ui tree, which is what makes any node a starting point (§10.1, AD-09)
        class NodeSemanticProvider final : public SemanticProvider
        {
        public:
            // the node is borrowed, because the ui tree owns it and outlives every tree built from it (§8.1, AD-09)
            explicit NodeSemanticProvider(const UiNode& node) :
                m_node(&node) {}

            [[nodiscard]] SemanticNode semantic_node() const override {
                // a node that publishes its own semantics is asked, because that is the one true description (§10.1)
                const auto* provider = dynamic_cast<const SemanticProvider*>(&*m_node);
                // the published node is the base a tooltip describes, so it is held by value (§10.1)
                SemanticNode node = provider != nullptr ? provider->semantic_node() : SemanticNode{SemanticRole::Group};
                // a node that publishes none is announced as a group, which is what a container is (§10.1, §8)
                if (provider == nullptr) {
                    // the author id travels, because automation selects by it and by nothing else (AD-11, §18.1)
                    node.set_explicit_id(m_node->identity().explicit_id);
                    // the bounds travel, because an adapter places the announcement where it was drawn (§10.1)
                    node.set_bounds(m_node->bounds());
                    // an unnamed group announces no name, which is the quiet default (§10.1)
                    node.set_name("");
                }
                // the hover text a node carries is a name when it has none and a description otherwise (§8.3, §10.1)
                return with_tooltip(std::move(node), *m_node);
            }

            [[nodiscard]] std::size_t semantic_child_count() const override {
                // a node that publishes its own children is asked, because it decides what it exposes (§10.1, AD-09)
                if (const auto* provider = dynamic_cast<const SemanticProvider*>(&*m_node))
                    return provider->semantic_child_count();
                // any other node exposes only the visible children the layout walks (§10.1, AD-09, §18.2)
                std::size_t visible = 0;
                for (std::size_t index = 0; index < m_node->child_count(); index++) {
                    const UiNode* child = m_node->child_at(index);
                    if (child != nullptr && child->visible())
                        visible++;
                }
                return visible;
            }

            [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
                // a node that publishes its own children is asked first, because it may expose fewer (§10.1, AD-09)
                if (const auto* provider = dynamic_cast<const SemanticProvider*>(&*m_node)) {
                    // the published child is wrapped, because the ui tree stays the only owner (§10.1, AD-09)
                    std::unique_ptr<SemanticProvider> child = provider->semantic_child(index);
                    // a provider that refuses a child contributes nothing, rather than a hole (§10.1)
                    if (child == nullptr)
                        return nullptr;
                    // the child is returned whole, because the walk below cannot improve on it (§10.1)
                    return child;
                }
                // the position counts only visible children, because a hidden row is not a reader stop (§10.1, AD-09)
                std::size_t position = 0;
                for (std::size_t child_index = 0; child_index < m_node->child_count(); child_index++) {
                    UiNode* child = m_node->child_at(child_index);
                    if (child == nullptr || !child->visible())
                        continue;
                    if (position != index) {
                        position++;
                        continue;
                    }
                    // the walk continues through the child, which is what reaches a widget buried in containers (§10.1)
                    return std::make_unique<NodeSemanticProvider>(*child);
                }
                // an index past the visible children names no child, which the walk treats as absent (§10.1)
                return nullptr;
            }

        private:
            // the node being announced, which the ui tree owns (§8.1)
            const UiNode* m_node;
        };
    } // namespace

    SemanticNode with_tooltip(SemanticNode node, const UiNode& source) {
        // a node with no hover text keeps the node it published, which is the quiet default (§8.3)
        if (source.tooltip().empty())
            return node;
        // an unnamed control takes the tooltip as its name, which is what an icon-only action needs (§10.1)
        if (node.name().empty())
            node.set_name(source.tooltip());
        // a named control carries the tooltip as its description, which a reader speaks beside it (§10.1)
        else if (node.name() != source.tooltip())
            node.set_description(source.tooltip());
        // the node is handed back whole, which is what the walk reads (§10.1)
        return node;
    }

    SemanticTree build_semantic_tree(const UiNode& root) {
        // the tree is empty first, because the walk fills it from the root (§10.1)
        SemanticTree tree;
        // the walk starts at a provider over the root, which any node can be (§10.1, AD-09)
        tree.build_from(NodeSemanticProvider(root));
        // the tree is returned whole, which is what an adapter or a command reads (§10.1)
        return tree;
    }

    std::unique_ptr<SemanticProvider> make_node_semantic_provider(const UiNode& node) {
        // the generic provider walks the subtree, which is what a component facade needs (§10.1, AD-09)
        return std::make_unique<NodeSemanticProvider>(node);
    }
} // namespace albor
