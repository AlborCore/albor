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

TEST(ManifestGlue, TheScalarPropertiesCoverTheBoolAndTextSpellings) {
    // arrange
    constexpr std::string_view manifest = R"(
native component ToggleView {
    in property<bool> enabled: true;
    in property<string> label: "x";
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/toggle_view.h");
    EXPECT_TRUE(contains(header.content, "virtual void set_enabled(bool value) = 0;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] virtual bool enabled() const = 0;"));
    EXPECT_TRUE(contains(header.content, "virtual void set_label(std::string value) = 0;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] virtual std::string label() const = 0;"));
}

TEST(ManifestGlue, ANestedOrEmptySnapshotTypeBlocksTheOutput) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    in property<Snapshot<>> empty_data;
}
native component ChartView {
    in property<Snapshot<Series<Point>>> nested_data;
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<GeneratedFile> files = generate(manifest, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0305"));
}

TEST(ManifestGlue, SignalEmittersSeparateWhenAComponentHasSeveral) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    signal picked(float x);
    signal zoomed(float scale);
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/waveform_view.h");
    EXPECT_TRUE(contains(header.content, "std::function<void(float)> m_on_picked;\n\n        // the slot behind the zoomed signal"));
    const GeneratedFile& source = file_named(files, "chart/waveform_view.cpp");
    EXPECT_TRUE(contains(source.content, "m_on_zoomed(scale);"));
}

