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
        // the structs the file declares join the catalog, which is what makes a property of one resolve (§11.1)
        register_ui_structs(file, catalog, diagnostics);
        // the enums the file declares join the catalog, which is what makes a property of one hold a member (§11.1)
        register_ui_enums(file, catalog);
        // the checker resolves it against the catalog, which is where the native types come from (§11.1, AD-17)
        return TypeChecker(catalog, diagnostics).check(file);
    }

    // check a source against a catalog that is expected to be clean, which fails the test when it is not (§11.1)
    CheckedFile check(std::string_view source, ElementCatalog catalog) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.1)
        DiagnosticList diagnostics;
        // the file is checked, and the collected problems are checked before it is returned (§11.1)
        CheckedFile file = check(source, diagnostics, std::move(catalog));
        // a clean source produces nothing, so a problem here is a checker defect (§11.1)
        EXPECT_EQ(diagnostics.count(), 0U);
        return file;
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

    // count the diagnostics carrying the given code, because a rule that must fire once is asserted by its count (§8.1)
    std::size_t count_code(const DiagnosticList& diagnostics, std::string_view code) {
        // the tally starts at zero, which is what a code the run never reported leaves behind (§8.1)
        std::size_t count = 0;
        // the problems are scanned, because a run reports everything it found (§8.1)
        for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
            // a match adds one, which is what turns the tally into a count (§8.1)
            if (diagnostic.code == code)
                count++;
        }
        return count;
    }

    // the element catalog holding one record, which is what a repeat walks (§11.2, AD-17, AD-13)
    ElementCatalog with_task() {
        // the builtins are always present, because a component mixes them with its own types (§11.1, §8)
        ElementCatalog catalog = ElementCatalog::with_builtins();
        // the record is what the manifest declares, so the checker resolves its fields (§11.2, AD-17)
        catalog.add_record(RecordType{.name = "Task",
                                      .fields = {// the key carries the identity a row is keyed by, which AD-13 requires (§18.1, AD-13)
                                                 RecordFieldType{.name = "key", .type = scalar(TypeKind::String), .is_key = true},
                                                 // a plain field is read by name and carries no identity (§11.2, AD-17)
                                                 RecordFieldType{.name = "title", .type = scalar(TypeKind::String), .is_key = false},
                                                 // a boolean field is read the same way a scalar is (§11.2, AD-17)
                                                 RecordFieldType{.name = "done", .type = scalar(TypeKind::Bool), .is_key = false}}});
        return catalog;
    }

    // the element catalog holding one native component, which is what a manifest contributes (§11.2, AD-17)
    ElementCatalog with_waveform() {
        // the builtins are always present, because a component mixes them with its own types (§11.1, §8)
        ElementCatalog catalog = ElementCatalog::with_builtins();
        // the native type is what the manifest declares, so the checker resolves against it (§11.2, AD-17)
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
    const CheckedAssignment& assignment = only(file).root->assignments.at(0);
    EXPECT_TRUE(assignment.type.is(TypeKind::Length));
    EXPECT_TRUE(assignment.scaled_by_density);
}

TEST(TypeChecker, RecordsTheDependenciesOfABinding) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; Label { text: @tr(\"x\") + string(a + b); } }");
    // assert
    const CheckedAssignment& assignment = only(file).root->assignments.at(0);
    ASSERT_EQ(assignment.dependencies.size(), 2U);
    EXPECT_EQ(assignment.dependencies.at(0), "a");
    EXPECT_EQ(assignment.dependencies.at(1), "b");
}

TEST(TypeChecker, RecordsTheDependenciesOfADefault) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: a + 1; Column {} }");
    // assert
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0407");
}

