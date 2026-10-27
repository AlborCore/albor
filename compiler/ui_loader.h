#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "compiler/ast.h"
#include "compiler/diagnostic.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"

namespace albor::compiler
{
    // the source text a loader asks for, which a driver reads from disk and a test may answer in memory (§11.1)
    using UiFileReader = std::function<std::optional<std::string>(const std::filesystem::path&)>;

    // the loader that resolves ui imports into one checked file and the interfaces the file brings into scope (§11.1)
    class UiImportLoader
    {
    public:
        // build a loader over the catalog the files resolve types against and the source reader that finds them (§11.1)
        UiImportLoader(ElementCatalog& catalog, DiagnosticList& diagnostics, UiFileReader reader);

        // load one root and its imports from the built catalog, returning the tree even on diagnostics (§11.1)
        [[nodiscard]] std::optional<CheckedFile> check(const std::filesystem::path& source);

        // hand out every file the last run read or tried to read, the root first and each import in load order (§14.2)
        [[nodiscard]] const std::vector<std::filesystem::path>& dependencies() const;

    private:
        // load one file, reporting a missing, cyclic or invalid import against the span that named it (§11.1)
        [[nodiscard]] std::optional<CheckedFile> load(const std::filesystem::path& source, const SourceSpan& import_span, bool root, const std::filesystem::path& importing);

        // append one problem with the file it belongs to, because an imported diagnostic names its own source (§11.1)
        void report(SourceSpan span, std::string code, std::string message, const std::filesystem::path& path);

        // append every problem a file produced with that file's path, so the driver prints the right source (§11.1)
        void report_all(const DiagnosticList& source, const std::filesystem::path& path);

        // the catalog every file resolves its types against, which is shared by the whole import graph (§11.1)
        ElementCatalog& m_catalog;

        // the catalog as it stood before the first run, which every run starts from (§11.1)
        ElementCatalog m_initial_catalog;

        // the problems every file produced, which the driver reports in load order (§11.1)
        DiagnosticList& m_diagnostics;

        // the source reader, which is the filesystem in a driver and an in-memory map in a test (§11.1)
        UiFileReader m_reader;

        // the checked files already loaded, which keeps a shared import checked once (§11.1)
        std::map<std::string, CheckedFile> m_checked;

        // the parse trees the checked expressions alias, which stay alive for every result the loader returned (§11.1)
        std::vector<std::unique_ptr<SourceFile>> m_parsed;

        // the files currently open on the import path, which is what makes a cycle detectable (§11.1)
        std::vector<std::filesystem::path> m_stack;

        // every file the last run read or attempted, in load order, which the driver writes into its depfile (§14.2)
        std::vector<std::filesystem::path> m_dependencies;
    };
} // namespace albor::compiler
