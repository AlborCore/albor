#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "compiler/ast.h"
#include "compiler/diagnostic.h"
#include "compiler/font_catalog.h"
#include "compiler/font_usage.h"
#include "compiler/type_checker.h"

using namespace albor;
using namespace albor::compiler;

namespace
{
    // wrap a literal as the shared expression an assignment carries (§11.1)
    SharedExpression string_literal(std::string text) {
        // the literal is built by its own factory, which the parser also uses (§11.1)
        ExpressionPtr expression = LiteralExpression::make(LiteralExpression::Kind::String, std::move(text), SourceSpan{});
        // the shared view takes ownership, which is what a checked assignment holds (§11.1)
        return SharedExpression(std::move(expression));
    }

    // wrap a Weight member as the shared expression a weight assignment carries (§9.1)
    SharedExpression weight_literal(std::string member) {
        // the object names the builtin Weight set, which the field access resolves against (§9.1)
        ExpressionPtr object = PropertyExpression::make("Weight", SourceSpan{});
        // the field is the written member, which names the weight (§9.1)
        ExpressionPtr expression = FieldAccessExpression::make(std::move(object), std::move(member), SourceSpan{});
        // the shared view takes ownership, which is what a checked assignment holds (§11.1)
        return SharedExpression(std::move(expression));
    }

    // wrap a fallback list as the shared expression a fallback assignment carries (§9.1)
    SharedExpression fallback_literal(std::vector<std::string> families) {
        // the elements are string literals in written order (§9.1)
        std::vector<ExpressionPtr> elements;
        // every family joins the list, because the array factory takes owning expressions (§11.1)
        for (std::string& family : families)
            elements.push_back(LiteralExpression::make(LiteralExpression::Kind::String, std::move(family), SourceSpan{}));
        // the array factory builds the bracketed list (§11.1)
        ExpressionPtr expression = ArrayExpression::make(std::move(elements), SourceSpan{});
        // the shared view takes ownership, which is what a checked assignment holds (§11.1)
        return SharedExpression(std::move(expression));
    }

    // build a node with one assignment, which is the smallest selection the checker reads (§11.1)
    CheckedNode node_with(std::string name, SharedExpression value) {
        // the node is a label, because that is the element the font properties belong to (§9.1)
        CheckedNode node;
        // the element type is what gates the font checks, so the helper names the real element (§9.1)
        node.element_type = "Label";
        // the assignment carries the name and the value the checker inspects (§11.1)
        node.assignments.push_back(CheckedAssignment{.name = std::move(name), .value = std::move(value)});
        // the node is what a component root or a child holds (§11.1)
        return node;
    }

    // build a file whose one component has the given root, which is what the checker walks (§11.1)
    CheckedFile file_with(CheckedNode root) {
        // the file starts empty, because only one component is under test (§11.1)
        CheckedFile file;
        // the component carries the root the checker walks (§11.1)
        CheckedComponent component;
        // the root is shared, because a checked tree is handed around by shared ownership (§11.1)
        component.root = std::make_shared<CheckedNode>(std::move(root));
        // the component joins the file, which is what the checker iterates (§11.1)
        file.components.push_back(std::move(component));
        // the file is what the checker receives (§11.1)
        return file;
    }
} // namespace

TEST(FontCatalogChecks, CarriesAFamilyAndAnExactWeight) {
    // arrange a catalog with one family at two weights (§9.1)
    FontCatalog catalog;
    // the default family is the family the build names (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is what a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold face is what a weighted selection resolves (§9.1)
    catalog.add("Lato", FontWeight::Bold);
    // act each lookup is asked for, including a family and weight the catalog lacks (§9.1)
    const bool family = catalog.carries("Lato");
    const bool regular = catalog.carries("Lato", FontWeight::Regular);
    const bool bold = catalog.carries("Lato", FontWeight::Bold);
    const bool black = catalog.carries("Lato", FontWeight::Black);
    const bool absent = catalog.carries("Missing");
    // assert the family and exact weights answer, and the missing ones are refused (§9.1)
    EXPECT_TRUE(family);
    EXPECT_TRUE(regular);
    EXPECT_TRUE(bold);
    EXPECT_FALSE(black);
    EXPECT_FALSE(absent);
    EXPECT_EQ(catalog.default_family(), "Lato");
    EXPECT_EQ(catalog.count(), 2u);
}

