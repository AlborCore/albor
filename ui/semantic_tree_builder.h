#pragma once

#include "semantics/semantic_provider.h"
#include "ui/ui_node.h"

namespace albor
{
    // build the semantic tree of a ui tree, starting from any node because a root publishes nothing (§10.1, AD-09)
    [[nodiscard]] SemanticTree build_semantic_tree(const UiNode& root);

    // wrap any node in a provider that walks its subtree, which a container without its own provider needs (§10.1, AD-09)
    [[nodiscard]] std::unique_ptr<SemanticProvider> make_node_semantic_provider(const UiNode& node);
} // namespace albor