TEST(TypeChecker, ResolvesAHandlerAgainstTheElementSignal) {
    // arrange, act
    const CheckedFile file = check("component C { Button { on_clicked: { } } }");
    // assert
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

TEST(TypeChecker, ChecksACollectionAppendAgainstTheElementType) {
    // arrange, act
    const CheckedFile file = check("component C { inout property<vector<string>> items; Button { on_clicked: { items.append(\"x\"); } } }");
    // assert
    const CheckedStatement& statement = only(file).root->handlers.at(0).body.at(0);
    EXPECT_EQ(statement.kind, HandlerStatementKind::PropertyMethod);
    EXPECT_EQ(statement.element.to_string(), "string");
    ASSERT_EQ(only(file).root->handlers.at(0).writes.size(), 1U);
}

TEST(TypeChecker, ReportsACollectionMethodOnANonVectorProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<int> count: 0; Button { on_clicked: { count.append(1); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0419");
}

TEST(TypeChecker, ReportsAnUnknownCollectionMethod) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<vector<int>> items; Button { on_clicked: { items.sort(); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0418");
}

TEST(TypeChecker, ReportsAnAppendWithTheWrongArity) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<vector<int>> items; Button { on_clicked: { items.append(); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0406");
}

TEST(TypeChecker, ReportsACollectionMethodOnAnIncomingProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<vector<int>> items; Button { on_clicked: { items.append(1); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0413");
}

TEST(TypeChecker, ChecksAFieldAssignmentAgainstTheDeclaredFieldType) {
    // arrange, act
    const CheckedFile file = check("struct Frame { float width; } component C { inout property<Frame> frame; Button { on_clicked: { frame.width = 10.0; } } }");
    // assert
    const CheckedStatement& statement = file.components.at(0).root->handlers.at(0).body.at(0);
    EXPECT_EQ(statement.kind, HandlerStatementKind::FieldAssignment);
    EXPECT_EQ(statement.field, "width");
}

TEST(TypeChecker, ReportsAFieldAssignmentOnANonRecordProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<float> total: 0.0; Button { on_clicked: { total.width = 10.0; } } }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0417"));
}

TEST(TypeChecker, ReportsAFieldTheRecordDoesNotDeclare) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("struct Frame { float width; } component C { inout property<Frame> frame; Button { on_clicked: { frame.nope = 10.0; } } }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0416"));
}

TEST(TypeChecker, ReportsAFieldAssignmentOnAnIncomingProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("struct Frame { float width; } component C { in property<Frame> frame; Button { on_clicked: { frame.width = 10.0; } } }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0413"));
}

TEST(TypeChecker, ChecksASignalInvocationAgainstItsParameters) {
    // arrange, act
    const CheckedFile file = check("component C { signal moved(float t); Button { on_clicked: { moved(1.5); } } }");
    // assert
    ASSERT_EQ(only(file).root->handlers.at(0).body.size(), 1U);
    EXPECT_EQ(only(file).root->handlers.at(0).body.at(0).kind, HandlerStatementKind::SignalInvocation);
    EXPECT_EQ(only(file).root->handlers.at(0).body.at(0).arguments.size(), 1U);
}

TEST(TypeChecker, PassesARecordToASignalByValue) {
    // arrange, act
    const CheckedFile file = check("struct Frame { float width; } component C { inout property<Frame> frame; signal frame_changed(Frame frame); Button { on_clicked: { frame_changed(frame); } } }");
    // assert the record argument matches the record parameter, so the whole file checks clean (§11.1)
    EXPECT_EQ(file.components.at(0).root->handlers.at(0).body.at(0).kind, HandlerStatementKind::SignalInvocation);
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesTheRemainderOnFloats) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<float> a: 1.5; in property<float> b: 2.5; in property<float> c: a % b; Column {} }", diagnostics);
    // assert
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsAStaticDependencyCycle) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: b; in property<int> b: a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0414");
}

TEST(TypeChecker, AcceptsAOneWayDependencyChain) {
    // arrange, act
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0414");
}

TEST(TypeChecker, ResolvesARepeatOverAVector) {
    // arrange, act
    const CheckedFile file = check("component C { in property<vector<int>> items; Column { Repeat(item in items) { Label { text: string(item); } } } }");
    // assert
    ASSERT_TRUE(only(file).root->children.size() >= 1U);
    EXPECT_TRUE(only(file).root->children.at(0)->kind == TreeNodeKind::Repeat);
    EXPECT_EQ(only(file).root->children.at(0)->repeat_source_type.to_string(), "vector<int>");
}

