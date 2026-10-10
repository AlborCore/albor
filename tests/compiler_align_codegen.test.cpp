#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "compiler/codegen/codegen.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"

using namespace albor::compiler;

namespace
{
    // generate one component over the builtin catalogue, which is what a layout component needs (§11.3, §8)
    std::vector<GeneratedFile> generate(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.3)
        DiagnosticList diagnostics;
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const SourceFile file = Parser(tokens, diagnostics).parse();
        // the checker resolves the tree, which is where an alignment member becomes a type (§11.1, §8.2)
        const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
        // the generator turns the checked file into the facade files (§11.3)
        std::vector<GeneratedFile> files = CodeGenerator(diagnostics).generate(checked);
        // a clean source produces nothing, so a problem here is a generator defect (§11.3)
        EXPECT_EQ(diagnostics.count(), 0U);
        return files;
    }

    // the implementation unit of a generated component, which is where a setter call lands (§14.1)
    const GeneratedFile& implementation(const std::vector<GeneratedFile>& files) {
        // the file is looked up by its stem, which the generator names after the component (§14.2)
        for (const GeneratedFile& file : files) {
            // the extension decides, because the header and the unit share a stem (§14.2)
            if (file.path.size() > 4 && file.path.compare(file.path.size() - 4, 4, ".cpp") == 0)
                return file;
        }
        // a missing file is a suite defect rather than a generator one (§14.2)
        ADD_FAILURE() << "no generated implementation unit";
        // a safe empty file is handed out, because the failure above already reported the miss (§14.2)
        static const GeneratedFile empty;
        return empty;
    }

    // report whether the generated unit carries a fragment, which is what an emission assertion reads (§11.3)
    bool contains(std::string_view text, std::string_view fragment) {
        // the fragment decides, so a missing line fails the exact assertion (§14.2)
        return text.find(fragment) != std::string_view::npos;
    }
} // namespace

TEST(AlignCodegen, AWrittenAlignmentBecomesItsEnumerator) {
    // arrange a spacer, which is the idiom the toolbar pattern needs (§8.2)
    const std::vector<GeneratedFile> files = generate("component C { Row { Label { horizontal_align: Align.stretch; } } }");
    // act
    const std::string_view unit = implementation(files).content;
    // assert the written form became the enumerator the runtime declares (§11.3, §8.2)
    EXPECT_TRUE(contains(unit, "set_horizontal_align(albor::Align::Stretch)"));
}

TEST(AlignCodegen, BothAxesReachTheirOwnSetters) {
    // arrange
    const std::vector<GeneratedFile> files = generate("component C { Column { Label { horizontal_align: Align.end; vertical_align: Align.center; } } }");
    // act
    const std::string_view unit = implementation(files).content;
    // assert the axes are independent, because neither one is the parent's main axis (§8.2)
    EXPECT_TRUE(contains(unit, "set_horizontal_align(albor::Align::End)"));
    EXPECT_TRUE(contains(unit, "set_vertical_align(albor::Align::Center)"));
}

TEST(AlignCodegen, TheEnumeratorHeaderJoinsTheGeneratedSet) {
    // arrange
    const std::vector<GeneratedFile> files = generate("component C { Row { Label { horizontal_align: Align.center; } } }");
    // act
    const std::string_view unit = implementation(files).content;
    // assert the enumerator is declared by an included header rather than by a transitive one (§14.1, §8.2)
    EXPECT_TRUE(contains(unit, "#include \"core/align.h\""));
}

TEST(AlignCodegen, AComponentWithoutAlignmentDoesNotPullTheHeader) {
    // arrange a component that writes no alignment, which is every component before this change (§11.3)
    const std::vector<GeneratedFile> files = generate("component C { Column { Label { text: \"a\"; } } }");
    // act
    const std::string_view unit = implementation(files).content;
    // assert the include set only carries what the component reached (§14.1)
    EXPECT_FALSE(contains(unit, "#include \"core/align.h\""));
}

TEST(AlignCodegen, TheToolbarPatternCompilesToASpacerCall) {
    // arrange the three part toolbar: an action, a filler and trailing content (§8.2)
    const std::vector<GeneratedFile> files = generate(R"(
component Toolbar {
    Row {
        spacing: 4px;
        Button { id: run; text: "Run"; }
        Label { id: filler; horizontal_align: Align.stretch; }
        Button { id: stop; text: "Stop"; }
    }
}
)");
    // act
    const std::string_view unit = implementation(files).content;
    // assert only the filler stretched, which is what pushes the trailing button to the far end (§8.2)
    EXPECT_TRUE(contains(unit, "set_horizontal_align(albor::Align::Stretch)"));
    EXPECT_FALSE(contains(unit, "set_vertical_align"));
}
