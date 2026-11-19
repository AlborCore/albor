#pragma once

#include "albor/semantics/semantic_provider.h"
#include "albor/ui/ui_node.h"

namespace albor
{
    // build the semantic tree of a ui tree, starting from any node because a root publishes nothing (§10.1, AD-09)
    [[nodiscard]] SemanticTree build_semantic_tree(const UiNode& root);

    // wrap any node in a provider that walks its subtree, which a container without its own provider needs (§10.1, AD-09)
    [[nodiscard]] std::unique_ptr<SemanticProvider> make_node_semantic_provider(const UiNode& node);

    // apply the hover text a node carries as its accessible name or description (§8.3, §10.1)
    [[nodiscard]] SemanticNode with_tooltip(SemanticNode node, const UiNode& source);
} // namespace albor