TEST(ManifestGlue, ASignalWithSeveralParametersJoinsItsSignature) {
    // arrange
    constexpr std::string_view manifest = R"(
native component WaveformView {
    signal moved(float x, float y, color tint);
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/waveform_view.h");
    EXPECT_TRUE(contains(header.content, "void set_on_moved(std::function<void(float, float, Color)> handler);"));
    EXPECT_TRUE(contains(header.content, "void emit_moved(float x, float y, Color tint);"));
    EXPECT_TRUE(contains(header.content, "std::function<void(float, float, Color)> m_on_moved;"));
    const GeneratedFile& source = file_named(files, "chart/waveform_view.cpp");
    EXPECT_TRUE(contains(source.content, "m_on_moved(x, y, tint);"));
}

TEST(ManifestGlue, APreviewBlockEmitsTheDesignTimeStub) {
    // arrange
    constexpr std::string_view manifest = R"(
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
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/waveform_view_preview.h");
    EXPECT_TRUE(contains(header.content, "class WaveformView_Preview final : public WaveformView"));
    EXPECT_TRUE(contains(header.content, "explicit WaveformView_Preview(BindingEngine* engine, std::string explicit_id = {});"));
    EXPECT_TRUE(contains(header.content, "void set_line_color(Color value) override;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] Color line_color() const override;"));
    EXPECT_TRUE(contains(header.content, "void set_data(std::shared_ptr<const WaveformData> value) override;"));
    EXPECT_TRUE(contains(header.content, "Color m_line_color{Color::rgb(1.0f, 0.0f, 0.0f)};"));
    EXPECT_TRUE(contains(header.content, "std::shared_ptr<const WaveformData> m_data{};"));
    const GeneratedFile& source = file_named(files, "chart/waveform_view_preview.cpp");
    EXPECT_TRUE(contains(source.content, "WaveformView_Preview::WaveformView_Preview(BindingEngine* engine, std::string explicit_id) :"));
    EXPECT_TRUE(contains(source.content, "WaveformView(engine, std::move(explicit_id)) {}"));
    EXPECT_TRUE(contains(source.content, "void WaveformView_Preview::set_line_color(Color value) {"));
    EXPECT_TRUE(contains(source.content, "m_line_color = value;"));
    EXPECT_TRUE(contains(source.content, "Color WaveformView_Preview::line_color() const {"));
    EXPECT_TRUE(contains(source.content, "return m_line_color;"));
}

TEST(ManifestGlue, AStubMemberFallsBackToTheDeclaredDefault) {
    // arrange
    constexpr std::string_view manifest = R"(
native component GaugeView {
    in property<float> level: 0.5;
    in property<bool> enabled: true;
    in property<string> caption: "ready";
    in property<int> ticks;
    in property<color> tint: #0000FF;
    in property<color> wash: #80FFFFFF;
    preview {
        level: 0.75;
        tint: broken;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/gauge_view_preview.h");
    EXPECT_TRUE(contains(header.content, "float m_level{0.75};"));
    EXPECT_TRUE(contains(header.content, "bool m_enabled{true};"));
    EXPECT_TRUE(contains(header.content, "std::string m_caption{\"ready\"};"));
    EXPECT_TRUE(contains(header.content, "int m_ticks{};"));
    EXPECT_TRUE(contains(header.content, "Color m_tint{Color{}};"));
    EXPECT_TRUE(contains(header.content, "Color m_wash{Color::rgba(0.501960784f, 1.0f, 1.0f, 1.0f)};"));
    EXPECT_FALSE(contains(header.content, "0.5};"));
}

TEST(ManifestGlue, AnOverrideNamingAnUndeclaredPropertyBlocksTheOutput) {
    // arrange
    constexpr std::string_view manifest = R"(
native component GaugeView {
    in property<float> level: 0.5;
    preview {
        missing: 1;
    }
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<GeneratedFile> files = generate(manifest, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0307"));
}

TEST(ManifestGlue, AStubMemberEscapesItsTextValue) {
    // arrange
    std::string manifest = std::string(R"(native component GaugeView {
    in property<string> caption: "a\\b\"c\n\td)");
    manifest += std::string("\x01");
    manifest += R"(";
    in property<string> note: "z";
    preview {
        note: "e\\f\"g\n\th)";
    manifest += std::string("\x02");
    manifest += R"(";
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(manifest);
    // assert
    const GeneratedFile& header = file_named(files, "chart/gauge_view_preview.h");
    EXPECT_TRUE(contains(header.content, "std::string m_caption{\"a\\\\b\\\"c\\n\\td\\x01\"};"));
    EXPECT_TRUE(contains(header.content, "std::string m_note{\"e\\\\f\\\"g\\n\\th\\x02\"};"));
    EXPECT_FALSE(contains(header.content, "m_note{\"z\"};"));
}

TEST(ManifestGlue, AMalformedColourSpellingFallsBackToTheDefaultColour) {
    // arrange
    constexpr std::string_view manifest = R"(
native component GaugeView {
    in property<color> tint: #0000FF;
    preview {
        tint: #FF00;
    }
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<GeneratedFile> files = generate(manifest, diagnostics);
    // assert
    const GeneratedFile& header = file_named(files, "chart/gauge_view_preview.h");
    EXPECT_TRUE(contains(header.content, "Color m_tint{Color{}};"));
}

TEST(RecordGlue, ARecordBecomesAPlainStruct) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
    done: bool = false;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert
    ASSERT_EQ(files.size(), 1U);
    EXPECT_EQ(files.at(0).path, "chart/task.h");
    EXPECT_NE(files.at(0).content.find("struct Task"), std::string::npos);
}

TEST(RecordGlue, ARecordFieldBecomesAPlainMember) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
    done: bool = false;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the members carry their declared types, which is what a binding reads (§11.2)
    EXPECT_NE(files.at(0).content.find("std::string key;"), std::string::npos);
    EXPECT_NE(files.at(0).content.find("std::string title;"), std::string::npos);
}

TEST(RecordGlue, ADeclaredDefaultIsReEmittedOnTheMember) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    done: bool = false;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the default survives, because the member is aggregate initialized (§11.2)
    EXPECT_NE(files.at(0).content.find("bool done = false;"), std::string::npos);
}

TEST(RecordGlue, AColourFieldPullsThePaintHeader) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    tint: color;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the framework colour type lives in the paint header, which the struct must include (AD-05)
    EXPECT_NE(files.at(0).content.find("#include \"core/paint.h\""), std::string::npos);
    EXPECT_NE(files.at(0).content.find("Color tint;"), std::string::npos);
}

TEST(RecordGlue, TheKeyFieldIsNamedAsCarryingIdentity) {
    // arrange
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the key is called out, because a row is keyed by it and identity is explicit (§18.1, AD-11)
    EXPECT_NE(files.at(0).content.find("which carries the identity a row is keyed by"), std::string::npos);
}

TEST(RecordGlue, AnUntranslatableFieldTypeBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    const std::string_view source = R"(
record Task {
    key: string;
    odd: Texture;
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert the output is empty, because a struct with a missing member would not compile (§11.2)
    EXPECT_TRUE(files.empty());
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_EQ(diagnostics.diagnostics().at(0).code, "E0309");
}

TEST(RecordGlue, ARecordAndAComponentBothEmit) {
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
    const std::vector<GeneratedFile> files = generate(source);
    // assert both are emitted, because a manifest declares both kinds (§11.2, AD-17)
    EXPECT_EQ(files.size(), 3U);
}

TEST(RecordRegistration, ARecordReachesTheCompilerCatalog) {
    // arrange the manifest a task record is declared in, because the manifest is the single source (§11.2, AD-17)
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
    done: bool = false;
}
)";
    DiagnosticList diagnostics;
    const ManifestFile file = ManifestParser(Lexer(source, diagnostics).tokenize(), diagnostics).parse("chart");
    // the catalog starts empty of records, because registration is what puts one there (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    register_records(file, catalog);
    // assert the record resolved by name, which is what makes vector<Task> a known type (§11.2, AD-17)
    const RecordType* record = catalog.find_record("Task");
    ASSERT_NE(record, nullptr);
    // assert every declared field reached the catalog in declaration order (§11.2, AD-17)
    ASSERT_EQ(record->fields.size(), 3U);
    EXPECT_EQ(record->fields.at(0).name, "key");
    EXPECT_EQ(record->fields.at(2).name, "done");
}

TEST(RecordRegistration, AFieldKeepsTheTypeTheManifestWrote) {
    // arrange a record whose fields have different types, because each must survive the bridge (§11.2, AD-17)
    const std::string_view source = R"(
record Task {
    key: string;
    retries: int;
    ratio: float;
    done: bool = false;
}
)";
    DiagnosticList diagnostics;
    const ManifestFile file = ManifestParser(Lexer(source, diagnostics).tokenize(), diagnostics).parse("chart");
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    register_records(file, catalog);
    // assert each written type became the type a field read produces (§11.2, AD-17)
    const RecordType* record = catalog.find_record("Task");
    ASSERT_NE(record, nullptr);
    EXPECT_TRUE(record->find_field("key")->type.is(TypeKind::String));
    EXPECT_TRUE(record->find_field("retries")->type.is(TypeKind::Int));
    EXPECT_TRUE(record->find_field("ratio")->type.is(TypeKind::Float));
    EXPECT_TRUE(record->find_field("done")->type.is(TypeKind::Bool));
}

TEST(RecordRegistration, TheKeyFieldCarriesTheIdentity) {
    // arrange a record whose key the manifest flagged, which is what a row is keyed by (§18.1, AD-13)
    const std::string_view source = R"(
record Task {
    key: string;
    title: string;
}
)";
    DiagnosticList diagnostics;
    const ManifestFile file = ManifestParser(Lexer(source, diagnostics).tokenize(), diagnostics).parse("chart");
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    register_records(file, catalog);
    // assert exactly one field carries the identity, and it is the one the manifest marked (§18.1, AD-11, AD-13)
    const RecordType* record = catalog.find_record("Task");
    ASSERT_NE(record, nullptr);
    ASSERT_NE(record->key_field(), nullptr);
    EXPECT_EQ(record->key_field()->name, "key");
    EXPECT_FALSE(record->find_field("title")->is_key);
}
