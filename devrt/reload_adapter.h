#pragma once

#include <memory>
#include <string>

#include "devrt/ir.h"
#include "ui/ui_node.h"

namespace albor::devrt
{
    // builds one element by name, which a reload needs for anything the tree did not already have (§12.1, AD-02)
    using ElementFactory = std::unique_ptr<UiNode> (*)(const std::string& element);

    // the result of one reconciliation, which is what a reload reports (§12.1)
    struct ReloadReport
    {
        // how many nodes the definition added, which is what a developer watches while typing (§12.1)
        std::size_t added = 0;

        // how many nodes the definition removed, which is what a developer watches while typing (§12.1)
        std::size_t removed = 0;

        // how many existing nodes kept their instance, which is the state a reload must preserve (§12.1, §12.2)
        std::size_t preserved = 0;

        // how many nodes changed element, which forces a rebuild and loses that node's state (§12.1)
        std::size_t replaced = 0;
    };

    // reconcile a tree against a definition, matching by the author assigned id first (§18.1, AD-11, AD-13)
    ReloadReport apply_reload(UiNode& root, const IrNode& definition, ElementFactory factory);
} // namespace albor::devrt
