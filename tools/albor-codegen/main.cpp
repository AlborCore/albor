#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "compiler/font_catalog.h"
#include "compiler/font_usage.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"
#include "manifest/font_embed.h"
#include "manifest/manifest.h"
#include "tools/albor-codegen/depfile.h"

namespace
{
    // write every generated file under the output directory, which is the compile step of a ui file (§14.2)
    bool write_files(const std::vector<albor::compiler::GeneratedFile>& files, const std::filesystem::path& output) {
        // the directory must exist before a file lands in it, which is what the driver arranges (§14.2)
        std::error_code error;
        std::filesystem::create_directories(output, error);
        if (error) {
            std::fprintf(stderr, "albor-codegen: cannot create '%s'\n", albor::codegen::utf8_path(output).c_str());
            return false;
        }
        // each file is written only when its content changed, which keeps incremental builds quiet (§14.2, AD-03a)
        for (const albor::compiler::GeneratedFile& file : files) {
            // the write happens under the output directory, which prefixes the generated path (§14.2)
            const albor::compiler::WriteResult result = albor::compiler::write_if_changed(output / file.path, file.content);
            // a failed write stops the compile, because a stale unit must not read as a success (§14.2)
            if (result == albor::compiler::WriteResult::Failed) {
                // the failure names the file, which is what a build log needs to fix it (§14.2)
                std::fprintf(stderr, "albor-codegen: cannot write '%s'\n", albor::codegen::utf8_path(output / file.path).c_str());
                return false;
            }
            // the outcome is reported per file, which is what the build log shows (§14.2)
            std::fprintf(stdout, "albor-codegen: %s %s\n", file.path.c_str(), result == albor::compiler::WriteResult::Written ? "written" : "unchanged");
        }
        // every file landed, which is what the driver reports as success (§14.2)
        return true;
    }