TEST(TypeChecker, ResolvesARepeatIndexBinding) {
    // arrange, act
    const CheckedFile file = check("component C { in property<vector<int>> items; Column { Repeat(item in items, i) { Label { text: string(item + i); } } } }");
    // assert
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ResolvesAConditionalWithABoolCondition) {
    // arrange, act
    const CheckedFile file = check("component C { in property<bool> show; Column { if show { Label { text: \"x\"; } } } }");
    // assert
    const std::shared_ptr<CheckedNode>& conditional = only(file).root->children.at(0);
    EXPECT_EQ(conditional->kind, TreeNodeKind::Conditional);
    ASSERT_TRUE(conditional->conditional_body != nullptr);
    EXPECT_EQ(conditional->conditional_body->element_type, "Label");
}

TEST(TypeChecker, RecordsTheConditionDependencies) {
    // arrange, act
    const CheckedFile file = check("component C { in property<bool> show; Column { if show { Label {} } } }");
    // assert
    const std::shared_ptr<CheckedNode>& conditional = only(file).root->children.at(0);
    EXPECT_EQ(conditional->condition_dependencies, std::vector<std::string>{"show"});
}

TEST(TypeChecker, ReportsAConditionalConditionThatIsNotABool) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> count: 1; Column { if count { Label {} } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, KeepsTheAuthorIdentityOutOfTheBindingGraph) {
    // arrange, act
    const CheckedFile file = check("component C { Button { id: action; on_clicked: { } } }");
    // assert
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
    EXPECT_EQ(check(source).components.size(), 1U);
}

TEST(TypeChecker, ReusesTheCheckerForASecondFile) {
    // arrange
    DiagnosticList diagnostics;
    // act
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesALogicalNegationOverANumber) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<bool> b: !a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, TypesAGroupingAsItsInnerExpression) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; Slider { value: (a + b) * 1.0; } }");
    // assert
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::Float));
}

TEST(TypeChecker, ReportsASignalParameterWhoseTypeIsUnknown) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { signal moved(Nonsense t); Button { on_clicked: { moved(1); } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0400");
}

TEST(TypeChecker, ResolvesASignalParameterToANativeType) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const CheckedFile file = check("component C { signal moved(WaveformData d); Button { on_clicked: { } } }", diagnostics, with_waveform());
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    EXPECT_EQ(only(file).signals.at(0).parameter_types.at(0).to_string(), "WaveformData");
}

TEST(TypeChecker, ResolvesADeclaredLengthType) {
    // arrange, act
    const CheckedFile file = check("component C { in property<length> gap: 1px; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(0).type.is(TypeKind::Length));
}

TEST(TypeChecker, SuppressesTheOperatorRuleWhenAnOperandIsBroken) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<int> c: a + missing; Column {} }", diagnostics);
    // assert
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
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, ReportsACycleOnlyOnceForAWholeGraph) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: c; in property<int> b: a; in property<int> c: b; Column {} }", diagnostics);
    // assert
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
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsACycleThatTwoChainsShare) {
    // arrange
    DiagnosticList diagnostics;
    // act
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
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(TypeChecker, ReportsALiteralKindTheCheckerHasNoRuleFor) {
    // arrange
    DiagnosticList diagnostics;
    // act
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
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(TypeChecker, ReportsABinaryOperatorTheCheckerHasNoRuleFor) {
    // arrange
    DiagnosticList diagnostics;
    // act
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
    const CheckedFile file = check("component C { in property<vector<int>> items; Column { Repeat(item in items) { } } }", diagnostics);
    // assert
    ASSERT_TRUE(only(file).root->children.size() >= 1U);
    EXPECT_TRUE(only(file).root->children.at(0)->repeat_body == nullptr);
}

TEST(TypeChecker, ChecksATypeNameThatIsTooShortToNest) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<vector<> x; Column {} }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, RecordsTheSameDependencyOnlyOncePerBinding) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; Label { text: string(a) + string(a); } }");
    // assert
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
    check("component C { signal only(); Column {} }", diagnostics);
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, RecordsOneDependencyPerReaderAcrossSeveralBindings) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; Column { Label { text: string(a); } Button { text: string(a); } } }");
    // assert
    const CheckedNode& column = *only(file).root;
    ASSERT_EQ(column.children.size(), 2U);
    EXPECT_EQ(column.children.at(0)->assignments.at(0).dependencies.size(), 1U);
    EXPECT_EQ(column.children.at(1)->assignments.at(0).dependencies.size(), 1U);
}

