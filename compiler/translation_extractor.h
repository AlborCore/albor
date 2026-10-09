#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "compiler/type_checker.h"

namespace albor::compiler
{
    // the translatable strings a file declares, keyed by the source text the generated code passes (§9.2)
    [[nodiscard]] std::vector<std::string> extract_translations(const CheckedFile& file);

    // render a catalogue as a template a translator edits, which lists every source with an empty translation (§9.2)
    [[nodiscard]] std::string write_catalog_template(const std::vector<std::string>& sources);

    // render a catalogue as generated c++, which is what an application installs at startup (§9.2)
    [[nodiscard]] std::string write_catalog_table(const std::vector<std::string>& sources, const std::vector<std::pair<std::string, std::string>>& entries);
} // namespace albor::compiler
