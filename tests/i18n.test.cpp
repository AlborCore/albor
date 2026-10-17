#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/translation_extractor.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"
#include "i18n/catalog.h"
#include "i18n/locale.h"

using namespace albor;

namespace
{
    // the translated strings a file declares, which is what a build extracts (§9.2)
    std::vector<std::string> translations_in(const std::string& source) {
        // the diagnostics accumulate, because extraction must not need a clean file (§9.2)
        compiler::DiagnosticList diagnostics;
        // the whole front end runs, because extraction walks the checked tree (§11.1, §9.2)
        const std::vector<compiler::Token> tokens = compiler::Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const compiler::SourceFile file = compiler::Parser(tokens, diagnostics).parse();
        // the checker resolves it, which is what carries the expressions (§11.1)
        const compiler::CheckedFile checked = compiler::TypeChecker(compiler::ElementCatalog::with_builtins(), diagnostics).check(file);
        // the strings are extracted from the checked tree (§9.2)
        return compiler::extract_translations(checked);
    }
} // namespace

TEST(TranslationExtraction, TheStringsAFileDeclaresAreExtracted) {
    // assert the macro's own argument is what lands in the catalogue, because that is the key (§9.2)
    const std::vector<std::string> found = translations_in(R"(component P { Column { Label { text: @tr("Hello, "); } } })");
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found.at(0), "Hello, ");
}

TEST(TranslationExtraction, AStringThatIsNotMarkedIsNotExtracted) {
    // assert an unmarked literal is not translatable, because the author did not ask for it (§9.2)
    EXPECT_TRUE(translations_in(R"(component P { Column { Label { text: "plain"; } } })").empty());
}

TEST(TranslationExtraction, ARepeatedStringIsOneEntry) {
    // assert the same source written twice is one catalogue entry (§9.2)
    const std::vector<std::string> found = translations_in(R"(component P { Column { Label { text: @tr("Save"); } Button { text: @tr("Save"); } } })");
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found.at(0), "Save");
}

TEST(TranslationExtraction, ASurroundingExpressionStillYieldsItsString) {
    // assert a translated string inside a concatenation is extracted, because it is still translatable (§9.2)
    const std::vector<std::string> found = translations_in(R"(component P { Column { Label { text: @tr("Count: ") + "1"; } } })");
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found.at(0), "Count: ");
}

TEST(TranslationExtraction, TheTemplateListsEverySourceWithAnEmptyTranslation) {
    // arrange
    const std::vector<std::string> sources = {"Hello, ", "Save"};
    // act
    const std::string text = compiler::write_catalog_template(sources);
    // assert each source appears as a key, which is what a translator files a translation under (§9.2)
    EXPECT_NE(text.find("msgid \"Hello, \""), std::string::npos);
    EXPECT_NE(text.find("msgid \"Save\""), std::string::npos);
    // assert every entry starts untranslated, which is the state a translator fills in (§9.2)
    EXPECT_NE(text.find("msgstr \"\""), std::string::npos);
}

TEST(TranslationExtraction, TheGeneratedTableCarriesOnlyTranslatedEntries) {
    // arrange one translated and one left empty, which is what a partial catalogue looks like (§9.2)
    const std::vector<std::string> sources = {"Hello, ", "Save"};
    // the entries are the translated pairs a translator produced (§9.2)
    const std::vector<std::pair<std::string, std::string>> entries = {{"Hello, ", "Bonjour, "}, {"Save", ""}};
    // act
    const std::string table = compiler::write_catalog_table(sources, entries);
    // assert the translated entry is written, because that is the whole point of the table (§9.2)
    EXPECT_NE(table.find("catalog.add(\"Hello, \", \"Bonjour, \")"), std::string::npos);
    // assert an untranslated entry is absent, because the runtime falls back to the source (§9.2)
    EXPECT_EQ(table.find("catalog.add(\"Save\""), std::string::npos);
    // assert the unit installs the catalogue, because that is what makes lookups resolve (§9.2)
    EXPECT_NE(table.find("set_current"), std::string::npos);
}

