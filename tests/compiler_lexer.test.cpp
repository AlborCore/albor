#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "compiler/lexer.h"

using namespace albor::compiler;

namespace
{
    std::vector<Token> scan(std::string_view source, DiagnosticList& diagnostics) {
        // the lexer is built over the literal, whose storage the caller's source view keeps alive (§11.1)
        return Lexer(source, diagnostics).tokenize();
    }

    std::vector<Token> scan(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.1)
        DiagnosticList diagnostics;
        // the units are scanned, and the collected problems are checked before they are returned (§11.1)
        std::vector<Token> tokens = scan(source, diagnostics);
        // a clean source produces nothing, so a problem here is a scanner defect (§11.1)
        EXPECT_EQ(diagnostics.count(), 0U);
        return tokens;
    }

    std::string spellings(const std::vector<Token>& tokens) {
        // the spellings are separated by a space, so a missing or doubled unit is visible (§11.1)
        std::string joined;
        for (const Token& token : tokens) {
            // the end unit has no spelling, so it contributes nothing at all (§11.1)
            if (token.kind == Token::Kind::End)
                break;
            // the separator is placed before each unit but the first (§11.1)
            if (!joined.empty())
                joined += ' ';
            joined += token.text;
        }
        return joined;
    }
} // namespace

TEST(Token, DefaultsToTheEndUnit) {
    // arrange
    const Token token;
    // assert
    EXPECT_EQ(token.kind, Token::Kind::End);
    EXPECT_TRUE(token.text.empty());
    EXPECT_TRUE(token.span.empty());
}

TEST(Token, IsKeywordOnlyForAnIdentifier) {
    // arrange
    const Token identifier{.kind = Token::Kind::Identifier, .text = "Repeat", .value = "Repeat", .span = SourceSpan{}};
    const Token literal{.kind = Token::Kind::String, .text = "Repeat", .value = "Repeat", .span = SourceSpan{}};
    const Token punctuation{.kind = Token::Kind::Punctuation, .text = "Repeat", .value = "Repeat", .span = SourceSpan{}};
    // assert
    EXPECT_TRUE(identifier.is("Repeat"));
    EXPECT_FALSE(identifier.is("Row"));
    EXPECT_FALSE(literal.is("Repeat"));
    EXPECT_FALSE(punctuation.is("Repeat"));
}

TEST(Token, IsPunctuationOnlyForPunctuation) {
    // arrange
    const Token punctuation{.kind = Token::Kind::Punctuation, .text = "==", .value = "==", .span = SourceSpan{}};
    const Token identifier{.kind = Token::Kind::Identifier, .text = "==", .value = "==", .span = SourceSpan{}};
    // assert
    EXPECT_TRUE(punctuation.is_punctuation("=="));
    EXPECT_FALSE(punctuation.is_punctuation("="));
    EXPECT_FALSE(identifier.is_punctuation("=="));
}

TEST(Lexer, EmptySourceIsOnlyTheEndUnit) {
    // arrange, act
    const std::vector<Token> tokens = scan("");
    // assert
    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::End);
    EXPECT_EQ(tokens.at(0).span.start.line, 1U);
    EXPECT_EQ(tokens.at(0).span.start.column, 1U);
}

TEST(Lexer, ReadsIdentifiersOverLettersDigitsAndUnderscores) {
    // arrange, act
    const std::vector<Token> tokens = scan("_row2 main_view9");
    // assert
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Identifier);
    EXPECT_EQ(tokens.at(0).text, "_row2");
    EXPECT_EQ(tokens.at(1).text, "main_view9");
    EXPECT_EQ(tokens.at(2).kind, Token::Kind::End);
}

TEST(Lexer, ReadsIntegerAndFloatLiterals) {
    // arrange, act
    const std::vector<Token> tokens = scan("42 3.5 1e3 2.5e-2 0xFF");
    // assert
    ASSERT_EQ(tokens.size(), 6U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(0).text, "42");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(1).text, "3.5");
    EXPECT_EQ(tokens.at(2).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(2).text, "1e3");
    EXPECT_EQ(tokens.at(3).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(3).text, "2.5e-2");
    EXPECT_EQ(tokens.at(4).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(4).text, "0xFF");
}

