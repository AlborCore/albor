#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "compiler/diagnostic.h"
#include "compiler/type_checker.h"
#include "compiler/ui_loader.h"
#include "compiler/value_type.h"

using namespace albor::compiler;

TEST(UiLoader, LoadsAnImportedComponentRelativeToTheImportingFile) {
    // arrange a root in one directory and a panel in a sibling directory, which the import names relatively (§11.1)
    std::map<std::string, std::string> files;
    files["views/root.ui"] = "import \"../panels/side_panel.ui\"; component Root { Column { SidePanel { title: \"x\"; } } }";
    files["panels/side_panel.ui"] = "component SidePanel { in property<string> title; Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is loaded with its import (§11.1)
    const std::optional<CheckedFile> checked = loader.check("views/root.ui");
    // assert the imported interface resolves and the generated header is named after the component (§11.1)
    ASSERT_TRUE(checked.has_value());
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_EQ(checked->imported_components.size(), 1U);
    EXPECT_EQ(checked->imported_components.at(0).name, "SidePanel");
    EXPECT_EQ(checked->imported_components.at(0).header, "side_panel.h");
    ASSERT_EQ(checked->components.size(), 1U);
    ASSERT_NE(checked->components.at(0).root, nullptr);
    EXPECT_EQ(checked->components.at(0).root->children.at(0)->element_type, "SidePanel");
    ASSERT_EQ(loader.dependencies().size(), 2U);
    EXPECT_EQ(loader.dependencies().at(0).generic_string(), "views/root.ui");
    EXPECT_EQ(loader.dependencies().at(1).generic_string(), "panels/side_panel.ui");
}

TEST(UiLoader, AFailedImportedFileStillPublishesItsInterface) {
    // arrange a panel whose body has an error, which must not erase the component the window instantiates (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"panel.ui\"; component Root { Column { Panel {} } }";
    files["panel.ui"] = "component Panel { Column { Nope {} } }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked even though the panel body is broken (§11.1)
    const std::optional<CheckedFile> checked = loader.check("root.ui");
    // assert the panel interface survived, because the window references a component that exists (§11.1)
    ASSERT_TRUE(checked.has_value());
    ASSERT_EQ(checked->imported_components.size(), 1U);
    EXPECT_EQ(checked->imported_components.at(0).name, "Panel");
    // assert the error belongs to the panel and the window itself resolves cleanly (§11.1)
    std::size_t root_errors = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.path == "root.ui")
            root_errors++;
        if (diagnostic.path == "panel.ui")
            EXPECT_FALSE(diagnostic.message.empty());
    }
    EXPECT_EQ(root_errors, 0U);
    EXPECT_GT(diagnostics.count(), 0U);
}

TEST(UiLoader, ChecksATransitiveImportBeforeItsImporter) {
    // arrange a root that uses a panel, which itself uses a component from a third file (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"panel.ui\"; component Root { Column { Panel {} } }";
    files["panel.ui"] = "import \"inner.ui\"; component Panel { Column { Inner {} } }";
    files["inner.ui"] = "component Inner { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is loaded, which loads the panel and then its inner component (§11.1)
    const std::optional<CheckedFile> checked = loader.check("root.ui");
    // assert the root sees only its direct import while the transitive file is still a dependency (§11.1, §14.2)
    ASSERT_TRUE(checked.has_value());
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_EQ(checked->imported_components.size(), 1U);
    EXPECT_EQ(checked->imported_components.at(0).name, "Panel");
    ASSERT_EQ(loader.dependencies().size(), 3U);
    EXPECT_EQ(loader.dependencies().at(2).generic_string(), "inner.ui");
}

TEST(UiLoader, ReportsAnImportCycle) {
    // arrange two files that import each other, which no component graph may accept (§11.1)
    std::map<std::string, std::string> files;
    files["a.ui"] = "import \"b.ui\"; component A { Column { B {} } }";
    files["b.ui"] = "import \"a.ui\"; component B { Column { A {} } }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is loaded, which closes the cycle at the second file's import (§11.1)
    (void)loader.check("a.ui");
    // assert the cycle is reported once against the import that closes it (§11.1)
    std::size_t cycles = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0802")
            cycles++;
    EXPECT_EQ(cycles, 1U);
}

