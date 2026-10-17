#pragma once

#include <optional>

#include "compiler/ast.h"
#include "devrt/ir.h"

namespace albor::compiler
{
    // serialize a static tree; a repeat, a condition or a slot refuses it because the IR is a snapshot (§12.1, AD-25)
    [[nodiscard]] std::optional<devrt::IrNode> serialize_component(const ComponentDeclaration& component);
} // namespace albor::compiler
