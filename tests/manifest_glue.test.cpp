#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "compiler/lexer.h"
#include "manifest/glue.h"
#include "manifest/manifest.h"

using namespace albor::manifest;
using namespace albor::compiler;

namespace
{
    // scan, parse and glue a manifest, which is the pipeline the driver runs (§11.2, AD-17)
    std::vector<GeneratedFile> generate(std::string_view source, DiagnosticList& diagnostics) {
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser then builds the declared native types from those units (§11.2)
        const ManifestFile file = ManifestParser(tokens, diagnostics).parse("chart");
        // the generator turns the manifest into the facade files, which is what the driver writes (§11.2)
        return GlueGenerator(diagnostics).generate(file);
    }

    // generate a manifest that is expected to be clean, which fails the test when it is not (§11.2)
    std::vector<GeneratedFile> generate(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.2)
        DiagnosticList diagnostics;
        // the facade is generated once, so the check below sees the whole run (§11.2)
        std::vector<GeneratedFile> files = generate(source, diagnostics);
        // a clean source produces nothing, so a problem here is a generator defect (§11.2)
        EXPECT_EQ(diagnostics.count(), 0U);
        return files;
    }

    // the generated file with the given name, which a suite asserts its content against (§11.2)
    const GeneratedFile& file_named(const std::vector<GeneratedFile>& files, std::string_view name) {
        // the files are scanned, because a suite asks by the name it cares about (§11.2)
        for (const GeneratedFile& file : files) {
            // the name decides, so the lookup resolves to exactly one file (§11.2)
            if (file.path == name)
                return file;
        }
        // a missing file is a suite defect rather than a generator one (§11.2)
        ADD_FAILURE() << "no generated file named " << name;
        // a safe empty file is handed out, because the failure above already reported the miss (§11.2)
        static const GeneratedFile empty;
        return empty;
    }

    // report whether a generated text contains a fragment, which the suites assert on (§11.2)
    bool contains(std::string_view text, std::string_view fragment) {
        // the fragment decides, so a missing line fails the exact assertion (§11.2)
        return text.find(fragment) != std::string_view::npos;
    }

    // report whether any diagnostic carries the given code, because a problem may be reported in passes (§11.2)
    bool has_code(const DiagnosticList& diagnostics, std::string_view code) {
        // the problems are scanned, because a run reports everything it found (§11.2)
        for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
            // the code is the stable handle, so the assertion matches on it rather than the prose (§11.2)
            if (diagnostic.code == code)
                return true;
        }
        return false;
    }

    // the example manifest from §11.2, which the suites glue as a whole (§11.2)
    constexpr std::string_view CHART_MANIFEST = R"(
native component WaveformView {
    in property<Snapshot<WaveformData>> data;
    in property<color> line_color: #00FF00;
    signal selection_changed(float time_pos);
    semantic role: "chart";
}
)";
} // namespace

TEST(ManifestGlue, AReferenceManifestEmitsItsFacadeHeader) {
    // arrange, act
    const std::vector<GeneratedFile> files = generate(CHART_MANIFEST);
    // assert
    const GeneratedFile& header = file_named(files, "chart/waveform_view.h");
    EXPECT_TRUE(contains(header.content, "class WaveformView : public Widget"));
    EXPECT_TRUE(contains(header.content, "explicit WaveformView(BindingEngine* engine, std::string explicit_id = {});"));
    EXPECT_TRUE(contains(header.content, "virtual void set_line_color(Color value) = 0;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] virtual Color line_color() const = 0;"));
    EXPECT_TRUE(contains(header.content, "virtual void set_data(std::shared_ptr<const WaveformData> value) = 0;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] virtual std::shared_ptr<const WaveformData> data() const = 0;"));
    EXPECT_TRUE(contains(header.content, "void set_on_selection_changed(std::function<void(float)> handler);"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] std::string_view declared_role() const;"));
    EXPECT_TRUE(contains(header.content, "void emit_selection_changed(float time_pos);"));
    EXPECT_TRUE(contains(header.content, "std::function<void(float)> m_on_selection_changed;"));
    EXPECT_TRUE(contains(header.content, "#include <functional>"));
    EXPECT_TRUE(contains(header.content, "#include <memory>"));
    EXPECT_TRUE(contains(header.content, "#include <string_view>"));
    EXPECT_TRUE(contains(header.content, "#include \"core/paint.h\""));
    EXPECT_TRUE(contains(header.content, "#include \"ui/component.h\""));
    EXPECT_TRUE(contains(header.content, "#include \"widgets/widget.h\""));
}

TEST(ManifestGlue, AReferenceManifestEmitsItsImplementationSource) {
    // arrange, act
    const std::vector<GeneratedFile> files = generate(CHART_MANIFEST);
    // assert
    const GeneratedFile& source = file_named(files, "chart/waveform_view.cpp");
    EXPECT_TRUE(contains(source.content, "#include \"chart/waveform_view.h\""));
    EXPECT_TRUE(contains(source.content, "Widget(engine, std::move(explicit_id)) {}"));
    EXPECT_TRUE(contains(source.content, "WaveformView::~WaveformView() = default;"));
    EXPECT_TRUE(contains(source.content, "m_on_selection_changed = std::move(handler);"));
    EXPECT_TRUE(contains(source.content, "if (m_on_selection_changed)"));
    EXPECT_TRUE(contains(source.content, "m_on_selection_changed(time_pos);"));
    EXPECT_TRUE(contains(source.content, "return \"chart\";"));
}

TEST(ManifestGlue, AComponentWithoutASemanticRoleOmitsTheRole) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    in property<color> line_color: #00FF00;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/waveform_view.h");
    const GeneratedFile& source = file_named(files, "chart/waveform_view.cpp");
    EXPECT_FALSE(contains(header.content, "declared_role"));
    EXPECT_TRUE(contains(header.content, "set_line_color(Color value)"));
    EXPECT_FALSE(contains(source.content, "declared_role"));
    EXPECT_FALSE(contains(header.content, "#include <string_view>"));
}

TEST(ManifestGlue, AnInOutPropertyStillEmitsTheAccessorPair) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    inout property<int> track_count: 0;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/waveform_view.h");
    EXPECT_TRUE(contains(header.content, "virtual void set_track_count(int value) = 0;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] virtual int track_count() const = 0;"));
}

TEST(ManifestGlue, APropertyWithAnUnknownTypeBlocksTheOutput) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    in property<Table> grid;
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<GeneratedFile> files = generate(manifest, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0305"));
}

TEST(ManifestGlue, ASignalWithAnUnknownParameterTypeBlocksTheOutput) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    signal selected(Table row);
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<GeneratedFile> files = generate(manifest, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0306"));
}
