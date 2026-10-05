#include "semantics/semantic_provider.h"

namespace albor
{
    SemanticTree::SemanticTree() = default;

    std::size_t SemanticTree::add(SemanticNode node) {
        // the node is stored by value, because the tree outlives the provider that built it (§10.1)
        m_nodes.push_back(std::move(node));
        // the index the node took is what an adapter reads it back at (§10.1)
        return m_nodes.size() - 1;
    }

    void SemanticTree::build_from(const SemanticProvider& provider) {
        // the walk starts at the provider, so a caller never has to add the root by hand (§10.1)
        walk(provider);
    }

    void SemanticTree::walk(const SemanticProvider& provider) {
        // the node comes before its children, which is the order an adapter announces them in (§10.1)
        add(provider.semantic_node());
        // the children are built one at a time, because a virtualized node must not materialize all of them (§10.1, AD-09)
        for (std::size_t index = 0; index < provider.semantic_child_count(); index++) {
            // the child is requested by index, which is what keeps a large view virtualized (§10.1, AD-09)
            std::unique_ptr<SemanticProvider> child = provider.semantic_child(index);
            // a provider that refuses to build a child contributes nothing, rather than a hole (§10.1)
            if (child == nullptr)
                continue;
            walk(*child);
        }
    }

    std::size_t SemanticTree::size() const { return m_nodes.size(); }

    const SemanticNode* SemanticTree::at(std::size_t index) const {
        // an index past the end names no node (§10.1)
        if (index >= m_nodes.size())
            return nullptr;
        return &m_nodes[index];
    }

    const SemanticNode* SemanticTree::find(std::string_view name) const {
        // the lookup is by accessible name, which is what an automation selector uses (§10.1)
        for (const SemanticNode& node : m_nodes)
            if (node.name() == name)
                return &node;
        // a name nothing carries is not a node, so the search fails (§10.1)
        return nullptr;
    }

    std::size_t SemanticTree::count_role(SemanticRole role) const {
        std::size_t count = 0;
        for (const SemanticNode& node : m_nodes)
            if (node.role() == role)
                count++;
        return count;
    }

    bool SemanticTree::empty() const { return m_nodes.empty(); }

    void SemanticTree::clear() {
        // the tree is emptied rather than reused, so a rebuild starts from nothing (§10.1)
        m_nodes.clear();
    }
} // namespace albor
