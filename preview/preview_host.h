#pragma once

#include <memory>
#include <string>

#include "runtime/binding_engine.h"
#include "ui/ui_node.h"

namespace albor::preview
{
    // what one preview build produced, which is what the host window shows (§12.2)
    struct PreviewResult
    {
        // the tree the source described, which is null when the source could not be read (§12.2)
        std::unique_ptr<UiNode> root;

        // the diagnostics the compiler produced, which a preview window shows rather than swallows (§12.2)
        std::size_t diagnostics = 0;
    };

    // build a ui tree from a .ui source, using the compiler front end and the real runtime widgets (§12.2)
    [[nodiscard]] PreviewResult build(const std::string& source, BindingEngine& engine);
} // namespace albor::preview
