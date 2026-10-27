#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "compiler/diagnostic.h"

using namespace albor::compiler;

TEST(SourceSpan, EmptyWhenStartEqualsEnd) {
    // arrange
    const SourceLocation origin{.line = 3, .column = 7};
    // act
    const SourceSpan span{.start = origin, .end = origin};
    // assert
    EXPECT_TRUE(span.empty());
}

TEST(SourceSpan, NotEmptyWhenEndFollowsStart) {
    // arrange
    const SourceSpan on_one_line{.start = SourceLocation{.line = 3, .column = 7}, .end = SourceLocation{.line = 3, .column = 12}};
    const SourceSpan across_lines{.start = SourceLocation{.line = 3, .column = 7}, .end = SourceLocation{.line = 4, .column = 1}};
    // assert
    EXPECT_FALSE(on_one_line.empty());
    EXPECT_FALSE(across_lines.empty());
}

TEST(DiagnosticList, ReportsEveryProblemInOrder) {
    // arrange
    DiagnosticList diagnostics;
    const SourceSpan first{.start = SourceLocation{.line = 1, .column = 1}, .end = SourceLocation{.line = 1, .column = 2}};
    const SourceSpan second{.start = SourceLocation{.line = 2, .column = 1}, .end = SourceLocation{.line = 2, .column = 2}};
    // act
    diagnostics.add(Diagnostic::Severity::Warning, first, "W0100", "first problem");
    diagnostics.add(Diagnostic::Severity::Error, second, "E0100", "second problem");
    // assert
    ASSERT_EQ(diagnostics.count(), 2U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "W0100");
    EXPECT_EQ(diagnostics.diagnostics().at(1).code, "E0100");
}

TEST(DiagnosticList, SeverityFieldsAreCopied) {
    // arrange
    DiagnosticList diagnostics;
    Diagnostic assembled;
    assembled.severity = Diagnostic::Severity::Error;
    assembled.span = SourceSpan{.start = SourceLocation{.line = 4, .column = 2}, .end = SourceLocation{.line = 4, .column = 9}};
    assembled.code = "E0200";
    assembled.message = "assembled problem";
    // act
    diagnostics.add(std::move(assembled));
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diagnostics.diagnostics().at(0).span.start.line, 4U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0200");
    EXPECT_EQ(diagnostics.diagnostics().at(0).message, "assembled problem");
}

TEST(DiagnosticList, HasErrorsOnlyWhenAnErrorIsPresent) {
    // arrange
    DiagnosticList warnings_only;
    DiagnosticList with_error;
    DiagnosticList empty;
    // act
    warnings_only.add(Diagnostic::Severity::Warning, SourceSpan{}, "W0100", "only a warning");
    with_error.add(Diagnostic::Severity::Error, SourceSpan{}, "E0100", "an error");
    // assert
    EXPECT_FALSE(empty.has_errors());
    EXPECT_FALSE(warnings_only.has_errors());
    EXPECT_TRUE(with_error.has_errors());
}

TEST(DiagnosticList, FiltersBySeverity) {
    // arrange
    DiagnosticList diagnostics;
    // act
    diagnostics.add(Diagnostic::Severity::Warning, SourceSpan{}, "W0100", "first warning");
    diagnostics.add(Diagnostic::Severity::Error, SourceSpan{}, "E0100", "only error");
    diagnostics.add(Diagnostic::Severity::Warning, SourceSpan{}, "W0101", "second warning");
    // assert
    const std::vector<Diagnostic> warnings = diagnostics.of_severity(Diagnostic::Severity::Warning);
    const std::vector<Diagnostic> errors = diagnostics.of_severity(Diagnostic::Severity::Error);
    ASSERT_EQ(warnings.size(), 2U);
    EXPECT_EQ(warnings.at(0).code, "W0100");
    EXPECT_EQ(warnings.at(1).code, "W0101");
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.at(0).code, "E0100");
}

TEST(DiagnosticList, ClearDropsEveryProblem) {
    // arrange
    DiagnosticList diagnostics;
    diagnostics.add(Diagnostic::Severity::Error, SourceSpan{}, "E0100", "a problem");
    // act
    diagnostics.clear();
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    EXPECT_TRUE(diagnostics.diagnostics().empty());
    EXPECT_FALSE(diagnostics.has_errors());
}

TEST(Format, NamesTheSeverityCodePositionAndMessage) {
    // arrange
    Diagnostic error;
    error.severity = Diagnostic::Severity::Error;
    error.span = SourceSpan{.start = SourceLocation{.line = 12, .column = 5}, .end = SourceLocation{.line = 12, .column = 8}};
    error.code = "E0105";
    error.message = "unexpected character";
    // act
    const std::string text = format(error, "views/MainView.ui");
    // assert
    EXPECT_EQ(text, "error: E0105: views/MainView.ui:12:5: unexpected character");
}

TEST(Format, AnImportedDiagnosticNamesItsOwnFile) {
    // arrange a diagnostic the loader produced while reading an imported file (§11.1)
    Diagnostic error;
    error.severity = Diagnostic::Severity::Error;
    error.span = SourceSpan{.start = SourceLocation{.line = 3, .column = 2}, .end = SourceLocation{.line = 3, .column = 5}};
    error.code = "E0403";
    error.message = "unknown element";
    error.path = "panels/side_panel.ui";
    // act the driver formats it with its root input path (§11.1)
    const std::string text = format(error, "views/MainView.ui");
    // assert the diagnostic names the file it came from, not the root the driver opened (§11.1)
    EXPECT_EQ(text, "error: E0403: panels/side_panel.ui:3:2: unknown element");
}

TEST(Format, WarningIsNamedAsAWarning) {
    // arrange
    Diagnostic warning;
    warning.severity = Diagnostic::Severity::Warning;
    warning.span = SourceSpan{.start = SourceLocation{.line = 1, .column = 1}, .end = SourceLocation{.line = 1, .column = 1}};
    warning.code = "W0102";
    warning.message = "unknown escape";
    // act
    const std::string text = format(warning, "views/MainView.ui");
    // assert
    EXPECT_EQ(text, "warning: W0102: views/MainView.ui:1:1: unknown escape");
}
