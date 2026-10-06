#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"

using namespace albor::compiler;

namespace
{
    // scan, parse and check a source, which the suites below use so they only state what they are about (§11.1)
    CheckedFile check(std::string_view source, DiagnosticList& diagnostics, ElementCatalog catalog) {
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const SourceFile file = Parser(tokens, diagnostics).parse();
        // the checker resolves it against the catalog, which is where the native types come from (§11.1, AD-17)
        return TypeChecker(catalog, diagnostics).check(file);
    }

    // check a source against the builtins, which is what most suites need (§11.1, §8)
    CheckedFile check(std::string_view source, DiagnosticList& diagnostics) {
        // the builtin catalog is what a ui file may use without a manifest (§11.1, §8)
        return check(source, diagnostics, ElementCatalog::with_builtins());
    }

    // check a source that is expected to be clean, which fails the test when it is not (§11.1)
    CheckedFile check(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.1)
        DiagnosticList diagnostics;
        // the file is checked, and the collected problems are checked before it is returned (§11.1)
        CheckedFile file = check(source, diagnostics);
        // a clean source produces nothing, so a problem here is a checker defect (§11.1)
        EXPECT_EQ(diagnostics.count(), 0U);
        return file;
    }

    // the first diagnostic of a run, which most failure assertions read better against (§11.1)
    const Diagnostic& first(const DiagnosticList& diagnostics) {
        // a run with no diagnostic fails the test rather than reading past the list (§11.1)
        EXPECT_GE(diagnostics.count(), 1U);
        return diagnostics.diagnostics().at(0);
    }

    // the only component of a file, which every suite checks one (§11.1)
    const CheckedComponent& only(const CheckedFile& file) {
        // a file with other components is a suite defect rather than a checker one (§11.1)
        EXPECT_EQ(file.components.size(), 1U);
        return file.components.at(0);
    }

    // an expression form the checked set has no rule for, which a new node kind would otherwise slip past (§11.1)
    class OddExpression final : public Expression
    {
    public:
        // the span is fixed, because the checker only reads it for a diagnostic (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return m_span; }

    private:
        // the span every instance reports, because the test never positions one (§11.1)
        SourceSpan m_span;
    };

    // report whether any diagnostic carries the given code, because one mistake may be reported by two passes (§11.1)
    bool has_code(const DiagnosticList& diagnostics, std::string_view code) {
        // the problems are scanned, because a run reports everything it found (§11.1)
        for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
            // the code is the stable handle, so the assertion matches on it rather than the prose (§11.1)
            if (diagnostic.code == code)
                return true;
        }
        return false;
    }

    // the element catalog holding one native component, which is what a manifest contributes (§11.2, AD-17)
    ElementCatalog with_waveform() {
        // the builtins are always present, because a component mixes them with its own types (§11.1, §8)
        ElementCatalog catalog = ElementCatalog::with_builtins();
        // the native type is what the manifest declares, so the checker resolves against it (§11.2, AD-17)
        // the data type is what the snapshot carries, and it is never an element itself (§11.2, AD-17)
        catalog.add_native_data("WaveformData");
        // the component is the element the ui file instantiates (§11.2, AD-17)
        catalog.add_native(ElementType{.name = "WaveformView", .properties = {ElementProperty{.name = "line_color", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "data", .type = ValueType::parameterized(TypeKind::Snapshot, ValueType::native("WaveformData")), .assignable = false, .scaled_by_density = false}}, .signals = {ElementSignal{.name = "selection_changed", .parameter_types = {scalar(TypeKind::Float)}}}, .accepts_children = false});
        return catalog;
    }
} // namespace

TEST(TypeChecker, EmptyFileChecksToNothing) {
    // arrange, act
    const CheckedFile file = check("");
    // assert
    EXPECT_TRUE(file.components.empty());
}

TEST(TypeChecker, ResolvesAComponentNameAndItsRoot) {
    // arrange, act
    const CheckedFile file = check("component C { Column {} }");
    // assert
    const CheckedComponent& component = only(file);
    EXPECT_EQ(component.name, "C");
    ASSERT_TRUE(component.root != nullptr);
    EXPECT_EQ(component.root->element_type, "Column");
}

TEST(TypeChecker, ResolvesDeclaredPropertyTypes) {
    // arrange, act
    const CheckedFile file = check("component C { in property<string> name: \"a\"; inout property<int> count: 0; Column {} }");
    // assert
    const CheckedComponent& component = only(file);
    ASSERT_EQ(component.properties.size(), 2U);
    EXPECT_TRUE(component.properties.at(0).type.is(TypeKind::String));
    EXPECT_EQ(component.properties.at(0).direction, PropertyDirection::In);
    EXPECT_TRUE(component.properties.at(0).has_default);
    EXPECT_TRUE(component.properties.at(1).type.is(TypeKind::Int));
    EXPECT_EQ(component.properties.at(1).direction, PropertyDirection::InOut);
}