TEST(Lexer, ReadsAHexadecimalFloatWithItsFractionalDigits) {
    // arrange, act
    const std::vector<Token> tokens = scan("0xF.8");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(0).text, "0xF.8");
}

TEST(Lexer, ReadsADotThatIsNotAFloatAsPunctuation) {
    // arrange, act
    const std::vector<Token> tokens = scan("1.");
    // assert
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Punctuation);
    EXPECT_EQ(tokens.at(1).text, ".");
    EXPECT_EQ(tokens.at(2).kind, Token::Kind::End);
}

TEST(Lexer, ReadsLengthLiteralsWithTheirUnit) {
    // arrange, act
    const std::vector<Token> tokens = scan("12px 3.5phx");
    // assert
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Length);
    EXPECT_EQ(tokens.at(0).text, "12px");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Length);
    EXPECT_EQ(tokens.at(1).text, "3.5phx");
}

TEST(Lexer, ReadsAUppercaseHexadecimalPrefix) {
    // arrange, act
    const std::vector<Token> tokens = scan("0XFF");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(0).text, "0XFF");
}

TEST(Lexer, ReportsAHexadecimalLiteralWithNoDigits) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("0xG", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0107");
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(0).text, "0x");
    EXPECT_EQ(tokens.at(1).text, "G");
}

TEST(Lexer, ReadsAnUppercaseAndAPositiveExponent) {
    // arrange, act
    const std::vector<Token> tokens = scan("2E+3 1e-3");
    // assert
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(0).text, "2E+3");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(1).text, "1e-3");
}

TEST(Lexer, ReportsAnUnknownLengthUnit) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("12em", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0101");
    EXPECT_EQ(diagnostics.diagnostics().at(0).severity, Diagnostic::Severity::Error);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Length);
}

TEST(Lexer, ReadsStringLiteralsWithTheirEscapesApplied) {
    // arrange, act
    const std::vector<Token> tokens = scan(R"("a\nb\"c\\d\te")");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::String);
    EXPECT_EQ(tokens.at(0).value, "a\nb\"c\\d\te");
    EXPECT_EQ(tokens.at(0).text, "a\nb\"c\\d\te");
}

TEST(Lexer, ReportsAnUnterminatedStringLiteral) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("\"unfinished", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0103");
    EXPECT_EQ(diagnostics.diagnostics().at(0).severity, Diagnostic::Severity::Error);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::String);
    EXPECT_EQ(tokens.at(0).value, "unfinished");
}

TEST(Lexer, ReportsAStringLiteralBrokenByANewline) {
    // arrange
    DiagnosticList diagnostics;
    // act
    scan("\"unfinished\nnext", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0103");
}

TEST(Lexer, ReportsAnUnknownEscapeAsAWarning) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan(R"("a\qb")", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "W0102");
    EXPECT_EQ(diagnostics.diagnostics().at(0).severity, Diagnostic::Severity::Warning);
    EXPECT_FALSE(diagnostics.has_errors());
    EXPECT_EQ(tokens.at(0).value, "aqb");
}

TEST(Lexer, AcceptsAnEscapedNewlineInsideALiteral) {
    // arrange, act
    const std::vector<Token> tokens = scan(R"("a\nb")");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::String);
    EXPECT_EQ(tokens.at(0).value, "a\nb");
    EXPECT_EQ(tokens.at(0).span.end.line, 1U);
}

TEST(Lexer, ReportsABackslashAtTheEndOfABuffer) {
    // arrange
    DiagnosticList diagnostics;
    // act
    scan("\"trailing\\", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0103");
}

TEST(Lexer, ReadsColourLiteralsOfSixAndEightDigits) {
    // arrange, act
    const std::vector<Token> tokens = scan("#FF0000 #80FF0000");
    // assert
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Color);
    EXPECT_EQ(tokens.at(0).value, "FF0000");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Color);
    EXPECT_EQ(tokens.at(1).value, "80FF0000");
}