TEST(UiLoader, ReportsAMissingImportAgainstItsImporter) {
    // arrange a root whose import names no file the reader can find (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"missing.ui\"; component Root { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is loaded, which cannot read the file it imports (§11.1)
    (void)loader.check("root.ui");
    // assert the problem points at the importing file, where the missing path is written (§11.1)
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0801");
    EXPECT_EQ(diagnostics.diagnostics().at(0).path, "root.ui");
    EXPECT_EQ(diagnostics.diagnostics().at(0).span.start.line, 1U);
}

TEST(UiLoader, ReportsTwoImportsThatDeclareTheSameComponent) {
    // arrange two files with the same component name in different directories, which an element cannot distinguish (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"a/shared.ui\"; import \"b/shared.ui\"; component Root { Column { Shared {} } }";
    files["a/shared.ui"] = "component Shared { Column {} }";
    files["b/shared.ui"] = "component Shared { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is loaded, which sees the same declaration name twice (§11.1)
    (void)loader.check("root.ui");
    // assert the later import reports the ambiguity, because an element would name two files (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0803");
}

TEST(UiLoader, AnIdenticalImportIsIdempotent) {
    // arrange a root that imports the same panel twice, which is one interface and not an ambiguity (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"panel.ui\"; import \"panel.ui\"; component Root { Column { Panel {} } }";
    files["panel.ui"] = "component Panel { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked with the repeated import (§11.1)
    const std::optional<CheckedFile> checked = loader.check("root.ui");
    // assert the repeated line adds no second declaration and reports no false ambiguity (§11.1)
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_TRUE(checked.has_value());
    ASSERT_EQ(checked->imported_components.size(), 1U);
    EXPECT_EQ(checked->imported_components.at(0).name, "Panel");
    // assert the file was read once, because the cache key is the normalized path (§11.1, §14.2)
    EXPECT_EQ(loader.dependencies().size(), 2U);
}

TEST(UiLoader, ReusesOneLoaderAcrossRootsWithoutLeakingDeclarations) {
    // arrange two roots that each declare a struct named Item, which must not collide across runs (§11.1)
    std::map<std::string, std::string> files;
    files["first.ui"] = "struct Item { float width; } component First { Column {} }";
    files["second.ui"] = "struct Item { float width; } component Second { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the first root is checked and then the second root reuses the loader (§11.1)
    const std::optional<CheckedFile> first = loader.check("first.ui");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(diagnostics.count(), 0U);
    diagnostics.clear();
    const std::optional<CheckedFile> second = loader.check("second.ui");
    // assert the second run starts from the catalog it was built with, so Item is declared once per run (§11.1)
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(diagnostics.count(), 0U);
    // assert the first result is still readable, because its parse tree stays alive across runs (§11.1)
    ASSERT_FALSE(first->components.empty());
    ASSERT_NE(first->components.at(0).root, nullptr);
    EXPECT_EQ(first->components.at(0).root->element_type, "Column");
    // assert the dependency list belongs to the last run, because the depfile names one root (§14.2)
    EXPECT_EQ(loader.dependencies().size(), 1U);
    EXPECT_EQ(loader.dependencies().at(0).generic_string(), "second.ui");
}

TEST(UiLoader, ReportsAnImportWithNoPathOrModule) {
    // arrange an empty quoted import, which names nothing the loader can read (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"\"; component Root { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked with the empty import (§11.1)
    (void)loader.check("root.ui");
    // assert the empty path is reported rather than silently treated as a module import (§11.1)
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0801");
    EXPECT_EQ(diagnostics.diagnostics().at(0).path, "root.ui");
}

TEST(UiLoader, ReportsAnImportedFileThatDeclaresTwoComponents) {
    // arrange an imported file with two components, which the build cannot name with one generated pair (§11.1, §14.2)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"shared.ui\"; component Root { Column { First {} } }";
    files["shared.ui"] = "component First { Column {} } component Second { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is loaded, which rejects the imported file's shape (§11.1)
    (void)loader.check("root.ui");
    // assert the rule names the imported file, because that is what must be split (§11.1)
    std::size_t shape_errors = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0804")
            shape_errors++;
    EXPECT_EQ(shape_errors, 1U);
}