TEST(TypeChecker, ResolvesNestedAndNativePropertyTypes) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const CheckedFile file = check("component C { in property<Snapshot<WaveformData>> data; in property<vector<int>> items; Column {} }", diagnostics, with_waveform());
    // assert
    // a nested type resolves element by element, because the manifest holds the inner name (§11.1, §11.2, AD-17)
    EXPECT_EQ(diagnostics.count(), 0U);
    const CheckedComponent& component = only(file);
    EXPECT_EQ(component.properties.at(0).type.to_string(), "Snapshot<WaveformData>");
    EXPECT_EQ(component.properties.at(1).type.to_string(), "vector<int>");
}

TEST(TypeChecker, ReportsATypeNameNothingResolves) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<Nonsense> x; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0400");
}

TEST(TypeChecker, ReportsADefaultThatDoesNotMatchItsProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> count: \"nope\"; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsADuplicatePropertyName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a; in property<int> a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0401");
}

TEST(TypeChecker, ReportsASignalThatCollidesWithAProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<int> changed; signal changed(); Column {} }", diagnostics);
    // assert
    // a name means one thing in a component, so a handler could not tell them apart (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0401");
}

TEST(TypeChecker, ReportsADuplicateSignalName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { signal a(); signal a(); Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0401");
}

TEST(TypeChecker, ReportsAComponentWithNoRoot) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0402");
}

TEST(TypeChecker, ReportsAnElementTypeTheCatalogDoesNotHold) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Nonsense {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0403");
}

TEST(TypeChecker, ResolvesANativeElementFromTheCatalog) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const CheckedFile file = check("component C { WaveformView { line_color: #FF0000; } }", diagnostics, with_waveform());
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_TRUE(only(file).root != nullptr);
    EXPECT_EQ(only(file).root->element_type, "WaveformView");
    ASSERT_EQ(only(file).root->assignments.size(), 1U);
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::Color));
}

TEST(TypeChecker, ReportsAPropertyTheElementDoesNotExpose) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Label { colour: #FF0000; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0411");
}

TEST(TypeChecker, ReportsAValueThatDoesNotMatchTheProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Label { text: 42; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, AcceptsALengthIntoADensityScaledProperty) {
    // arrange, act
    const CheckedFile file = check("component C { Column { spacing: 12px; } }");
    // assert
    // a length is a pixel count, so it fills a float the codegen scales (§11.1, AD-27)
    const CheckedAssignment& assignment = only(file).root->assignments.at(0);
    EXPECT_TRUE(assignment.type.is(TypeKind::Length));
    EXPECT_TRUE(assignment.scaled_by_density);
}

TEST(TypeChecker, RecordsTheDependenciesOfABinding) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; Label { text: @tr(\"x\") + string(a + b); } }");
    // assert
    // the codegen emits the binding with these as its declared dependencies (§7.3, AD-04a)
    const CheckedAssignment& assignment = only(file).root->assignments.at(0);
    ASSERT_EQ(assignment.dependencies.size(), 2U);
    EXPECT_EQ(assignment.dependencies.at(0), "a");
    EXPECT_EQ(assignment.dependencies.at(1), "b");
}

TEST(TypeChecker, RecordsTheDependenciesOfADefault) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: a + 1; Column {} }");
    // assert
    // a default is a binding like any other, so it registers its reads (§7.3, AD-04a)
    const CheckedProperty& property = only(file).properties.at(1);
    ASSERT_EQ(property.default_dependencies.size(), 1U);
    EXPECT_EQ(property.default_dependencies.at(0), "a");
}

TEST(TypeChecker, ReportsAReferenceToSomethingUndeclared) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Label { text: missing; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0409");
}

TEST(TypeChecker, ReportsAChildUnderALeaf) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Label { Label {} } }", diagnostics);
    // assert
    // a label takes no children, so one written under it cannot be built (§11.1, §8)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0407");
}

TEST(TypeChecker, ResolvesAHandlerAgainstTheElementSignal) {
    // arrange, act
    const CheckedFile file = check("component C { Button { on_clicked: { } } }");
    // assert
    // a handler with an empty body is legal, because it need not do anything yet (§11.1)
    ASSERT_EQ(only(file).root->handlers.size(), 1U);
    EXPECT_EQ(only(file).root->handlers.at(0).name, "clicked");
    EXPECT_TRUE(only(file).root->handlers.at(0).body.empty());
}

TEST(TypeChecker, ReportsAHandlerForASignalTheElementDoesNotRaise) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Label { on_clicked: { } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0412");
}

TEST(TypeChecker, ChecksAnAssignmentInsideAHandler) {
    // arrange, act
    const CheckedFile file = check("component C { inout property<int> count: 0; Button { on_clicked: { count = count + 1; } } }");
    // assert
    const CheckedHandler& handler = only(file).root->handlers.at(0);
    ASSERT_EQ(handler.body.size(), 1U);
    ASSERT_EQ(handler.writes.size(), 1U);
    EXPECT_EQ(handler.writes.at(0), "count");
    ASSERT_EQ(handler.body.at(0).dependencies.size(), 1U);
    EXPECT_EQ(handler.body.at(0).dependencies.at(0), "count");
}

