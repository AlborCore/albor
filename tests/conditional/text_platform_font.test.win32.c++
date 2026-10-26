// the win32 half of the engine's platform surfaces: the directwrite resolver and the sfnt it hands over (§9.1, AD-28)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dwrite.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// the sdk uuid expansion is the language extension token clang flags, so the diagnostic is silenced
#pragma clang diagnostic ignored "-Wlanguage-extension-token"

#include "resources/embedded_font.h"
#include "text/text-harfbuzz/harfbuzz_platform_font.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text-harfbuzz/harfbuzz_typeface_resolver.h"

using namespace albor;

TEST(Win32PlatformFont, AnEmptyFamilyIsRefusedByTheFactory) {
    // act assert an empty family names no font, so the resolver is refused rather than built empty (§9.1)
    EXPECT_EQ(harfbuzz::HarfbuzzTypefaceResolver::create(std::string()), nullptr);
}

TEST(Win32PlatformFont, AnUnknownFamilyIsRefusedRatherThanSubstituted) {
    // arrange a family no system ships, which directwrite would otherwise substitute a face for (§15, AD-34)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Albor Missing Family 0xA");
    // assert the resolver came up but carries no face, because a fallback is refused under the wrong family (§15)
    ASSERT_NE(resolver, nullptr);
    EXPECT_EQ(resolver->typeface(), nullptr);
}

TEST(Win32PlatformFont, AFamilyTheSystemShipsResolves) {
    // arrange a family every windows desktop ships, which the system collection answers (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create("Segoe UI");
    // assert the face resolved, because the family the system offers is the one the run named (§9.1, AD-28)
    ASSERT_NE(resolver, nullptr);
    EXPECT_NE(resolver->typeface(), nullptr);
}

TEST(Win32PlatformFont, TheSystemFaceResolvesThroughADefaultCreate) {
    // act the empty family asks the system for its ui face, which the message font names (§9.1)
    const std::unique_ptr<harfbuzz::HarfbuzzTypefaceResolver> resolver = harfbuzz::HarfbuzzTypefaceResolver::create_default();
    // assert the face resolved, because a run that names none still draws (§9.1, AD-28)
    ASSERT_NE(resolver, nullptr);
    EXPECT_NE(resolver->typeface(), nullptr);
}

TEST(Win32PlatformFont, APlatformFaceHandsOverAReadableSfnt) {
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

TEST(Win32PlatformFont, ANullFaceCarriesNoBytes) {
    // act assert a handle the platform did not resolve carries nothing, which is a refusal (§15)
    EXPECT_TRUE(harfbuzz::platform_font_bytes(nullptr).empty());
}

TEST(Win32PlatformFont, APlatformEngineShapesTheSystemFace) {
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

TEST(Win32PlatformFont, ACollectionFaceCarriesItsOwnMember) {
    // arrange the factory the collection face is created over, which every windows host carries (§15)
    IDWriteFactory* factory = nullptr;
    ASSERT_TRUE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)))) << "no directwrite factory on this host";
    ASSERT_NE(factory, nullptr);
    // arrange the collection file a windows desktop carries, whose members the index selects (§15)
    IDWriteFontFile* file = nullptr;
    if (factory->CreateFontFileReference(L"C:\\Windows\\Fonts\\cambria.ttc", nullptr, &file) != S_OK || file == nullptr) {
        factory->Release();
        FAIL() << "no collection font on this host";
    }
    // arrange the two members the collection carries, which the face factory creates by index (§15)
    IDWriteFontFace* first = nullptr;
    IDWriteFontFace* second = nullptr;
    if (factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &first) != S_OK || first == nullptr || factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION, 1, &file, 1, DWRITE_FONT_SIMULATIONS_NONE, &second) != S_OK || second == nullptr) {
        // the faces that came up are released, because the failure leaves nothing behind (§15)
        if (first != nullptr)
            first->Release();
        file->Release();
        factory->Release();
        FAIL() << "no multi member collection on this host";
    }
    // act
    const std::vector<std::uint8_t> first_bytes = harfbuzz::platform_font_bytes(first);
    const std::vector<std::uint8_t> second_bytes = harfbuzz::platform_font_bytes(second);
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
    // exit the com objects go back, because the test owns them for exactly its own life (§15)
    first->Release();
    second->Release();
    file->Release();
    factory->Release();
}
