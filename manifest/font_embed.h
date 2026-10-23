#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "core/font_weight.h"

namespace albor::manifest
{
    // one face a build declares, which names where its bytes come from (§11.3, AD-34)
    struct FontAssetSpec
    {
        // the family an application names, which is a label rather than a platform lookup (§9.1)
        std::string family;

        // the weight the face carries inside that family (§9.1, AD-34)
        FontWeight weight = FontWeight::Regular;

        // the file the build reads the face from, which a font never touches at runtime (§11.3)
        std::filesystem::path path;
    };

    // one face read into the build, which the generated unit registers at startup (§11.3, AD-34)
    struct FontAsset
    {
        // the family the generated code registers the face under (§9.1)
        std::string family;

        // the weight the generated code registers beside the family (§9.1, AD-34)
        FontWeight weight = FontWeight::Regular;

        // the bytes the generated unit carries into the binary (§15, AD-34)
        std::vector<std::uint8_t> bytes;
    };

    // the faces one target carries and the family its composition root names by default (§11.3, AD-34)
    struct FontAssetSet
    {
        // the family a run that names no family resolves, which the composition root names (§9.1)
        std::string default_family;

        // the faces the generated unit registers, in declaration order (§11.3)
        std::vector<FontAsset> faces;
    };

    // resolve a weight written on the command line, which is the spelling a build script uses (§9.1)
    [[nodiscard]] std::optional<FontWeight> parse_font_weight(std::string_view name);

    // spell a weight for C++, which the generated registration names (§11.3)
    [[nodiscard]] std::string_view font_weight_name(FontWeight weight);

    // report whether the bytes begin like an sfnt, which is what a face can be built from (§15)
    [[nodiscard]] bool is_sfnt(std::span<const std::uint8_t> bytes);

    // read the declared faces and refuse what a generated unit cannot carry (§11.3, AD-34)
    [[nodiscard]] FontAssetSet load_font_assets(std::string default_family, const std::vector<FontAssetSpec>& specs, compiler::DiagnosticList& diagnostics);

    // report every problem a loaded set carries, before the generator emits anything (§11.3)
    void validate_font_assets(const FontAssetSet& set, compiler::DiagnosticList& diagnostics);

    // emit the generated registration unit and its header, which the build writes if changed (§14.2, AD-34)
    [[nodiscard]] std::vector<compiler::GeneratedFile> emit_font_assets(const FontAssetSet& set);

    // run the embedding driver over its command line, which returns zero only when every file landed (§14.2)
    [[nodiscard]] int run_font_embed(std::span<const std::string_view> arguments);
} // namespace albor::manifest