TEST(TypeChecker, ReportsAHandlerThatAssignsAnIncomingProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> count: 0; Button { on_clicked: { count = 1; } } }", diagnostics);
    // assert
    // an incoming property is owned by a binding, so a handler may not write it (§11.1, §7)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0413");
}

TEST(TypeChecker, ReportsAHandlerThatAssignsSomethingUndeclared) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Button { on_clicked: { missing = 1; } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0404");
}

TEST(TypeChecker, ReportsAnAssignmentThatDoesNotMatchTheProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<int> count: 0; Button { on_clicked: { count = \"nope\"; } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ChecksASignalInvocationAgainstItsParameters) {
    // arrange, act
    const CheckedFile file = check("component C { signal moved(float t); Button { on_clicked: { moved(1.5); } } }");
    // assert
    ASSERT_EQ(only(file).root->handlers.at(0).body.size(), 1U);
    EXPECT_EQ(only(file).root->handlers.at(0).body.at(0).kind, HandlerStatementKind::SignalInvocation);
    EXPECT_EQ(only(file).root->handlers.at(0).body.at(0).arguments.size(), 1U);
}

TEST(TypeChecker, ReportsAnInvocationWithTheWrongArgumentCount) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { signal moved(float t); Button { on_clicked: { moved(); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0406");
}

TEST(TypeChecker, ReportsAnInvocationOfSomethingUndeclared) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Button { on_clicked: { missing(); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0405");
}

TEST(TypeChecker, ReportsAnArgumentThatDoesNotMatchItsParameter) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { signal moved(float t); Button { on_clicked: { moved(true); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, TypesEveryLiteralKind) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> i: 1; in property<float> f: 1.5; in property<string> s: \"x\"; in property<color> c: #FF0000; in property<bool> b: true; Column { spacing: 1px; } }");
    // assert
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(0).type.is(TypeKind::Int));
    EXPECT_TRUE(component.properties.at(1).type.is(TypeKind::Float));
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::String));
    EXPECT_TRUE(component.properties.at(3).type.is(TypeKind::Color));
    EXPECT_TRUE(component.properties.at(4).type.is(TypeKind::Bool));
    EXPECT_TRUE(component.root->assignments.at(0).type.is(TypeKind::Length));
}

TEST(TypeChecker, RefusesAnIntegerIntoAFloatProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Slider { value: 1; } }", diagnostics);
    // assert
    // nothing widens implicitly, so an integer needs a conversion first (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, AddsTwoIntegersWithoutWidening) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<int> c: a + b; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Int));
}

TEST(TypeChecker, WidensToFloatWhenOneOperandIsAFloat) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<float> f: 1.5; Slider { value: a + f; } }");
    // assert
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::Float));
}

TEST(TypeChecker, ConcatenatesTwoStrings) {
    // arrange, act
    const CheckedFile file = check("component C { in property<string> a: \"x\"; in property<string> b: \"y\"; Label { text: a + b; } }");
    // assert
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::String));
}

TEST(TypeChecker, RefusesConcatenatingANumberWithoutAConversion) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> a: \"x\"; in property<int> b: 1; Label { text: a + b; } }", diagnostics);
    // assert
    // a number may join text only through string(), because nothing converts implicitly (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesTheRemainderOnFloats) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<float> a: 1.5; in property<float> b: 2.5; in property<float> c: a % b; Column {} }", diagnostics);
    // assert
    // a float has no defined remainder, so the operator refuses it (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ComparesNumbersAndStrings) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<string> s: \"x\"; in property<string> t: \"y\"; in property<bool> e1: a == b; in property<bool> e2: s != t; in property<bool> e3: a < b; Column {} }");
    // assert
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(4).type.is(TypeKind::Bool));
    EXPECT_TRUE(component.properties.at(5).type.is(TypeKind::Bool));
    EXPECT_TRUE(component.properties.at(6).type.is(TypeKind::Bool));
}

TEST(TypeChecker, RefusesOrderingText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> a: \"x\"; in property<string> b: \"y\"; in property<bool> c: a < b; Column {} }", diagnostics);
    // assert
    // text has no order, so the operator refuses it (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, CombinesBoolsWithTheLogicalOperators) {
    // arrange, act
    const CheckedFile file = check("component C { in property<bool> a: true; in property<bool> b: false; in property<bool> c: a && b; in property<bool> d: a || !b; Column {} }");
    // assert
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::Bool));
    EXPECT_TRUE(component.properties.at(3).type.is(TypeKind::Bool));
}

TEST(TypeChecker, RefusesTheLogicalOperatorsOnNumbers) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<bool> b: true; in property<bool> c: a && b; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, AppliesEachBuiltinConversion) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> n: 1; Label { text: string(n); } }");
    // assert
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::String));
}

TEST(TypeChecker, RefusesANumericConversionOfABool) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<bool> b: true; Slider { value: float(b); } }", diagnostics);
    // assert
    // a bool has no digits, so the conversion is refused (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsAStaticDependencyCycle) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // each default reads the other, which the eager push graph could never order (§7.3, AD-04a)
    check("component C { in property<int> a: b; in property<int> b: a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0414");
}