TEST(Lexer, ReportsAColourOfTheWrongLength) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("#FFF", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0104");
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Color);
}

TEST(Lexer, ReadsAMacroNameAsItsOwnUnit) {
    // arrange, act
    const std::vector<Token> tokens = scan("@tr(\"hello\")");
    // assert
    ASSERT_EQ(tokens.size(), 5U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Macro);
    EXPECT_EQ(tokens.at(0).text, "tr");
    EXPECT_EQ(tokens.at(0).value, "tr");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Punctuation);
    EXPECT_EQ(tokens.at(2).kind, Token::Kind::String);
    EXPECT_EQ(tokens.at(2).value, "hello");
}

TEST(Lexer, ReportsAMacroWithoutANameAndKeepsScanning) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("@ (\"later\")", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0106");
    ASSERT_GE(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Punctuation);
}

TEST(Lexer, TakesTheLongestOperatorThatMatches) {
    // arrange, act
    const std::vector<Token> tokens = scan("== != >= <= && || = < > ! + - * / % : . , ; ( ) [ ] { } $ & | ^ ~ ?");
    // assert
    EXPECT_EQ(spellings(tokens), "== != >= <= && || = < > ! + - * / % : . , ; ( ) [ ] { } $ & | ^ ~ ?");
}

TEST(Lexer, ReadsTheWhitespaceFormsTheSourceMayUse) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("\tone\r\ntwo", diagnostics);
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).text, "one");
    EXPECT_EQ(tokens.at(1).span.start.line, 2U);
    EXPECT_EQ(tokens.at(1).text, "two");
}

TEST(Lexer, ReadsALineCommentThatRunsToTheEndOfTheBuffer) {
    // arrange, act
    const std::vector<Token> tokens = scan("one // never closed");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).text, "one");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::End);
}

TEST(Lexer, ReadsABlockCommentWhoseStarIsNotItsTerminator) {
    // arrange, act
    const std::vector<Token> tokens = scan("/* a * b */ one");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).text, "one");
}

TEST(Lexer, ReadsAnIdentifierThatEndsInADigitAndInAnUnderscore) {
    // arrange, act
    const std::vector<Token> tokens = scan("a1 _b_");
    // assert
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).text, "a1");
    EXPECT_EQ(tokens.at(1).text, "_b_");
}

TEST(Lexer, ReadsAMacroNameThatCarriesDigitsAndAnUnderscore) {
    // arrange, act
    const std::vector<Token> tokens = scan("@tr_key2");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Macro);
    EXPECT_EQ(tokens.at(0).text, "tr_key2");
}

TEST(Lexer, ReadsALetterAfterAZeroThatIsNotAHexadecimalPrefixAsAUnitSuffix) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("0y1", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0101");
    // the suffix stops at the digit, because a unit carries letters only (§11.1, AD-27)
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Length);
    EXPECT_EQ(tokens.at(0).text, "0y");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(1).text, "1");
}

TEST(Lexer, ReadsAMacroNameAtTheEndOfTheBuffer) {
    // arrange, act
    const std::vector<Token> tokens = scan("@tr");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Macro);
    EXPECT_EQ(tokens.at(0).text, "tr");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::End);
}

TEST(Lexer, ReadsALowercaseHexadecimalPrefixThatEndsAtTheBuffer) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("0x", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0107");
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(0).text, "0x");
}

TEST(Lexer, StopsAHexadecimalRunAtANonHexLetterAndReportsItAsAUnit) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("0x1G", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0101");
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Length);
    EXPECT_EQ(tokens.at(0).text, "0x1G");
}