TEST(TypeChecker, ChecksASnapshotTypeNameThatIsTooShortToNest) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<Snapshot<", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, ChecksAVectorTypeNameThatIsTooShortToNest) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<vector<", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, SkipsAnAssignmentWhoseValueDidNotParse) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { inout property<int> count: 0; Button { on_clicked: { count = ; } } }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0203"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
    EXPECT_FALSE(has_code(diagnostics, "E0414"));
}

TEST(TypeChecker, ReportsADuplicateSignalNameAmongThreeSignals) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { signal a(); signal b(); signal b(); Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0401");
}

TEST(TypeChecker, InvokesTheSecondOfTwoSignals) {
    // arrange, act
    const CheckedFile file = check("component C { signal ping(); signal pong(); Button { on_clicked: { pong(); } } }");
    // assert
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
    EXPECT_TRUE(has_code(diagnostics, "E0409"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, SuppressesTheOperatorWhenAnOperandFailed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<int> c: a + missing; Column {} }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0409"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, RefusesAnAdditionOfAnIntAndABool) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<bool> b: true; in property<int> c: a + b; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, AddsAFloatOnTheLeft) {
    // arrange, act
    const CheckedFile file = check("component C { in property<float> f: 1.5; in property<int> a: 1; in property<float> c: f + a; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Float));
}

TEST(TypeChecker, SubtractsAFloatOnTheLeft) {
    // arrange, act
    const CheckedFile file = check("component C { in property<float> f: 1.5; in property<int> a: 1; in property<float> c: f - a; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Float));
}

TEST(TypeChecker, RefusesARemainderWithAFloatOperand) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 4; in property<float> f: 1.5; in property<int> c: a % f; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesAnEqualityBetweenAnIntAndAString) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<bool> c: a == s; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ComparesWithGreaterEqual) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<bool> c: a >= b; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Bool));
}

TEST(TypeChecker, RefusesAnOrderingOfAnIntAndAString) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<string> s: \"x\"; in property<bool> c: a < s; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, RefusesAConjunctionWithAnIntOnTheRight) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<bool> b: true; in property<int> a: 1; in property<bool> c: b && a; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0410");
}

TEST(TypeChecker, ReportsACycleThatDoesNotTouchTheFirstProperty) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<int> b: c; in property<int> c: b; Column {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0414");
}

TEST(TypeChecker, ChecksASnapshotTypeNameThatNeverCloses) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<Snapshot<int", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeChecker, SuppressesTheOperatorWhenTheLeftOperandFailed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    check("component C { in property<int> a: 1; in property<int> c: missing + a; Column {} }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0409"));
    EXPECT_FALSE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, ComparesWithGreater) {
    // arrange, act
    const CheckedFile file = check("component C { in property<int> a: 1; in property<int> b: 2; in property<bool> c: a > b; Column {} }");
    // assert
    EXPECT_TRUE(only(file).properties.at(2).type.is(TypeKind::Bool));
}

TEST(TypeChecker, TypesABracketedListAsTheCollectionItsElementsAgreeOn) {
    // arrange a list of one element type, which is the only shape a collection has (§8.1)
    const CheckedFile file = check("component C { in property<vector<int>> items: [1, 2]; Column {} }");
    // act
    const ValueType type = file.components.at(0).properties.at(0).type;
    // assert the declared type and the literal agree, which is what makes the default legal (§8.1)
    EXPECT_EQ(type.to_string(), "vector<int>");
}