TEST(TypeChecker, AcceptsAOneWayDependencyChain) {
    // arrange, act
    // a chain of three has an order, so the graph walk finds no cycle (§7.3, AD-04a)
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: a; in property<int> c: b; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(1).default_dependencies.at(0) == "a");
    EXPECT_TRUE(only(file).properties.at(2).default_dependencies.at(0) == "b");
}

TEST(TypeChecker, ReportsASelfReferencingDefault) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: a; Column {} }", diagnostics);
    // assert
    // a property cannot feed itself, because there is nothing to compute from (§7.3, AD-04a)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0414");
}

TEST(TypeChecker, ResolvesARepeatOverAVector) {
    // arrange, act
    const CheckedFile file = check("component C { in property<vector<int>> items; Column { Repeat(item in items) { Label { text: string(item); } } } }");
    // assert
    // the item binding carries the element type, so string(item) resolves (§11.1)
    ASSERT_TRUE(only(file).root->children.size() >= 1U);
    EXPECT_TRUE(only(file).root->children.at(0)->kind == TreeNodeKind::Repeat);
    EXPECT_EQ(only(file).root->children.at(0)->repeat_source_type.to_string(), "vector<int>");
}

TEST(TypeChecker, ResolvesARepeatIndexBinding) {
    // arrange, act
    const CheckedFile file = check("component C { in property<vector<int>> items; Column { Repeat(item in items, i) { Label { text: string(item + i); } } } }");
    // assert
    // the index is always an integer, so the addition stays an integer (§11.1)
    EXPECT_TRUE(only(file).root->children.at(0)->repeat_index_name == "i");
}

TEST(TypeChecker, ReportsAReferenceToAnUnboundRepeatName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<vector<int>> items; Column { Repeat(item in items) { Label { text: string(other); } } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0409");
}

TEST(TypeChecker, ReportsARepeatOverSomethingThatIsNotACollection) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> count: 1; Column { Repeat(item in count) { Label {} } } }", diagnostics);
    // assert
    // a repeat walks a collection, so an integer has nothing to walk (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, KeepsTheAuthorIdentityOutOfTheBindingGraph) {
    // arrange, act
    const CheckedFile file = check("component C { Button { id: action; on_clicked: { } } }");
    // assert
    // the identity is a node label, not a dependency, so the invalidation graph never sees it (§18.1, AD-11)
    EXPECT_EQ(only(file).root->explicit_id, "action");
    EXPECT_TRUE(only(file).root->handlers.at(0).writes.empty());
}

TEST(TypeChecker, ChecksTheDesignExampleWhole) {
    // arrange
    DiagnosticList diagnostics;
    // the design example resolves against the builtins alone, because it names only framework widgets (§11.1, §8)
    const std::string_view source = R"(
component MainView {
    in property<string> user_name: "World";
    inout property<int> click_count: 0;
    signal run_clicked();
    Column {
        spacing: 12px;
        padding: 16px;
        Label {
            text: @tr("Hello, ") + user_name + "!";
            font_size: 18px;
        }
        Row {
            spacing: 8px;
            Button {
                id: action_button;
                text: @tr("Increment");
                on_clicked: {
                    click_count = click_count + 1;
                    run_clicked();
                }
            }
            Label {
                text: @tr("Count: ") + string(click_count);
            }
        }
    }
})";
    // act
    const CheckedFile file = check(source, diagnostics);
    // assert
    // the whole example of §11.1 checks clean against the builtin catalog alone (§11.1, §8)
    EXPECT_EQ(diagnostics.count(), 0U);
    const CheckedComponent& component = only(file);
    EXPECT_EQ(component.name, "MainView");
    ASSERT_EQ(component.properties.size(), 2U);
    ASSERT_EQ(component.signals.size(), 1U);
    ASSERT_TRUE(component.root != nullptr);
    EXPECT_EQ(component.root->element_type, "Column");
    EXPECT_EQ(component.root->children.size(), 2U);
    EXPECT_EQ(component.root->children.at(0)->element_type, "Label");
    EXPECT_EQ(component.root->children.at(1)->element_type, "Row");
    // the handler assigns the incoming property and raises the declared signal (§11.1, §7)
    const CheckedHandler& handler = component.root->children.at(1)->children.at(0)->handlers.at(0);
    EXPECT_EQ(handler.name, "clicked");
    ASSERT_EQ(handler.body.size(), 2U);
    ASSERT_EQ(handler.writes.size(), 1U);
    EXPECT_EQ(handler.writes.at(0), "click_count");
    // the author identity is carried on the node, never as a binding (§18.1, AD-11)
    EXPECT_EQ(component.root->children.at(1)->children.at(0)->explicit_id, "action_button");
}

TEST(TypeChecker, ReportsNothingForACleanComponent) {
    // arrange, act
    const std::string_view source = "component C { inout property<int> n: 0; Column { Label { text: @tr(\"hi\"); } Button { on_clicked: { n = n + 1; } } } }";
    // act, assert
    // the whole shape of the design example resolves against the builtins alone (§11.1, §8)
    EXPECT_EQ(check(source).components.size(), 1U);
}

