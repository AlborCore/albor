// the macos half of the engine's platform surfaces: the core text resolver and the sfnt it hands over (§9.1, AD-28)

#import <CoreText/CoreText.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "albor/resources/embedded_font.h"
#include "albor/text/text-harfbuzz/harfbuzz_platform_font.h"
#include "albor/text/text-harfbuzz/harfbuzz_text_engine.h"
#include "albor/text/text-harfbuzz/harfbuzz_typeface_resolver.h"

using namespace albor;

TEST(MacosPlatformFont, AnEmptyFamilyIsRefusedByTheFactory) {
    // act assert an empty family names no font, so the resolver is refused rather than built empty (§9.1)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create(std::string()), nullptr);
}

TEST(MacosPlatformFont, AnUnknownFamilyIsRefusedRatherThanSubstituted) {
    // arrange a family no system ships, which core text would otherwise substitute a face for (§15, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Albor Missing Family 0xA");
    // assert the resolver came up but carries no face, because a fallback is refused under the wrong family (§15)
    ASSERT_NE(resolver, nullptr);
    EXPECT_EQ(resolver->typeface(), nullptr);
    // act a bold request names the same absent family, which the weighted descriptor path resolves (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> bold = harfbuzz::HarfbuzzTypefaceResolver::create("Albor Missing Family 0xA", FontWeight::Bold);
    // assert the substituted family is refused at a weight too, because identity does not depend on it (§15)
    ASSERT_NE(bold, nullptr);
    EXPECT_EQ(bold->typeface(), nullptr);
}

TEST(MacosPlatformFont, AFamilyTheSystemShipsResolves) {
    // arrange a family every macos desktop ships, which the system collection answers (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Helvetica");
    // assert the face resolved, because the family the system offers is the one the run named (§9.1, AD-28)
    ASSERT_NE(resolver, nullptr);
    EXPECT_NE(resolver->typeface(), nullptr);
}

TEST(MacosPlatformFont, AWeightSelectsTheBoldFaceOfItsFamily) {
    // arrange the upright family every macos desktop ships, which carries both faces the test asks for (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> regular = harfbuzz::HarfbuzzTypefaceResolver::create("Helvetica", FontWeight::Regular);
    // act the bold request names the same family at the heavier weight (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> bold = harfbuzz::HarfbuzzTypefaceResolver::create("Helvetica", FontWeight::Bold);
    // assert both requests resolved, because the family carries both members (§9.1, AD-28)
    ASSERT_NE(regular, nullptr);
    ASSERT_NE(bold, nullptr);
    EXPECT_NE(regular->typeface(), nullptr);
    EXPECT_NE(bold->typeface(), nullptr);
    // act the bytes each face hands over are what the engine shapes with (§15, AD-34)
    const std::vector<std::uint8_t> regular_bytes = harfbuzz::platform_font_bytes(regular->typeface());
    const std::vector<std::uint8_t> bold_bytes = harfbuzz::platform_font_bytes(bold->typeface());
    // assert the two requests selected different faces, because the weight reached core text (§9.1)
    EXPECT_FALSE(regular_bytes.empty());
    EXPECT_FALSE(bold_bytes.empty());
    EXPECT_NE(regular_bytes, bold_bytes);
    // act a request that names no weight is the regular face, which the default argument preserves (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> named = harfbuzz::HarfbuzzTypefaceResolver::create("Helvetica");
    ASSERT_NE(named, nullptr);
    // assert the default request stays on the regular face, because a weight is opt in (§9.1)
    EXPECT_EQ(harfbuzz::platform_font_bytes(named->typeface()), regular_bytes);
    // act the default face carries the weight too, which the system ui font answers (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> default_regular = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> default_bold = harfbuzz::HarfbuzzTypefaceResolver::create_default(FontWeight::Bold);
    ASSERT_NE(default_regular, nullptr);
    ASSERT_NE(default_bold, nullptr);
    // act the ui face is a variable font, whose instances share one set of tables (§9.1)
    const CTFontRef default_regular_font = static_cast<CTFontRef>(const_cast<void*>(default_regular->typeface()));
    const CTFontRef default_bold_font = static_cast<CTFontRef>(const_cast<void*>(default_bold->typeface()));
    // the instance name is what distinguishes the two, which the shared tables cannot (§9.1)
    CFStringRef regular_name = default_regular_font != nullptr ? CTFontCopyPostScriptName(default_regular_font) : nullptr;
    CFStringRef bold_name = default_bold_font != nullptr ? CTFontCopyPostScriptName(default_bold_font) : nullptr;
    // assert the ui instance changes with the weight, because the shared tables cannot prove it (§9.1)
    EXPECT_NE(regular_name, nullptr);
    EXPECT_NE(bold_name, nullptr);
    if (regular_name != nullptr && bold_name != nullptr) {
        EXPECT_NE(CFStringCompare(regular_name, bold_name, 0), kCFCompareEqualTo);
    }
    // assert the selected instance carries the bold trait, which a name change alone would not prove (§9.1)
    if (default_bold_font != nullptr) {
        EXPECT_TRUE((CTFontGetSymbolicTraits(default_bold_font) & kCTFontTraitBold) != 0);
    }
    // the copied names go back, because they were made above for this assertion (§9.1)
    if (regular_name != nullptr)
        CFRelease(regular_name);
    if (bold_name != nullptr)
        CFRelease(bold_name);
    // act the engine built at each weight resolves the face the factory handed over (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine_regular = harfbuzz::HarfbuzzTextEngine::create_platform("Helvetica", FontWeight::Regular);
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine_bold = harfbuzz::HarfbuzzTextEngine::create_platform("Helvetica", FontWeight::Bold);
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
}

