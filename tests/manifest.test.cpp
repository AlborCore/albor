#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "manifest/manifest.h"

using namespace albor::manifest;
using namespace albor::compiler;

namespace
{
    // scan and parse a manifest, which the suites below use so they only state what they are about (§11.2)
    ManifestFile parse(std::string_view source, DiagnosticList& diagnostics) {
        // the scanner runs first, because the parser takes units (§11.2)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser then builds the file from those units (§11.2)
        return ManifestParser(tokens, diagnostics).parse("test.module");
    }

    // scan and parse a manifest that is expected to be clean, which fails the test when it is not (§11.2)
    ManifestFile parse(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.2)
        DiagnosticList diagnostics;
        // the file is parsed, and the collected problems are checked before it is returned (§11.2)
        ManifestFile file = parse(source, diagnostics);
        // a clean source produces nothing, so a problem here is a parser defect (§11.2)
        EXPECT_EQ(diagnostics.count(), 0U);
        return file;
    }

    // the first diagnostic of a run, which most failure assertions read better against (§11.2)
    const Diagnostic& first(const DiagnosticList& diagnostics) {
        // a run with no diagnostic fails the test rather than reading past the list (§11.2)
        EXPECT_GE(diagnostics.count(), 1U);
        return diagnostics.diagnostics().at(0);
    }

    // the example manifest from §11.2, which most suites parse as a whole (§11.2)
    constexpr std::string_view CHART_MANIFEST = R"(
native component WaveformView {
    in property<Snapshot<WaveformData>> data;
    in property<color> line_color: #00FF00;
    signal selection_changed(float time_pos);
    semantic role: "chart";
    preview {
        line_color: #FF0000;
    }
}
)";
} // namespace

TEST(ManifestParser, EmptySourceDeclaresNothing) {
    // arrange, act
    const ManifestFile file = parse("");
    // assert
    EXPECT_EQ(file.module, "test.module");
    EXPECT_TRUE(file.components.empty());
}

TEST(ManifestParser, ReadsTheModuleNameTheDriverSupplied) {
    // arrange
    DiagnosticList diagnostics;
    // the module name comes from the file the driver named, not from the manifest text (§11.1, §11.2, AD-17)
    const std::vector<Token> tokens = Lexer("native component C {}", diagnostics).tokenize();
    // act
    const ManifestFile file = ManifestParser(tokens, diagnostics).parse("albor.controls");
    // assert
    EXPECT_EQ(file.module, "albor.controls");
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "C");
}

TEST(ManifestParser, ReadsTheExampleManifestWhole) {
    // arrange, act
    const ManifestFile file = parse(CHART_MANIFEST);
    // assert
    ASSERT_EQ(file.components.size(), 1U);
    const NativeComponent& component = file.components.at(0);
    EXPECT_EQ(component.name, "WaveformView");
    EXPECT_EQ(component.properties.size(), 2U);
    EXPECT_EQ(component.signals.size(), 1U);
    ASSERT_TRUE(component.semantic.has_value());
    EXPECT_EQ(component.semantic->role, "chart");
    ASSERT_EQ(component.preview_overrides.size(), 1U);
}

TEST(ManifestParser, ReadsAPropertyWithNoDefault) {
    // arrange, act
    const ManifestFile file = parse("native component C { in property<Snapshot<WaveformData>> data; }");
    // assert
    const NativeProperty& property = file.components.at(0).properties.at(0);
    EXPECT_EQ(property.name, "data");
    EXPECT_EQ(property.type, "Snapshot<WaveformData>");
    EXPECT_EQ(property.direction, PropertyDirection::In);
    EXPECT_FALSE(property.has_default);
    EXPECT_TRUE(property.default_text.empty());
}

TEST(ManifestParser, ReadsAPropertyWithADefault) {
    // arrange, act
    const ManifestFile file = parse("native component C { in property<color> line_color: #00FF00; }");
    // assert
    const NativeProperty& property = file.components.at(0).properties.at(0);
    EXPECT_EQ(property.type, "color");
    EXPECT_TRUE(property.has_default);
    EXPECT_EQ(property.default_text, "#00FF00");
}

TEST(ManifestParser, ReadsAnAssignablePropertyAsInOut) {
    // arrange, act
    const ManifestFile file = parse("native component C { inout property<int> count: 0; }");
    // assert
    const NativeProperty& property = file.components.at(0).properties.at(0);
    EXPECT_EQ(property.direction, PropertyDirection::InOut);
    EXPECT_EQ(property.type, "int");
    EXPECT_EQ(property.default_text, "0");
}