TEST(TypeChecker, TypesAnEmptyListAsACollectionOfAnUnknownElement) {
    // arrange an empty list, which has no element to read a type from (§8.1)
    const CheckedFile file = check("component C { in property<vector<string>> items: []; Column {} }");
    // act
    const ValueType type = file.components.at(0).properties.at(0).type;
    // assert the collection is still a collection, because the declared element fills in what the literal cannot (§8.1)
    EXPECT_TRUE(type.is(TypeKind::Vector));
}

TEST(TypeChecker, TypesANestedListAsACollectionOfCollections) {
    // arrange a list of lists, whose element is itself a collection (§8.1)
    const CheckedFile file = check("component C { in property<vector<vector<int>>> grid: [[1]]; Column {} }");
    // act
    const ValueType type = file.components.at(0).properties.at(0).type;
    // assert the nesting is carried, because a collection may hold a collection (§8.1)
    ASSERT_TRUE(type.is(TypeKind::Vector));
    EXPECT_EQ(type.element().to_string(), "vector<int>");
}

TEST(TypeChecker, RefusesAListWhoseElementsDisagree) {
    // arrange diagnostics the expectation reads, because the checker reports rather than guesses (§8.1)
    DiagnosticList diagnostics;
    // the list mixes a number and a string, which no single element type can be (§8.1)
    check("component C { in property<vector<int>> items: [1, \"two\"]; Column {} }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0408"));
}

TEST(TypeChecker, ReportsOnlyTheFirstDisagreementInAList) {
    // arrange diagnostics, because a list that disagrees twice must not complain twice (§8.1)
    DiagnosticList diagnostics;
    // the two bad elements sit on either side of a good one, which is what the checker walks (§8.1)
    check("component C { in property<vector<int>> items: [1, \"a\", \"b\"]; Column {} }", diagnostics);
    // assert exactly one problem is raised, because the first disagreement settles what the list cannot be (§8.1)
    EXPECT_EQ(count_code(diagnostics, "E0408"), 1U);
}

TEST(TypeChecker, ResolvesACollectionOfARecordTheManifestDeclared) {
    // arrange a collection of a record, which is what a list of tasks is (§11.2, AD-17, AD-13)
    const CheckedFile file = check("component C { in property<vector<Task>> tasks; Column {} }", with_task());
    // act
    const ValueType type = file.components.at(0).properties.at(0).type;
    // assert the element resolved to the record, which is what registration bought (§11.2, AD-17)
    ASSERT_TRUE(type.is(TypeKind::Vector));
    EXPECT_EQ(type.element().native_name(), "Task");
}

TEST(TypeChecker, TypesAFieldReadAsTheDeclaredFieldType) {
    // arrange a field read inside a repeat body, which is how a row reaches its item (§11.2, AD-17, AD-13)
    const CheckedFile file = check("component C { in property<vector<Task>> tasks; Column { Repeat(t in tasks) { Label { text: t.title; } } } }", with_task());
    // act the body is the second child of the column, and the label is inside it (§11.2)
    const CheckedNode& body = *file.components.at(0).root->children.at(0)->repeat_body;
    // assert the read produced the field's declared type, because there is no inference (§11.2, AD-17)
    EXPECT_TRUE(body.assignments.at(0).type.is(TypeKind::String));
}

TEST(TypeChecker, RefusesAFieldTheRecordDoesNotDeclare) {
    // arrange diagnostics, because a field read resolves against the declaration (§11.2, AD-17)
    DiagnosticList diagnostics;
    // the field is not one the manifest declared (§11.2, AD-17)
    check("component C { in property<vector<Task>> tasks; Column { Repeat(t in tasks) { Label { text: t.nope; } } } }", diagnostics, with_task());
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0416"));
}

TEST(TypeChecker, RefusesAFieldReadOnAScalar) {
    // arrange diagnostics, because a scalar carries no members (§11.2)
    DiagnosticList diagnostics;
    // the source is a collection of integers, which has no fields at all (§11.2)
    check("component C { in property<vector<int>> items; Column { Repeat(i in items) { Label { text: string(i.nope); } } } }", diagnostics);
    // assert the read is a type mismatch, because there is no record to look the field up in (§11.2)
    EXPECT_TRUE(has_code(diagnostics, "E0410"));
}

