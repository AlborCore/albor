#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>

#include "albor_embedded_fonts.h"
#include "font_panel.h"
#include "layout/column_layout.h"
#include "resources/embedded_font.h"
#include "runtime/binding_engine.h"
#include "text/font_spec.h"
#include "text/text-harfbuzz/harfbuzz_text_engine.h"
#include "text/text_engine.h"
#include "widgets/label.h"

using namespace albor;

TEST(GeneratedFontChecks, RegistersTheDeclaredFacesBeforeMain) {
    // act the faces the generated unit declared are looked up by family and weight (§11.3, AD-34)
    const std::shared_ptr<const resources::EmbeddedFont> regular = resources::FontRegistry::find_face(albor::generated::DEFAULT_FAMILY, FontWeight::Regular);
    const std::shared_ptr<const resources::EmbeddedFont> bold = resources::FontRegistry::find_face(albor::generated::DEFAULT_FAMILY, FontWeight::Bold);
    // assert both faces exist before any test called the installer, so the load time registration ran (§11.3)
    ASSERT_NE(regular, nullptr);
    ASSERT_NE(bold, nullptr);
    EXPECT_EQ(regular->weight, FontWeight::Regular);
    EXPECT_EQ(bold->weight, FontWeight::Bold);
}

TEST(GeneratedFontChecks, TheInstallerIsIdempotent) {
    // arrange the faces the static initializer already registered, and one face's identity (§11.3)
    const std::size_t before = resources::FontRegistry::count();
    const std::shared_ptr<const resources::EmbeddedFont> face_before = resources::FontRegistry::find_face(albor::generated::DEFAULT_FAMILY, FontWeight::Bold);
    ASSERT_NE(face_before, nullptr);
    // act the public installer is called again, which a static library consumer may have to do (§11.3)
    albor::generated::install_embedded_fonts();
    // assert the second call keeps the same face object, because the guard answers every later call (§11.3)
    EXPECT_EQ(resources::FontRegistry::count(), before);
    EXPECT_EQ(resources::FontRegistry::find_face(albor::generated::DEFAULT_FAMILY, FontWeight::Bold).get(), face_before.get());
}

TEST(GeneratedFontChecks, TheGeneratedPanelBuildsOverTheDeclaredFamily) {
    // arrange the engine the generated body registers its properties with (§7.2)
    BindingEngine engine;
    // act the generated panel is constructed, which the compiler validated against the font asset list (§11.1, AD-34)
    FontPanel panel(&engine, nullptr, "");
    // the root column is what the generated body built, which holds the labelled row (§11.3)
    auto* column = dynamic_cast<ColumnLayout*>(panel.child_at(0));
    ASSERT_NE(column, nullptr);
    // the label is what the generated assignment configured with the font selection (§9.1, AD-34)
    auto* label = dynamic_cast<Label*>(column->child_at(0));
    ASSERT_NE(label, nullptr);
    // assert the panel built its root column and the generated default names the declared family (§11.3)
    EXPECT_EQ(panel.child_count(), 1u);
    EXPECT_EQ(albor::generated::DEFAULT_FAMILY, "Lato");
    // assert the generated setters reached the label, which is what the declaration promised (§9.1, AD-34)
    EXPECT_EQ(label->font_family(), "Lato");
    EXPECT_EQ(label->font_weight(), FontWeight::Bold);
}

TEST(GeneratedFontChecks, TheDefaultFamilyShapesAtTwoWeights) {
    // arrange the generated family and the engine built over its regular face (§9.1, AD-34)
    set_installed_font_family(std::string(albor::generated::DEFAULT_FAMILY));
    const std::unique_ptr<harfbuzz::HarfbuzzTextEngine> engine = harfbuzz::HarfbuzzTextEngine::create_embedded(albor::generated::DEFAULT_FAMILY);
    ASSERT_NE(engine, nullptr);
    // act the same text is shaped at the default weight and at the bold one (§9.1, AD-34)
    const TextLayout regular = engine->measure("Albor", 0.0f, 20.0f);
    const TextLayout bold = engine->measure("Albor", 0.0f, 20.0f, FontSpec{.family = std::string(albor::generated::DEFAULT_FAMILY), .weight = FontWeight::Bold});
    // assert the generated faces resolve to different fonts, so the weight reached the engine (§9.1, AD-34)
    ASSERT_NE(regular.run, nullptr);
    ASSERT_NE(bold.run, nullptr);
    EXPECT_NE(regular.run->font_hash(), bold.run->font_hash());
    EXPECT_NE(*regular.run, *bold.run);
    set_installed_font_family("");
}
