#include "manifest/font_embed.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace albor::manifest
{
    namespace
    {
        // the four byte magic every sfnt begins with, which a face builder can read (§15)
        constexpr std::uint32_t SFNT_TRUETYPE = 0x00010000U;

        // the open type magic, which a CFF outline font carries (§15)
        constexpr std::uint32_t SFNT_OPENTYPE = 0x4F54544FU;

        // the true type collection magic, which a file of several faces begins with (§15)
        constexpr std::uint32_t SFNT_COLLECTION = 0x74746366U;

        // the apple true type magic, which an older mac face carries (§15)
        constexpr std::uint32_t SFNT_APPLE = 0x74727565U;

        // read the big endian word the magic uses, which a byte stream carries in network order (§15)
        std::uint32_t read_word(std::span<const std::uint8_t> bytes) {
            // a short stream has no magic, so it is refused before the read (§15)
            if (bytes.size() < 4)
                return 0;
            // the four bytes assemble into the word the format declares (§15)
            return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
        }

        // report a problem at the file level, because a build input has no source span (§11.3)
        void report(compiler::DiagnosticList& diagnostics, compiler::Diagnostic::Severity severity, std::string code, std::string message) {
            // the diagnostic points at nothing in a ui file, because the input is a build asset (§11.3)
            diagnostics.add(severity, compiler::SourceSpan{}, std::move(code), std::move(message));
        }

        // escape a string for a C++ literal, which a family name may need (§11.3)
        std::string escape_string(std::string_view text) {
            // the escaped text accumulates, because one byte may become two (§11.3)
            std::string escaped;
            // every byte is copied or escaped, because the generated literal must reproduce the name (§11.3)
            for (char byte : text) {
                // a quote or a backslash needs a backslash, because either would end or alter the literal (§11.3)
                if (byte == '"' || byte == '\\') {
                    // the escape opens the pair, which the byte below closes (§11.3)
                    escaped.push_back('\\');
                    // the byte follows the escape, which is the whole of the written form (§11.3)
                    escaped.push_back(byte);
                    continue;
                }
                // a newline, carriage return or tab has a short escape, which keeps the literal valid (§11.3)
                if (byte == '\n') {
                    // the newline becomes its escape, because a raw newline would end the literal (§11.3)
                    escaped.append("\\n");
                    continue;
                }
                // the carriage return becomes its escape, because a raw return would end the literal (§11.3)
                if (byte == '\r') {
                    // the carriage return is written as an escape, which the compiler reads back (§11.3)
                    escaped.append("\\r");
                    continue;
                }
                // the tab becomes its escape, because a raw tab is legal but unreadable (§11.3)
                if (byte == '\t') {
                    // the tab is written as an escape, which the compiler reads back (§11.3)
                    escaped.append("\\t");
                    continue;
                }
                // every other byte is copied, because the validation refuses control bytes before this point (§11.3)
                escaped.push_back(byte);
            }
            // the escaped text is what the generated literal carries (§11.3)
            return escaped;
        }

        // report whether a name carries a control byte, which the generated source cannot carry safely (§11.3)
        bool has_control_byte(std::string_view text) {
            // every byte is checked, because a single control byte is enough to break the generated literal (§11.3)
            for (char character : text) {
                // the character is read unsigned, because a control byte is below the printable range (§11.3)
                const auto byte = static_cast<unsigned char>(character);
                // a control byte or the delete byte is refused, because a font family has no use for one (§11.3)
                if (byte < 0x20 || byte == 0x7F)
                    return true;
            }
            // the name is printable, which the generated source can carry (§11.3)
            return false;
        }

        // find the default family's regular face, which a selection without a family resolves (§9.1)
        bool has_default_regular(const FontAssetSet& set) {
            // every face is checked, because the default family may carry several weights (§9.1)
            for (const FontAsset& face : set.faces) {
                // the regular face of the default family is what a bare family name resolves (§9.1)
                if (face.family == set.default_family && face.weight == FontWeight::Regular)
                    return true;
            }
            // no regular face of that family is carried, which the default selection cannot resolve (§9.1)
            return false;
        }
    } // namespace

    std::optional<FontWeight> parse_font_weight(std::string_view name) {
        // the written forms are the lower case names the language uses, because a build script follows the language (§9.1)
        if (name == "thin")
            return FontWeight::Thin;
        // the light form is the next stroke above thin (§9.1)
        if (name == "light")
            return FontWeight::Light;
        // the regular form is the default a bare family name resolves (§9.1)
        if (name == "regular")
            return FontWeight::Regular;
        // the medium form sits between regular and semibold (§9.1)
        if (name == "medium")
            return FontWeight::Medium;
        // the semibold form sits below bold (§9.1)
        if (name == "semibold")
            return FontWeight::Semibold;
        // the bold form is the emphatic stroke a family usually carries (§9.1)
        if (name == "bold")
            return FontWeight::Bold;
        // the black form is the heaviest stroke a family may carry (§9.1)
        if (name == "black")
            return FontWeight::Black;
        // an unknown spelling names no weight, which the caller reports (§9.1)
        return std::nullopt;
    }

    std::string_view font_weight_name(FontWeight weight) {
        // each enumerator spells the C++ name it generates, because the registration names it (§11.3)
        switch (weight) {
        case FontWeight::Thin:
            return "Thin";
        case FontWeight::Light:
            return "Light";
        case FontWeight::Regular:
            return "Regular";
        case FontWeight::Medium:
            return "Medium";
        case FontWeight::Semibold:
            return "Semibold";
        case FontWeight::Bold:
            return "Bold";
        case FontWeight::Black:
            return "Black";
        }
        // every enumerator is handled above, so this reads as a defensive default (§11.3)
        return "Regular";
    }

    bool is_sfnt(std::span<const std::uint8_t> bytes) {
        // the magic decides, because a face builder reads an sfnt and nothing else (§15)
        const std::uint32_t magic = read_word(bytes);
        // each container the format declares is a valid face source (§15)
        return magic == SFNT_TRUETYPE || magic == SFNT_OPENTYPE || magic == SFNT_COLLECTION || magic == SFNT_APPLE;
    }

    FontAssetSet load_font_assets(std::string default_family, const std::vector<FontAssetSpec>& specs, compiler::DiagnosticList& diagnostics) {
        // the set carries the default family the composition root will name (§9.1)
        FontAssetSet set;
        // the default family is stored before the faces, because a missing one is a set-level problem (§9.1)
        set.default_family = std::move(default_family);
        // every declared face is read, because the generated unit carries all of them (§11.3)
        for (const FontAssetSpec& spec : specs) {
            // the file is opened in binary mode, because a font is not text (§11.3)
            std::ifstream file(spec.path, std::ios::binary);
            // a file that cannot be read is reported and skipped, because a partial asset is worse than none (§11.3)
            if (!file) {
                report(diagnostics, compiler::Diagnostic::Severity::Error, "E0708", "the font file '" + spec.path.string() + "' cannot be read");
                continue;
            }
            // the bytes are read whole, because a face is built from a complete font (§9.1)
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            // the face joins the set even when its bytes are empty, because validation names the problem once (§11.3)
            set.faces.push_back(FontAsset{.family = spec.family, .weight = spec.weight, .bytes = std::move(bytes)});
        }
        // the loaded set is validated before the caller emits anything (§11.3)
        validate_font_assets(set, diagnostics);
        // the set is handed back even with problems, so a caller prints every one (§11.3)
        return set;
    }

    void validate_font_assets(const FontAssetSet& set, compiler::DiagnosticList& diagnostics) {
        // a set with no face cannot register anything, which a target that asked for fonts must not accept (§11.3)
        if (set.faces.empty()) {
            // the empty set is reported, because a generated unit with no face is a build mistake (§11.3)
            report(diagnostics, compiler::Diagnostic::Severity::Error, "E0709", "no font face is declared for the target");
            return;
        }
        // the declared pairings are tracked, because two faces of one family and weight cannot both register (§11.3)
        std::set<std::pair<std::string, FontWeight>> declared;
        // every face is checked against the form a generated unit can carry (§11.3)
        for (const FontAsset& face : set.faces) {
            // a face with no family cannot be asked for, which the registry would refuse anyway (§9.1)
            if (face.family.empty()) {
                // the missing family is reported against the face that declared it (§11.3)
                report(diagnostics, compiler::Diagnostic::Severity::Error, "E0702", "a font face declares no family");
                continue;
            }
            // a family with a control byte cannot be written into C++ safely, so it is refused (§11.3)
            if (has_control_byte(face.family)) {
                // the control byte is reported against the family that carries it (§11.3)
                report(diagnostics, compiler::Diagnostic::Severity::Error, "E0710", "the font family '" + face.family + "' carries a control byte");
                continue;
            }
            // a face with no bytes cannot shape, so it is refused before generation (§9.1)
            if (face.bytes.empty()) {
                // the empty face is reported against the family that named it (§11.3)
                report(diagnostics, compiler::Diagnostic::Severity::Error, "E0703", "the font family '" + face.family + "' carries no bytes");
                continue;
            }
            // a face that is not an sfnt cannot be read by the face builder (§15)
            if (!is_sfnt(face.bytes)) {
                // the malformed face is reported against the family that named it (§11.3)
                report(diagnostics, compiler::Diagnostic::Severity::Error, "E0704", "the font family '" + face.family + "' does not carry sfnt bytes");
                continue;
            }
            // a pairing already declared is reported, because only one face of a family and weight can answer (§11.3)
            if (!declared.insert({face.family, face.weight}).second)
                report(diagnostics, compiler::Diagnostic::Severity::Error, "E0701", "the font family '" + face.family + "' declares weight '" + std::string(font_weight_name(face.weight)) + "' twice");
        }
        // the default family must be named, because a composition root needs one family to resolve (§9.1)
        if (set.default_family.empty())
            report(diagnostics, compiler::Diagnostic::Severity::Error, "E0705", "no default font family is declared");
        // a default family with a control byte cannot be written into C++ safely, so it is refused (§11.3)
        else if (has_control_byte(set.default_family))
            report(diagnostics, compiler::Diagnostic::Severity::Error, "E0710", "the default font family carries a control byte");
        // the default family must be carried, because the engine resolves it by name (§9.1)
        else if (std::none_of(set.faces.begin(), set.faces.end(), [&set](const FontAsset& face) { return face.family == set.default_family; }))
            report(diagnostics, compiler::Diagnostic::Severity::Error, "E0705", "the default font family '" + set.default_family + "' is not declared");
        // the default family must carry a regular face, because a bare family name resolves that weight (§9.1)
        else if (!has_default_regular(set))
            report(diagnostics, compiler::Diagnostic::Severity::Error, "E0706", "the default font family '" + set.default_family + "' carries no regular face");
        // every face is compared with the next, because two weights with one file shape identically (§9.1)
        for (std::size_t index = 0; index < set.faces.size(); index++) {
            // the later faces are the only ones a pair can repeat, because the earlier ones were already compared (§9.1)
            for (std::size_t other = index + 1; other < set.faces.size(); other++) {
                // only a same family pair is a weight mistake, because two families may intentionally share bytes (§9.1)
                if (set.faces[index].family != set.faces[other].family || set.faces[index].weight == set.faces[other].weight)
                    continue;
                // identical bytes under two weights shape identically, because the engine does not instance fonts (§9.1)
                if (set.faces[index].bytes == set.faces[other].bytes)
                    report(diagnostics, compiler::Diagnostic::Severity::Warning, "W0707", "the font family '" + set.faces[index].family + "' declares two weights over one file");
            }
        }
    }

    std::vector<compiler::GeneratedFile> emit_font_assets(const FontAssetSet& set) {
        // an empty set cannot register a default family, so nothing is emitted (§11.3)
        if (set.faces.empty())
            return {};
        // a face with no bytes would emit an empty array, which is invalid C++, so nothing is emitted (§11.3)
        if (std::any_of(set.faces.begin(), set.faces.end(), [](const FontAsset& face) { return face.bytes.empty(); }))
            return {};
        // the header declares the default family and the installer, which the composition root calls (§9.1)
        std::ostringstream header;
        // the generated header is guarded by pragma once, which the tree uses (§14.1)
        header << "#pragma once\n\n";
        // the string view is what the default family constant is (§11.3)
        header << "#include <string_view>\n\n";
        // the generated namespace is separate from the framework, because its symbols are per target (§14.1)
        header << "namespace albor::generated\n{\n";
        // the installer is public, because a static library consumer may have to call it explicitly (§11.3)
        header << "    // register every generated face, which the default family and the static initializer call (§11.3, AD-34)\n";
        // the function takes nothing and answers nothing, because registration is the whole effect (§11.3)
        header << "    void install_embedded_fonts();\n\n";
        // the default family is what a run without a family resolves (§9.1)
        header << "    // the family the generated faces carry, which using it registers before a window (§9.1, AD-34)\n";
        // the anchor calls the installer, so a static library cannot drop the registration (§11.3)
        header << "    inline const std::string_view DEFAULT_FAMILY = [] { install_embedded_fonts(); return std::string_view(\"" << escape_string(set.default_family) << "\"); }();\n";
        // the namespace closes, which ends the generated header (§14.1)
        header << "} // namespace albor::generated\n";
        // the source includes the standard headers first, which the style guide orders that way (§14.1)
        std::ostringstream source;
        // the source is marked generated, so a reader does not edit it (§14, AD-03a)
        source << "// generated by albor-font-embed, do not edit (§14, AD-03a)\n\n";
        // the byte arrays need the fixed width type (§11.3)
        source << "#include <cstdint>\n";
        // the iterator helpers delimit the arrays the registration copies (§11.3)
        source << "#include <iterator>\n";
        // the vector is what the registry stores the bytes in (§11.3)
        source << "#include <vector>\n\n";
        // the installer and the default family are declared in the generated header (§11.3)
        source << "#include \"albor_embedded_fonts.h\"\n";
        // the weight the registration names is the core enumeration (§9.1)
        source << "#include \"core/font_weight.h\"\n";
        // the registry is what the installer writes into (§11.3)
        source << "#include \"resources/embedded_font.h\"\n\n";
        // the generated namespace matches the header, because the symbols are one library (§14.1)
        source << "namespace albor::generated\n{\n";
        // the arrays are private, because only the installer names them (§14.1)
        source << "    namespace\n    {\n";
        // every face gets one array and one registration (§11.3)
        for (std::size_t index = 0; index < set.faces.size(); index++) {
            // the face is named by its index, because a family name cannot be trusted inside a comment (§11.3)
            source << "        // the bytes of face " << index << ", which the registry copies once (§11.3, AD-34)\n";
            // the array is written with the unique index, which keeps two arrays apart (§14.1)
            source << "        constexpr std::uint8_t FONT_BYTES_" << index << "[] = {";
            // sixteen bytes fill a line, which keeps the generated file readable (§11.3)
            for (std::size_t byte = 0; byte < set.faces[index].bytes.size(); byte++) {
                // a new line starts every sixteen bytes, because one line of bytes is unreadable (§11.3)
                if (byte % 16 == 0)
                    source << "\n            ";
                // a space separates adjacent bytes on a line, which keeps the initializer readable (§11.3)
                else
                    source << " ";
                // the byte is written in canonical two digit hexadecimal form (§11.3)
                char encoded[8];
                std::snprintf(encoded, sizeof(encoded), "0x%02x", static_cast<unsigned int>(set.faces[index].bytes[byte]));
                // the encoded byte joins the line, which the formatter already indented (§11.3)
                source << encoded;
                // a comma separates the byte from the next, which the initializer requires (§11.3)
                if (byte + 1 < set.faces[index].bytes.size())
                    source << ",";
            }
            // the array closes after a final newline, which the next declaration follows (§14.1)
            source << "\n        };\n";
        }
        // the private namespace closes before the public installer (§14.1)
        source << "    } // namespace\n\n";
        // the installer registers every face exactly once, because a second call is a no-op (§11.3)
        source << "    void install_embedded_fonts() {\n";
        // the guard is a function local static, which the language initializes once (§11.3)
        source << "        // the registration runs once, because every later call is a no-op (§11.3)\n";
        // the lambda returns true after every registration, which the static guard stores (§11.3)
        source << "        static const bool installed = [] {\n";
        // every face is registered with the family, weight and bytes the build declared (§11.3)
        for (std::size_t index = 0; index < set.faces.size(); index++) {
            // the registration names the face and copies its bytes into the registry (§11.3, AD-34)
            source << "            // register face " << index << " at weight " << font_weight_name(set.faces[index].weight) << ", which a run resolves exactly (§9.1)\n";
            // the aggregate carries the family, weight and bytes the registry stores (§11.3)
            source << "            resources::FontRegistry::register_font(resources::EmbeddedFont{.family = \"" << escape_string(set.faces[index].family) << "\", .weight = FontWeight::" << font_weight_name(set.faces[index].weight) << ", .bytes = std::vector<std::uint8_t>(std::begin(FONT_BYTES_" << index << "), std::end(FONT_BYTES_" << index << "))});\n";
        }
        // the guard returns true, which the static initialization stores (§11.3)
        source << "            return true;\n";
        // the lambda closes, which is what the static guard initializes from (§11.3)
        source << "        }();\n";
        // the guard is read, so a compiler cannot discard the initialization (§11.3)
        source << "        (void)installed;\n";
        // the installer closes, which ends the public function (§14.1)
        source << "    }\n\n";
        // the automatic installer runs before main, so linking the unit is the selection (§11.3, AD-34)
        source << "    namespace\n    {\n";
        // the installer object has one job, which is to call the public function (§11.3)
        source << "        // the installer runs at load, so the faces exist before the first window (§11.3, AD-34)\n";
        // the object type is private, because only the object below uses it (§14.1)
        source << "        struct FontAutoInstall\n        {\n";
        // the constructor registers the faces, which the registry makes idempotent (§11.3)
        source << "            // register the faces once the process loads, which the registry makes idempotent (§11.3)\n";
        // the constructor body calls the public installer, which is the one registration path (§11.3)
        source << "            FontAutoInstall() { install_embedded_fonts(); }\n";
        // the type closes, which ends the private installer (§14.1)
        source << "        };\n\n";
        // the one object runs before main, because every dynamic initializer does (§11.3)
        source << "        // the one installer object, which the dynamic initializer runs before main (§11.3)\n";
        // the object is constant, because its only effect is the constructor (§11.3)
        source << "        const FontAutoInstall g_font_auto_install;\n";
        // the private namespace closes, which ends the generated unit (§14.1)
        source << "    } // namespace\n";
        // the generated namespace closes, which ends the source (§14.1)
        source << "} // namespace albor::generated\n";
        // the two generated files are handed out, which the build writes if changed (§14.2)
        return {compiler::GeneratedFile{.path = "albor_embedded_fonts.h", .content = header.str()}, compiler::GeneratedFile{.path = "albor_embedded_fonts.cpp", .content = source.str()}};
    }

    int run_font_embed(std::span<const std::string_view> arguments) {
        // the generated files land in the target directory, which the build names (§14.2)
        std::filesystem::path output;
        // the default family is what a run without a family resolves, which the build must name (§9.1)
        std::string default_family;
        // every declared face joins the set in the order the build wrote it (§11.3)
        std::vector<FontAssetSpec> specs;
        // the flags are consumed in order, because each names its following values (§11.3)
        for (std::size_t index = 0; index < arguments.size(); index++) {
            // the output flag takes the directory the generated files land in (§14.2)
            if (arguments[index] == "--output") {
                // a flag without its value is a misuse, which the build that wired it up got wrong (§14.2)
                if (index + 1 >= arguments.size()) {
                    // the missing value is named, because the build log should say what to fix (§14.2)
                    std::fprintf(stderr, "albor-font-embed: --output needs a directory\n");
                    return 2;
                }
                // the directory is captured after the flag, which is the one the build named (§14.2)
                output = std::filesystem::path(std::string(arguments[++index]));
                continue;
            }
            // the default family flag names the family a bare selection resolves (§9.1)
            if (arguments[index] == "--default-family") {
                // a flag without its value is a misuse, which the build that wired it up got wrong (§14.2)
                if (index + 1 >= arguments.size()) {
                    // the missing value is named, because the build log should say what to fix (§14.2)
                    std::fprintf(stderr, "albor-font-embed: --default-family needs a family\n");
                    return 2;
                }
                // the family is captured after the flag, which the composition root will name (§9.1)
                default_family = std::string(arguments[++index]);
                continue;
            }
            // the font flag takes a family, a weight and a file, which is one declared face (§11.3)
            if (arguments[index] == "--font") {
                // a face needs three values, and a shorter tail is a misuse (§11.3)
                if (index + 3 >= arguments.size()) {
                    // the missing values are named, because the build log should say what to fix (§11.3)
                    std::fprintf(stderr, "albor-font-embed: --font needs a family, a weight and a file\n");
                    return 2;
                }
                // the family is read first, because the weight and path follow it (§9.1)
                std::string family(arguments[++index]);
                // the weight is read next, and an unknown spelling is a misuse (§9.1)
                const std::optional<FontWeight> weight = parse_font_weight(arguments[++index]);
                // an unknown weight stops the run before anything is generated (§9.1)
                if (!weight) {
                    // the bad spelling is named, because the build log should say what to fix (§9.1)
                    std::fprintf(stderr, "albor-font-embed: '%s' is not a font weight\n", std::string(arguments[index]).c_str());
                    return 2;
                }
                // the file is the last value, which the face is read from (§11.3)
                std::string file(arguments[++index]);
                // the face joins the set, which the loader reads and validates (§11.3)
                specs.push_back(FontAssetSpec{.family = std::move(family), .weight = *weight, .path = std::move(file)});
                continue;
            }
            // an unknown flag is a misuse, which the build that wired it up got wrong (§11.3)
            std::fprintf(stderr, "albor-font-embed: unknown flag '%.*s'\n", static_cast<int>(arguments[index].size()), arguments[index].data());
            return 2;
        }
        // an output, a default family and at least one face are what the generator needs (§11.3)
        if (output.empty() || default_family.empty() || specs.empty()) {
            // the usage is printed, because the build omitted one of the required pieces (§11.3)
            std::fprintf(stderr, "albor-font-embed: usage: albor-font-embed --output <dir> --default-family <family> --font <family> <weight> <file> [--font ...]\n");
            return 2;
        }
        // the problems are collected in one list through the load and validation (§11.3)
        compiler::DiagnosticList diagnostics;
        // the faces are read and validated before anything is written (§11.3)
        const FontAssetSet set = load_font_assets(default_family, specs, diagnostics);
        // a blocking problem stops the run, because a broken unit must not land in the build (§11.3)
        if (diagnostics.has_errors()) {
            // every problem is printed with its code, which is what a build log needs (§11.3)
            for (const compiler::Diagnostic& diagnostic : diagnostics.diagnostics())
                std::fprintf(stderr, "albor-font-embed: %s: %s\n", diagnostic.code.c_str(), diagnostic.message.c_str());
            return 1;
        }
        // the non blocking warnings still reach the log, because they are worth reading (§11.3)
        if (diagnostics.count() != 0U) {
            // every warning is printed with its code, which is what a build log needs (§11.3)
            for (const compiler::Diagnostic& diagnostic : diagnostics.diagnostics())
                std::fprintf(stderr, "albor-font-embed: %s: %s\n", diagnostic.code.c_str(), diagnostic.message.c_str());
        }
        // the generated files are emitted from the validated set, which always carries at least one face (§14.2)
        const std::vector<compiler::GeneratedFile> files = emit_font_assets(set);
        // the output directory must exist before a file lands in it (§14.2)
        std::error_code error;
        std::filesystem::create_directories(output, error);
        // a directory that cannot be created stops the run, because no file can land there (§14.2)
        if (error) {
            // the directory is named, which is what the build log needs to fix it (§14.2)
            std::fprintf(stderr, "albor-font-embed: cannot create '%s'\n", output.string().c_str());
            return 1;
        }
        // each file is written only when its content changed, which keeps incremental builds quiet (§14.2, AD-03a)
        for (const compiler::GeneratedFile& file : files) {
            // the write happens under the output directory, which the build named (§14.2)
            const compiler::WriteResult result = compiler::write_if_changed(output / file.path, file.content);
            // a failed write stops the run, because a stale unit must not read as a success (§14.2)
            if (result == compiler::WriteResult::Failed) {
                // the file is named, which is what the build log needs to fix it (§14.2)
                std::fprintf(stderr, "albor-font-embed: cannot write '%s'\n", (output / file.path).string().c_str());
                return 1;
            }
            // the outcome is reported per file, which is what the build log shows (§14.2)
            std::fprintf(stdout, "albor-font-embed: %s %s\n", file.path.c_str(), result == compiler::WriteResult::Written ? "written" : "unchanged");
        }
        // every file landed, which is what the driver reports as success (§14.2)
        return 0;
    }
} // namespace albor::manifest
