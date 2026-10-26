#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>

#include "devrt/element_table.h"
#include "devrt/ir.h"

namespace albor
{
    class BindingEngine;
    class TextEngine;
    class UiNode;
} // namespace albor

namespace albor::devrt
{
    // what one reload needs to change a tree, which an extension installs over the engines it was given (§12.1)
    struct ReloadContext
    {
        // the reactive engine the built nodes declare their properties over, which a window owns for its run (§7.3)
        BindingEngine* engine = nullptr;

        // the text engine the built nodes measure with, which a window owns for its run (§9.1)
        TextEngine* text = nullptr;

        // the factory that builds a node from an element name, which is empty when the extension built none (§12.1, AD-02)
        NodeFactory factory;

        // the resolver that names the element a live node was built from, which finds a changed element (§12.1, AD-01)
        std::function<std::string_view(const UiNode&)> element_of;

        // the directory an embedded image resolves against, or empty to use the path as written (§6.4, §12.1)
        std::filesystem::path asset_root;
    };

    // what one reload changed, which a developer watches while typing and a test asserts on (§12.1)
    struct ReloadReport
    {
        // the nodes the definition created, which is what a new element in the file produces (§12.1)
        std::size_t added = 0;

        // the nodes the definition dropped, which is what a removed element produces (§12.1)
        std::size_t removed = 0;

        // the nodes that kept their instance, which is the state a reload must preserve (§12.1, §12.2)
        std::size_t preserved = 0;

        // the nodes that changed element, which forces a rebuild and loses that node's state (§12.1, §12.2)
        std::size_t replaced = 0;
    };

    // reconcile a tree against a definition, matching by the author assigned id first (§18.1, AD-11, AD-13)
    ReloadReport apply_reload(UiNode& root, const IrNode& definition, const ReloadContext& context);
} // namespace albor::devrt
