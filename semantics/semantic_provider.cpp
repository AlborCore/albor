#include "semantics/semantic_provider.h"

namespace albor
{
    SemanticTree::SemanticTree() = default;

    std::size_t SemanticTree::add(SemanticNode node, std::size_t parent) {
        // the node is stored by value, because the tree outlives the provider that built it (§10.1)
        m_nodes.push_back(std::move(node));
        // the parent is recorded beside it, because the flat order alone loses the hierarchy (§10.2)
        m_parents.push_back(parent);
        // the index the node took is what an adapter reads it back at (§10.1)
        return m_nodes.size() - 1;
    }

    void SemanticTree::build_from(const SemanticProvider& provider) {
        // the walk starts at the provider, so a caller never has to add the root by hand (§10.1)
        walk(provider, NO_PARENT);
    }

    void SemanticTree::walk(const SemanticProvider& provider, std::size_t parent) {
        // the node comes before its children, which is the order an adapter announces them in (§10.1)
        const std::size_t index = add(provider.semantic_node(), parent);
        // the children are built one at a time, because a virtualized node must not materialize all of them (§10.1, AD-09)
        for (std::size_t i = 0; i < provider.semantic_child_count(); i++) {
            // the child is requested by index, which is what keeps a large view virtualized (§10.1, AD-09)
            std::unique_ptr<SemanticProvider> child = provider.semantic_child(i);
            // a provider that refuses to build a child contributes nothing, rather than a hole (§10.1)
            if (child == nullptr)
                continue;
            walk(*child, index);
        }
    }

    std::size_t SemanticTree::parent_of(std::size_t index) const {
        // an index past the end names no node, so it has no parent either (§10.1)
        if (index >= m_parents.size())
            return NO_PARENT;
        return m_parents[index];
    }

    std::vector<std::size_t> SemanticTree::children_of(std::size_t index) const {
        // the children are collected in order, which is the order assistive technology announces them (§10.2)
        std::vector<std::size_t> children;
        // every node is scanned, because the tree is flat and the parent links carry the shape (§10.1)
        for (std::size_t i = 0; i < m_parents.size(); i++) {
            // a node whose parent is this one is a child, which is what the scan is looking for (§10.1)
            if (m_parents[i] == index)
                children.push_back(i);
        }
        return children;
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
        // the parents are emptied with it, because a stale parent would attach a new node to an old one (§10.1)
        m_parents.clear();
    }
} // namespace albor
