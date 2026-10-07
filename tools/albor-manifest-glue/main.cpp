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
#include "manifest/glue.h"
#include "manifest/manifest.h"

namespace
{
    // write every generated file under the output directory, which is the compile step of a manifest (§11.2)
    void write_files(const std::vector<albor::compiler::GeneratedFile>& files, const std::filesystem::path& output) {
        // each file needs its parent directory, which is the module directory of its facade (§11.1, §11.2)
        std::error_code error;
        for (const albor::compiler::GeneratedFile& file : files) {
            // the module directory is created on demand, because a fresh build directory lacks it (§11.2)
            const std::filesystem::path relative = file.path;
            std::filesystem::create_directories(output / relative.parent_path(), error);
            if (error) {
                std::fprintf(stderr, "albor-manifest-glue: cannot create '%s'\n", (output / relative.parent_path()).c_str());
                return;
            }
            error.clear();
        }
        // each file is written only when its content changed, which keeps incremental builds quiet (§14.2, AD-03a)
        for (const albor::compiler::GeneratedFile& file : files) {
            // the write happens under the output directory, which prefixes the generated path (§11.2)
            const bool changed = albor::compiler::write_if_changed(output / file.path, file.content);
            // the outcome is reported per file, which is what the build log shows (§11.2)
            std::fprintf(stdout, "albor-manifest-glue: %s %s\n", file.path.c_str(), changed ? "written" : "unchanged");
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
    // the input manifest and the output directory are what the driver needs, which the flags provide (§11.2)
    std::filesystem::path input;
    std::filesystem::path output;
    // the flags are consumed in order, because each names its following value (§11.2)
    for (int i = 1; i < argc; ++i) {
        // the input flag takes the manifest to compile (§11.2)
        if (std::string_view(argv[i]) == "--input" && i + 1 < argc) {
            input = argv[++i];
            continue;
        }
        // the output flag takes the directory the generated facades land in (§11.2)
        if (std::string_view(argv[i]) == "--output" && i + 1 < argc) {
            output = argv[++i];
            continue;
        }
        // an unknown flag is a misuse, which the build that wired it up got wrong (§11.2)
        std::fprintf(stderr, "albor-manifest-glue: unknown flag '%s'\n", argv[i]);
        std::fprintf(stderr, "albor-manifest-glue: usage: albor-manifest-glue --input <file.ui-manifest> --output <dir>\n");
        return 2;
    }
    // both flags are mandatory, because a driver without a source or a target has nothing to do (§11.2)
    if (input.empty() || output.empty()) {
        std::fprintf(stderr, "albor-manifest-glue: usage: albor-manifest-glue --input <file.ui-manifest> --output <dir>\n");
        return 2;
    }
    // the manifest is read whole, because the scanner takes the complete unit (§11.1)
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "albor-manifest-glue: cannot open '%s'\n", input.c_str());
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // the module is the name of the manifest file, which a ui import resolves against (§11.1, §11.2, AD-17)
    const std::string module = input.stem().string();
    // the problems are collected in one list through the whole pipeline (§11.1)
    albor::compiler::DiagnosticList diagnostics;
    // the scanner turns the manifest into tokens (§11.1, §11.2)
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(source, diagnostics).tokenize();
    // the parser turns the tokens into the declared native types (§11.2)
    const albor::manifest::ManifestFile file = albor::manifest::ManifestParser(tokens, diagnostics).parse(module);
    // a blocking problem stops the driver before any generation, which is what a console compile reports (§11.1)
    if (diagnostics.has_errors()) {
        print_diagnostics(diagnostics, input.c_str());
        return 1;
    }
    // the generator turns the manifest into the facade units the application subclasses (§11.2, AD-17)
    const std::vector<albor::compiler::GeneratedFile> files = albor::manifest::GlueGenerator(diagnostics).generate(file);
    // the generator adds its own blocking problems, which are reported the same way (§11.2)
    if (files.empty() || diagnostics.has_errors()) {
        print_diagnostics(diagnostics, input.c_str());
        return 1;
    }
    // the non blocking warnings still reach the console, because they are worth reading (§11.1)
    if (diagnostics.count() != 0U)
        print_diagnostics(diagnostics, input.c_str());
    // the generated files land under the output directory, which is the compile result of the manifest (§11.2)
    write_files(files, output);
    return 0;
}
