#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"

namespace
{
    // write every generated file under the output directory, which is the compile step of a ui file (§14.2)
    void write_files(const std::vector<albor::compiler::GeneratedFile>& files, const std::filesystem::path& output) {
        // the directory must exist before a file lands in it, which is what the driver arranges (§14.2)
        std::error_code error;
        std::filesystem::create_directories(output, error);
        if (error) {
            std::fprintf(stderr, "albor-codegen: cannot create '%s'\n", output.c_str());
            return;
        }
        // each file is written only when its content changed, which keeps incremental builds quiet (§14.2, AD-03a)
        for (const albor::compiler::GeneratedFile& file : files) {
            // the write happens under the output directory, which prefixes the generated path (§14.2)
            const bool changed = albor::compiler::write_if_changed(output / file.path, file.content);
            // the outcome is reported per file, which is what the build log shows (§14.2)
            std::fprintf(stdout, "albor-codegen: %s %s\n", file.path.c_str(), changed ? "written" : "unchanged");
        }
    }

    // print every problem with its file, which is what a console compilation shows (§11.1)
    void print_diagnostics(const albor::compiler::DiagnosticList& diagnostics, std::string_view input) {
        // the problems are printed in the order they were found, which a compilation reports that way (§11.1)
        for (const albor::compiler::Diagnostic& diagnostic : diagnostics.diagnostics())
            std::fprintf(stderr, "%s\n", albor::compiler::format(diagnostic, input).c_str());
    }
} // namespace

int main(int argc, char** argv) {
    // the input ui file and the output directory are what the driver needs, which the flags provide (§11.3)
    std::filesystem::path input;
    std::filesystem::path output;
    // the flags are consumed in order, because each names its following value (§11.3)
    for (int i = 1; i < argc; ++i) {
        // the input flag takes the file to compile (§11.3)
        if (std::string_view(argv[i]) == "--input" && i + 1 < argc) {
            input = argv[++i];
            continue;
        }
        // the output flag takes the directory the generated files land in (§11.3)
        if (std::string_view(argv[i]) == "--output" && i + 1 < argc) {
            output = argv[++i];
            continue;
        }
        // an unknown flag is a misuse, which the build that wired it up got wrong (§11.3)
        std::fprintf(stderr, "albor-codegen: unknown flag '%s'\n", argv[i]);
        std::fprintf(stderr, "albor-codegen: usage: albor-codegen --input <file.ui> --output <dir>\n");
        return 2;
    }
    // both flags are mandatory, because a driver without a source or a target has nothing to do (§11.3)
    if (input.empty() || output.empty()) {
        std::fprintf(stderr, "albor-codegen: usage: albor-codegen --input <file.ui> --output <dir>\n");
        return 2;
    }
    // the file is read whole, because the scanner takes the complete unit (§11.1)
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "albor-codegen: cannot open '%s'\n", input.c_str());
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // the problems are collected in one list through the whole pipeline (§11.1)
    albor::compiler::DiagnosticList diagnostics;
    // the scanner turns the unit into tokens (§11.1)
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(source, diagnostics).tokenize();
    // the parser turns the tokens into an untyped tree (§11.1)
    const albor::compiler::SourceFile file = albor::compiler::Parser(tokens, diagnostics).parse();
    // the checker resolves the tree against the builtin widgets (§11.1, §8)
    const albor::compiler::CheckedFile checked = albor::compiler::TypeChecker(albor::compiler::ElementCatalog::with_builtins(), diagnostics).check(file);
    // a blocking problem stops the driver before any generation, which is what a console compile reports (§11.1)
    if (diagnostics.has_errors()) {
        print_diagnostics(diagnostics, input.c_str());
        return 1;
    }
    // the generator turns the checked file into the facade units (§11.3, AD-03a)
    const std::vector<albor::compiler::GeneratedFile> files = albor::compiler::CodeGenerator(diagnostics).generate(checked);
    // the generator adds its own blocking problems, which are reported the same way (§11.3)
    if (files.empty() || diagnostics.has_errors()) {
        print_diagnostics(diagnostics, input.c_str());
        return 1;
    }
    // the non blocking warnings still reach the console, because they are worth reading (§11.1)
    if (diagnostics.count() != 0U)
        print_diagnostics(diagnostics, input.c_str());
    // the generated files land under the output directory, which is the compile result of the ui unit (§14.2)
    write_files(files, output);
    return 0;
}
