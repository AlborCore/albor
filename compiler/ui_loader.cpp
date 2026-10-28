#include "compiler/ui_loader.h"

#include <algorithm>
#include <utility>

#include "compiler/codegen/codegen.h"
#include "compiler/lexer.h"

namespace albor::compiler
{
    UiImportLoader::UiImportLoader(ElementCatalog& catalog, DiagnosticList& diagnostics, UiFileReader reader) :
        m_catalog(catalog), m_initial_catalog(catalog), m_diagnostics(diagnostics), m_reader(std::move(reader)) {}

    const std::vector<std::filesystem::path>& UiImportLoader::dependencies() const {
        // every file the run read is handed out in load order, which is what the depfile names (§14.2)
        return m_dependencies;
    }

    std::optional<CheckedFile> UiImportLoader::check(const std::filesystem::path& source) {
        // the catalog is reset first, because a declaration a previous root added must not leak into this run (§11.1)
        m_catalog = m_initial_catalog;
        // the previous checked cache is dropped while the parse trees stay alive for retained results (§11.1)
        m_checked.clear();
        // the open path starts empty, because the root has no importer (§11.1)
        m_stack.clear();
        // the dependency list starts empty, because it names this run alone (§14.2)
        m_dependencies.clear();
        // the root is loaded with an empty import span, because no line named it (§11.1)
        return load(source, SourceSpan{}, true, source);
    }

