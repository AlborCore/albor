#pragma once

#include "semantics/semantic_provider.h"
#include "ui/ui_node.h"

namespace albor
{
    // build the semantic tree of a ui tree, starting from any node because a root publishes nothing (§10.1, AD-09)
    [[nodiscard]] SemanticTree build_semantic_tree(const UiNode& root);
} // namespace albor