TEST(ManifestParser, KeepsANestedBracketedDefaultWhole) {
    // arrange, act
    const ManifestFile file = parse("native component C { in property<vector<int>> items: vector<int>(1, 2); }");
    // assert, the default is re-emitted rather than reinterpreted, so its brackets must survive (§11.2)
    EXPECT_EQ(file.components.at(0).properties.at(0).default_text, "vector<int>(1,2)");
}

TEST(ManifestParser, ReadsASignalWithNoParameters) {
    // arrange, act
    const ManifestFile file = parse("native component C { signal refreshed(); }");
    // assert
    const NativeSignal& signal = file.components.at(0).signals.at(0);
    EXPECT_EQ(signal.name, "refreshed");
    EXPECT_TRUE(signal.parameters.empty());
}

TEST(ManifestParser, ReadsASignalWithParametersInOrder) {
    // arrange, act
    const ManifestFile file = parse("native component C { signal moved(float time_pos, int index); }");
    // assert, the facade signature follows declaration order, so the order has to be kept (§11.2)
    const NativeSignal& signal = file.components.at(0).signals.at(0);
    ASSERT_EQ(signal.parameters.size(), 2U);
    EXPECT_EQ(signal.parameters.at(0).type, "float");
    EXPECT_EQ(signal.parameters.at(0).name, "time_pos");
    EXPECT_EQ(signal.parameters.at(1).type, "int");
    EXPECT_EQ(signal.parameters.at(1).name, "index");
}

TEST(ManifestParser, ReadsTheSemanticRole) {
    // arrange, act
    const ManifestFile file = parse("native component C { semantic role: \"chart\"; }");
    // assert
    ASSERT_TRUE(file.components.at(0).semantic.has_value());
    EXPECT_EQ(file.components.at(0).semantic->role, "chart");
}

TEST(ManifestParser, LeavesTheSemanticRoleAbsentWhenNoneIsDeclared) {
    // arrange, act
    const ManifestFile file = parse("native component C {}");
    // assert
    EXPECT_FALSE(file.components.at(0).semantic.has_value());
}

TEST(ManifestParser, ReportsASemanticRoleThatIsNotAString) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { semantic role: chart; }", diagnostics);
    // assert, the runtime matches a role by name, so the manifest has to write it as a string (§10.1, §11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsASemanticFieldThatIsNotARole) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { semantic label: \"chart\"; }", diagnostics);
    // assert, a semantic declaration carries only a role, because that is all the facade publishes (§10.1, §11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0304");
}

TEST(ManifestParser, ReadsThePreviewOverrides) {
    // arrange, act
    const ManifestFile file = parse("native component C { preview { line_color: #FF0000; data: 1; } }");
    // assert
    const std::vector<PreviewOverride>& overrides = file.components.at(0).preview_overrides;
    ASSERT_EQ(overrides.size(), 2U);
    EXPECT_EQ(overrides.at(0).property, "line_color");
    EXPECT_EQ(overrides.at(0).value_text, "#FF0000");
    EXPECT_EQ(overrides.at(1).property, "data");
    EXPECT_EQ(overrides.at(1).value_text, "1");
}

TEST(ManifestParser, ReadsAnEmptyPreviewBlock) {
    // arrange, act
    const ManifestFile file = parse("native component C { preview {} }");
    // assert
    EXPECT_TRUE(file.components.at(0).preview_overrides.empty());
}

TEST(ManifestParser, ReadsSeveralComponentsInOneFile) {
    // arrange, act
    const ManifestFile file = parse("native component A {} native component B { in property<int> n; }");
    // assert
    ASSERT_EQ(file.components.size(), 2U);
    EXPECT_EQ(file.components.at(0).name, "A");
    EXPECT_EQ(file.components.at(1).name, "B");
    EXPECT_EQ(file.components.at(1).properties.at(0).name, "n");
}

TEST(ManifestParser, ReadsAComponentDeclarationWithNoNativeKeyword) {
    // arrange, act
    const ManifestFile file = parse("component C {}");
    // assert, a manifest declares native types only, so the keyword is what distinguishes one (§11.2, AD-17)
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "C");
}

TEST(ManifestParser, ReportsAUnitThatCannotStartTheFile) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("42", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAUnitThatCannotStartAComponentMember) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { 42 }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAMissingComponentName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = parse("native component { }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
    // a nameless component is a recovered fragment rather than a declaration (§11.2, AD-17)
    EXPECT_TRUE(file.components.empty());
}