    std::optional<CheckedFile> UiImportLoader::load(const std::filesystem::path& source, const SourceSpan& import_span, bool root, const std::filesystem::path& importing) {
        // the path is normalized first, because two spellings of one file are one import (§11.1)
        const std::filesystem::path normalized = source.lexically_normal();
        // the normalized spelling is the cache and cycle key, which keeps both checks consistent (§11.1)
        const std::string key = normalized.generic_string();
        // a file already checked is handed back, because a diamond import is checked once (§11.1)
        if (const auto found = m_checked.find(key); found != m_checked.end())
            return found->second;
        // a file already open on the path is a cycle, because it would need itself checked first (§11.1)
        if (std::find(m_stack.begin(), m_stack.end(), normalized) != m_stack.end()) {
            // the problem points at the import that closed the cycle, because that is what the author changes (§11.1)
            report(import_span, "E0802", "the import graph contains a cycle through '" + key + "'", importing);
            return std::nullopt;
        }
        // the dependency is recorded before the read, because a missing import is a reverse edge (§14.2, §12.2)
        if (std::find(m_dependencies.begin(), m_dependencies.end(), normalized) == m_dependencies.end())
            m_dependencies.push_back(normalized);
        // the source is read through the callback, which is the filesystem in a driver and a map in a test (§11.1)
        std::optional<std::string> text = m_reader(normalized);
        // a missing file is reported against the import that named it, because nothing else can be checked (§11.1)
        if (!text.has_value()) {
            // the root and an import differ only in which span names the failure (§11.1)
            report(import_span, "E0801", "the ui file '" + key + "' could not be read", root ? normalized : importing);
            return std::nullopt;
        }
        // the file enters the open path before its imports are loaded, which is what detects a cycle (§11.1)
        m_stack.push_back(normalized);
        // the file's own problems are collected first, because they are annotated with its path (§11.1)
        DiagnosticList file_diagnostics;
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(*text, file_diagnostics).tokenize();
        // the parse tree is owned by the loader, because every checked expression aliases it (§11.1)
        auto parsed = std::make_unique<SourceFile>(Parser(tokens, file_diagnostics).parse());
        // the structs the file declares join the catalog, because an imported component property may name one (§11.1)
        register_ui_structs(*parsed, m_catalog, file_diagnostics);
        // the enums the file declares join the catalog for the same reason (§11.1)
        register_ui_enums(*parsed, m_catalog, file_diagnostics);
        // the interfaces the file's own imports contribute are collected before its check (§11.1)
        std::vector<ImportedComponent> imported_components;
        // the paths already imported by this file are remembered, because a repeated import is idempotent (§11.1)
        std::vector<std::string> imported_paths;
        for (const ImportDeclaration& declaration : parsed->imports) {
            // an import that names neither a file nor a module has nothing to read (§11.1)
            if (declaration.path.empty() && declaration.module.empty()) {
                // the problem points at the import, because that is what the author must complete (§11.1)
                file_diagnostics.add(Diagnostic::Severity::Error, declaration.span, "E0801", "an import names no ui file or module");
                continue;
            }
            // a manifest module is not a ui file, so it is left to the checker's module list (§11.1, AD-17)
            if (declaration.path.empty())
                continue;
            // the path resolves against the importing file, because that is what the author wrote it beside (§11.1)
            const std::filesystem::path imported_path = (normalized.parent_path() / declaration.path).lexically_normal();
            // the normalized spelling is the idempotence key, because two spellings of one file are one import (§11.1)
            const std::string imported_key = imported_path.generic_string();
            // an identical import adds no interface, because a declaration cannot clash with itself (§11.1)
            if (std::find(imported_paths.begin(), imported_paths.end(), imported_key) != imported_paths.end())
                continue;
            // the path joins the set before it is loaded, so a failed import is not retried by a repeated line (§11.1)
            imported_paths.push_back(imported_key);
            // the imported file is checked first, because its interface is what this file resolves against (§11.1)
            const std::optional<CheckedFile> imported_file = load(imported_path, declaration.span, false, normalized);
            // a failed import already reported why, so the rest of the file still checks (§11.1)
            if (!imported_file.has_value())
                continue;
            for (const CheckedComponent& component : imported_file->components) {
                // a duplicate name from another file is ambiguous, because an element would name two declarations (§11.1)
                const bool duplicate = std::any_of(imported_components.begin(), imported_components.end(), [&component](const ImportedComponent& existing) { return existing.name == component.name; });
                if (duplicate) {
                    // the problem points at the later import, because that is where the ambiguity enters the file (§11.1)
                    file_diagnostics.add(Diagnostic::Severity::Error, declaration.span, "E0803", "'" + component.name + "' is imported more than once from different ui files");
                    continue;
                }
                // the interface carries only what an instance resolves, because its tree belongs to its own file (§11.1)
                ImportedComponent interface;
                // the name is what an element writes, so an instance resolves against it (§11.1)
                interface.name = component.name;
                // the generated header is named after the component, which is the file the importer includes (§11.1)
                interface.header = component_header_name(component.name);
                // the properties are copied, because an assignment resolves against them (§11.1)
                interface.properties = component.properties;
                // the signals are copied, because a handler resolves against them (§11.1)
                interface.signals = component.signals;
                // the checked tree travels with the interface, because a design time path expands the instance (§12.1)
                interface.root = component.root;
                // the file's own imports travel too, because a nested instance expands through them (§11.1)
                interface.imported_components = imported_file->imported_components;
                // the file names where its design time assets resolve, because an imported image sits beside it (§6.4)
                interface.source_path = imported_path;
                // the interface joins the file's imported set, which the checker publishes to codegen (§11.1)
                imported_components.push_back(std::move(interface));
            }
        }
        // an imported file is one reusable component, because the build declares one generated pair for it (§11.1, §14.2)
        if (!root) {
            // a file with no component or several cannot be named by one import (§11.1)
            if (parsed->components.size() != 1)
                file_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{}, "E0804", "an imported ui file must declare exactly one component");
            // the component must be named after the file, because the build output is the file stem (§11.1, §14.2)
            else if (component_header_name(parsed->components.at(0).name) != normalized.stem().string() + ".h")
                file_diagnostics.add(Diagnostic::Severity::Error, parsed->components.at(0).span, "E0805", "an imported component must match its file, so '" + normalized.stem().string() + ".ui' names it in PascalCase");
        }
        // the checker resolves the file against its own declarations and the imported interfaces (§11.1)
        TypeChecker checker(m_catalog, file_diagnostics);
        // the imports are published before the check, because an element resolves against them (§11.1)
        checker.set_imported_components(imported_components);
        // the file is checked whole, which reports every problem at once (§11.1)
        CheckedFile checked = checker.check(*parsed);
        // the interfaces travel with the checked file, because codegen includes their headers (§11.1)
        checked.imported_components = imported_components;
        // the parse tree is kept before the result is cached, because the checked expressions alias it (§11.1)
        m_parsed.push_back(std::move(parsed));
        // the file's problems join the run with its path, so an imported diagnostic names its own source (§11.1)
        report_all(file_diagnostics, normalized);
        // the open path is closed, because this file's imports are checked (§11.1)
        m_stack.pop_back();
        // a file with a problem is cached too, because its interface still resolves the instances around the error (§11.1)
        m_checked.emplace(key, checked);
        // the caller decides whether to generate, because the diagnostics already carry the blocking problem (§11.1)
        return checked;
    }

    void UiImportLoader::report(SourceSpan span, std::string code, std::string message, const std::filesystem::path& path) {
        // the diagnostic is assembled locally, because the path is what makes an imported problem locatable (§11.1)
        Diagnostic diagnostic;
        // the problem blocks generation, because nothing can be emitted from an unread or cyclic file (§11.1)
        diagnostic.severity = Diagnostic::Severity::Error;
        // the span is where the import was written, which is what an editor points at (§11.1)
        diagnostic.span = span;
        // the code is the stable handle tooling matches on (§11.1)
        diagnostic.code = std::move(code);
        // the message is what a person reads, which names the path that failed (§11.1)
        diagnostic.message = std::move(message);
        // the path is the file the problem belongs to, which differs from the root for an import (§11.1)
        diagnostic.path = path.lexically_normal().generic_string();
        // the problem joins the run, because the driver reports every problem at once (§11.1)
        m_diagnostics.add(std::move(diagnostic));
    }

    void UiImportLoader::report_all(const DiagnosticList& source, const std::filesystem::path& path) {
        // each problem keeps its span and code, because only the file path is added here (§11.1)
        for (const Diagnostic& diagnostic : source.diagnostics()) {
            // the copy is made first, because the problem already has its message and code (§11.1)
            Diagnostic annotated = diagnostic;
            // the path names the file that produced it, so the driver prints the right source (§11.1)
            annotated.path = path.lexically_normal().generic_string();
            // the annotated problem joins the run in the order it was found (§11.1)
            m_diagnostics.add(std::move(annotated));
        }
    }
} // namespace albor::compiler