TEST(TypeChecker, ReusesTheCheckerForASecondFile) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the first file leaves no pass state behind, because the checker is reusable (§11.1)
    const CheckedFile first_file = check("component A { in property<int> a: 1; Column {} }", diagnostics);
    const CheckedFile second_file = check("component B { in property<int> b: 2; Column {} }", diagnostics);
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    EXPECT_EQ(first_file.components.at(0).name, "A");
    EXPECT_EQ(second_file.components.at(0).name, "B");
    EXPECT_TRUE(second_file.components.at(0).properties.at(0).default_dependencies.empty());
}

TEST(TypeChecker, SubtractsMultipliesAndDividesWithoutWidening) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 4; in property<int> b: 2; in property<int> c: a - b; in property<int> d: a * b; in property<int> e: a / b; Column {} }");
    // assert
    // two integers stay an integer through every arithmetic operator (§11.1)
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::Int));
    EXPECT_TRUE(component.properties.at(3).type.is(TypeKind::Int));
    EXPECT_TRUE(component.properties.at(4).type.is(TypeKind::Int));
}

TEST(TypeChecker, WidensArithmeticWhenOneOperandIsAFloat) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 4; in property<float> f: 2.5; in property<float> c: a - f; in property<float> d: a / f; Column {} }");
    // assert
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::Float));
    EXPECT_TRUE(component.properties.at(3).type.is(TypeKind::Float));
}

TEST(TypeChecker, RefusesArithmeticOnText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> a: \"x\"; in property<string> b: \"y\"; in property<int> c: a - b; Column {} }", diagnostics);
    // assert
    // an arithmetic operator has no meaning over text, so it is reported (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, TypesTheRemainderOfTwoIntegers) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 4; in property<int> b: 2; in property<int> c: a % b; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Int));
}

TEST(TypeChecker, RefusesAnEqualityAcrossIncomparableTypes) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> s: \"x\"; in property<int> a: 1; in property<bool> c: s == a; Column {} }", diagnostics);
    // assert
    // comparing across types has no meaning, so the operator is reported (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, TypesEveryComparisonOperator) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<bool> e1: a <= b; in property<bool> e2: a >= b; Column {} }");
    // assert
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::Bool));
    EXPECT_TRUE(component.properties.at(3).type.is(TypeKind::Bool));
}

TEST(TypeChecker, TypesANumericNegation) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: -a; in property<float> f: -1.5; Column {} }");
    // assert
    // a sign keeps the operand's own numeric type (§11.1)
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(1).type.is(TypeKind::Int));
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::Float));
}

TEST(TypeChecker, RefusesANegationOverText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> s: \"x\"; in property<int> a: -s; Column {} }", diagnostics);
    // assert
    // a sign is arithmetic, so it has no meaning over text (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesALogicalNegationOverANumber) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<bool> b: !a; Column {} }", diagnostics);
    // assert
    // a negation is a question, so it takes a bool (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, TypesAGroupingAsItsInnerExpression) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; Slider { value: (a + b) * 1.0; } }");
    // assert
    // the parentheses contribute nothing to the type, so the grouping is transparent (§11.1)
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::Float));
}

TEST(TypeChecker, ReportsASignalParameterWhoseTypeIsUnknown) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { signal moved(Nonsense t); Button { on_clicked: { moved(1); } } }", diagnostics);
    // assert
    // a parameter type that resolves to nothing is reported against the declaration (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0400");
}

TEST(TypeChecker, ResolvesASignalParameterToANativeType) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const CheckedFile file = check("component C { signal moved(WaveformData d); Button { on_clicked: { } } }", diagnostics, with_waveform());
    // assert
    // the manifest holds the parameter type, so it resolves (§11.2, AD-17)
    EXPECT_EQ(diagnostics.count(), 0U);
    EXPECT_EQ(only(file).signals.at(0).parameter_types.at(0).to_string(), "WaveformData");
}

TEST(TypeChecker, ResolvesADeclaredLengthType) {
    // arrange, act
    const CheckedFile file = check("component C { in property<length> gap: 1px; Column {} }");
    // assert
    // a declared length and a literal length agree, so a gap may be declared as its own type (§11.1, AD-27)
    EXPECT_TRUE(only(file).properties.at(0).type.is(TypeKind::Length));
}

TEST(TypeChecker, SuppressesTheOperatorRuleWhenAnOperandIsBroken) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the right operand names nothing, so the rule has nothing to apply to (§11.1)
    check("component C { in property<int> a: 1; in property<int> c: a + missing; Column {} }", diagnostics);
    // assert
    // one mistake produces one message, because the rule is suppressed rather than applied to an error (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0409");
}

TEST(TypeChecker, SuppressesTheUnaryRuleWhenTheOperandIsBroken) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<bool> c: !missing; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0409");
}

