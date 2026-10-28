#pragma once

#include <filesystem>
#include <memory>

#include "compiler/type_checker.h"

namespace albor::compiler
{
    // expand imported instances into their declared trees, resolving assets beside each defining file (§12.1)
    [[nodiscard]] std::shared_ptr<CheckedNode> expand_imported_components(const std::shared_ptr<const CheckedNode>& root, const std::vector<ImportedComponent>& imports, const std::filesystem::path& source_path = {});
} // namespace albor::compiler