TEST(TypeChecker, RefusesAFieldReadOnADataTypeThatIsNotARecord) {
    // arrange diagnostics, because a data type carries no members (§11.2, AD-17)
    DiagnosticList diagnostics;
    // the item is a data type the manifest declared, which is named but is not a record (§11.2, AD-17)
    check("component C { in property<vector<WaveformData>> waves; Column { Repeat(w in waves) { Label { text: string(w.peak); } } } }", diagnostics, with_waveform());
    // assert the problem names the type rather than a missing field, because no field list exists (§11.2, AD-17)
    EXPECT_TRUE(has_code(diagnostics, "E0417"));
}

TEST(TypeChecker, ReportsAKeyFieldExactlyOnce) {
    // arrange a catalog, because identity is what a row is keyed by (§18.1, AD-13)
    const ElementCatalog catalog = with_task();
    // act
    const RecordType* record = catalog.find_record("Task");
    // assert exactly one field carries the identity, because two keys would key a row ambiguously (§18.1, AD-13)
    ASSERT_NE(record, nullptr);
    ASSERT_NE(record->key_field(), nullptr);
    EXPECT_EQ(record->key_field()->name, "key");
}

TEST(TypeChecker, ResolvesAStructPropertyFieldToItsDeclaredType) {
    // arrange a struct a property carries and a field read inside a binding (§11.1)
    DiagnosticList diagnostics;
    // the field read feeds a conversion, which accepts the float the field declares (§11.1)
    check("struct Frame { float width; float height; } component C { in property<Frame> frame; Column { Label { text: string(frame.width); } } }", diagnostics);
    // assert the struct registered and the field resolved, so the whole file checks clean (§11.1)
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, ReportsAStructFieldOfAnUnknownType) {
    // arrange diagnostics, because a field must name a type the catalog resolves (§11.1)
    DiagnosticList diagnostics;
    // the field names a type nothing declares, so it produces nothing a read could hold (§11.1)
    check("struct Bad { Nope value; } component C { Column {} }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0420"));
}

TEST(TypeChecker, RefusesAStructFieldTheDeclarationDoesNotHold) {
    // arrange diagnostics, because a field read resolves against the declaration (§11.1)
    DiagnosticList diagnostics;
    // the field is not one the struct declared (§11.1)
    check("struct Frame { float width; } component C { in property<Frame> frame; Column { Label { text: string(frame.nope); } } }", diagnostics);
    // assert
    EXPECT_TRUE(has_code(diagnostics, "E0416"));
}

TEST(TypeChecker, ResolvesAnEnumPropertyToItsDeclaredType) {
    // arrange an enum a property holds and a member read inside a binding (§11.1)
    DiagnosticList diagnostics;
    // the member read types as the enum, which the string conversion accepts (§11.1)
    check("enum Status { Todo, Doing, Done } component C { in property<Status> status; Column { Label { text: string(status); } } }", diagnostics);
    // assert the enum registered and the property resolved, so the whole file checks clean (§11.1)
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, ResolvesAnEnumMemberReadToTheEnumItself) {
    // arrange an enum a property holds and a member written as a value inside a binding (§11.1)
    DiagnosticList diagnostics;
    // the written member types as the enum, which the assignment to the property accepts (§11.1)
    check("enum Status { Todo, Doing, Done } component C { in property<Status> status; Column { Label { text: string(status); } } }", diagnostics);
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(TypeChecker, ReportsAnEnumMemberTheDeclarationDoesNotHold) {
    // arrange diagnostics, because a member read resolves against the declaration (§11.1)
    DiagnosticList diagnostics;
    // the member is not one the enum declared (§11.1)
    check("enum Status { Todo, Doing, Done } component C { in property<Status> status; Column { Label { text: string(status.Nope); } } }", diagnostics);
    // assert the unknown member is reported against the ones the enum does hold (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0416"));
}

TEST(TypeChecker, ReportsAPropertyOfAnUnknownEnumName) {
    // arrange diagnostics, because a property type must resolve against the catalog (§11.1)
    DiagnosticList diagnostics;
    // the property names a type nothing declares, so a value of it cannot resolve (§11.1)
    check("component C { in property<Status> status; Column {} }", diagnostics);
    // assert the unknown type is reported, which is what an unresolved property name produces (§11.1)
    EXPECT_TRUE(has_code(diagnostics, "E0400"));
}

TEST(TypeCheckerHandlers, ABodyTheParserCouldNotReadIsReportedNotFollowed) {
    // arrange a handler body holding an element, which the language does not allow in a body (§11.1)
    DiagnosticList diagnostics;
    const std::string source = R"UI(component Panel {
    inout property<float> total;

    Column {
        Button {
            text: "grow";

            on_clicked: {
                Label { text: "not a statement"; }
            }
        }
    }
})UI";
    // act, assert the run finishes and reports rather than reading the value the parser never produced
    const CheckedFile file = check(source, diagnostics);
    // assert the parser named the problem, because a body takes only an assignment or an invocation (§11.1)
    bool reported = false;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0201")
            reported = true;
    EXPECT_TRUE(reported);
    // assert the checker still produced a file to describe, because a crash reports nothing (§11.1)
    EXPECT_EQ(file.components.size(), 1U);
}