TEST(TypeChecker, ReportsAConversionOverAnUnknownName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the conversion target is not one the language defines, so the parser already refused it (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "C";
    TreeNode root;
    root.element_type = "Label";
    PropertyAssignment assignment;
    assignment.name = "text";
    assignment.value = ConversionExpression::make("length", LiteralExpression::make(LiteralExpression::Kind::String, "x", SourceSpan{}), SourceSpan{});
    root.assignments.push_back(std::move(assignment));
    component.root = std::make_unique<TreeNode>(std::move(root));
    file.components.push_back(std::move(component));
    // act
    static_cast<void>(TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert
    // the checker stays silent because the parser owns that message (§11.1)
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, ReportsACycleOnlyOnceForAWholeGraph) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // two loops share a property, so one walk finds both and reports each once (§7.3, AD-04a)
    check("component C { in property<int> a: c; in property<int> b: a; in property<int> c: b; Column {} }", diagnostics);
    // assert
    // the walk starts from every property, so the same loop must not be reported from each (§7.3, AD-04a)
    std::size_t cycles = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        // the code is the stable handle, so the count is by code (§7.3, AD-04a)
        if (diagnostic.code == "E0414")
            cycles++;
    }
    EXPECT_EQ(cycles, 1U);
}

TEST(TypeChecker, RefusesAnAdditionOfTwoColours) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<color> a: #FF0000; in property<color> b: #00FF00; in property<color> c: a + b; Column {} }", diagnostics);
    // assert
    // two colours have no addition, so the operator is refused (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsACycleThatTwoChainsShare) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // one property feeds a loop that another loop also feeds, so the walk reports it once (§7.3, AD-04a)
    check("component C { in property<int> a: b; in property<int> b: c; in property<int> c: b; Column {} }", diagnostics);
    // assert
    std::size_t cycles = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        // the code is the stable handle, so the count is by code (§7.3, AD-04a)
        if (diagnostic.code == "E0414")
            cycles++;
    }
    EXPECT_GE(cycles, 1U);
}

TEST(TypeChecker, ReportsEveryDistinctLoopOnce) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // two loops touch no common property, so each is its own finding (§7.3, AD-04a)
    check("component C { in property<int> a: b; in property<int> b: a; in property<int> c: d; in property<int> d: c; Column {} }", diagnostics);
    // assert
    std::size_t cycles = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        // the code is the stable handle, so the count is by code (§7.3, AD-04a)
        if (diagnostic.code == "E0414")
            cycles++;
    }
    // the walk starts at every property, so the second one must survive the dedupe (§7.3, AD-04a)
    EXPECT_EQ(cycles, 2U);
}

TEST(TypeChecker, ClosesTheReportedCycleChain) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: b; in property<int> b: a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    // the chain repeats its first property, so a reader can see that the loop closes (§7.3, AD-04a)
    EXPECT_NE(first(diagnostics).message.find("->"), std::string::npos);
}

TEST(TypeChecker, ReportsAnExpressionFormTheCheckerHasNoRuleFor) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // a node kind outside the checked set is refused rather than guessed at, so a new node cannot slip through (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "C";
    TreeNode root;
    root.element_type = "Label";
    PropertyAssignment assignment;
    assignment.name = "text";
    assignment.value.reset(new OddExpression());
    root.assignments.push_back(std::move(assignment));
    component.root = std::make_unique<TreeNode>(std::move(root));
    file.components.push_back(std::move(component));
    static_cast<void>(TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert
    // the unknown form types as the error type, so nothing downstream raises a second complaint (§11.1)
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(TypeChecker, ReportsALiteralKindTheCheckerHasNoRuleFor) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // an out of range literal kind is refused rather than guessed at, so a new kind cannot slip through (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "C";
    TreeNode root;
    root.element_type = "Label";
    PropertyAssignment assignment;
    assignment.name = "text";
    ExpressionPtr odd = LiteralExpression::make(LiteralExpression::Kind::Integer, "1", SourceSpan{});
    static_cast<LiteralExpression&>(*odd).kind = static_cast<LiteralExpression::Kind>(99);
    assignment.value = std::move(odd);
    root.assignments.push_back(std::move(assignment));
    component.root = std::make_unique<TreeNode>(std::move(root));
    file.components.push_back(std::move(component));
    static_cast<void>(TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert
    // the unknown kind types as the error type, so the assignment is refused (§11.1)
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(TypeChecker, ReportsABinaryOperatorTheCheckerHasNoRuleFor) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // an out of range operator is refused rather than guessed at, so a new operator cannot slip through (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "C";
    TreeNode root;
    root.element_type = "Label";
    PropertyAssignment assignment;
    assignment.name = "text";
    ExpressionPtr odd = BinaryExpression::make(static_cast<BinaryOperator>(99), LiteralExpression::make(LiteralExpression::Kind::Integer, "1", SourceSpan{}), LiteralExpression::make(LiteralExpression::Kind::Integer, "2", SourceSpan{}), SourceSpan{});
    assignment.value = std::move(odd);
    root.assignments.push_back(std::move(assignment));
    component.root = std::make_unique<TreeNode>(std::move(root));
    file.components.push_back(std::move(component));
    static_cast<void>(TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(TypeChecker, ReportsAUnaryOperatorTheCheckerHasNoRuleFor) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // an out of range operator takes the arithmetic rule, because a negation is the other form (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "C";
    TreeNode root;
    root.element_type = "Label";
    PropertyAssignment assignment;
    assignment.name = "text";
    ExpressionPtr odd = UnaryExpression::make(static_cast<UnaryOperator>(99), LiteralExpression::make(LiteralExpression::Kind::Integer, "1", SourceSpan{}), SourceSpan{});
    assignment.value = std::move(odd);
    root.assignments.push_back(std::move(assignment));
    component.root = std::make_unique<TreeNode>(std::move(root));
    file.components.push_back(std::move(component));
    static_cast<void>(TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(TypeChecker, RefusesAConcatenationWhoseTextIsOnTheRight) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the text is on the right, so the other operand is the one that cannot join it (§11.1)
    check("component C { in property<string> s: \"x\"; in property<int> a: 1; Label { text: a + s; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesAnAdditionWhoseTextIsOnTheRight) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> s: \"x\"; in property<int> a: 1; in property<int> c: a + s; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ComparesWithTheNumberOnTheRight) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<bool> e1: 2 == a; in property<bool> e2: 2 < a; Column {} }");
    // assert
    // the operands may arrive in either order, so both sides of each rule are exercised (§11.1)
    const CheckedComponent& component = only(file);
    EXPECT_TRUE(component.properties.at(2).type.is(TypeKind::Bool));
    EXPECT_TRUE(component.properties.at(3).type.is(TypeKind::Bool));
}

TEST(TypeChecker, TakesTheRemainderWithTheOperandsSwapped) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 4; in property<int> b: 2; in property<int> c: b % a; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Int));
}

