#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/diagnostic.h"
#include "manifest/font_embed.h"

using namespace albor;
using namespace albor::manifest;

TEST(FontEmbedChecks, ParsesEveryWeightAndRefusesAnUnknownOne) {
    // act every written form is parsed, and an unknown spelling is asked for (§9.1)
    const std::optional<FontWeight> thin = parse_font_weight("thin");
    const std::optional<FontWeight> light = parse_font_weight("light");
    const std::optional<FontWeight> regular = parse_font_weight("regular");
    const std::optional<FontWeight> medium = parse_font_weight("medium");
    const std::optional<FontWeight> semibold = parse_font_weight("semibold");
    const std::optional<FontWeight> bold = parse_font_weight("bold");
    const std::optional<FontWeight> black = parse_font_weight("black");
    const std::optional<FontWeight> unknown = parse_font_weight("heavy");
    // assert every spelling resolves to its enumerator, and the unknown one resolves to nothing (§9.1)
    EXPECT_EQ(thin, FontWeight::Thin);
    EXPECT_EQ(light, FontWeight::Light);
    EXPECT_EQ(regular, FontWeight::Regular);
    EXPECT_EQ(medium, FontWeight::Medium);
    EXPECT_EQ(semibold, FontWeight::Semibold);
    EXPECT_EQ(bold, FontWeight::Bold);
    EXPECT_EQ(black, FontWeight::Black);
    EXPECT_EQ(unknown, std::nullopt);
}

TEST(FontEmbedChecks, SpellsEveryWeightForCpp) {
    // act every enumerator is spelled, and an out of range value is asked for (§11.3)
    const std::string_view thin = font_weight_name(FontWeight::Thin);
    const std::string_view light = font_weight_name(FontWeight::Light);
    const std::string_view regular = font_weight_name(FontWeight::Regular);
    const std::string_view medium = font_weight_name(FontWeight::Medium);
    const std::string_view semibold = font_weight_name(FontWeight::Semibold);
    const std::string_view bold = font_weight_name(FontWeight::Bold);
    const std::string_view black = font_weight_name(FontWeight::Black);
    const std::string_view fallback = font_weight_name(static_cast<FontWeight>(99));
    // assert every enumerator spells its C++ name, and the defensive default names regular (§11.3)
    EXPECT_EQ(thin, "Thin");
    EXPECT_EQ(light, "Light");
    EXPECT_EQ(regular, "Regular");
    EXPECT_EQ(medium, "Medium");
    EXPECT_EQ(semibold, "Semibold");
    EXPECT_EQ(bold, "Bold");
    EXPECT_EQ(black, "Black");
    EXPECT_EQ(fallback, "Regular");
}

TEST(FontEmbedChecks, RecognizesTheSfntContainersAndRefusesAShortHeader) {
    // act each container magic is asked for, plus a short and a foreign stream (§15)
    const bool truetype = is_sfnt(std::vector<std::uint8_t>{0x00, 0x01, 0x00, 0x00});
    const bool opentype = is_sfnt(std::vector<std::uint8_t>{0x4F, 0x54, 0x54, 0x4F});
    const bool collection = is_sfnt(std::vector<std::uint8_t>{0x74, 0x74, 0x63, 0x66});
    const bool apple = is_sfnt(std::vector<std::uint8_t>{0x74, 0x72, 0x75, 0x65});
    const bool short_header = is_sfnt(std::vector<std::uint8_t>{0x00, 0x01});
    const bool foreign = is_sfnt(std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x00});
    // assert every container is accepted and a header that cannot be an sfnt is refused (§15)
    EXPECT_TRUE(truetype);
    EXPECT_TRUE(opentype);
    EXPECT_TRUE(collection);
    EXPECT_TRUE(apple);
    EXPECT_FALSE(short_header);
    EXPECT_FALSE(foreign);
}

TEST(FontEmbedChecks, AValidSetCarriesNoProblem) {
    // arrange a set with one family at two weights, which is the shape the generator expects (§11.3)
    FontAssetSet set;
    // the default family is the regular face, which a bare selection resolves (§9.1)
    set.default_family = "Lato";
    // the regular face is the first face a run without a weight resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0xAA}});
    // the bold face is the second face a weighted selection resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {0x00, 0x01, 0x00, 0x00, 0xBB}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert nothing is reported, because every face is readable and the default resolves (§11.3)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontEmbedChecks, AnEmptySetIsRefused) {
    // arrange a set with no face, which cannot register a default family (§11.3)
    FontAssetSet set;
    // the default family names a family nothing carries (§9.1)
    set.default_family = "Lato";
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the empty set is refused before the default is checked (§11.3)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0709");
}