TEST(TypeChecker, TypesAFunctionCallAgainstItsDeclaration) {
    // act a component calls a declared function inside a binding, which the call resolves against (§11.1)
    const CheckedFile file = check("fn double(x: float): float { return x * 2.0; } component C { in property<float> amount; Label { text: string(double(amount)); } }");
    // assert the file generated cleanly, because the call and its arguments both resolved (§11.1)
    EXPECT_EQ(file.functions.size(), 1U);
    EXPECT_TRUE(only(file).root->assignments.at(0).type.is(TypeKind::String));
}

TEST(TypeChecker, TypesAFunctionThatCallsAnotherFunction) {
    // act a function calls one declared after it, which the two-pass signature resolution makes legal (§11.1)
    const CheckedFile file = check("fn outer(x: float): float { return inner(x) + 1.0; } fn inner(x: float): float { return x; } component C { in property<float> amount; Label { text: string(outer(amount)); } }");
    // assert the forward call resolved, because both signatures were known before either body (§11.1)
    EXPECT_EQ(file.functions.size(), 2U);
}

TEST(TypeChecker, ReportsAnUnknownFunctionName) {
    // arrange diagnostics, because a call to an undeclared name cannot resolve (§11.1)
    DiagnosticList diagnostics;
    // act the component calls a function the file never declares (§11.1)
    check("component C { in property<float> amount; Label { text: string(missing(amount)); } }", diagnostics);
    // assert the problem is reported by name, because that is what the author must change (§11.1)
    EXPECT_EQ(first(diagnostics).code, "E0416");
}

TEST(TypeChecker, ReportsAWrongArityFunctionCall) {
    // arrange diagnostics, because a call that passes the wrong count cannot resolve (§11.1)
    DiagnosticList diagnostics;
    // act the call passes one argument where the declaration takes two (§11.1)
    check("fn double(x: float): float { return x * 2.0; } component C { in property<float> amount; Label { text: string(double(amount, amount)); } }", diagnostics);
    // assert the problem names the count, because both the name and the arity are what the author must fix (§11.1)
    EXPECT_EQ(first(diagnostics).code, "E0417");
}

TEST(TypeChecker, ReportsAMismatchedFunctionArgument) {
    // arrange diagnostics, because an argument that cannot fill its parameter cannot resolve (§11.1)
    DiagnosticList diagnostics;
    // act the call passes a string where the parameter is a float (§11.1)
    check("fn double(x: float): float { return x * 2.0; } component C { in property<string> name; Label { text: string(double(name)); } }", diagnostics);
    // assert the problem is a mismatch, because the written type cannot fill the parameter (§11.1)
    EXPECT_GT(diagnostics.count(), 0U);
}