TEST(TypeChecker, CombinesBoolsWithTheOperandsSwapped) {
    // arrange, act
    const CheckedFile file = check("component C { in property<bool> a: true; in property<bool> b: false; in property<bool> c: b && a; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Bool));
}

TEST(TypeChecker, ResolvesARepeatOverANativeCollection) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // a manifest component is itself a collection, so a repeat may walk one (§11.1, §11.2, AD-17)
    const CheckedFile file = check("component C { in property<WaveformView> data; Column { Repeat(item in data) { Label { text: string(item); } } } }", diagnostics, with_waveform());
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    // the item binding carries the component type itself, because it is not a vector (§11.1)
    EXPECT_EQ(only(file).root->children.at(0)->repeat_source_type.to_string(), "WaveformView");
}

TEST(TypeChecker, ChecksARepeatThatHasNoBody) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the parser already reported the missing body, so the checker still walks the node (§11.1)
    const CheckedFile file = check("component C { in property<vector<int>> items; Column { Repeat(item in items) { } } }", diagnostics);
    // assert
    // the node survives so the codegen never sees a half built tree (§11.1)
    ASSERT_TRUE(only(file).root->children.size() >= 1U);
    EXPECT_TRUE(only(file).root->children.at(0)->repeat_body == nullptr);
}

TEST(TypeChecker, ChecksATypeNameThatIsTooShortToNest) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the name opens a parameter form but holds no element, so it is not one (§11.1)
    check("component C { in property<vector<> x; Column {} }", diagnostics);
    // assert
    // the parser reports the missing type and the checker reports the unresolved name (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, RecordsTheSameDependencyOnlyOncePerBinding) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; Label { text: string(a) + string(a); } }");
    // assert
    // a binding that reads one property twice declares it once, because the engine registers a set (§7.3, AD-04a)
    const CheckedAssignment& assignment = only(file).root->assignments.at(0);
    EXPECT_EQ(assignment.dependencies.size(), 1U);
}

TEST(TypeChecker, RefusesSubtractionWhoseRightOperandIsText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<int> c: a - s; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesMultiplicationWhoseRightOperandIsText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<int> c: a * s; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesDivisionWhoseRightOperandIsText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<int> c: a / s; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesARemainderWhoseLeftOperandIsAFloat) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<float> a: 1.5; in property<int> b: 2; in property<int> c: a % b; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesAComparisonWhoseLeftOperandIsText) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<string> s: \"x\"; in property<int> a: 1; in property<bool> c: s < a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesADisjunctionWhoseLeftOperandIsANumber) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<bool> b: true; in property<bool> c: a || b; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ChecksADuplicateSignalNameInAComponentWithOneSignal) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // one signal has nothing to collide with, so the scan finds nothing (§11.1)
    check("component C { signal only(); Column {} }", diagnostics);
    // assert
    // the scan reports nothing, because the component is already well formed (§11.1)
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, RecordsOneDependencyPerReaderAcrossSeveralBindings) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; Column { Label { text: string(a); } Button { text: string(a); } } }");
    // assert
    // two bindings reading one property each declare it, because each is its own binding (§7.3, AD-04a)
    const CheckedNode& column = *only(file).root;
    ASSERT_EQ(column.children.size(), 2U);
    EXPECT_EQ(column.children.at(0)->assignments.at(0).dependencies.size(), 1U);
    EXPECT_EQ(column.children.at(1)->assignments.at(0).dependencies.size(), 1U);
}

