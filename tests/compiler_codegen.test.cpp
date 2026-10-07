#include <gtest/gtest.h>

#include <filesystem>
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
    // scan, parse, check and generate a source, which is the pipeline the driver runs (§11.3)
    std::vector<GeneratedFile> generate(std::string_view source, DiagnosticList& diagnostics, ElementCatalog catalog) {
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const SourceFile file = Parser(tokens, diagnostics).parse();
        // the checker resolves it against the catalog, which is where the native types come from (§11.1, AD-17)
        const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
        // the generator turns the checked file into the facade files, which is what the driver writes (§11.3)
        return CodeGenerator(diagnostics).generate(checked);
    }

    // generate a source against the builtins, which is what most suites need (§11.3, §8)
    std::vector<GeneratedFile> generate(std::string_view source, DiagnosticList& diagnostics) {
        // the builtin catalog is what a ui file may use without a manifest (§11.3, §8)
        return generate(source, diagnostics, ElementCatalog::with_builtins());
    }

    // generate a source that is expected to be clean, which fails the test when it is not (§11.3)
    std::vector<GeneratedFile> generate(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.3)
        DiagnosticList diagnostics;
        // the file is generated, and the collected problems are checked before the files are returned (§11.3)
        std::vector<GeneratedFile> files = generate(source, diagnostics);
        // a clean source produces nothing, so a problem here is a generator defect (§11.3)
        EXPECT_EQ(diagnostics.count(), 0U);
        return files;
    }

    // the generated file with the given name, which a suite asserts its content against (§14.2)
    const GeneratedFile& file_named(const std::vector<GeneratedFile>& files, std::string_view name) {
        // the files are scanned, because a suite asks by the name it cares about (§14.2)
        for (const GeneratedFile& file : files) {
            // the name decides, so the lookup resolves to exactly one file (§14.2)
            if (file.path == name)
                return file;
        }
        // a missing file is a suite defect rather than a generator one (§14.2)
        ADD_FAILURE() << "no generated file named " << name;
        // a safe empty file is handed out, because the failure above already reported the miss (§14.2)
        static const GeneratedFile empty;
        return empty;
    }

    // report whether a generated text contains a fragment, which the suites assert on (§14.2)
    bool contains(std::string_view text, std::string_view fragment) {
        // the fragment decides, so a missing line fails the exact assertion (§14.2)
        return text.find(fragment) != std::string_view::npos;
    }

    // report whether any diagnostic carries the given code, because a problem may be reported in passes (§11.3)
    bool has_code(const DiagnosticList& diagnostics, std::string_view code) {
        // the problems are scanned, because a run reports everything it found (§11.3)
        for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
            // the code is the stable handle, so the assertion matches on it rather than the prose (§11.3)
            if (diagnostic.code == code)
                return true;
        }
        return false;
    }
} // namespace

