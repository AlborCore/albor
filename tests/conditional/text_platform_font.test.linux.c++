// the linux half of the engine's platform surfaces: the fontconfig resolver and the sfnt it hands over (§9.1, AD-28)

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#if defined(ALBOR_HAVE_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#endif

#include <gtest/gtest.h>

#include "albor/resources/embedded_font.h"
#include "albor/text/text-harfbuzz/harfbuzz_platform_font.h"
#include "albor/text/text-harfbuzz/harfbuzz_text_engine.h"
#include "albor/text/text-harfbuzz/harfbuzz_typeface_resolver.h"

using namespace albor;

TEST(LinuxPlatformFont, AnEmptyFamilyIsRefusedByTheFactory) {
    // act assert an empty family names no font, so the resolver is refused rather than built empty (§9.1)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create(std::string()), nullptr);
}

TEST(LinuxPlatformFont, AnUnknownFamilyIsRefusedRatherThanSubstituted) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private configuration that carries the test fonts, because the host must not answer the family (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-unknown.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act a family no carried font ships, which fontconfig would otherwise substitute a face for (§15, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Albor Missing Family 0xA");
    // assert the resolver came up but carries no face, because a fallback is refused under the wrong family (§15)
    EXPECT_NE(resolver, nullptr);
    if (resolver != nullptr) {
        // the typeface is what the caller reads, and it is null when the family was substituted (§15)
        EXPECT_EQ(resolver->typeface(), nullptr);
    }
    // act the same absent family is asked for at a bold weight, which the pattern matches at that weight (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> bold = harfbuzz::HarfbuzzTypefaceResolver::create("Albor Missing Family 0xA", FontWeight::Bold);
    // assert the substituted family is refused at a weight too, because identity does not depend on it (§15)
    EXPECT_NE(bold, nullptr);
    if (bold != nullptr) {
        // the typeface is null for the bold request as well, because fontconfig substituted a family (§15)
        EXPECT_EQ(bold->typeface(), nullptr);
    }
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig resolves no platform face, so the factory is refused (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create("Albor Missing Family 0xA"), nullptr);
#endif
}

TEST(LinuxPlatformFont, AFamilyTheSystemShipsResolves) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private configuration that carries the test fonts, because resolution must not depend on the host (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-family.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act the carried family is resolved, which is the face the configuration builds (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Inter");
    // assert the face resolved, because the family the configuration offers is the one the run named (§9.1, AD-28)
    EXPECT_NE(resolver, nullptr);
    if (resolver != nullptr) {
        // the typeface is what the caller reads, which a resolved family must carry (§9.1)
        EXPECT_NE(resolver->typeface(), nullptr);
    }
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig resolves no platform face, so the factory is refused (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create("Inter"), nullptr);
#endif
}

TEST(LinuxPlatformFont, AResolvedFamilyHandsOverTheCarriedFace) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private configuration that carries the test fonts, because resolution must not depend on the host (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-carried.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act the resolved family hands over the carried file and not another face of the same name (§9.1, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Inter");
    EXPECT_NE(resolver, nullptr);
    if (resolver != nullptr) {
        // act the bytes the resolver hands over are what the run draws (§15, AD-34)
        const std::vector<std::uint8_t> bytes = harfbuzz::platform_font_bytes(resolver->typeface());
        // arrange a pattern over the carried file, which is the identity the family must resolve to (§15)
        FcPattern* carried = FcPatternCreate();
        FcPatternAddString(carried, FC_FILE, reinterpret_cast<const FcChar8*>(ALBOR_TEST_FONT_PATH));
        FcPatternAddInteger(carried, FC_INDEX, 0);
        // act the carried pattern is the expected face, which the resolver must agree with (§15, AD-34)
        const std::vector<std::uint8_t> expected = harfbuzz::platform_font_bytes(carried);
        // the pattern goes back once its bytes are copied (§15)
        FcPatternDestroy(carried);
        // assert the two are the same face, because the family resolved the carried bytes (§9.1, AD-34)
        EXPECT_FALSE(bytes.empty());
        EXPECT_EQ(bytes, expected);
    }
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig resolves no platform face, so the factory is refused (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create("Inter"), nullptr);
#endif
}

TEST(LinuxPlatformFont, AWeightSelectsTheBoldFaceOfItsFamily) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private directory carrying both Lato weights, because the default face resolves one family alone (§9.1)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-fontconfig-weight";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    // the two carried faces are the only members the configuration can resolve (§9.1)
    std::filesystem::copy_file(ALBOR_TEST_WEIGHT_FONT_PATH, directory / "Lato-Regular.ttf");
    std::filesystem::copy_file(ALBOR_TEST_BOLD_FONT_PATH, directory / "Lato-Bold.ttf");
    // the configuration names that directory as its only source (§9.1)
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-weight.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file above is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act the family is resolved at each carried weight, which is the selection the run carries (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> regular = harfbuzz::HarfbuzzTypefaceResolver::create(ALBOR_TEST_WEIGHT_FONT_FAMILY, FontWeight::Regular);
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> bold = harfbuzz::HarfbuzzTypefaceResolver::create(ALBOR_TEST_WEIGHT_FONT_FAMILY, FontWeight::Bold);
    // assert both requests resolved, because the configuration carries both members (§9.1, AD-28)
    EXPECT_NE(regular, nullptr);
    EXPECT_NE(bold, nullptr);
    // arrange the carried file each weight names, which is the face the resolver must hand over (§15)
    FcPattern* regular_file = FcPatternCreate();
    FcPatternAddString(regular_file, FC_FILE, reinterpret_cast<const FcChar8*>(ALBOR_TEST_WEIGHT_FONT_PATH));
    FcPatternAddInteger(regular_file, FC_INDEX, 0);
    FcPattern* bold_file = FcPatternCreate();
    FcPatternAddString(bold_file, FC_FILE, reinterpret_cast<const FcChar8*>(ALBOR_TEST_BOLD_FONT_PATH));
    FcPatternAddInteger(bold_file, FC_INDEX, 0);
    // act the bytes each resolution hands over are what the run draws, where a refusal carries none (§15, AD-34)
    const std::vector<std::uint8_t> regular_bytes = regular != nullptr ? harfbuzz::platform_font_bytes(regular->typeface()) : std::vector<std::uint8_t>{};
    const std::vector<std::uint8_t> bold_bytes = bold != nullptr ? harfbuzz::platform_font_bytes(bold->typeface()) : std::vector<std::uint8_t>{};
    const std::vector<std::uint8_t> regular_carried = harfbuzz::platform_font_bytes(regular_file);
    const std::vector<std::uint8_t> bold_carried = harfbuzz::platform_font_bytes(bold_file);
    // the patterns go back once their bytes are copied (§15)
    FcPatternDestroy(regular_file);
    FcPatternDestroy(bold_file);
    // assert each weight resolved its own carried face, because the weight reached fontconfig (§9.1)
    EXPECT_EQ(regular_bytes, regular_carried);
    EXPECT_EQ(bold_bytes, bold_carried);
    EXPECT_NE(regular_bytes, bold_bytes);
    // act a request that names no weight is the regular face, which the default argument preserves (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> named = harfbuzz::HarfbuzzTypefaceResolver::create(ALBOR_TEST_WEIGHT_FONT_FAMILY);
    // assert the default request resolves the regular face, because a weight is opt in (§9.1)
    EXPECT_NE(named, nullptr);
    if (named != nullptr) {
        EXPECT_EQ(harfbuzz::platform_font_bytes(named->typeface()), regular_bytes);
    }
    // act the default face carries the weight too, which the configured family answers (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> default_regular = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> default_bold = harfbuzz::HarfbuzzTypefaceResolver::create_default(FontWeight::Bold);
    // assert the default face resolves the same carried pair, because the configuration holds one family (§9.1)
    EXPECT_NE(default_regular, nullptr);
    EXPECT_NE(default_bold, nullptr);
    if (default_regular != nullptr) {
        EXPECT_EQ(harfbuzz::platform_font_bytes(default_regular->typeface()), regular_carried);
    }
    if (default_bold != nullptr) {
        EXPECT_EQ(harfbuzz::platform_font_bytes(default_bold->typeface()), bold_carried);
    }
    // act the engine built at each weight resolves the face the factory handed over (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine_regular = harfbuzz::HarfbuzzTextEngine::create_platform(ALBOR_TEST_WEIGHT_FONT_FAMILY, FontWeight::Regular);
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine_bold = harfbuzz::HarfbuzzTextEngine::create_platform(ALBOR_TEST_WEIGHT_FONT_FAMILY, FontWeight::Bold);
    // assert the engine keys a different face per weight, so the selection reaches the shaper (§9.1, AD-34)
    EXPECT_NE(engine_regular, nullptr);
    EXPECT_NE(engine_bold, nullptr);
    if (engine_regular != nullptr && engine_bold != nullptr) {
        EXPECT_NE(engine_regular->font_hash(), engine_bold->font_hash());
    }
    // act the same run is measured by each engine, which is what a bold label asks it for (§9.1)
    const TextLayout regular_layout = engine_regular != nullptr ? engine_regular->measure("Hamburgefonstiv", 0.0f, 20.0f) : TextLayout{};
    const TextLayout bold_layout = engine_bold != nullptr ? engine_bold->measure("Hamburgefonstiv", 0.0f, 20.0f) : TextLayout{};
    // assert the bold face shapes a wider run, because the selected face reached the shaper (§9.1, AD-34)
    EXPECT_FALSE(regular_layout.empty());
    EXPECT_FALSE(bold_layout.empty());
    EXPECT_GT(bold_layout.size.width, regular_layout.size.width);
    // exit the registry is emptied, so the next test starts from nothing (§2.2)
    resources::FontRegistry::clear();
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
    // the private directory goes back, so the test leaves nothing behind (§2.2)
    std::filesystem::remove_all(directory);
#else
    // act assert a build without fontconfig resolves no platform face, so the factory is refused (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create(ALBOR_TEST_WEIGHT_FONT_FAMILY, FontWeight::Bold), nullptr);
#endif
}

TEST(LinuxPlatformFont, TheSystemFaceResolvesThroughADefaultCreate) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private configuration that carries the test fonts and aliases the system ui face to one of them (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-default.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir><alias><family>sans-serif</family><prefer><family>Inter</family></prefer></alias></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act the empty family asks fontconfig for its system ui face, which the alias names (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    // assert the face resolved, because a run that names none still draws (§9.1, AD-28)
    EXPECT_NE(resolver, nullptr);
    if (resolver != nullptr) {
        // the typeface is what the caller reads, which a resolved family must carry (§9.1)
        EXPECT_NE(resolver->typeface(), nullptr);
    }
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig resolves no system face, so the factory is refused (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create_default(), nullptr);
#endif
}

TEST(LinuxPlatformFont, TheDefaultFaceResolvesTheConfiguredFamily) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private directory whose only face is the carried Lato regular file (§9.1)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-fontconfig-only-lato";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    // the carried Lato face is the only family the configuration can resolve (§9.1)
    std::filesystem::copy_file(ALBOR_TEST_WEIGHT_FONT_PATH, directory / "Lato-Regular.ttf");
    // the configuration names that directory as its only source (§9.1)
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-default-family.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act the default face resolves the configuration's only family, which is Lato (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    EXPECT_NE(resolver, nullptr);
    if (resolver != nullptr) {
        // act the bytes the default face hands over are what the run draws (§15, AD-34)
        const std::vector<std::uint8_t> bytes = harfbuzz::platform_font_bytes(resolver->typeface());
        // arrange a pattern over the carried Lato regular face, which the alias names (§15)
        FcPattern* expected = FcPatternCreate();
        FcPatternAddString(expected, FC_FILE, reinterpret_cast<const FcChar8*>(ALBOR_TEST_WEIGHT_FONT_PATH));
        FcPatternAddInteger(expected, FC_INDEX, 0);
        // act the carried Lato pattern is what the alias must resolve to (§9.1)
        const std::vector<std::uint8_t> expected_bytes = harfbuzz::platform_font_bytes(expected);
        // the pattern goes back once its bytes are copied (§15)
        FcPatternDestroy(expected);
        // assert the default resolves the configured family and not a host face (§9.1)
        EXPECT_FALSE(bytes.empty());
        EXPECT_EQ(bytes, expected_bytes);
    }
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
    // the private directory goes back, so the test leaves nothing behind (§2.2)
    std::filesystem::remove_all(directory);
#else
    // act assert a build without fontconfig resolves no system face, so the factory is refused (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create_default(), nullptr);
#endif
}

TEST(LinuxPlatformFont, APlatformFaceHandsOverAReadableSfnt) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private configuration that carries the test fonts and aliases the system ui face to one of them (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-sfnt.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir><alias><family>sans-serif</family><prefer><family>Inter</family></prefer></alias></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // arrange the default face, which the resolver issued (§9.1, AD-28)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    EXPECT_NE(resolver, nullptr);
    if (resolver != nullptr) {
        // act
        const std::vector<std::uint8_t> bytes = harfbuzz::platform_font_bytes(resolver->typeface());
        // assert the sfnt is there and readable, because a face the registry keys on starts at the version tag (§15)
        EXPECT_FALSE(bytes.empty());
        // the assertions below read the version tag, which needs the bytes to exist first (§15)
        if (!bytes.empty()) {
            // the version tag reads big endian, which is what every sfnt field reads as (§15)
            const std::uint32_t version = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
            // the tags a rebuilt face opens with are the truetype and the opentype one, never a collection (§15)
            EXPECT_TRUE(version == 0x00010000u || version == 0x4f54544fu);
        }
        // act a second ask is the reproducibility of the identity, which is total (§15, AD-34)
        const std::vector<std::uint8_t> again = harfbuzz::platform_font_bytes(resolver->typeface());
        // assert the two asks agree, because the identity is the bytes and not the moment (§15)
        EXPECT_EQ(bytes, again);
    }
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig hands over no bytes, because no platform face exists (§9.1, AD-34)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
#endif
}

TEST(LinuxPlatformFont, ANullFaceCarriesNoBytes) {
    // act assert a handle the platform did not resolve carries nothing, which is a refusal (§15)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
}

TEST(LinuxPlatformFont, AMissingFileCarriesNoBytes) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a pattern whose file does not exist, which is the error the reader must refuse (§15)
    FcPattern* missing = FcPatternCreate();
    FcPatternAddString(missing, FC_FILE, reinterpret_cast<const FcChar8*>("/nonexistent/albor-font-file.ttf"));
    FcPatternAddInteger(missing, FC_INDEX, 0);
    // act assert a file that cannot be read carries nothing, which is a refusal (§15)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(missing).empty());
    // exit the pattern goes back, because the test owns it for exactly its own life (§15)
    FcPatternDestroy(missing);
#else
    // act assert a build without fontconfig hands over nothing, because no platform face exists (§9.1, AD-34)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
#endif
}

TEST(LinuxPlatformFont, ADirectoryInPlaceOfAFontFileCarriesNoBytes) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a directory where the pattern names a file, which a broken configuration can produce (§15, #246)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-font-file-is-a-directory";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FILE, reinterpret_cast<const FcChar8*>(directory.string().c_str()));
    FcPatternAddInteger(pattern, FC_INDEX, 0);
    // act assert a directory in place of a font file carries nothing, which is a refusal (§15, #246)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(pattern).empty());
    // exit the pattern goes back, because the test owns it for exactly its own life (§15)
    FcPatternDestroy(pattern);
    std::filesystem::remove_all(directory, ignored);
#else
    // act assert a build without fontconfig hands over nothing, because no platform face exists (§9.1, AD-34)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
#endif
}

TEST(LinuxPlatformFont, ACollectionFaceCarriesItsOwnMember) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange the system collection the host carries, because a collection member is what the index selects (§15)
    FcFontSet* fonts = FcConfigGetFonts(FcConfigGetCurrent(), FcSetSystem);
    // a host with no system set has no collection to compare (§15)
    if (fonts == nullptr)
        GTEST_SKIP() << "no system font set on this host";
    // the first collection pattern of the set is what the walk below searches for (§15)
    FcPattern* first = nullptr;
    // the file the first pattern names is what a second member must share (§15)
    const FcChar8* collection = nullptr;
    for (int index = 0; index < fonts->nfont && first == nullptr; index++) {
        // the file of a pattern is what decides whether it is a collection (§15)
        FcChar8* file = nullptr;
        // a pattern with no file is skipped, because it names no collection (§15)
        if (FcPatternGetString(fonts->fonts[index], FC_FILE, 0, &file) != FcResultMatch || file == nullptr)
            continue;
        // a file that ends in ttc is a collection, which is what the index selects from (§15)
        const std::string path = reinterpret_cast<const char*>(file);
        if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".ttc") == 0) {
            collection = file;
            first = fonts->fonts[index];
        }
    }
    // a host with no collection font has no pair to compare (§15)
    if (first == nullptr)
        GTEST_SKIP() << "no collection font on this host";
    // the member of the collection is the low half of the packed index, which is what the rebuild reads (§15)
    int first_index = 0;
    FcPatternGetInteger(first, FC_INDEX, 0, &first_index);
    // the first member is what the second must differ from, and the instance bits are not a member (§15)
    const int first_member = first_index & 0xFFFF;
    // the second member is a pattern of the same file with a different member index (§15)
    FcPattern* second = nullptr;
    // the member the second pattern names is what the packed case below reuses (§15)
    int second_member = 0;
    for (int index = 0; index < fonts->nfont && second == nullptr; index++) {
        // the file of a pattern is what decides whether it belongs to the same collection (§15)
        FcChar8* file = nullptr;
        // a pattern with no file is skipped, because it names no collection (§15)
        if (FcPatternGetString(fonts->fonts[index], FC_FILE, 0, &file) != FcResultMatch || file == nullptr)
            continue;
        // a pattern of another file is skipped, because it is a different collection (§15)
        if (std::string(reinterpret_cast<const char*>(file)) != reinterpret_cast<const char*>(collection))
            continue;
        // the index of this pattern is what decides whether it is another member (§15)
        int member_index = 0;
        // a pattern with no index names the same member as the first (§15)
        if (FcPatternGetInteger(fonts->fonts[index], FC_INDEX, 0, &member_index) != FcResultMatch)
            member_index = 0;
        // a different member is another face of the collection, while another instance is the same (§15)
        if ((member_index & 0xFFFF) != first_member) {
            second = fonts->fonts[index];
            second_member = member_index & 0xFFFF;
        }
    }
    // a host whose collection carries a single face has no pair to compare (§15)
    if (second == nullptr)
        GTEST_SKIP() << "no multi member collection on this host";
    // act the two members are handed over through the patterns fontconfig built (§15)
    const std::vector<std::uint8_t> first_bytes = harfbuzz::platform_font_bytes(first);
    const std::vector<std::uint8_t> second_bytes = harfbuzz::platform_font_bytes(second);
    // assert both members carried bytes, because the walk refused nothing of a real collection (§15)
    EXPECT_FALSE(first_bytes.empty());
    EXPECT_FALSE(second_bytes.empty());
    // assert both members are single face sfnts, which a member of a collection never names a set (§15)
    if (!first_bytes.empty() && !second_bytes.empty()) {
        // the version tag reads big endian, which is what every sfnt field reads as (§15)
        const std::uint32_t first_version = (static_cast<std::uint32_t>(first_bytes[0]) << 24) | (static_cast<std::uint32_t>(first_bytes[1]) << 16) | (static_cast<std::uint32_t>(first_bytes[2]) << 8) | static_cast<std::uint32_t>(first_bytes[3]);
        // the second version reads the same way, because the same contract applies to it (§15)
        const std::uint32_t second_version = (static_cast<std::uint32_t>(second_bytes[0]) << 24) | (static_cast<std::uint32_t>(second_bytes[1]) << 16) | (static_cast<std::uint32_t>(second_bytes[2]) << 8) | static_cast<std::uint32_t>(second_bytes[3]);
        EXPECT_NE(first_version, 0x74746366u);
        EXPECT_NE(second_version, 0x74746366u);
        // assert the two members differ, because each face read its own member and not the first one (§15, AD-34)
        EXPECT_NE(first_bytes, second_bytes);
    }
    // arrange the second member with an instance number in the high half, which fontconfig packs into the index (§15)
    FcPattern* packed = FcPatternCreate();
    FcPatternAddString(packed, FC_FILE, collection);
    FcPatternAddInteger(packed, FC_INDEX, (1 << 16) | second_member);
    // act the packed index must still read the second member, because the instance bits are masked off (§15)
    const std::vector<std::uint8_t> packed_bytes = harfbuzz::platform_font_bytes(packed);
    // the pattern goes back once its bytes are copied (§15)
    FcPatternDestroy(packed);
    // assert the packed index reads the second member and not the first one (§15, AD-34)
    EXPECT_EQ(packed_bytes, second_bytes);
    EXPECT_NE(packed_bytes, first_bytes);