TEST(ManifestParser, ReportsAComponentThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { in property<int> n;", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAPropertyWhoseTypeIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { in property<int n; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAMissingPropertyKeyword) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { in int n; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAMissingPropertyName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { in property<int>; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsASignalParameterWithNoName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { signal moved(float); }", diagnostics);
    // assert, the facade could not emit a signature without a parameter name (§11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsASignalParameterThatStartsWithNoType) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { signal moved(42 index); }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsASignalWhoseParameterListRunsToTheEnd) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = parse("native component C { signal moved(", diagnostics);
    // assert, the declaration was still read as far as the stream allowed (§11.2)
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).signals.at(0).name, "moved");
    EXPECT_GE(diagnostics.count(), 1U);
}

TEST(ManifestParser, ReportsASignalWithATrailingComma) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { signal moved(float time_pos,); }", diagnostics);
    // assert, the comma promises a parameter that is not there (§11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAPreviewBlockThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { preview { a: 1;", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, ReportsAnOverrideThatIsNotANameAssignment) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { preview { 42; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
}

TEST(ManifestParser, RecoverySkipsTheBadMemberAndKeepsReadingTheComponent) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = parse("native component C { 42 in property<int> n; }", diagnostics);
    // assert, the bad member is dropped, so the members beside it are still read (§11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).name, "n");
}

TEST(ManifestParser, RecoveryStepsOverANestedBodyWithoutEndingTheComponent) {
    // arrange
    DiagnosticList diagnostics;
    // act, the stray body opens and closes inside the broken member, so the scan continues past it (§11.2)
    const ManifestFile file = parse("native component C { 42 { } in property<int> n; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
}

TEST(ManifestParser, RecoveryStopsAtTheEndOfTheComponent) {
    // arrange
    DiagnosticList diagnostics;
    // act, a closing brace at the top level ends the component, so the scan stops there (§11.2)
    const ManifestFile file = parse("native component C { 42 } native component Other { in property<int> n; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 2U);
    EXPECT_EQ(file.components.at(0).name, "C");
    EXPECT_EQ(file.components.at(1).name, "Other");
    ASSERT_EQ(file.components.at(1).properties.size(), 1U);
}

TEST(ManifestParser, RecoverySkipsAMemberThatRunsToTheEndOfTheStream) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = parse("native component C { 42", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "C");
}

TEST(ManifestParser, ReadsAComponentWhoseMembersRunToTheEndOfTheStream) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = parse("native component C { in property<int> n", diagnostics);
    // assert, the member was read as far as the stream allowed, and the terminator was reported (§11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).name, "n");
}

TEST(ManifestParser, ReportsTheEndOfFileWhereAUnitWasExpected) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("native component C { in property<", diagnostics);
    // assert, the report names what was found, which at the end of the file is the file itself (§11.2)
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_NE(first(diagnostics).message.find("the end of the file"), std::string::npos);
}

TEST(ManifestParser, TerminatesOnAStreamThatCarriesNoEndUnit) {
    // arrange, the scanner always appends an end unit, so this stream is one a caller must never build (§11.2)
    const std::vector<Token> tokens = {Token{.kind = Token::Kind::Identifier, .text = "native", .value = "native", .span = SourceSpan{}}};
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = ManifestParser(tokens, diagnostics).parse("test.module");
    // assert, the parse terminated rather than running off the end of the stream (§11.2)
    EXPECT_TRUE(file.components.empty());
    EXPECT_GE(diagnostics.count(), 1U);
}

TEST(ManifestParser, TerminatesOnAnEmptyStream) {
    // arrange, an empty stream is the shape a caller must never build, and this proves the reader survives it (§11.2)
    const std::vector<Token> tokens;
    DiagnosticList diagnostics;
    // act
    const ManifestFile file = ManifestParser(tokens, diagnostics).parse("test.module");
    // assert
    EXPECT_TRUE(file.components.empty());
    EXPECT_EQ(diagnostics.count(), 0U);
    // an empty stream has no construct to close, so no position was ever asked of it (§11.2)
    EXPECT_EQ(tokens.size(), 0U);
}

TEST(ManifestParser, ReportsAPropertyWhoseValueRunsIntoTheClosingBrace) {
    // arrange
    DiagnosticList diagnostics;
    // act, the value has no terminator, so it runs to the brace that ends the component (§11.2)
    const ManifestFile file = parse("native component C { in property<int> n }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
    // the property was still read as far as the brace allowed (§11.2)
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).name, "n");
}

TEST(ManifestParser, ReadsAPropertyDefaultThatRunsIntoTheClosingBrace) {
    // arrange
    DiagnosticList diagnostics;
    // act, the default has no terminator, so it runs to the brace that ends the component (§11.2)
    const ManifestFile file = parse("native component C { in property<int> n: 1 + 2 }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
    // the default is kept whole, because the glue generator re-emits what the author wrote (§11.2)
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).default_text, "1+2");
}