TEST(FontEmbedChecks, AFaceWithoutAFamilyOrBytesIsRefused) {
    // arrange a valid default face beside one without a family and one without bytes (§11.3)
    FontAssetSet set;
    // the default family is carried by a valid face, so the set has a usable default (§9.1)
    set.default_family = "Lato";
    // the valid regular face is what the default resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // the nameless face is refused, because no run can name it (§9.1)
    set.faces.push_back(FontAsset{.family = "", .weight = FontWeight::Bold, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // the empty face is refused, because it cannot shape (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert both unusable faces are reported with their own codes (§11.3)
    ASSERT_EQ(diagnostics.count(), 2u);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0702");
    EXPECT_EQ(diagnostics.diagnostics().at(1).code, "E0703");
}

TEST(FontEmbedChecks, AFontFamilyWithAControlByteIsRefused) {
    // arrange a valid default face beside a face whose family carries a newline (§11.3)
    FontAssetSet set;
    // the default family is carried by a valid face, so the set has a usable default (§9.1)
    set.default_family = "Lato";
    // the valid regular face is what the default resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // the control byte would end the generated comment or literal, so the family is refused (§11.3)
    set.faces.push_back(FontAsset{.family = "A\nB", .weight = FontWeight::Bold, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the control byte is reported with its own code (§11.3)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0710");
}

TEST(FontEmbedChecks, ADefaultFamilyWithAControlByteIsRefused) {
    // arrange a default family that carries a tab (§11.3)
    FontAssetSet set;
    // the default family names the control byte, which a generated literal cannot carry (§11.3)
    set.default_family = "A\tB";
    // the face carries the same name, so the control byte is the only problem (§11.3)
    set.faces.push_back(FontAsset{.family = "A\tB", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the default family's control byte is reported (§11.3)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0710");
}

TEST(FontEmbedChecks, AFaceThatIsNotAnSfntIsRefused) {
    // arrange a valid default face beside a face whose bytes are not a font (§15)
    FontAssetSet set;
    // the default family is carried by a valid face, so the set has a usable default (§9.1)
    set.default_family = "Lato";
    // the valid regular face is what the default resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // the malformed face is refused, because a face builder cannot read it (§15)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {0xDE, 0xAD, 0xBE, 0xEF}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the malformed face is reported with the sfnt code (§11.3)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0704");
}

TEST(FontEmbedChecks, ADuplicateFamilyAndWeightIsRefused) {
    // arrange two faces of one family and weight, which the registry cannot both answer (§11.3)
    FontAssetSet set;
    // the default family is carried by the first face (§9.1)
    set.default_family = "Lato";
    // the first regular face is what the default resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // the second regular face repeats the pairing, which only one can register (§11.3)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x02}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the duplicate pairing is reported (§11.3)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0701");
}

TEST(FontEmbedChecks, ADefaultFamilyMustBeNamedAndCarried) {
    // arrange a valid face whose family is not the default (§9.1)
    FontAssetSet set;
    // the regular face is carried under a family the default does not name (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // act an unnamed default is validated, then a name nothing carries (§9.1)
    compiler::DiagnosticList unnamed;
    validate_font_assets(set, unnamed);
    set.default_family = "Missing";
    compiler::DiagnosticList absent;
    validate_font_assets(set, absent);
    // assert both forms are reported with the default family code (§11.3)
    ASSERT_EQ(unnamed.count(), 1u);
    ASSERT_EQ(absent.count(), 1u);
    EXPECT_EQ(unnamed.diagnostics().front().code, "E0705");
    EXPECT_EQ(absent.diagnostics().front().code, "E0705");
}

TEST(FontEmbedChecks, ADefaultFamilyMustCarryItsRegularFace) {
    // arrange a default family that carries only a bold face (§9.1)
    FontAssetSet set;
    // the default family names the family, which has no regular face to resolve (§9.1)
    set.default_family = "Lato";
    // the bold face is carried, but a bare family name asks for regular (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the missing regular face is reported with its own code (§11.3)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0706");
}

TEST(FontEmbedChecks, TwoWeightsOverOneFileWarn) {
    // arrange one family whose two weights carry the same bytes, which shape identically (§9.1)
    FontAssetSet set;
    // the default family is carried by both faces (§9.1)
    set.default_family = "Lato";
    // the regular face is the default a bare family name resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // the bold face repeats the same bytes, which the engine does not instance (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert the repeated file is a warning rather than a blocking problem (§11.3)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().severity, compiler::Diagnostic::Severity::Warning);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "W0707");
}

TEST(FontEmbedChecks, TwoFamiliesMayShareOneFile) {
    // arrange two families whose regular faces carry the same bytes, which is legal aliasing (§9.1)
    FontAssetSet set;
    // the default family is the first face, which a bare selection resolves (§9.1)
    set.default_family = "Lato";
    // the first family carries the bytes under its own name (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // the second family repeats the bytes under another name, which is not a weight mistake (§9.1)
    set.faces.push_back(FontAsset{.family = "Alias", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // act the set is validated (§11.3)
    compiler::DiagnosticList diagnostics;
    validate_font_assets(set, diagnostics);
    // assert no warning is reported, because two families may intentionally share bytes (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontEmbedChecks, LoadReadsTheDeclaredFaces) {
    // arrange the carried lato faces, which are the two weights of one real family (§9.1, AD-34)
    const std::vector<FontAssetSpec> specs{{.family = ALBOR_TEST_WEIGHT_FONT_FAMILY, .weight = FontWeight::Regular, .path = ALBOR_TEST_WEIGHT_FONT_PATH}, {.family = ALBOR_TEST_WEIGHT_FONT_FAMILY, .weight = FontWeight::Bold, .path = ALBOR_TEST_BOLD_FONT_PATH}};
    // act the faces are read into the set (§11.3)
    compiler::DiagnosticList diagnostics;
    const FontAssetSet set = load_font_assets(ALBOR_TEST_WEIGHT_FONT_FAMILY, specs, diagnostics);
    // assert both faces are carried with their bytes and no problem (§11.3)
    EXPECT_EQ(diagnostics.count(), 0u);
    ASSERT_EQ(set.faces.size(), 2u);
    EXPECT_EQ(set.faces.at(0).weight, FontWeight::Regular);
    EXPECT_EQ(set.faces.at(1).weight, FontWeight::Bold);
    EXPECT_FALSE(set.faces.at(0).bytes.empty());
    EXPECT_FALSE(set.faces.at(1).bytes.empty());
}

TEST(FontEmbedChecks, LoadReportsAFileThatCannotBeRead) {
    // arrange a declared face whose file does not exist (§11.3)
    const std::vector<FontAssetSpec> specs{{.family = "Missing", .weight = FontWeight::Regular, .path = "/albor/no-such-font.ttf"}};
    // act the face is read into the set (§11.3)
    compiler::DiagnosticList diagnostics;
    const FontAssetSet set = load_font_assets("Missing", specs, diagnostics);
    // assert the unreadable file is reported and the set carries no face for it (§11.3)
    ASSERT_EQ(set.faces.size(), 0u);
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0708");
}

TEST(FontEmbedChecks, EmitWritesTheHeaderAndSource) {
    // arrange a family at two weights, which the generated unit registers (§11.3, AD-34)
    FontAssetSet set;
    // the default family is the regular face, which a bare selection resolves (§9.1)
    set.default_family = "Lato";
    // the regular face is the first face a run without a weight resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0xAA}});
    // the bold face is the second face a weighted selection resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {0x00, 0x01, 0x00, 0x00, 0xBB}});
    // act the unit is emitted (§14.2)
    const std::vector<compiler::GeneratedFile> files = emit_font_assets(set);
    // assert the header and source are handed out with the family, weights and byte arrays (§11.3)
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files.at(0).path, "albor_embedded_fonts.h");
    EXPECT_EQ(files.at(1).path, "albor_embedded_fonts.cpp");
    EXPECT_NE(files.at(0).content.find("DEFAULT_FAMILY"), std::string::npos);
    EXPECT_NE(files.at(0).content.find("return std::string_view(\"Lato\")"), std::string::npos);
    EXPECT_NE(files.at(0).content.find("install_embedded_fonts"), std::string::npos);
    EXPECT_NE(files.at(1).content.find("FONT_BYTES_0"), std::string::npos);
    EXPECT_NE(files.at(1).content.find("FONT_BYTES_1"), std::string::npos);
    EXPECT_NE(files.at(1).content.find("FontWeight::Bold"), std::string::npos);
    EXPECT_NE(files.at(1).content.find("register_font"), std::string::npos);
}

TEST(FontEmbedChecks, EmitEscapesAFamilyName) {
    // arrange a family name with a quote and a backslash, which a C++ literal must escape (§11.3)
    FontAssetSet set;
    // the default family is the escaped name, which the header carries (§9.1)
    set.default_family = "A\"B\\C";
    // the regular face uses the same name, which the registration carries (§9.1)
    set.faces.push_back(FontAsset{.family = "A\"B\\C", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // act the unit is emitted (§14.2)
    const std::vector<compiler::GeneratedFile> files = emit_font_assets(set);
    // assert the generated literals escape the quote and the backslash (§11.3)
    ASSERT_EQ(files.size(), 2u);
    EXPECT_NE(files.at(0).content.find("return std::string_view(\"A\\\"B\\\\C\")"), std::string::npos);
    EXPECT_NE(files.at(1).content.find(".family = \"A\\\"B\\\\C\""), std::string::npos);
}

TEST(FontEmbedChecks, EmitRefusesAFaceWithoutBytes) {
    // arrange a valid default face beside a face with no bytes (§11.3)
    FontAssetSet set;
    // the default family names the valid face (§9.1)
    set.default_family = "Lato";
    // the valid regular face is what the default resolves (§9.1)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00}});
    // the empty face would produce an empty array, which is invalid C++ (§11.3)
    set.faces.push_back(FontAsset{.family = "Lato", .weight = FontWeight::Bold, .bytes = {}});
    // act the unit is emitted directly, which the validator would have refused before this point (§14.2)
    const std::vector<compiler::GeneratedFile> files = emit_font_assets(set);
    // assert no file is produced, because an empty array cannot compile (§11.3)
    EXPECT_TRUE(files.empty());
}

TEST(FontEmbedChecks, EmitEscapesControlBytes) {
    // arrange a family with a newline, a carriage return and a tab, which a literal must escape (§11.3)
    FontAssetSet set;
    // the default family is the escaped name, which the header carries (§9.1)
    set.default_family = "A\nB\rC\tD";
    // the regular face uses the same name, which the registration carries (§9.1)
    set.faces.push_back(FontAsset{.family = "A\nB\rC\tD", .weight = FontWeight::Regular, .bytes = {0x00, 0x01, 0x00, 0x00, 0x01}});
    // act the unit is emitted directly, which the validator would have refused before this point (§14.2)
    const std::vector<compiler::GeneratedFile> files = emit_font_assets(set);
    // assert the generated literals escape the control bytes rather than ending the literal (§11.3)
    ASSERT_EQ(files.size(), 2u);
    EXPECT_NE(files.at(0).content.find("A\\nB\\rC\\tD"), std::string::npos);
    EXPECT_NE(files.at(1).content.find("A\\nB\\rC\\tD"), std::string::npos);
}

TEST(FontEmbedChecks, EmitRefusesAnEmptySet) {
    // arrange a set with no face, which has nothing to register (§11.3)
    FontAssetSet set;
    // the default family names a family nothing carries (§9.1)
    set.default_family = "Lato";
    // act the unit is emitted (§14.2)
    const std::vector<compiler::GeneratedFile> files = emit_font_assets(set);
    // assert an empty set produces no file, because an empty unit is a build mistake (§11.3)
    EXPECT_TRUE(files.empty());
}

TEST(FontEmbedChecks, RunEmbedsADeclaredSet) {
    // arrange a fresh output directory and the two carried lato faces (§11.3, AD-34)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-font-embed-cli-success";
    std::filesystem::remove_all(directory);
    const std::string output = directory.string();
    const std::string regular = ALBOR_TEST_WEIGHT_FONT_PATH;
    const std::string bold = ALBOR_TEST_BOLD_FONT_PATH;
    const std::vector<std::string_view> arguments{"--output", output, "--default-family", "Lato", "--font", "Lato", "regular", regular, "--font", "Lato", "bold", bold};
    // act the driver runs over the command line, and then again over the same output (§14.2)
    const int result = run_font_embed(arguments);
    const int unchanged = run_font_embed(arguments);
    // assert both generated files landed and the second run reported them unchanged (§14.2)
    EXPECT_EQ(result, 0);
    EXPECT_EQ(unchanged, 0);
    EXPECT_TRUE(std::filesystem::exists(directory / "albor_embedded_fonts.h"));
    EXPECT_TRUE(std::filesystem::exists(directory / "albor_embedded_fonts.cpp"));
    // the probe is removed, so the test leaves nothing behind (§14.2)
    std::filesystem::remove_all(directory);
}

TEST(FontEmbedChecks, RunWarnsWhenTwoWeightsShareOneFile) {
    // arrange a fresh output directory and one file declared at two weights (§9.1)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-font-embed-cli-warning";
    std::filesystem::remove_all(directory);
    const std::string output = directory.string();
    const std::string regular = ALBOR_TEST_WEIGHT_FONT_PATH;
    const std::vector<std::string_view> arguments{"--output", output, "--default-family", "Lato", "--font", "Lato", "regular", regular, "--font", "Lato", "bold", regular};
    // act the driver runs over the command line (§14.2)
    const int result = run_font_embed(arguments);
    // assert a warning does not block the generated unit (§11.3)
    EXPECT_EQ(result, 0);
    // the probe is removed, so the test leaves nothing behind (§14.2)
    std::filesystem::remove_all(directory);
}

TEST(FontEmbedChecks, RunRefusesAnUnreadableFile) {
    // arrange a fresh output directory and a face whose file does not exist (§11.3)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-font-embed-cli-missing";
    std::filesystem::remove_all(directory);
    const std::string output = directory.string();
    const std::vector<std::string_view> arguments{"--output", output, "--default-family", "Missing", "--font", "Missing", "regular", "/albor/no-such-font.ttf"};
    // act the driver runs over the command line (§14.2)
    const int result = run_font_embed(arguments);
    // assert the unreadable file stops the run (§11.3)
    EXPECT_EQ(result, 1);
    // the probe is removed, so the test leaves nothing behind (§14.2)
    std::filesystem::remove_all(directory);
}

TEST(FontEmbedChecks, RunRefusesAMissingFlagValue) {
    // act each flag is asked for without its value (§14.2)
    const int output = run_font_embed(std::vector<std::string_view>{"--output"});
    const int family = run_font_embed(std::vector<std::string_view>{"--output", "/tmp/x", "--default-family"});
    const int font = run_font_embed(std::vector<std::string_view>{"--output", "/tmp/x", "--default-family", "Lato", "--font", "Lato", "regular"});
    // assert each omission is a usage error (§14.2)
    EXPECT_EQ(output, 2);
    EXPECT_EQ(family, 2);
    EXPECT_EQ(font, 2);
}

TEST(FontEmbedChecks, RunRefusesAnUnknownFlagOrWeight) {
    // act an unknown flag and an unknown weight are asked for (§14.2)
    const int flag = run_font_embed(std::vector<std::string_view>{"--unknown"});
    const int weight = run_font_embed(std::vector<std::string_view>{"--output", "/tmp/x", "--default-family", "Lato", "--font", "Lato", "heavy", "/tmp/font.ttf"});
    // assert both are usage errors (§14.2)
    EXPECT_EQ(flag, 2);
    EXPECT_EQ(weight, 2);
}

TEST(FontEmbedChecks, RunRefusesAMissingRequiredPiece) {
    // act an empty command line is run, which names no output, family or face (§14.2)
    const int result = run_font_embed(std::vector<std::string_view>{});
    // assert the missing pieces are a usage error (§14.2)
    EXPECT_EQ(result, 2);
}

TEST(FontEmbedChecks, RunReportsAnOutputThatCannotBeCreated) {
    // arrange an output path that is a regular file, which no directory can be created over (§14.2)
    const std::filesystem::path output = std::filesystem::temp_directory_path() / "albor-font-embed-cli-output-file";
    std::filesystem::remove_all(output);
    std::ofstream file(output);
    file << "occupied";
    file.close();
    const std::string output_text = output.string();
    const std::string regular = ALBOR_TEST_WEIGHT_FONT_PATH;
    const std::vector<std::string_view> arguments{"--output", output_text, "--default-family", "Lato", "--font", "Lato", "regular", regular};
    // act the driver runs over the command line (§14.2)
    const int result = run_font_embed(arguments);
    // assert the failed directory stops the run (§14.2)
    EXPECT_EQ(result, 1);
    // the probe is removed, so the test leaves nothing behind (§14.2)
    std::filesystem::remove_all(output);
}

TEST(FontEmbedChecks, RunReportsAFileThatCannotBeWritten) {
    // arrange an output directory whose generated header is already a directory (§14.2)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-font-embed-cli-write-failure";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory / "albor_embedded_fonts.h");
    const std::string output = directory.string();
    const std::string regular = ALBOR_TEST_WEIGHT_FONT_PATH;
    const std::vector<std::string_view> arguments{"--output", output, "--default-family", "Lato", "--font", "Lato", "regular", regular};
    // act the driver runs over the command line (§14.2)
    const int result = run_font_embed(arguments);
    // assert the failed write stops the run rather than reporting unchanged (§14.2)
    EXPECT_EQ(result, 1);
    // the probe is removed, so the test leaves nothing behind (§14.2)
    std::filesystem::remove_all(directory);
}