    // read a stream whole, because the scanner takes the whole unit and both inputs are text (§11.1)
    std::string stream_text(std::istream& stream) {
        // the content accumulates, because a scanner needs every character before it produces a token (§11.1)
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
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
    // the chunk size is one component per unit, which the flag tunes for large projects (AD-03b)
    std::size_t tu_chunk_size = 1;
    // the manifest declares the records a ui file may name, which is optional because a file may use none (§11.2, AD-17)
    std::filesystem::path manifest;
    // the depfile tracks embedded assets, which the build reads, and stays optional for a direct run (§14.2)
    std::filesystem::path depfile;
    // the catalog starts with the builtin widgets, which every ui file may instantiate (§11.1, §8)
    albor::compiler::ElementCatalog catalog = albor::compiler::ElementCatalog::with_builtins();
    // the fonts the target carries are empty until a build declares them, which keeps the platform path (§9.1, AD-34)
    albor::compiler::FontCatalog fonts;
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
        // the chunk size flag tunes how many components share a generated unit (AD-03b)
        if (std::string_view(argv[i]) == "--tu-chunk-size" && i + 1 < argc) {
            // the value text is captured, because the parse reads it while walking the flag (AD-03b)
            const char* value = argv[++i];
            // the numeric value is parsed without allocating, which is what the flag needs (AD-03b)
            if (std::from_chars(value, value + std::strlen(value), tu_chunk_size).ec != std::errc{}) {
                // an invalid chunk value is a misuse, which a script that wired it up got wrong (AD-03b)
                std::fprintf(stderr, "albor-codegen: '%s' is not a size\n", value);
                return 2;
            }
            continue;
        }
        // the manifest flag names the manifest that declares the records a ui file uses (§11.2, AD-17)
        if (std::string_view(argv[i]) == "--manifest" && i + 1 < argc) {
            manifest = argv[++i];
            continue;
        }
        // the depfile flag names the make dependency file the build reads after this run (§14.2)
        if (std::string_view(argv[i]) == "--depfile" && i + 1 < argc) {
            depfile = argv[++i];
            continue;
        }
        // the default font flag names the family a bare selection resolves (§9.1, AD-34)
        if (std::string_view(argv[i]) == "--default-font-family") {
            // a flag without its value is a misuse, which the build that wired it up got wrong (§9.1)
            if (i + 1 >= argc) {
                // the missing value is named, because the build log should say what to fix (§9.1)
                std::fprintf(stderr, "albor-codegen: --default-font-family needs a family\n");
                return 2;
            }
            // the family is captured after the flag, which the composition root will name (§9.1)
            fonts.set_default_family(argv[++i]);
            continue;
        }
        // the font flag names one face the target carries, which a selection is checked against (§9.1, AD-34)
        if (std::string_view(argv[i]) == "--font") {
            // a face needs three values, and a shorter tail is a misuse (§11.3)
            if (i + 3 >= argc) {
                // the missing values are named, because the build log should say what to fix (§11.3)
                std::fprintf(stderr, "albor-codegen: --font needs a family, a weight and a file\n");
                return 2;
            }
            // the family is read first, because the weight and path follow it (§9.1)
            std::string family = argv[++i];
            // the weight is read next, and an unknown spelling is a misuse (§9.1)
            const std::optional<albor::FontWeight> weight = albor::manifest::parse_font_weight(argv[++i]);
            // an unknown weight stops the run before anything is generated (§9.1)
            if (!weight) {
                std::fprintf(stderr, "albor-codegen: '%s' is not a font weight\n", argv[i]);
                return 2;
            }
            // the file is read last, because it is the path the face was declared from (§11.3)
            const std::filesystem::path file = argv[++i];
            // a file the build cannot read is a misuse, because the catalog would name a face nothing carries (§11.3)
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error)) {
                // the missing file is named, because the build log should say what to fix (§11.3)
                std::fprintf(stderr, "albor-codegen: cannot read font file '%s'\n", albor::codegen::utf8_path(file).c_str());
                return 2;
            }
            // the face joins the catalog, which the compiler checks the ui selection against (§11.1, AD-34)
            fonts.add(std::move(family), *weight);
            continue;
        }
        // an unknown flag is a misuse, which the build that wired it up got wrong (§11.3)
        std::fprintf(stderr, "albor-codegen: unknown flag '%s'\n", argv[i]);
        std::fprintf(stderr, "albor-codegen: usage: albor-codegen --input <file.ui> --output <dir> [--tu-chunk-size <n>] [--manifest <file>] [--depfile <file>] [--default-font-family <family>] [--font <family> <weight> <file> ...]\n");
        return 2;
    }
    // both flags are mandatory, because a driver without a source or a target has nothing to do (§11.3)
    if (input.empty() || output.empty()) {
        std::fprintf(stderr, "albor-codegen: usage: albor-codegen --input <file.ui> --output <dir> [--tu-chunk-size <n>] [--manifest <file>] [--depfile <file>] [--default-font-family <family>] [--font <family> <weight> <file> ...]\n");
        return 2;
    }
    // the file is read whole, because the scanner takes the complete unit (§11.1)
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        std::fprintf(stderr, "albor-codegen: cannot open '%s'\n", albor::codegen::utf8_path(input).c_str());
        return 1;
    }
    // the source text is read, because a scanner needs every character before it produces a token (§11.1)
    std::string source = stream_text(stream);
    // the problems are collected in one list through the whole pipeline (§11.1)
    albor::compiler::DiagnosticList diagnostics;
    // the manifest is read and parsed, because its records are what the catalog resolves (§11.2, AD-17)
    if (!manifest.empty()) {
        // the manifest text is read, because its records must be known before the ui file is checked (§11.2, AD-17)
        std::ifstream manifest_stream(manifest, std::ios::binary);
        // a manifest that cannot be read is reported, because the compiler cannot resolve records without it (§11.2)
        if (!manifest_stream) {
            std::fprintf(stderr, "albor-codegen: cannot open '%s'\n", albor::codegen::utf8_path(manifest).c_str());
            return 1;
        }
        // the module name is the file stem, which is what the glue generator names the directory after (§11.2)
        const std::string module = std::filesystem::path(manifest).stem().string();
        // the problems of the manifest join the run, because a bad record is the author's problem too (§11.2)
        const std::vector<albor::compiler::Token> manifest_tokens = albor::compiler::Lexer(stream_text(manifest_stream), diagnostics).tokenize();
        // the manifest is parsed into declarations, which is where the records live (§11.2, AD-17)
        const albor::manifest::ManifestFile parsed = albor::manifest::ManifestParser(manifest_tokens, diagnostics).parse(module);
        // the records join the catalog, which is what makes vector<Task> resolve (§11.2, AD-17, AD-13)
        albor::manifest::register_records(parsed, catalog);
    }

    // the scanner turns the unit into tokens, after the records are known (§11.1, §11.2, AD-17)
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(source, diagnostics).tokenize();
    // the parser turns the tokens into an untyped tree (§11.1)
    const albor::compiler::SourceFile file = albor::compiler::Parser(tokens, diagnostics).parse();
    // the structs the file declares join the catalog, which is what makes a property of one resolve (§11.1)
    albor::compiler::register_ui_structs(file, catalog, diagnostics);
    // the enums the file declares join the catalog, which is what makes a property of one hold a member (§11.1)
    albor::compiler::register_ui_enums(file, catalog);
    // the checker resolves the tree against the catalog, which holds the builtins and records (§11.2, §8)
    const albor::compiler::CheckedFile checked = albor::compiler::TypeChecker(catalog, diagnostics).check(file);
    // a clean tree is checked against the carried faces, because an uncarried selection is an error (§9.1)
    if (!diagnostics.has_errors())
        albor::compiler::check_font_usage(checked, fonts, diagnostics);
    // a blocking problem stops the driver before any generation, which is what a console compile reports (§11.1)
    if (diagnostics.has_errors()) {
        print_diagnostics(diagnostics, albor::codegen::utf8_path(input));
        return 1;
    }
    // the generator turns the checked file into the facade units (§11.3, AD-03a)
    albor::compiler::CodeGenerator generator(diagnostics);
    // the catalog is handed over, because a record header is written into a module the generator must name (§11.2, AD-17)
    generator.set_catalog(&catalog);
    // the chunk size is passed through, which tunes clean rebuild times on large projects (AD-03b)
    generator.set_tu_chunk_size(tu_chunk_size);
    // the source path is what embedded assets resolve against, because a build runs from any directory (§6.4)
    generator.set_source_path(input);
    const std::vector<albor::compiler::GeneratedFile> files = generator.generate(checked);
    // the generator adds its own blocking problems, which are reported the same way (§11.3)
    if (files.empty() || diagnostics.has_errors()) {
        print_diagnostics(diagnostics, albor::codegen::utf8_path(input));
        return 1;
    }
    // the non blocking warnings still reach the console, because they are worth reading (§11.1)
    if (diagnostics.count() != 0U)
        print_diagnostics(diagnostics, albor::codegen::utf8_path(input));
    // the generated files land under the output directory, which is the compile result of the ui unit (§14.2)
    if (!write_files(files, output))
        return 1;
    // the depfile is written after the units, so a build only reads dependencies for a successful run (§14.2)
    if (!depfile.empty() && !albor::codegen::write_depfile(depfile, files, output, input, generator.asset_paths())) {
        // a depfile that cannot be written leaves the build unable to track assets, which is a failed run (§14.2)
        std::fprintf(stderr, "albor-codegen: cannot write '%s'\n", albor::codegen::utf8_path(depfile).c_str());
        return 1;
    }
    return 0;
}
