#pragma once

#include <filesystem>
#include <optional>

#include "compiler/ast.h"
#include "compiler/type_checker.h"
#include "devrt/ir.h"

namespace albor::compiler
{
    // serialize a static tree; a repeat, a condition or a slot refuses it because the IR is a snapshot (§12.1, AD-25)
    [[nodiscard]] std::optional<devrt::IrNode> serialize_component(const ComponentDeclaration& component);

    // serialize a checked component with its imports expanded, which the dev runtime can build (§12.1, AD-25)
    [[nodiscard]] std::optional<devrt::IrNode> serialize_component(const CheckedComponent& component, const std::vector<ImportedComponent>& imported_components, const std::filesystem::path& source_path = {});
} // namespace albor::compiler
