#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"

using namespace albor::compiler;

namespace
{
    // the problems one source produced, which a test asserts on (§11.1)
    DiagnosticList diagnose(const std::string& source) {
        // the diagnostics are reported against, which every phase below fills (§11.1)
        DiagnosticList diagnostics;
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const SourceFile file = Parser(tokens, diagnostics).parse();
        // the builtins are the catalogue a plain layout component resolves against (§11.1, §8)
        ElementCatalog catalog = ElementCatalog::with_builtins();
        // the checker resolves the tree against the catalogue (§11.1)
        const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
        // the checked tree is not read here, because a diagnostic is what this helper is about (§11.1)
        (void)checked;
        return diagnostics;
    }

    // the first problem code one source produced, which is what a test asserts against (§11.1)
    std::string first_code(const std::string& source) {
        // the problems are read back, because a clean source has none to name (§11.1)
        const DiagnosticList diagnostics = diagnose(source);
        // an empty run reports nothing, which is a clean source rather than a test bug (§11.1)
        return diagnostics.diagnostics().empty() ? std::string{} : diagnostics.diagnostics().front().code;
    }

    // the first problem message one source produced, which is what an author reads (§11.1)
    std::string first_message(const std::string& source) {
        // the problems are read back, because a clean source has none to name (§11.1)
        const DiagnosticList diagnostics = diagnose(source);
        // an empty run reports nothing, which is a clean source rather than a test bug (§11.1)
        return diagnostics.diagnostics().empty() ? std::string{} : diagnostics.diagnostics().front().message;
    }
} // namespace

TEST(AlignTypeChecker, ALeafAcceptsBothAxes) {
    // assert a leaf arranges inside its parent, so both axes reach it (§8.2)
    EXPECT_TRUE(diagnose("component C { Column { Label { horizontal_align: Align.center; } } }").diagnostics().empty());
}

TEST(AlignTypeChecker, AContainerAcceptsBothAxesOnItsChildren) {
    // assert the container itself is also a child of something, so it accepts them too (§8.2)
    EXPECT_TRUE(diagnose("component C { Row { Column { vertical_align: Align.end; } } }").diagnostics().empty());
}

TEST(AlignTypeChecker, EveryAlignmentMemberIsAccepted) {
    // each written member is one a property of the alignment type resolves (§11.1, §8.2)
    for (const char* member : {"start", "center", "end", "stretch"})
        EXPECT_TRUE(diagnose(std::string("component C { Column { Label { horizontal_align: Align.") + member + "; } } }").diagnostics().empty());
}

TEST(AlignTypeChecker, AnUndeclaredMemberIsReported) {
    // assert
    EXPECT_EQ(first_code("component C { Column { Label { horizontal_align: Align.middle; } } }"), "E0416");
}

TEST(AlignTypeChecker, TheUndeclaredMemberMessageListsWhatTheSetCarries) {
    // arrange, act
    const std::string message = first_message("component C { Column { Label { horizontal_align: Align.middle; } } }");
    // assert the author is shown every name they may write, because the set is closed (§11.1)
    EXPECT_NE(message.find("Align.start"), std::string::npos);
    EXPECT_NE(message.find("Align.center"), std::string::npos);
    EXPECT_NE(message.find("Align.end"), std::string::npos);
    EXPECT_NE(message.find("Align.stretch"), std::string::npos);
}

TEST(AlignTypeChecker, AnIntegerIsRefused) {
    // assert nothing widens into the named set, because it is not a number (§11.1)
    EXPECT_FALSE(diagnose("component C { Column { Label { horizontal_align: 1; } } }").diagnostics().empty());
}

TEST(AlignTypeChecker, AStringIsRefused) {
    // assert the set is typed rather than a spelling convention (§11.1)
    EXPECT_FALSE(diagnose("component C { Column { Label { horizontal_align: \"center\"; } } }").diagnostics().empty());
}

TEST(AlignTypeChecker, AnUndeclaredNamespaceIsAnUnknownName) {
    // assert an unknown namespace still falls through to the property read, so the diagnostic is the old one (§11.1)
    EXPECT_EQ(first_code("component C { Column { Label { horizontal_align: Orientation.center; } } }"), "E0409");
}

TEST(AlignTypeChecker, TheAxesAreIndependent) {
    // assert both axes on one child resolve separately, because neither is the parent's main axis (§8.2)
    EXPECT_TRUE(diagnose("component C { Column { Label { horizontal_align: Align.end; vertical_align: Align.stretch; } } }").diagnostics().empty());
}