TEST(Codegen, AReferenceComponentEmitsItsHeaderAndSource) {
    // arrange
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
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert
    const GeneratedFile& header = file_named(files, "main_view.h");
    // the provenance, the facade and the base class open the header (§14, AD-03a)
    EXPECT_TRUE(contains(header.content, "// generated from MainView.ui, do not edit (§14, AD-03a)"));
    EXPECT_TRUE(contains(header.content, "class MainView final : public Component"));
    // the constructor takes the engines, which the body reads to build the tree (§14.1)
    EXPECT_TRUE(contains(header.content, "MainView(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id = {});"));
    // the string property hands out a reference, which avoids a copy on the read path (§14.1)
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] const std::string& user_name() const;"));
    EXPECT_TRUE(contains(header.content, "void set_user_name(std::string value);"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] int click_count() const;"));
    EXPECT_TRUE(contains(header.content, "void set_click_count(int value);"));
    // the signal exposes a slot, which a host wires (§8.3)
    EXPECT_TRUE(contains(header.content, "void set_on_run_clicked(std::function<void()> handler);"));
    // the body stays private behind its owning pointer, which is the concrete facade (AD-03)
    EXPECT_TRUE(contains(header.content, "struct Impl;"));
    EXPECT_TRUE(contains(header.content, "std::unique_ptr<Impl> m_impl;"));
    // the implementation follows the header, which nothing else may include (§14.2)
    const GeneratedFile& source_file = file_named(files, "main_view.cpp");
    EXPECT_TRUE(contains(source_file.content, "#include \"main_view.h\""));
    // the layout and the widget headers reach the constructed classes (§11.3)
    EXPECT_TRUE(contains(source_file.content, "#include \"layout/column_layout.h\""));
    EXPECT_TRUE(contains(source_file.content, "#include \"layout/row_layout.h\""));
    EXPECT_TRUE(contains(source_file.content, "#include \"widgets/button.h\""));
    EXPECT_TRUE(contains(source_file.content, "#include \"widgets/label.h\""));
    // the properties initialise from their literals, which is what the facade presents at once (§7.1)
    EXPECT_TRUE(contains(source_file.content, "Property<std::string> m_user_name{std::string(\"World\")};"));
    EXPECT_TRUE(contains(source_file.content, "Property<int> m_click_count{0};"));
    // the signal slot and the bound sinks join the body, which the engine settles (§12)
    EXPECT_TRUE(contains(source_file.content, "std::function<void()> m_on_run_clicked;"));
    EXPECT_TRUE(contains(source_file.content, "Property<std::string> m_sink_1_text;"));
    EXPECT_TRUE(contains(source_file.content, "Property<std::string> m_sink_4_text;"));
    // the root is a column, which the labels and the row hang under (§8.1)
    EXPECT_TRUE(contains(source_file.content, "auto node_0 = std::make_unique<ColumnLayout>();"));
    EXPECT_TRUE(contains(source_file.content, "n0->set_gap(12.0f);"));
    EXPECT_TRUE(contains(source_file.content, "n0->set_padding(16.0f);"));
    // the first label reads the property through a sink, which recomputes when the source changes (§13.3)
    EXPECT_TRUE(contains(source_file.content, "auto node_1 = std::make_unique<Label>(engine, text_engine);"));
    EXPECT_TRUE(contains(source_file.content, "n1->set_font_size(18.0f);"));
    EXPECT_TRUE(contains(source_file.content, "const PropertyId deps_1_text[] = {m_user_name.id()};"));
    EXPECT_TRUE(contains(source_file.content, "m_sink_1_text.bind_static(&Impl::evaluate_1_text, this, deps_1_text);"));
    // the row and the button carry their constants, and the identity is applied (AD-11)
    EXPECT_TRUE(contains(source_file.content, "auto node_2 = std::make_unique<RowLayout>();"));
    EXPECT_TRUE(contains(source_file.content, "n2->set_gap(8.0f);"));
    EXPECT_TRUE(contains(source_file.content, "n3->set_explicit_id(std::string(\"action_button\"));"));
    EXPECT_TRUE(contains(source_file.content, "n3->set_label(std::string(\"Increment\"));"));
    // the click handler wires to the written body, which the lambda forwards without allocating (§11.3)
    EXPECT_TRUE(contains(source_file.content, "n3->set_on_click([this] { handle_3_clicked(this); });"));
    // the children join their parents in preorder, and the root is adopted last (§8.1)
    EXPECT_TRUE(contains(source_file.content, "n2->add_child(std::move(node_4));"));
    EXPECT_TRUE(contains(source_file.content, "owner->add_child(std::move(node_0));"));
    // the bound sink evaluators compute and push, which is the binding body (§13.3)
    EXPECT_TRUE(contains(source_file.content, "std::string MainView::Impl::evaluate_1_text(void* raw) {"));
    EXPECT_TRUE(contains(source_file.content, "const std::string value = ((std::string(\"Hello, \") + self->m_user_name.get()) + std::string(\"!\"));"));
    EXPECT_TRUE(contains(source_file.content, "self->m_node_1->set_text(value);"));
    // the handler writes the property and reports the signal, which is the written body (§11.3)
    EXPECT_TRUE(contains(source_file.content, "void MainView::Impl::handle_3_clicked(void* raw) {"));
    EXPECT_TRUE(contains(source_file.content, "self->m_click_count.set((self->m_click_count.get() + 1));"));
    EXPECT_TRUE(contains(source_file.content, "if (self->m_on_run_clicked)"));
    EXPECT_TRUE(contains(source_file.content, "self->m_on_run_clicked();"));
}

TEST(Codegen, WriteIfChangedLeavesAnIdenticalFileUntouched) {
    // arrange
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "albor-codegen-write-if-changed.txt";
    // a leftover from a failed run would skew the outcome, so it is removed first (§14.2)
    std::filesystem::remove(path);
    // act
    const bool first = write_if_changed(path, "one");
    // the same content is written again, which must be detected as unchanged (§14.2)
    const bool second = write_if_changed(path, "one");
    // the different content is written, which must be detected as a change (§14.2)
    const bool third = write_if_changed(path, "two");
    // the probe is removed, so the test leaves nothing behind (§14.2)
    std::filesystem::remove(path);
    // assert
    EXPECT_TRUE(first);
    EXPECT_FALSE(second);
    EXPECT_TRUE(third);
}

TEST(Codegen, ARepeatCannotBeGeneratedYet) {
    // arrange
    DiagnosticList diagnostics;
    // a repeat is the one tree form the compiler cannot generate yet, which §11.3 reserves (§11.3)
    const std::string_view source = R"(
component C {
    in property<vector<int>> items;
    Column {
        Repeat(item in items) {
            Label { text: string(item); }
        }
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0500"));
}

TEST(Codegen, APhysicalPixelLengthBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    // a physical length needs the surface density, which a compiled build does not carry yet (AD-27)
    const std::string_view source = "component C { Label { font_size: 20phx; } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0502"));
}

TEST(Codegen, ANativeElementBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    // the manifest supplies a native element, which the generated tree cannot construct (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the native element is declared in a manifest, so the checker lets it through (§11.2, AD-17)
    catalog.add_native(ElementType{.name = "Knob", .properties = {}, .signals = {}, .accepts_children = false});
    // the component uses the native element as its root (§11.2, AD-17)
    const std::string_view source = "component C { Knob {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics, catalog);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0501"));
}

TEST(Codegen, ANativePropertyTypeBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    // the snapshot carries a native record, which a compiled property cannot transport yet (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the native record is what the manifest declares, so the checker types it (§11.2, AD-17)
    catalog.add_native_data("WaveformData");
    // the component declares a property over the native record (§11.2, AD-17)
    const std::string_view source = "component C { in property<Snapshot<WaveformData>> data; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics, catalog);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0501"));
}

TEST(Codegen, AFileTheCheckerBlockedProducesNothing) {
    // arrange
    DiagnosticList diagnostics;
    // an unknown element never checks, so the generator must refuse the file untouched (§11.3)
    const std::string_view source = "component C { Missing {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    // the checker spoke, and the generator adds nothing of its own on top (§11.3)
    EXPECT_GE(diagnostics.count(), 1U);
}