TEST(Lexer, ReadsASecondDotAsItsOwnPunctuation) {
    // arrange, act
    const std::vector<Token> tokens = scan("1.2.3");
    // assert
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(0).text, "1.2");
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::Punctuation);
    EXPECT_EQ(tokens.at(1).text, ".");
    EXPECT_EQ(tokens.at(2).kind, Token::Kind::Integer);
    EXPECT_EQ(tokens.at(2).text, "3");
}

TEST(Lexer, ReadsAnExponentSignThatIsNotFollowedByDigitsAsAUnitSuffix) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("1e+x 2E3", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0101");
    ASSERT_EQ(tokens.size(), 5U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::Length);
    EXPECT_EQ(tokens.at(0).text, "1e");
    EXPECT_EQ(tokens.at(1).text, "+");
    EXPECT_EQ(tokens.at(2).text, "x");
    EXPECT_EQ(tokens.at(3).kind, Token::Kind::Float);
    EXPECT_EQ(tokens.at(3).text, "2E3");
}

TEST(Lexer, ReportsAnUnexpectedCharacterAndKeepsScanning) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("a ` b", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0105");
    EXPECT_EQ(diagnostics.diagnostics().at(0).severity, Diagnostic::Severity::Error);
    EXPECT_EQ(spellings(tokens), "a b");
}

TEST(Lexer, SkipsWhitespaceAndLineComments) {
    // arrange, act
    const std::vector<Token> tokens = scan("  \t one // trailing\n two");
    // assert
    EXPECT_EQ(spellings(tokens), "one two");
}

TEST(Lexer, SkipsBlockCommentsAcrossLines) {
    // arrange, act
    const std::vector<Token> tokens = scan("/* first\nsecond */ one");
    // assert
    EXPECT_EQ(spellings(tokens), "one");
    // the comment spanned a newline, so the unit after it is on the second line (§11.1)
    EXPECT_EQ(tokens.at(0).span.start.line, 2U);
}

TEST(Lexer, ReportsAnUnterminatedBlockComment) {
    // arrange
    DiagnosticList diagnostics;
    // act
    scan("/* never closed", diagnostics);
    // assert
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0100");
}

TEST(Lexer, TracksLineAndColumnAcrossNewlines) {
    // arrange, act
    const std::vector<Token> tokens = scan("one\n  two\nthree");
    // assert
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens.at(0).span.start.line, 1U);
    EXPECT_EQ(tokens.at(0).span.start.column, 1U);
    EXPECT_EQ(tokens.at(0).span.end.column, 4U);
    EXPECT_EQ(tokens.at(1).span.start.line, 2U);
    EXPECT_EQ(tokens.at(1).span.start.column, 3U);
    EXPECT_EQ(tokens.at(2).span.start.line, 3U);
    EXPECT_EQ(tokens.at(2).span.start.column, 1U);
    EXPECT_EQ(tokens.at(3).span.start.line, 3U);
    EXPECT_EQ(tokens.at(3).span.start.column, 6U);
}

TEST(Lexer, SpanCoversTheWholeUnit) {
    // arrange, act
    const std::vector<Token> tokens = scan("Repeat");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(0).span.start.column, 1U);
    EXPECT_EQ(tokens.at(0).span.end.column, 7U);
    EXPECT_FALSE(tokens.at(0).span.empty());
}

TEST(Lexer, ReportsThePositionOfTheEndUnitAfterTrivia) {
    // arrange, act
    const std::vector<Token> tokens = scan("one\n  // comment\n");
    // assert
    ASSERT_EQ(tokens.size(), 2U);
    EXPECT_EQ(tokens.at(1).kind, Token::Kind::End);
    EXPECT_EQ(tokens.at(1).span.start.line, 3U);
    EXPECT_EQ(tokens.at(1).span.start.column, 1U);
}

TEST(Lexer, ScanningAnEmptyBufferDoesNotReadPastIt) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = scan("", diagnostics);
    // assert
    EXPECT_EQ(diagnostics.count(), 0U);
    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens.at(0).kind, Token::Kind::End);
}