TEST(ManifestParser, KeepsReadingAfterAClosedCallInsideADefault) {
    // arrange
    DiagnosticList diagnostics;
    // act, the call closes, so the terminator behind it ends the declaration rather than the closer (§11.2)
    const ManifestFile file = parse("native component C { in property<int> n: string(1) + 2; }", diagnostics);
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    // the whole default survived, because the depth let the reader step over the call (§11.2)
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).default_text, "string(1)+2");
}

TEST(ManifestParser, ReadsADefaultWhoseCallRunsToTheEndOfTheStream) {
    // arrange
    DiagnosticList diagnostics;
    // act, the call is never closed, so the value reader reaches the end of the stream (§11.2)
    const ManifestFile file = parse("native component C { in property<int> n: string(", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0303");
    // the property was still read as far as the stream allowed (§11.2)
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).default_text, "string(");
}

TEST(ManifestParser, ReportsAComponentThatEndsInsideABrokenMember) {
    // arrange
    DiagnosticList diagnostics;
    // act, the unit cannot start a member and nothing after it can either, so the reader reaches the end (§11.2)
    const ManifestFile file = parse("native component C { 42", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "C");
}

TEST(ManifestParser, SpansCoverTheConstructsTheyBelongTo) {
    // arrange, act
    const ManifestFile file = parse(CHART_MANIFEST);
    // assert
    const NativeComponent& component = file.components.at(0);
    // the component span runs from its keyword to its closing brace (§11.2)
    EXPECT_EQ(component.span.start.line, 2U);
    EXPECT_EQ(component.span.end.line, 10U);
    // the first property is declared on the third line (§11.2)
    EXPECT_EQ(component.properties.at(0).span.start.line, 3U);
    EXPECT_EQ(component.properties.at(1).span.start.line, 4U);
    // the signal and the semantic role follow on their own lines (§11.2)
    EXPECT_EQ(component.signals.at(0).span.start.line, 5U);
    EXPECT_EQ(component.semantic->span.start.line, 6U);
    // the preview override is the last line of the block (§11.2)
    EXPECT_EQ(component.preview_overrides.at(0).span.start.line, 8U);
}

TEST(ManifestParser, CommentsAndWhitespaceDoNotChangeTheDeclarations) {
    // arrange, act
    const ManifestFile file = parse("native component C { /* a */ in property<int> n; // a line\n }");
    // assert
    ASSERT_EQ(file.components.size(), 1U);
    ASSERT_EQ(file.components.at(0).properties.size(), 1U);
    EXPECT_EQ(file.components.at(0).properties.at(0).name, "n");
}

TEST(ManifestParser, ReadsARecordWithItsFields) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
    done: bool = false;
}
)";
    // act
    const ManifestFile file = parse(source);
    // assert
    ASSERT_EQ(file.records.size(), 1U);
    EXPECT_EQ(file.records.at(0).name, "Task");
    ASSERT_EQ(file.records.at(0).fields.size(), 3U);
}

TEST(ManifestParser, TheKeyFieldCarriesTheRecordsIdentity) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
}
)";
    // act
    const ManifestFile file = parse(source);
    // assert the key field is marked, because a row is keyed by it (§18.1, AD-13)
    ASSERT_EQ(file.records.at(0).fields.size(), 2U);
    EXPECT_TRUE(file.records.at(0).fields.at(0).is_key);
    EXPECT_FALSE(file.records.at(0).fields.at(1).is_key);
}

TEST(ManifestParser, ReadsARecordFieldWithADefault) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    done: bool = false;
}
)";
    // act
    const ManifestFile file = parse(source);
    // assert the default is kept as written, because the glue re-emits rather than reinterprets (§11.2)
    EXPECT_EQ(file.records.at(0).fields.at(1).default_value, "false");
}

TEST(ManifestParser, ARecordWithNoKeyFieldIsReported) {
    // arrange
    DiagnosticList diagnostics;
    const std::string_view source = R"(
record Task {
    title: string;
}
)";
    // act
    (void)parse(source, diagnostics);
    // assert the identity is explicit, so a record with none cannot be keyed (§18.1, AD-11)
    EXPECT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0308");
}

TEST(ManifestParser, ARecordAndAComponentCoexist) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
}

native component Gauge {
    in property<float> value;
}
)";
    // act
    const ManifestFile file = parse(source);
    // assert both declarations survive, because a manifest holds both kinds (§11.2, AD-17)
    EXPECT_EQ(file.records.size(), 1U);
    EXPECT_EQ(file.components.size(), 1U);
}