TEST(FontUsageChecks, AnEmptyCatalogSkipsValidation) {
    // arrange a selection naming a family no build carries, with an empty catalog (§9.1)
    const FontCatalog catalog;
    // the node names a family the catalog cannot resolve (§9.1)
    const CheckedFile file = file_with(node_with("font_family", string_literal("Missing")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert nothing is reported, because a build without fonts keeps the platform path (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, ACarriedFamilyAndWeightProduceNoProblem) {
    // arrange a catalog with the family at the selected weight (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the default a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold face is what the node selects (§9.1)
    catalog.add("Lato", FontWeight::Bold);
    // the node names the family and the weight, which the catalog carries (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the weight assignment joins the family, because the two decide one face (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("bold")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the exact pairing is carried (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AnUncarriedFamilyIsRefused) {
    // arrange a catalog with a default family, and a node naming another family (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the default a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names a family the catalog does not carry (§9.1)
    const CheckedFile file = file_with(node_with("font_family", string_literal("Missing")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the uncarried family is a compile error (§11.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0510");
}

TEST(FontUsageChecks, AWeightTheFamilyLacksIsRefused) {
    // arrange a catalog whose family carries only regular, and a node selecting bold (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the family carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the family and asks for a weight it lacks (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the weight assignment selects the missing face (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("bold")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert the missing weight is a compile error (§11.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0511");
}

TEST(FontUsageChecks, AFamilyWithoutARegularFaceIsRefusedWhenNoWeightIsWritten) {
    // arrange a valid default family and a bold-only family, and a node naming the bold-only one (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is what a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold face is the only face the second family carries (§9.1)
    catalog.add("AlborBoldOnly", FontWeight::Bold);
    // the node names the bold-only family, which asks for regular by default (§9.1)
    const CheckedFile file = file_with(node_with("font_family", string_literal("AlborBoldOnly")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the missing regular face is a compile error (§11.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0511");
}

TEST(FontUsageChecks, AWeightWithoutAFamilyChecksTheDefaultFamily) {
    // arrange a catalog whose default family carries only regular, and a node selecting bold (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the default family carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node selects a weight without naming a family (§9.1)
    const CheckedFile file = file_with(node_with("font_weight", weight_literal("bold")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the default family's missing weight is a compile error (§11.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0511");
}

TEST(FontUsageChecks, AWeightWithoutAFamilyChecksAnUncarriedDefault) {
    // arrange a catalog whose default family is not among its faces, and a node selecting bold (§9.1)
    FontCatalog catalog;
    // the default family names a family nothing carries (§9.1)
    catalog.set_default_family("Missing");
    // the regular face belongs to another family (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node selects a weight without naming a family (§9.1)
    const CheckedFile file = file_with(node_with("font_weight", weight_literal("bold")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert both the global and the node report the missing default family (§11.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0512");
}

TEST(FontUsageChecks, AFallbackFamilyTheTargetLacksWarns) {
    // arrange a catalog with one family, and a node whose fallback names another (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the carried family and a fallback the build lacks (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the fallback names a family the catalog does not carry (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Missing"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert the missing fallback is a warning rather than a blocking error (§11.3)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "W0513");
    EXPECT_EQ(diagnostics.diagnostics().front().severity, Diagnostic::Severity::Warning);
}

TEST(FontUsageChecks, AFallbackRescuesAnAbsentFamily) {
    // arrange a catalog whose default family is carried, and a node naming an absent family with that fallback (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names an absent family and the carried fallback (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Missing"));
    // the fallback names the carried family, which rescues the selection (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Lato"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the fallback supplies the face (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AFallbackRescuesAMissingWeight) {
    // arrange a catalog with a regular named family and a bold fallback (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the named family's only face (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold fallback is what rescues the weighted selection (§9.1)
    catalog.add("Alias", FontWeight::Bold);
    // the node names the regular-only family at bold, with the bold fallback (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the weight assignment selects the missing bold face (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("bold")});
    // the fallback names the family that carries bold (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Alias"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the fallback supplies the weight (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AFallbackCarriedAtAnotherWeightWarns) {
    // arrange a catalog whose only fallback carries regular, and a node asking for bold (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names an absent family at bold, with the regular-only fallback (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Missing"));
    // the weight assignment selects the bold face no family carries (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("bold")});
    // the fallback names the regular-only family (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Lato"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert the missing family is an error and the fallback's missing weight is a warning (§11.3)
    ASSERT_EQ(diagnostics.count(), 2u);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0510");
    EXPECT_EQ(diagnostics.diagnostics().at(1).code, "W0513");
}

TEST(FontUsageChecks, ADynamicWeightOnlyChecksFallbackPresence) {
    // arrange a catalog with a regular fallback, and a node whose weight is computed (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names an absent family and a computed weight (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Missing"));
    // the weight is a property, which the compiler cannot resolve at compile time (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = SharedExpression(PropertyExpression::make("weight", SourceSpan{}))});
    // the fallback names the carried family, which the presence check accepts (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Lato"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the weight is runtime data and the fallback is carried (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, ADynamicWeightStillRefusesAnAbsentFamilyWithoutFallback) {
    // arrange a catalog with a valid default, and a node naming an absent family with a computed weight (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names an absent family and a computed weight, with no fallback (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Missing"));
    // the weight is a property, which the compiler cannot resolve at compile time (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = SharedExpression(PropertyExpression::make("weight", SourceSpan{}))});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert the absent family is refused even though the weight is computed, because no fallback can rescue it (§9.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0510");
}

TEST(FontUsageChecks, ADynamicFamilyLeavesTheSelectionToTheEngine) {
    // arrange a catalog with a default family, and a node whose family is a computed property (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the family is a property reference, which the compiler cannot resolve (§11.1)
    ExpressionPtr reference = PropertyExpression::make("family", SourceSpan{});
    // the node names the dynamic family and a weight, which the engine checks at runtime (§9.1)
    CheckedNode node = node_with("font_family", SharedExpression(std::move(reference)));
    // the weight assignment selects a face within the runtime family (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("bold")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the family is runtime data rather than a literal (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, ADynamicWeightOnABoldOnlyFamilyIsLeftToTheEngine) {
    // arrange a catalog with a valid default and a bold-only family, and a node whose weight is computed (§9.1)
    FontCatalog catalog;
    // the default family carries regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is what a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold-only family is legal for an explicit selection (§9.1)
    catalog.add("AlborBoldOnly", FontWeight::Bold);
    // the node names the bold-only family and a weight property, which the compiler cannot resolve (§11.1)
    CheckedNode node = node_with("font_family", string_literal("AlborBoldOnly"));
    // the property is the dynamic weight, which the engine resolves at runtime (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = SharedExpression(PropertyExpression::make("weight", SourceSpan{}))});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the weight is runtime data and the family may be bold-only (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AnEmptyFamilyLiteralResolvesTheDefault) {
    // arrange a catalog whose default family carries the selected bold face (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is what a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold face is what the node selects (§9.1)
    catalog.add("Lato", FontWeight::Bold);
    // the empty family names the default, which the engine resolves (§9.1)
    CheckedNode node = node_with("font_family", string_literal(""));
    // the weight assignment selects the bold face of the default family (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("bold")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the default family carries the weight (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, NestedBodiesAreWalked) {
    // arrange a catalog with a default family, and three nested nodes naming missing families (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the child names a missing family, which the walk must reach (§9.1)
    CheckedNode child = node_with("font_family", string_literal("MissingChild"));
    // the repeat body names another missing family, which the walk must reach (§9.1)
    CheckedNode repeat = node_with("font_family", string_literal("MissingRepeat"));
    // the conditional body names a third missing family, which the walk must reach (§9.1)
    CheckedNode conditional = node_with("font_family", string_literal("MissingConditional"));
    // the root carries the child and both bodies, which the recursive walk visits (§11.1)
    CheckedNode root;
    // the child joins the root, because a selection may sit anywhere (§11.1)
    root.children.push_back(std::make_shared<CheckedNode>(std::move(child)));
    // the repeat body joins the root, because a row carries its own selection (§11.1)
    root.repeat_body = std::make_shared<CheckedNode>(std::move(repeat));
    // the conditional body joins the root, because a guarded node carries its own selection (§11.1)
    root.conditional_body = std::make_shared<CheckedNode>(std::move(conditional));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(root)), catalog, diagnostics);
    // assert every nested selection is reported, because the walk reached all three (§11.1)
    EXPECT_EQ(diagnostics.count(), 3u);
}

TEST(FontUsageChecks, AMissingDefaultFamilyIsRefusedOnceAtTheFile) {
    // arrange a catalog with faces and no default family, and a file with no component root (§9.1)
    FontCatalog catalog;
    // the regular face belongs to a family the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the file has no component, so only the file-level default is checked (§11.1)
    const CheckedFile file;
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the missing default family is reported once (§11.1)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0512");
}

TEST(FontUsageChecks, ACarriedRegularFamilyWithoutAWeightProducesNoProblem) {
    // arrange a catalog whose family carries regular, and a node naming the family without a weight (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the face a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the family, which asks for regular by default (§9.1)
    const CheckedFile file = file_with(node_with("font_family", string_literal("Lato")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert nothing is reported, because the family carries the default weight (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AWeightWithoutAFamilyUsesACarriedDefaultWeight) {
    // arrange a catalog whose default family carries the selected bold face (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is what a bare family name resolves (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the bold face is what the node selects (§9.1)
    catalog.add("Lato", FontWeight::Bold);
    // the node selects the weight without naming a family (§9.1)
    const CheckedFile file = file_with(node_with("font_weight", weight_literal("bold")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert nothing is reported, because the default family carries the weight (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, ACarriedFallbackDoesNotWarn) {
    // arrange a catalog carrying both the named family and the fallback (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the named family's default weight (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the fallback family is carried beside it (§9.1)
    catalog.add("Alias", FontWeight::Regular);
    // the node names the carried family and a carried fallback (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the fallback names the second carried family (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Alias"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because both families are carried (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AFallbackThatIsNotAListIsNotChecked) {
    // arrange a catalog with one family, and a fallback value that is a single string (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the carried family and a fallback that is not a list (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the fallback value is a string, which carries no literal list to check (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = string_literal("Alias")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because a non-list fallback carries no checkable family (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AFallbackWithADynamicElementIsSkipped) {
    // arrange a catalog with one family, and a fallback list whose element is a property (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the carried family and a fallback list with one dynamic element (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the dynamic element is built as a property reference, which carries no literal family (§11.1)
    std::vector<ExpressionPtr> elements;
    // the property is the one dynamic element the list carries (§11.1)
    elements.push_back(PropertyExpression::make("fallback", SourceSpan{}));
    // the array is the fallback value, which the checker reads as a list (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = SharedExpression(ArrayExpression::make(std::move(elements), SourceSpan{}))});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the dynamic element is not a literal family (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AFamilyThatIsNotAStringLiteralIsLeftToTheEngine) {
    // arrange a catalog with a default family, and a family value that is an integer literal (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the family value is an integer, which names no family literal (§11.1)
    ExpressionPtr integer = LiteralExpression::make(LiteralExpression::Kind::Integer, "1", SourceSpan{});
    // the node carries the non-string family, which the checker leaves to the engine (§11.1)
    const CheckedFile file = file_with(node_with("font_family", SharedExpression(std::move(integer))));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert nothing is reported, because a non-string family is runtime data (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AnUnknownWeightMemberIsLeftToTheChecker) {
    // arrange a catalog with the carried family, and a weight member the set does not declare (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the family and a weight member the set does not declare (§11.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the weight member is unknown to the mapping, which the type checker already reports (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal("heavy")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported here, because the checker owns the unknown member (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, EveryWeightMemberResolves) {
    // arrange a catalog carrying the family at every weight, so each written member has a face (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // every weight joins the family, because each member is checked below (§9.1)
    catalog.add("Lato", FontWeight::Thin);
    catalog.add("Lato", FontWeight::Light);
    catalog.add("Lato", FontWeight::Regular);
    catalog.add("Lato", FontWeight::Medium);
    catalog.add("Lato", FontWeight::Semibold);
    catalog.add("Lato", FontWeight::Bold);
    catalog.add("Lato", FontWeight::Black);
    // the file carries one component per written member, because one node holds one weight (§11.1)
    CheckedFile file;
    // each member is a node of its own, which the walk checks against the catalog (§9.1)
    for (const char* member : {"thin", "light", "regular", "medium", "semibold", "bold", "black"}) {
        // the node names the family and the member, which the catalog carries (§9.1)
        CheckedNode node = node_with("font_family", string_literal("Lato"));
        // the weight assignment carries the member being checked (§9.1)
        node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = weight_literal(member)});
        // the component carries the node, which the file walks (§11.1)
        CheckedComponent component;
        // the root is shared, because a checked tree is handed around by shared ownership (§11.1)
        component.root = std::make_shared<CheckedNode>(std::move(node));
        // the component joins the file, which the checker iterates (§11.1)
        file.components.push_back(std::move(component));
    }
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert every member resolved to its carried face (§9.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AWeightThatIsNotAFieldAccessIsIgnored) {
    // arrange a catalog with the carried family, and a weight value that is a string literal (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the family and a weight value that is not a field access (§11.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the string is not a Weight member, so the weight reader answers nothing (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = string_literal("bold")});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the value is not a weight literal the compiler can read (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AWeightFromAnotherSetIsNotAWeightLiteral) {
    // arrange a catalog with the carried family, and a field access on another set (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the family and a field of the Align set (§11.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the field access names Align, which is not a weight (§11.1)
    ExpressionPtr object = PropertyExpression::make("Align", SourceSpan{});
    // the field is a valid Align member, which the weight reader ignores (§11.1)
    ExpressionPtr access = FieldAccessExpression::make(std::move(object), "center", SourceSpan{});
    // the weight assignment carries the unrelated field access (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = SharedExpression(std::move(access))});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the field is not a weight member (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AFieldAccessOnANonPropertyObjectIsNotAWeightLiteral) {
    // arrange a catalog with the carried family, and a weight value whose object is a literal (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node names the family and a field access whose object is not the Weight set (§11.1)
    CheckedNode node = node_with("font_family", string_literal("Lato"));
    // the object is a literal, which names no set, so the weight reader ignores it (§11.1)
    ExpressionPtr object = LiteralExpression::make(LiteralExpression::Kind::String, "Weight", SourceSpan{});
    // the field access names a member, but the object cannot resolve a set (§11.1)
    ExpressionPtr access = FieldAccessExpression::make(std::move(object), "bold", SourceSpan{});
    // the weight assignment carries the unrelated field access (§11.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_weight", .value = SharedExpression(std::move(access))});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the field is not a weight member (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, ATextFieldSelectionIsChecked) {
    // arrange a catalog with the carried family, and a text field naming a missing family (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node is a text field, whose font properties are checked like a label's (§9.1)
    CheckedNode node = node_with("font_family", string_literal("Missing"));
    // the element type is the second widget that declares the font surface (§9.1)
    node.element_type = "TextField";
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert the uncarried family is reported for the text field (§11.1)
    ASSERT_GE(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0510");
}

TEST(FontUsageChecks, AFontFamilyOnANonFontComponentIsIgnored) {
    // arrange a catalog with a default family, and a custom component that owns a font_family property (§9.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node is a custom component, which owns its own property of that name (§11.1)
    CheckedNode node = node_with("font_family", string_literal("Missing"));
    // the element type is not a label or a field, so the font checks skip it (§9.1)
    node.element_type = "Card";
    // the fallback assignment is skipped too, because the component's property name is its own (§9.1)
    node.assignments.push_back(CheckedAssignment{.name = "font_fallback", .value = fallback_literal({"Missing"})});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file_with(std::move(node)), catalog, diagnostics);
    // assert nothing is reported, because the property belongs to the custom component (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AnUnrelatedAssignmentIsIgnored) {
    // arrange a catalog with the carried family, and a node with a text assignment (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node carries a text assignment, which is not a font selection (§11.1)
    const CheckedFile file = file_with(node_with("text", string_literal("Not a family")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the unrelated assignment is ignored (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, AMissingDefaultFamilyPointsAtTheFirstComponent) {
    // arrange a file whose first component starts at a known position, with no default family (§9.1)
    FontCatalog catalog;
    // the regular face belongs to a family the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the file carries one component with a root, which gives the file-level problem a span (§11.1)
    CheckedFile file = file_with(node_with("font_family", string_literal("Lato")));
    // the component's span names where the file begins, which the diagnostic points at (§11.1)
    file.components.front().span = SourceSpan{.start = SourceLocation{.line = 3, .column = 1}, .end = SourceLocation{.line = 5, .column = 2}};
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the file-level problem points at the component rather than at zero (§11.1)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().span.start.line, 3u);
}

TEST(FontUsageChecks, AWeightWithoutAFamilyAndNoDefaultSkipsTheWeightCheck) {
    // arrange a catalog with a face but no default family, and a node selecting a weight (§9.1)
    FontCatalog catalog;
    // the regular face is carried, but the build names no default (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the node selects bold without naming a family (§9.1)
    const CheckedFile file = file_with(node_with("font_weight", weight_literal("bold")));
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert only the missing default is reported, because there is no family to weight check (§11.1)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0512");
}

TEST(FontUsageChecks, ACatalogWithAnotherFamilyFirstStillFindsTheSecond) {
    // arrange a catalog whose first face belongs to another family (§9.1)
    FontCatalog catalog;
    // the first face is an alias, which the lookup must skip (§9.1)
    catalog.add("Alias", FontWeight::Regular);
    // the second face is the family the lookup asks for (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // act the second family is looked up without a weight (§9.1)
    const bool family = catalog.carries("Lato");
    // assert the lookup skipped the first family and found the second (§9.1)
    EXPECT_TRUE(family);
}

TEST(FontUsageChecks, AnInheritedTreeReportsOneDiagnostic) {
    // arrange a catalog with a default family, and a file whose two components share one root (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the file starts with the base component, whose root names a missing family (§9.1)
    CheckedFile file = file_with(node_with("font_family", string_literal("Missing")));
    // the derived component clones the base tree, so the same root is walked twice with one span (§11.1)
    file.components.push_back(file.components.front());
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the cloned tree produced one diagnostic rather than a duplicate (§11.1)
    EXPECT_EQ(diagnostics.count(), 1u);
}

TEST(FontUsageChecks, AComponentWithoutARootIsSkipped) {
    // arrange a catalog with a valid default, and a component with no root (§11.1)
    FontCatalog catalog;
    // the default family is carried at regular, which a bare selection resolves (§9.1)
    catalog.set_default_family("Lato");
    // the regular face is the only face the catalog carries (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the file carries one component, which has no tree to walk (§11.1)
    CheckedFile file;
    // the component joins the file without a root, which the checker skips (§11.1)
    file.components.push_back(CheckedComponent{});
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert nothing is reported, because there is no tree to inspect (§11.1)
    EXPECT_EQ(diagnostics.count(), 0u);
}

TEST(FontUsageChecks, ADefaultFamilyWithoutARegularFaceIsRefused) {
    // arrange a catalog whose default family carries only bold (§9.1)
    FontCatalog catalog;
    // the default family names the family the build carries (§9.1)
    catalog.set_default_family("Lato");
    // the bold face is the only face the default family carries (§9.1)
    catalog.add("Lato", FontWeight::Bold);
    // the file has no component, so only the file-level default is checked (§11.1)
    const CheckedFile file;
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the missing regular face is reported once (§11.1)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0512");
}

TEST(FontUsageChecks, AMissingDefaultFamilyThatIsNamedIsRefused) {
    // arrange a catalog whose default family is not among its faces, and a file with no component root (§9.1)
    FontCatalog catalog;
    // the default family names a family nothing carries (§9.1)
    catalog.set_default_family("Missing");
    // the regular face belongs to another family (§9.1)
    catalog.add("Lato", FontWeight::Regular);
    // the file has no component, so only the file-level default is checked (§11.1)
    const CheckedFile file;
    // act the usage is checked (§11.1)
    DiagnosticList diagnostics;
    check_font_usage(file, catalog, diagnostics);
    // assert the uncarried default family is reported once (§11.1)
    ASSERT_EQ(diagnostics.count(), 1u);
    EXPECT_EQ(diagnostics.diagnostics().front().code, "E0512");
}