#else
    // act assert a build without fontconfig hands over nothing, so the collection path is a refusal (§9.1, AD-34)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
#endif
}

TEST(LinuxPlatformFont, APlatformEngineShapesTheSystemFace) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a private configuration that carries the test fonts and aliases the system ui face to one of them (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-engine.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir><alias><family>sans-serif</family><prefer><family>Inter</family></prefer></alias></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the match below needs the family list (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act the platform engine is built over the system face, which is the transitional path (§9.1, AD-28)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_platform("");
    // assert the engine is usable with a registered identity, and the asserts stay non fatal for the cleanup (§15)
    EXPECT_NE(engine, nullptr);
    if (engine != nullptr) {
        EXPECT_TRUE(engine->ready());
        EXPECT_NE(engine->font_hash(), 0u);
    }
    // exit the registry is emptied, so the next test starts from nothing (§2.2)
    resources::FontRegistry::clear();
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig builds no platform engine, so embedded text is the only path (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTextEngine::create_platform(""), nullptr);
#endif
}

TEST(LinuxPlatformFont, AnEmbeddedEngineIgnoresFontconfig) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange a configuration that would answer a platform face, so an embedded run must still not reach it (§9.1)
    const std::filesystem::path directory = std::filesystem::path(ALBOR_TEST_FONT_PATH).parent_path();
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-embedded.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig><dir>" << directory.string() << "</dir></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because the hostile configuration must carry a face (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // arrange the carried font is registered, which is the embedded path and never touches fontconfig (§9.1, AD-34)
    std::ifstream font_file(ALBOR_TEST_FONT_PATH, std::ios::binary);
    const std::vector<std::uint8_t> font_bytes((std::istreambuf_iterator<char>(font_file)), std::istreambuf_iterator<char>());
    // the carried bytes are the identity the embedded engine must shape with (§15, AD-34)
    const std::uint64_t carried_hash = resources::hash_font_bytes(font_bytes.data(), font_bytes.size());
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = ALBOR_TEST_FONT_FAMILY, .weight = FontWeight::Regular, .content_hash = carried_hash, .bytes = font_bytes});
    // act the embedded engine is built from the registered bytes, which the hostile configuration must not answer (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    // assert the engine shaped the carried bytes and not the platform face the configuration would answer (§15, AD-34)
    EXPECT_NE(engine, nullptr);
    if (engine != nullptr) {
        // the engine is usable only when it loaded the carried face (§9.1)
        EXPECT_TRUE(engine->ready());
        // the identity is the carried hash, which proves fontconfig did not answer the run (§15)
        EXPECT_EQ(engine->font_hash(), carried_hash);
    }
    // act the platform engine resolves the named Lato face, which the embedded engine must not have used (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> platform = harfbuzz::HarfbuzzTextEngine::create_platform("Lato");
    // assert the identities differ, because one read the carried bytes and the other the platform face (§15, AD-34)
    EXPECT_NE(platform, nullptr);
    if (platform != nullptr && engine != nullptr) {
        // the identities differ, because the embedded engine read the carried bytes (§15, AD-34)
        EXPECT_NE(platform->font_hash(), engine->font_hash());
    }
    // exit the registry is emptied, so the next test starts from nothing (§2.2)
    resources::FontRegistry::clear();
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // arrange the carried font is registered, which is the embedded path and never touches fontconfig (§9.1, AD-34)
    std::ifstream font_file(ALBOR_TEST_FONT_PATH, std::ios::binary);
    const std::vector<std::uint8_t> font_bytes((std::istreambuf_iterator<char>(font_file)), std::istreambuf_iterator<char>());
    resources::FontRegistry::register_font(resources::EmbeddedFont{.family = ALBOR_TEST_FONT_FAMILY, .weight = FontWeight::Regular, .content_hash = resources::hash_font_bytes(font_bytes.data(), font_bytes.size()), .bytes = font_bytes});
    // act assert the embedded engine is built from the registered bytes, which needs no platform face (§9.1, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(ALBOR_TEST_FONT_FAMILY);
    EXPECT_NE(engine, nullptr);
    if (engine != nullptr) {
        // the engine is usable only when it loaded the carried face (§9.1)
        EXPECT_TRUE(engine->ready());
    }
    // exit the registry is emptied, so the next test starts from nothing (§2.2)
    resources::FontRegistry::clear();