TEST(UiLoader, ChecksADiamondImportOnce) {
    // arrange a shared panel reached through both sides of a diamond, which must resolve once (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"left.ui\"; import \"right.ui\"; component Root { Column { Left {} Right {} } }";
    files["left.ui"] = "import \"shared.ui\"; component Left { Row { Shared {} } }";
    files["right.ui"] = "import \"shared.ui\"; component Right { Row { Shared {} } }";
    files["shared.ui"] = "component Shared { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked, which reaches the shared file through two importers (§11.1)
    const std::optional<CheckedFile> checked = loader.check("root.ui");
    // assert the graph is clean, because a shared declaration is not a duplicate (§11.1)
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_TRUE(checked.has_value());
    EXPECT_EQ(checked->imported_components.size(), 2U);
    // assert the shared file joined the run once, because its checked tree is cached (§11.1, §14.2)
    EXPECT_EQ(loader.dependencies().size(), 4U);
}

TEST(UiLoader, ReportsADuplicateStructAcrossImports) {
    // arrange two imported files that declare one struct name, which resolves to two shapes (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"a.ui\"; import \"b.ui\"; component Root { Column { A {} B {} } }";
    files["a.ui"] = "struct Item { float width; } component A { Column {} }";
    files["b.ui"] = "struct Item { float width; } component B { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked, which registers both declarations in one catalog (§11.1)
    (void)loader.check("root.ui");
    // assert the later file reports the ambiguity, because a property would take the first shape (§11.1)
    std::size_t duplicates = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0432")
            duplicates++;
    EXPECT_EQ(duplicates, 1U);
}

TEST(UiLoader, ReportsADuplicateEnumAcrossImports) {
    // arrange two imported files that declare one enum name, which resolves to two member sets (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"a.ui\"; import \"b.ui\"; component Root { Column { A {} B {} } }";
    files["a.ui"] = "enum Mode { On, Off } component A { Column {} }";
    files["b.ui"] = "enum Mode { Fast, Slow } component B { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked, which registers both member sets in one catalog (§11.1)
    (void)loader.check("root.ui");
    // assert the later file reports the ambiguity, because a property would take the first members (§11.1)
    std::size_t duplicates = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0433")
            duplicates++;
    EXPECT_EQ(duplicates, 1U);
}

TEST(UiLoader, ReportsContentOnAnImportedInstance) {
    // arrange a window that writes content under an imported panel, which the imported facade does not accept (§11.1)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"panel.ui\"; component Root { Column { Panel { Label {} } } }";
    files["panel.ui"] = "component Panel { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked, which refuses the content an imported instance cannot take (§11.1)
    (void)loader.check("root.ui");
    // assert the imported instance rule has its own code, so an editor can tell it from recursion (§11.1)
    std::size_t content_errors = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0434")
            content_errors++;
    EXPECT_EQ(content_errors, 1U);
}

TEST(UiLoader, ReportsAnImportedComponentNotNamedAfterItsFile) {
    // arrange a panel file whose component has another name, which the build output cannot match (§11.1, §14.2)
    std::map<std::string, std::string> files;
    files["root.ui"] = "import \"panel.ui\"; component Root { Column { Panel {} } }";
    files["panel.ui"] = "component Wrong { Column {} }";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const auto reader = [&files](const std::filesystem::path& path) -> std::optional<std::string> {
        const auto found = files.find(path.generic_string());
        return found == files.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
    UiImportLoader loader(catalog, diagnostics, reader);
    // act the root is checked, which cannot name an instance of the mismatched component (§11.1)
    (void)loader.check("root.ui");
    // assert the rule names the imported file, because its component must be renamed (§11.1)
    std::size_t shape_errors = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0805")
            shape_errors++;
    EXPECT_EQ(shape_errors, 1U);
}
