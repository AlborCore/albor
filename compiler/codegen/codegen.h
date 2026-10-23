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

        // set how many components share one implementation unit, which is one by default (AD-03b)
        void set_tu_chunk_size(std::size_t size);

        // set the catalog the components resolve records against, because a record header lives in a module (§11.2, AD-17)
        void set_catalog(const ElementCatalog* catalog);

        // generate one file of components, which is empty when a diagnostic blocked the output (§11.3, §14.3)
        [[nodiscard]] std::vector<GeneratedFile> generate(const CheckedFile& file);

    private:
        // the problems the generator reports, which the caller owns (§11.3)
        DiagnosticList& m_diagnostics;

        // how many components share one implementation unit, which the chunk flag tunes (AD-03b)
        std::size_t m_tu_chunk_size = 1;

        // the catalog records resolve against, which is null when the caller registered none (§11.2, AD-17)
        const ElementCatalog* m_catalog = nullptr;
    };

    // what a write-if-changed attempt did, which tells a caller a failure apart from an unchanged file (§14.2)
    enum class WriteResult
    {
        // the file already carried the content, so nothing was written (§14.2)
        Unchanged,

        // the content was written, so the file now carries it (§14.2)
        Written,

        // the write failed, which a caller reports rather than treating as unchanged (§14.2)
        Failed,
    };

    // rewrite the file only when its content changed, which is what keeps incremental builds quiet (§14.2, AD-03a)
    [[nodiscard]] WriteResult write_if_changed(const std::filesystem::path& path, std::string_view content);
} // namespace albor::compiler