#endif
}

TEST(LinuxPlatformFont, ANoFontsConfiguredResolvesNothing) {
#if defined(ALBOR_HAVE_FONTCONFIG)
    // arrange an empty configuration, which carries no directory and therefore no face (§9.1)
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "albor-fontconfig-empty.conf";
    {
        // the file is written in one pass, because fontconfig reads it whole (§9.1)
        std::ofstream config_file(config_path);
        config_file << "<fontconfig></fontconfig>\n";
    }
    // the replaced configuration is restored after the test, so one suite cannot leak into the next (§2.2)
    FcConfig* previous = FcConfigGetCurrent();
    // the host configuration is borrowed, so a reference keeps it alive while the test replaces it (§2.2)
    FcConfigReference(previous);
    // the private configuration starts empty, because the file below is its only input (§9.1)
    FcConfig* config = FcConfigCreate();
    // the configuration is parsed from the file the test wrote, which is the only input it reads (§9.1)
    FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>(config_path.string().c_str()), FcTrue);
    // the fonts are built once, because an empty configuration must carry none (§9.1)
    FcConfigBuildFonts(config);
    // the private configuration is current for the test's whole scope (§2.2)
    FcConfigSetCurrent(config);
    // act assert the platform engine cannot be built, because no face carries bytes (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTextEngine::create_platform(""), nullptr);
    // act assert a named family is refused for the same reason, because no face carries bytes (§9.1)
    EXPECT_EQ(harfbuzz::HarfbuzzTextEngine::create_platform("Inter"), nullptr);
    // exit the replaced configuration goes back, so the rest of the suite sees the host again (§2.2)
    FcConfigSetCurrent(previous);
    // the private configuration is released, because the create above owns the one the set took (§2.2)
    FcConfigDestroy(config);
    // the host reference is released, because the global now holds the one the set above took (§2.2)
    FcConfigDestroy(previous);
    std::filesystem::remove(config_path);
#else
    // act assert a build without fontconfig builds no platform engine, so embedded text is the only path (§9.1, AD-34)
    EXPECT_EQ(harfbuzz::HarfbuzzTextEngine::create_platform(""), nullptr);
#endif
}
