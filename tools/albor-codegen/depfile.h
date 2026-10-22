#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "compiler/codegen/codegen.h"

namespace albor::codegen
{
    // spell a path as utf-8, because std::filesystem makes one wide on windows alone (§11.1)
    [[nodiscard]] std::string utf8_path(const std::filesystem::path& path);

    // escape one path for a make depfile, which separates paths on spaces and hashes (§14.2)
    [[nodiscard]] std::string depfile_path(const std::filesystem::path& path);

    // write the make depfile a custom command reads, which tracks the assets the generator embedded (§14.2)
    [[nodiscard]] bool write_depfile(const std::filesystem::path& path, const std::vector<compiler::GeneratedFile>& files, const std::filesystem::path& output, const std::filesystem::path& input, const std::vector<std::filesystem::path>& assets);
} // namespace albor::codegen