TEST(MacosPlatformFont, TheSystemFaceResolvesThroughADefaultCreate) {
    // act the empty family asks the system for its ui face, which the core text ui face names (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    // assert the face resolved, because a run that names none still draws (§9.1, AD-28)
    ASSERT_NE(resolver, nullptr);
    EXPECT_NE(resolver->typeface(), nullptr);
}

TEST(MacosPlatformFont, APlatformFaceHandsOverAReadableSfnt) {
    // arrange the default face, which the resolver issued (§9.1, AD-28)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    ASSERT_NE(resolver, nullptr);
    // act
    const std::vector<std::uint8_t> bytes = harfbuzz::platform_font_bytes(resolver->typeface());
    // assert the sfnt is there and readable, because a face the registry keys on starts at the version tag (§15)
    ASSERT_FALSE(bytes.empty());
    // the version tag reads big endian, which is what every sfnt field reads as (§15)
    const std::uint32_t version = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
    // the tags a rebuilt face opens with are the truetype and the opentype one, never a collection (§15)
    EXPECT_TRUE(version == 0x00010000u || version == 0x4f54544fu);
    // act a second ask is the reproducibility of the identity, which is total (§15, AD-34)
    const std::vector<std::uint8_t> again = harfbuzz::platform_font_bytes(resolver->typeface());
    // assert the two asks agree, because the identity is the bytes and not the moment (§15)
    EXPECT_EQ(bytes, again);
}

TEST(MacosPlatformFont, ANullFaceCarriesNoBytes) {
    // act assert a handle the platform did not resolve carries nothing, which is a refusal (§15)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
}

TEST(MacosPlatformFont, ACollectionFaceCarriesItsOwnMember) {
    // arrange the collection file a macos desktop carries, whose members the descriptor index selects (§15)
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, CFSTR("/System/Library/Fonts/Supplemental/Athelas.ttc"), kCFURLPOSIXPathStyle, false);
    if (url == nullptr)
        GTEST_SKIP() << "no collection font path";
    // act
    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL(url);
    CFRelease(url);
    // assert a collection with fewer than two members has no pair to compare (§15)
    if (descriptors == nullptr || CFArrayGetCount(descriptors) < 2) {
        if (descriptors != nullptr)
            CFRelease(descriptors);
        GTEST_SKIP() << "no multi member collection on this host";
    }
    // arrange the two faces the collection names, which the platform resolver hands over (§15, AD-34)
    CTFontRef first_font = CTFontCreateWithFontDescriptor(static_cast<CTFontDescriptorRef>(const_cast<void*>(CFArrayGetValueAtIndex(descriptors, 0))), 12.0, nullptr);
    CTFontRef second_font = CTFontCreateWithFontDescriptor(static_cast<CTFontDescriptorRef>(const_cast<void*>(CFArrayGetValueAtIndex(descriptors, 1))), 12.0, nullptr);
    // act
    const std::vector<std::uint8_t> first_bytes = harfbuzz::platform_font_bytes(first_font);
    const std::vector<std::uint8_t> second_bytes = harfbuzz::platform_font_bytes(second_font);
    // assert both members carried bytes, because the walk refused nothing of a real collection (§15)
    EXPECT_FALSE(first_bytes.empty());
    EXPECT_FALSE(second_bytes.empty());
    // assert both members are single face sfnts, which a member of a collection never names a set (§15)
    if (!first_bytes.empty() && !second_bytes.empty()) {
        // the version tag reads big endian, which is what every sfnt field reads as (§15)
        const std::uint32_t first_version = (static_cast<std::uint32_t>(first_bytes[0]) << 24) | (static_cast<std::uint32_t>(first_bytes[1]) << 16) | (static_cast<std::uint32_t>(first_bytes[2]) << 8) | static_cast<std::uint32_t>(first_bytes[3]);
        const std::uint32_t second_version = (static_cast<std::uint32_t>(second_bytes[0]) << 24) | (static_cast<std::uint32_t>(second_bytes[1]) << 16) | (static_cast<std::uint32_t>(second_bytes[2]) << 8) | static_cast<std::uint32_t>(second_bytes[3]);
        EXPECT_NE(first_version, 0x74746366u);
        EXPECT_NE(second_version, 0x74746366u);
        // assert the two members differ, because each face read its own member and not the first one (§15, AD-34)
        EXPECT_NE(first_bytes, second_bytes);
    }
    // exit the platform objects go back, because the test owns them for exactly its own life (§15)
    if (first_font != nullptr)
        CFRelease(first_font);
    if (second_font != nullptr)
        CFRelease(second_font);
    CFRelease(descriptors);
}

TEST(MacosPlatformFont, APlatformEngineShapesTheSystemFace) {
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
}
