#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "semantics/semantic_node.h"

namespace albor
{
    // the interface a virtualized or custom render node implements to expose semantics on demand (§10.1, AD-09)
    class SemanticProvider
    {
    public:
        virtual ~SemanticProvider() = default;

        // build the semantic node that describes this provider, which an adapter reads (§10.1)
        [[nodiscard]] virtual SemanticNode semantic_node() const = 0;

        // hand out how many children the provider exposes, which avoids materializing them (§10.1, AD-09)
        [[nodiscard]] virtual std::size_t semantic_child_count() const = 0;

        // build the child at an index on demand, returning null when the index is past the end (§10.1, AD-09)
        [[nodiscard]] virtual std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const = 0;
    };

    // the flattened semantic tree an accessibility adapter walks, which materializes providers on demand (§10.1)
    class SemanticTree
    {
    public:
        // build an empty tree, which a provider then populates
        SemanticTree();

        // add a node to the tree and hand back its index, which is where an adapter reads it from (§10.1)
        std::size_t add(SemanticNode node);

        // build the tree from a provider, walking its children on demand (§10.1, AD-09)
        void build_from(const SemanticProvider& provider);

        // hand out how many nodes the tree holds (§10.1)
        [[nodiscard]] std::size_t size() const;

        // hand out the node at an index, returning null when the index is past the end (§10.1)
        [[nodiscard]] const SemanticNode* at(std::size_t index) const;

        // find the first node with the given accessible name, returning null when none matches (§10.1)
        [[nodiscard]] const SemanticNode* find(std::string_view name) const;

        // count how many nodes carry the given role, which an adapter uses to audit coverage (§10.1)
        [[nodiscard]] std::size_t count_role(SemanticRole role) const;

        // report whether the tree holds no node (§10.1)
        [[nodiscard]] bool empty() const;

        // empty the tree, which a rebuild starts from (§10.1)
        void clear();

    private:
        // walk a provider and its children depth first, which flattens the virtualized tree (§10.1, AD-09)
        void walk(const SemanticProvider& provider);

        // the flattened nodes, in depth first order (§10.1)
        std::vector<SemanticNode> m_nodes;
    };
} // namespace albor
