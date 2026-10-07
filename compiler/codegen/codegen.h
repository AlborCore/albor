#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/diagnostic.h"
#include "compiler/type_checker.h"

namespace albor::compiler
{
    // one emitted file, which the caller writes under its output directory (§14.2)
    struct GeneratedFile
    {
        // the file name, which the caller prefixes with its output directory (§14.2)
        std::string path;

        // the whole content, which write if changed compares against what is on disk (§14.2)
        std::string content;
    };

    // the generator that turns one checked file into concrete facade headers and implementation units (§11.3, AD-03a)
    class CodeGenerator
    {
    public:
        // build a generator that reports problems through the given list, which never throws (§11.3)
        explicit CodeGenerator(DiagnosticList& diagnostics);

        // generate one file of components, which is empty when a diagnostic blocked the output (§11.3, §14.3)
        [[nodiscard]] std::vector<GeneratedFile> generate(const CheckedFile& file);

    private:
        // the problems the generator reports, which the caller owns (§11.3)
        DiagnosticList& m_diagnostics;
    };

    // rewrite the file only when its content changed, which is what keeps incremental builds quiet (§14.2, AD-03a)
    [[nodiscard]] bool write_if_changed(const std::filesystem::path& path, std::string_view content);
} // namespace albor::compiler