TEST(TypeChecker, ChecksASnapshotTypeNameThatIsTooShortToNest) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the angle is never closed, so the name stops at the prefix (§11.1)
    check("component C { in property<Snapshot<", diagnostics);
    // assert
    // a prefix without a body resolves to nothing, because Snapshot<T> needs its T (§10.5)
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, ChecksAVectorTypeNameThatIsTooShortToNest) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the angle is never closed, so the name stops at the prefix (§11.1)
    check("component C { in property<vector<", diagnostics);
    // assert
    // a prefix without a body resolves to nothing, because vector<T> needs its T (§8.1)
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, SkipsAnAssignmentWhoseValueDidNotParse) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the value is missing, so the parser reports and the checker must not type what is not there (§11.1)
    check("component C { inout property<int> count: 0; Button { on_clicked: { count = ; } } }", diagnostics);
    // assert
    // the parser owns the message and no second complaint follows a value that does not exist (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0203"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
    EXPECT_FALSE(has_code(diagnostics, "E0414"));
}

TEST(TypeChecker, ReportsADuplicateSignalNameAmongThreeSignals) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the second name differs from the first, so only the third collides (§11.1)
    check("component C { signal a(); signal b(); signal b(); Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0401");
}

TEST(TypeChecker, InvokesTheSecondOfTwoSignals) {
    // arrange, act
    const CheckedFile file = check("component C { signal ping(); signal pong(); Button { on_clicked: { pong(); } } }");
    // assert
    // the first declaration does not match, so the scan keeps looking before it resolves (§11.1)
    const CheckedStatement& statement = only(file).root->handlers.at(0).body.at(0);
    EXPECT_EQ(statement.kind, HandlerStatementKind::SignalInvocation);
    EXPECT_EQ(statement.target, "pong");
}

TEST(TypeChecker, ReportsARepeatSourceThatDoesNotResolve) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { Column { Repeat(item in missing) { Label {} } } }", diagnostics);
    // assert
    // the source failed, so the collection rule stays silent rather than reporting twice (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0409"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, SuppressesTheOperatorWhenAnOperandFailed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<int> c: a + missing; Column {} }", diagnostics);
    // assert
    // an operand with no type suppresses the rule, because its type is not known (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0409"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, RefusesAnAdditionOfAnIntAndABool) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<bool> b: true; in property<int> c: a + b; Column {} }", diagnostics);
    // assert
    // a bool is not a number, so the addition has no rule (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, AddsAFloatOnTheLeft) {
    // arrange, act
    const CheckedFile file = check("component C { in property<float> f: 1.5; in property<int> a: 1; in property<float> c: f + a; Column {} }");
    // assert
    // a float on either side decides, so the left position is enough (§11.1)
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Float));
}

TEST(TypeChecker, SubtractsAFloatOnTheLeft) {
    // arrange, act
    const CheckedFile file = check("component C { in property<float> f: 1.5; in property<int> a: 1; in property<float> c: f - a; Column {} }");
    // assert
    // the subtraction follows the same widening rule as the addition (§11.1)
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Float));
}

TEST(TypeChecker, RefusesARemainderWithAFloatOperand) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 4; in property<float> f: 1.5; in property<int> c: a % f; Column {} }", diagnostics);
    // assert
    // a float has no defined remainder rounding, so the operator is refused (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesAnEqualityBetweenAnIntAndAString) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<bool> c: a == s; Column {} }", diagnostics);
    // assert
    // the two sides are not the same kind, so comparing them is meaningless (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ComparesWithGreaterEqual) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<bool> c: a >= b; Column {} }");
    // assert
    // the ordering operators all answer the same question (§11.1)
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Bool));
}

TEST(TypeChecker, RefusesAnOrderingOfAnIntAndAString) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<bool> c: a < s; Column {} }", diagnostics);
    // assert
    // text has no order, so an ordering against it is refused (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesAConjunctionWithAnIntOnTheRight) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<bool> b: true; in property<int> a: 1; in property<bool> c: b && a; Column {} }", diagnostics);
    // assert
    // a conjunction combines questions, and a number is not one (§11.1)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsACycleThatDoesNotTouchTheFirstProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the first property is an ordinary value, so the loop lives among the later two (§7.3, AD-04a)
    check("component C { in property<int> a: 1; in property<int> b: c; in property<int> c: b; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0414");
}

TEST(TypeChecker, ChecksASnapshotTypeNameThatNeverCloses) {
    // arrange
    DiagnosticList diagnostics;
    // act
    // the name reaches the end of the file with no closing angle, so it is one long word (§10.5)
    check("component C { in property<Snapshot<int", diagnostics);
    // assert
    // the prefix matches but the body is unwritten, so the type resolves to nothing (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, SuppressesTheOperatorWhenTheLeftOperandFailed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<int> c: missing + a; Column {} }", diagnostics);
    // assert
    // the left operand carries the failure, so the rule stays silent for both sides (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0409"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, ComparesWithGreater) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<bool> c: a > b; Column {} }");
    // assert
    // the strict ordering answers the same question as the others (§11.1)
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Bool));
}