TEST(TranslationRuntime, AnUntranslatedStringReadsAsItself) {
    // arrange an empty catalogue, which is what an application with no translations has (§9.2)
    i18n::set_current(i18n::Catalog());
    // assert the source is shown rather than an empty label, because that is the honest fallback (§9.2)
    EXPECT_EQ(i18n::translate("Hello, "), "Hello, ");
    // assert it is reported as untranslated, so a build check can find it (§9.2)
    EXPECT_FALSE(i18n::translated("Hello, "));
}

TEST(TranslationRuntime, ATranslatedStringReadsAsItsTranslation) {
    // arrange a catalogue carrying one entry (§9.2)
    i18n::Catalog catalog;
    // the entry is added, which is what a generated table contributes (§9.2)
    catalog.add("Hello, ", "Bonjour, ");
    // act
    i18n::set_current(std::move(catalog));
    // assert the translation is what a reader sees (§9.2)
    EXPECT_EQ(i18n::translate("Hello, "), "Bonjour, ");
    EXPECT_TRUE(i18n::translated("Hello, "));
    // assert a string the catalogue does not carry still reads as itself (§9.2)
    EXPECT_EQ(i18n::translate("Save"), "Save");
}

TEST(TranslationRuntime, CoverageCountsWhatTheCatalogueIsMissing) {
    // arrange a catalogue carrying one of three strings (§9.2)
    i18n::Catalog catalog;
    // the one entry is added (§9.2)
    catalog.add("Hello, ", "Bonjour, ");
    // act
    i18n::set_current(std::move(catalog));
    // assert two are missing, which is what a build check reports (§9.2)
    EXPECT_EQ(i18n::missing_count({"Hello, ", "Save", "Quit"}), 2U);
}

TEST(TranslationRuntime, ChangingTheLocaleNotifiesItsListeners) {
    // arrange one listener, which stands in for the binding engine re-evaluating (§9.2, §7.3)
    static int calls = 0;
    // the count starts at zero (§9.2)
    calls = 0;
    // the listener is registered, which the runtime does once at startup (§9.2)
    i18n::on_locale_changed([] { calls++; });
    // act
    i18n::set_locale("fr");
    // assert the listener ran, which is what re-evaluates a translated binding (§9.2, §7.3)
    EXPECT_GE(calls, 1);
    EXPECT_EQ(i18n::locale(), "fr");
}

TEST(TranslationRuntime, AnUnchangedLocaleNotifiesNobody) {
    // arrange the locale and one listener (§9.2)
    i18n::set_locale("en");
    // the count starts fresh (§9.2)
    static int calls = 0;
    // the count is reset (§9.2)
    calls = 0;
    // act the same locale is set again, which is what an editor picking the current one does (§7.3)
    i18n::set_locale("en");
    // assert nothing re-evaluated, because nothing changed (§7.3)
    EXPECT_EQ(calls, 0);
}

TEST(TranslationRuntime, DirectionFollowsTheLocale) {
    // assert the languages that read right to left are named, because layout mirrors for them (§9.2)
    EXPECT_TRUE(i18n::right_to_left("ar"));
    EXPECT_TRUE(i18n::right_to_left("he-IL"));
    EXPECT_TRUE(i18n::right_to_left("fa"));
    // assert a language that reads left to right is not mirrored (§9.2)
    EXPECT_FALSE(i18n::right_to_left("en"));
    EXPECT_FALSE(i18n::right_to_left("de-DE"));
    // assert the current locale decides the direction, which is what layout reads (§9.2)
    i18n::set_locale("ar");
    EXPECT_EQ(i18n::direction(), i18n::ReadingDirection::RightToLeft);
    i18n::set_locale("en");
    EXPECT_EQ(i18n::direction(), i18n::ReadingDirection::LeftToRight);
}
