#pragma once

#include <optional>

#include "compiler/ast.h"
#include "devrt/ir.h"

namespace albor::compiler
{
    // serialize one component's element tree into the dev IR, which the dev service hands the runtime (§12.1, AD-12)
    [[nodiscard]] std::optional<devrt::IrNode> serialize_component(const ComponentDeclaration& component);
} // namespace albor::compiler
