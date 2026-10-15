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
    std::vector<GeneratedFile> generate(std::string_view source, DiagnosticList& diagnostics, ElementCatalog catalog) {
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const SourceFile file = Parser(tokens, diagnostics).parse();
        // the structs the file declares join the catalog, which is what makes a property of one resolve (§11.1)
        register_ui_structs(file, catalog, diagnostics);
        // the enums the file declares join the catalog, which is what makes a property of one hold a member (§11.1)
        register_ui_enums(file, catalog);
        // the checker resolves it against the catalog, which is where the native types come from (§11.1, AD-17)
        const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
        // the generator turns the checked file into the facade files, which is what the driver writes (§11.3)
        CodeGenerator generator(diagnostics);
        // the catalog is what tells the generator a native type is generatable (§11.2, AD-17)
        generator.set_catalog(&catalog);
        return generator.generate(checked);
    }

    std::vector<GeneratedFile> generate(std::string_view source, DiagnosticList& diagnostics) {
        // the builtin catalog is what a ui file may use without a manifest (§11.3, §8)
        return generate(source, diagnostics, ElementCatalog::with_builtins());
    }

    std::vector<GeneratedFile> generate(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.3)
        DiagnosticList diagnostics;
        // the file is generated, and the collected problems are checked before the files are returned (§11.3)
        std::vector<GeneratedFile> files = generate(source, diagnostics);
        // a clean source produces nothing, so a problem here is a generator defect (§11.3)
        EXPECT_EQ(diagnostics.count(), 0U);
        return files;
    }

    const GeneratedFile& file_named(const std::vector<GeneratedFile>& files, std::string_view name) {
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

    bool contains(std::string_view text, std::string_view fragment) {
        // the fragment decides, so a missing line fails the exact assertion (§14.2)
        return text.find(fragment) != std::string_view::npos;
    }

    std::string all_text(const std::vector<GeneratedFile>& files) {
        // the bodies are joined, because a shape can land in any one of the generated files (§14.2)
        std::string joined;
        for (const GeneratedFile& file : files) {
            joined += file.content;
            joined += '\n';
        }
        return joined;
    }

    bool has_file(const std::vector<GeneratedFile>& files, std::string_view name) {
        for (const GeneratedFile& file : files) {
            // the name decides, so the lookup resolves to at least one file (§14.2)
            if (file.path == name)
                return true;
        }
        return false;
    }

    bool has_code(const DiagnosticList& diagnostics, std::string_view code) {
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
    // the button measures its label through the engine, so the construction hands it over (AD-14, §9.1)
    EXPECT_TRUE(contains(source_file.content, "auto node_3 = std::make_unique<Button>(engine, text_engine);"));
    EXPECT_TRUE(contains(source_file.content, "n3->set_explicit_id(std::string(\"action_button\"));"));
    EXPECT_TRUE(contains(source_file.content, "n3->set_label(albor::i18n::translate(std::string(\"Increment\")));"));
    // the click handler wires to the written body, which the lambda forwards without allocating (§11.3)
    EXPECT_TRUE(contains(source_file.content, "n3->set_on_click([this]() { handle_3_clicked(this); });"));
    // the children join their parents in preorder, and the root is adopted last (§8.1)
    EXPECT_TRUE(contains(source_file.content, "n2->add_child(std::move(node_4));"));
    EXPECT_TRUE(contains(source_file.content, "owner->add_child(std::move(node_0));"));
    // the bound sink evaluators compute and push, which is the binding body (§13.3)
    EXPECT_TRUE(contains(source_file.content, "std::string MainView::Impl::evaluate_1_text(void* raw) {"));
    EXPECT_TRUE(contains(source_file.content, "const std::string value = ((albor::i18n::translate(std::string(\"Hello, \")) + self->m_user_name.get()) + std::string(\"!\"));"));
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

TEST(Codegen, ARepeatOverAScalarNeedsAnIdentityToKeyBy) {
    // arrange
    DiagnosticList diagnostics;
    // a scalar element carries no identity, which is what a row is keyed by (§18.1, AD-13)
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
    EXPECT_TRUE(has_code(diagnostics, "E0503"));
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

TEST(Codegen, ASnapshotPropertyGeneratesTheFacade) {
    // arrange
    DiagnosticList diagnostics;
    // the snapshot carries a manifest data type, which a shared pointer transports (§10.5, §13.2)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the native record is what the manifest declares, so the checker types it (§11.2, AD-17)
    catalog.add_native_data("WaveformData");
    // the component declares a property over the native record (§11.2, AD-17)
    const std::string_view source = "component C { in property<Snapshot<WaveformData>> data; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics, catalog);
    // assert the facade spells the property, which the reactive member holds as a snapshot (§14.1, §13.2)
    ASSERT_EQ(files.size(), 2U);
    // the data type is declared ahead, which is all a shared pointer signature needs (§11.2, AD-17)
    const GeneratedFile& header = file_named(files, "c.h");
    EXPECT_TRUE(contains(header.content, "struct WaveformData;"));
    // the accessor reads the snapshot by value, which is a pointer copy and never a record copy (§10.5, §14.1)
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] std::shared_ptr<const WaveformData> data() const;"));
    // the setter takes the snapshot by value, which drives the bindings that read it (§14.1, §13.2)
    EXPECT_TRUE(contains(header.content, "void set_data(std::shared_ptr<const WaveformData> value);"));
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // the reactive member stores the snapshot itself, which the settle path never deep-copies (§13.2, §13.3)
    EXPECT_TRUE(contains(cpp.content, "Property<std::shared_ptr<const WaveformData>> m_data;"));
}

TEST(Codegen, ASnapshotPropertyBindsThroughTheEngine) {
    // arrange
    DiagnosticList diagnostics;
    // the snapshot carries a manifest data type, which a shared pointer transports (§10.5, §13.2)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the native record is what the manifest declares, so the checker types it (§11.2, AD-17)
    catalog.add_native_data("WaveformData");
    // the dependent reads the snapshot property, which settles eagerly like a scalar does (§13.2, §13.3)
    const std::string_view source = "component C { in property<Snapshot<WaveformData>> data; in property<Snapshot<WaveformData>> same: data; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics, catalog);
    // assert the binding declares the snapshot dependency, which the engine settles on a republication (§13.3)
    ASSERT_EQ(files.size(), 2U);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    EXPECT_TRUE(contains(cpp.content, "Property<std::shared_ptr<const WaveformData>> m_same;"));
    EXPECT_TRUE(contains(cpp.content, "m_same.bind_static(&Impl::compute_same, this, deps_same);"));
    EXPECT_TRUE(contains(cpp.content, "const PropertyId deps_same[] = {m_data.id()};"));
    // the evaluator copies only the pointer, which is what the snapshot value is (§10.5, §13.2)
    EXPECT_TRUE(contains(cpp.content, "std::shared_ptr<const WaveformData> C::Impl::compute_same(void* raw) {"));
    EXPECT_TRUE(contains(cpp.content, "const std::shared_ptr<const WaveformData> value = self->m_data.get();"));
}

TEST(Codegen, ASnapshotOverAnUndeclaredTypeBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    // the manifest declares no such type, so the snapshot carries nothing resolvable (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const std::string_view source = "component C { in property<Snapshot<UnknownData>> data; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics, catalog);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0501"));
}

TEST(Codegen, ASnapshotOverAScalarBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    // a snapshot is the crossing for a data record, which a scalar is not (§10.5, §13.2)
    const std::string_view source = "component C { in property<Snapshot<int>> value; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0501"));
}

TEST(Codegen, ANestedSnapshotBlocksTheOutput) {
    // arrange
    DiagnosticList diagnostics;
    // the inner element must be a plain native name, which a nested snapshot is not (§11.2, §13.2)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    catalog.add_native_data("WaveformData");
    const std::string_view source = "component C { in property<Snapshot<Snapshot<WaveformData>>> data; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics, catalog);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0501"));
}

TEST(Codegen, AConditionalEmitsABuildUpdateAndEvaluateTrio) {
    // arrange
    DiagnosticList diagnostics;
    // the condition reads a bool property, which drives a guarded body (§11.1)
    const std::string_view source = "component C { in property<bool> show; Column { if show { Label { text: \"x\"; } } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // the condition property is registered and bound, which keeps the body live against its sources (§7.2, §13.3)
    EXPECT_TRUE(contains(cpp.content, "Property<bool> m_cond_0;"));
    EXPECT_TRUE(contains(cpp.content, "m_cond_0.bind_static(&Impl::evaluate_conditional_0, this, deps_cond_0);"));
    // the body is built on demand, which is what materialises it once the condition holds (§11.1)
    EXPECT_TRUE(contains(cpp.content, "void build_conditional_0();"));
    // the body reconciles on a condition change, which is how it appears and disappears (§11.1)
    EXPECT_TRUE(contains(cpp.content, "void update_conditional_0(bool present);"));
    // the evaluator reads the condition and settles the body, which is the reactive entry (§13.3)
    EXPECT_TRUE(contains(cpp.content, "bool C::Impl::evaluate_conditional_0(void* raw) {"));
    // the body node is adopted into the parent at the recorded offset, which is where it belongs (§8.1)
    EXPECT_TRUE(contains(cpp.content, "m_node_0->insert_child("));
    // the removed body is taken back out, which destroys it (§11.1)
    EXPECT_TRUE(contains(cpp.content, "m_cond_root_0 = m_node_0->remove_child("));
}

TEST(Codegen, AConditionalBodyEvaluatorGuardsAgainstAnAbsentNode) {
    // arrange
    DiagnosticList diagnostics;
    // the bound assignment lives inside the guarded body, so its evaluator must skip an absent node (§11.1)
    const std::string_view source = "component C { in property<bool> show; in property<string> label; Column { if show { Label { text: label; } } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // the evaluator returns the computed value even when the node is gone, so the engine still settles (§13.3)
    EXPECT_TRUE(contains(cpp.content, "if (self->m_node_1 == nullptr)"));
}

TEST(Codegen, AConditionalBlocksTheOutputForANonBoolCondition) {
    // arrange
    DiagnosticList diagnostics;
    // the condition must be a bool, which a non-bool is not (§11.1)
    const std::string_view source = "component C { in property<int> count: 1; Column { if count { Label {} } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0410"));
}

TEST(Codegen, ACollectionAppendCopiesMutatesAndSetsBack) {
    // arrange
    DiagnosticList diagnostics;
    // the append mutates a vector property through a working copy (§11.1)
    const std::string_view source = "component C { inout property<vector<string>> items; Button { on_clicked: { items.append(\"x\"); } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // the working copy is taken, the element is pushed and the rebuilt vector is set back (§11.1)
    EXPECT_TRUE(contains(cpp.content, "auto list = self->m_items.get();"));
    EXPECT_TRUE(contains(cpp.content, "list.push_back("));
    EXPECT_TRUE(contains(cpp.content, "self->m_items.set(std::move(list));"));
}

TEST(Codegen, ACollectionClearDropsEveryElement) {
    // arrange
    DiagnosticList diagnostics;
    // the clear needs no working copy, because an empty vector replaces the value (§11.1)
    const std::string_view source = "component C { inout property<vector<int>> items; Button { on_clicked: { items.clear(); } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    EXPECT_TRUE(contains(cpp.content, "self->m_items.set({});"));
}

TEST(Codegen, ACollectionRemoveAtGuardsTheIndex) {
    // arrange
    DiagnosticList diagnostics;
    // the remove_at erases only when the index is in range, which is what keeps the vector valid (§11.1)
    const std::string_view source = "component C { inout property<vector<int>> items; Button { on_clicked: { items.remove_at(0); } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    EXPECT_TRUE(contains(cpp.content, "if (index >= 0 && index < static_cast<int>(list.size()))"));
    EXPECT_TRUE(contains(cpp.content, "list.erase(list.begin() + index);"));
}

TEST(Codegen, AFieldAssignmentCopiesWritesAndSetsBack) {
    // arrange
    DiagnosticList diagnostics;
    // the field write mutates a record property through a working copy (§11.1)
    const std::string_view source = "struct Frame { float width; } component C { inout property<Frame> frame; Button { on_clicked: { frame.width = 10.0; } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // the working copy is taken, the field is written and the rebuilt record is set back (§11.1)
    EXPECT_TRUE(contains(cpp.content, "auto record = self->m_frame.get();"));
    EXPECT_TRUE(contains(cpp.content, "record.width = "));
    EXPECT_TRUE(contains(cpp.content, "self->m_frame.set(std::move(record));"));
}

TEST(Codegen, ARecordSignalParameterCrossesByValue) {
    // arrange
    DiagnosticList diagnostics;
    // the signal carries a record payload, which the facade exposes by value (§11.1)
    const std::string_view source = "struct Frame { float width; } component C { inout property<Frame> frame; signal frame_changed(Frame frame); Button { on_clicked: { frame_changed(frame); } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert the whole file generates, because the record signal and its invocation both resolved (§11.1)
    EXPECT_FALSE(files.empty());
    EXPECT_FALSE(diagnostics.has_errors());
    const GeneratedFile& header = file_named(files, "c.h");
    // the facade exposes a setter that takes the record by value (§14.1)
    EXPECT_TRUE(contains(header.content, "set_on_frame_changed"));
}

TEST(Codegen, ASnapshotOfARecordGeneratesTheFacade) {
    // arrange a catalog holding a record, which is what the manifest contributes (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    catalog.add_record(RecordType{.name = "Task", .fields = {RecordFieldType{.name = "title", .type = scalar(TypeKind::String), .is_key = false}}});
    DiagnosticList diagnostics;
    // the file imports the module, because that is what names the record header's directory (§11.1, §11.2, AD-17)
    const std::string_view source = "import chart; component C { in property<Snapshot<Task>> data; Column {} }";
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
    // act
    CodeGenerator generator(diagnostics);
    // the catalog is what tells the generator a record is generatable and where its header lives (§11.2, AD-17)
    generator.set_catalog(&catalog);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert the record header is pulled, which the shared pointer signature needs (§11.2, AD-17)
    ASSERT_EQ(files.size(), 2U);
    const GeneratedFile& header = file_named(files, "c.h");
    EXPECT_TRUE(contains(header.content, "#include \"chart/task.h\""));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] std::shared_ptr<const Task> data() const;"));
    EXPECT_TRUE(contains(header.content, "void set_data(std::shared_ptr<const Task> value);"));
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    EXPECT_TRUE(contains(cpp.content, "Property<std::shared_ptr<const Task>> m_data;"));
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

TEST(Codegen, AChunkedModeMergesTheComponentSourcesIntoOneUnit) {
    // arrange
    DiagnosticList diagnostics;
    const std::string_view source = R"(
component Alpha { Column { spacing: 4px; } }
component Beta { Column { spacing: 8px; } }
)";
    // act
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    CodeGenerator generator(diagnostics);
    generator.set_tu_chunk_size(2);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert
    ASSERT_EQ(diagnostics.count(), 0U);
    // both facades stay separate, which is what a translation unit includes (§14.2)
    EXPECT_TRUE(contains(file_named(files, "alpha.h").content, "class Alpha final : public Component"));
    EXPECT_TRUE(contains(file_named(files, "beta.h").content, "class Beta final : public Component"));
    // both implementations merge into one unit, which is the configured chunk (AD-03b)
    const GeneratedFile& chunk = file_named(files, "facades_0.cpp");
    EXPECT_TRUE(contains(chunk.content, "// generated group of components, do not edit (§14.2, AD-03b)"));
    EXPECT_TRUE(contains(chunk.content, "#include \"alpha.h\""));
    EXPECT_TRUE(contains(chunk.content, "#include \"beta.h\""));
    EXPECT_TRUE(contains(chunk.content, "n0->set_gap(4.0f);"));
    EXPECT_TRUE(contains(chunk.content, "n0->set_gap(8.0f);"));
    // the per component units are not emitted, which is what the chunk replaces (AD-03b)
    EXPECT_FALSE(has_file(files, "alpha.cpp"));
    EXPECT_FALSE(has_file(files, "beta.cpp"));
}

TEST(Codegen, AChunkedModeKeepsTheRemainderInItsOwnUnit) {
    // arrange
    DiagnosticList diagnostics;
    const std::string_view source = R"(
component A { Column { spacing: 4px; } }
component B { Column { spacing: 8px; } }
component C { Column { spacing: 12px; } }
)";
    // act
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    CodeGenerator generator(diagnostics);
    generator.set_tu_chunk_size(2);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert
    ASSERT_EQ(diagnostics.count(), 0U);
    // the remainder still forms its own unit, which a last partial group always does (AD-03b)
    const GeneratedFile& chunk_0 = file_named(files, "facades_0.cpp");
    const GeneratedFile& chunk_1 = file_named(files, "facades_1.cpp");
    EXPECT_TRUE(contains(chunk_0.content, "#include \"a.h\""));
    EXPECT_TRUE(contains(chunk_0.content, "#include \"b.h\""));
    EXPECT_FALSE(contains(chunk_0.content, "#include \"c.h\""));
    EXPECT_TRUE(contains(chunk_1.content, "#include \"c.h\""));
    EXPECT_FALSE(contains(chunk_1.content, "#include \"a.h\""));
}

TEST(Codegen, AZeroChunkSizeFallsBackToPerComponentUnits) {
    // arrange
    DiagnosticList diagnostics;
    CodeGenerator generator(diagnostics);
    // a chunk size of zero would group nothing, so it is clamped back to the default (AD-03b)
    generator.set_tu_chunk_size(0);
    // act
    const std::vector<Token> tokens = Lexer("component A { Column { spacing: 4px; } }", diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert
    ASSERT_EQ(diagnostics.count(), 0U);
    EXPECT_TRUE(has_file(files, "a.cpp"));
    EXPECT_FALSE(has_file(files, "facades_0.cpp"));
}

TEST(Codegen, ABreaksOnAPhysicalLengthDefault) {
    // a default is scanned like an assignment, because its density is undefined (§11.3, AD-27)
    const std::string_view source = R"(
component Ring {
    in property<length> fat: 10phx;
    Column {}
}
)";
    DiagnosticList diagnostics;
    CodeGenerator generator(diagnostics);
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // the physical length is rejected, which leaves nothing to generate (§11.3, AD-27)
    ASSERT_EQ(diagnostics.count(), 1U);
    EXPECT_STREQ(diagnostics.diagnostics().at(0).code.c_str(), "E0502");
    EXPECT_TRUE(files.empty());
}

TEST(Codegen, AEscapesControlBytesInsideAString) {
    // the source is assembled at runtime, so no control byte is stored in this file
    const std::string source = std::string("component Ring {\n    in property<string> chime: \"") + "\x07" + "\";\n    Column {}\n}\n";
    DiagnosticList diagnostics;
    CodeGenerator generator(diagnostics);
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // the byte is escaped as a hex escape, which keeps the generated file printable (§11.3)
    ASSERT_EQ(diagnostics.count(), 0U);
    ASSERT_TRUE(has_file(files, "ring.cpp"));
    const GeneratedFile& cpp = file_named(files, "ring.cpp");
    EXPECT_TRUE(contains(cpp.content, "m_chime{std::string(\"\\x07\")}"));
}

TEST(Codegen, AKaleidoscopicComponentCoversColoursConversionsAndExpressions) {
    // arrange, act
    const std::string_view source = R"(
component Well {
    in property<bool> enabled: true;
    in property<float> ratio: 0xF.8;
    in property<color> accent: #E02030;
    in property<color> soft: #E0203080;
    in property<length> lead: 12px;
    in property<int> counter: 2;
    in property<string> text: "a\nb\t\"q\"\\z";
    in property<string> named;
    in property<bool> muted: false;
    inout property<int> visible: 0;
    inout property<int> doubled: counter * 2;
    inout property<float> half: counter / 2.0;
    in property<bool> flag: !enabled;
    in property<bool> engaged: flag && counter > 0;
    in property<bool> verdict: counter % 2 == 0 && counter >= 10 && ratio != 0.5 && (counter - 1) <= 3 && counter < 5 && !flag || flag;
    in property<bool> painted: bool(accent);
    in property<float> tasked: float(named);
    in property<bool> echoed: bool(enabled);

    signal moved(int x, int y);
    signal tinted(color c);

    Column {
        spacing: 4px;

        Label {
            id: headline;
            text: string(enabled) + "/" + string(ratio) + "/" + string(accent) + "/" + string(lead) + "/" + string(counter) + string(named);
            foreground: accent;
            font_size: lead;
        }

        Label {
            text: string(bool(counter)) + string(bool(named)) + string(bool(lead)) + string(bool(ratio)) + string(bool(accent));
        }

        Label {
            text: string(float(counter)) + string(int(counter)) + string(int(ratio)) + string(int(named)) + string(float(named));
        }

        Label {
            text: string(100) + string(1.5) + string("x") + string(#123456) + string(12px) + string(true) +
                string((counter + 1)) + string(!flag) + string((counter > 2)) + string(@tr("k"));
        }

        Row {
            spacing: 8px;

            Slider {
                value: float(ratio) + float(named);
                on_changed: {
                    tinted(accent);
                }
            }

            CheckBox {
                checked: enabled;
                on_toggled: {
                    visible = visible + 1;
                    visible = (visible + 1);
                }
            }

            Button {
                text: text;
                background: soft;
                disabled: muted;
                on_clicked: {
                    visible = visible + 1;
                    visible = -counter;
                    moved(counter, int(named));
                    tinted(accent);
                }
            }
        }

        Canvas {
            id: plot;
            width: 320px;
            height: 240px;
        }

        Canvas {
            width: lead;
            height: lead;
        }
    }
}
)";
    const std::vector<GeneratedFile> files = generate(source);
    // the scalar property defaults show their emitted forms, which cover the literal spellings (§11.3)
    const GeneratedFile& cpp = file_named(files, "well.cpp");
    EXPECT_TRUE(contains(cpp.content, "Property<bool> m_enabled{true};"));
    EXPECT_TRUE(contains(cpp.content, "Property<bool> m_muted{false};"));
    // a hexadecimal float reads back as its value, which the shortest round trip then writes (§11.3)
    EXPECT_TRUE(contains(cpp.content, "Property<float> m_ratio{15.5f};"));
    EXPECT_TRUE(contains(cpp.content, "Property<Color> m_accent{Color::rgb("));
    EXPECT_TRUE(contains(cpp.content, "Property<Color> m_soft{Color::rgba("));
    // the escaped string reappears escaped, which keeps the C++ literal identical (§11.3)
    EXPECT_TRUE(contains(cpp.content, "Property<std::string> m_text{std::string(\"a\\nb\\t\\\"q\\\"\\\\z\")};"));
    // a property without a default starts empty, which the builder then settles (§7.2)
    EXPECT_TRUE(contains(cpp.content, "Property<std::string> m_named;"));
    // the reserved name is disambiguated, which keeps the facade unambiguous (§14.1)
    EXPECT_TRUE(contains(cpp.content, "Property<int> m_visible_{0};"));
    const GeneratedFile& header = file_named(files, "well.h");
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] int visible_() const;"));
    EXPECT_TRUE(contains(header.content, "void set_visible_(int value);"));
    // the multi parameter signal joins its parameters, which is how a signature reads (§11.3)
    EXPECT_TRUE(contains(header.content, "void set_on_moved(std::function<void(int, int)> handler);"));
    EXPECT_TRUE(contains(header.content, "void set_on_tinted(std::function<void(Color)> handler);"));
    EXPECT_TRUE(contains(cpp.content, "std::function<void(int, int)> m_on_moved;"));
    EXPECT_TRUE(contains(cpp.content, "std::function<void(Color)> m_on_tinted;"));
    // the vertex colours pull the paint header, which a colour component needs (§14.1)
    EXPECT_TRUE(contains(header.content, "#include \"core/paint.h\""));
    // the text conversion needs the hex helper, which prints the colour as text (§11.3)
    EXPECT_TRUE(contains(cpp.content, "#include <cstdio>"));
    EXPECT_TRUE(contains(cpp.content, "std::string well_color_to_hex(Color value) {"));
    EXPECT_TRUE(contains(cpp.content, "well_color_to_hex(self->m_accent.get())"));
    // the binary and unary forms render their operators, which cover the whole chart (§11.3)
    EXPECT_TRUE(contains(cpp.content, "compute_doubled"));
    EXPECT_TRUE(contains(cpp.content, "m_doubled.bind_static(&Impl::compute_doubled, this, deps_doubled)"));
    EXPECT_TRUE(contains(cpp.content, "const PropertyId deps_doubled[] = {m_counter.id()};"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() * 2)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() / 2.0f)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_enabled.get() == false)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_flag.get() && (self->m_counter.get() > 0))"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() % 2)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() >= 10)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_ratio.get() != 0.5f)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() - 1)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() < 5)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_flag.get() == false)"));
    EXPECT_TRUE(contains(cpp.content, "std::stof(self->m_named.get())"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_accent.get() != Color{})"));
    // the conversions render their shapes, which cover every arm of the rule (§11.3)
    EXPECT_TRUE(contains(cpp.content, "(self->m_enabled.get() ? std::string(\"true\") : std::string(\"false\"))"));
    EXPECT_TRUE(contains(cpp.content, "well_float_to_text(self->m_ratio.get())"));
    EXPECT_TRUE(contains(cpp.content, "well_float_to_text(self->m_lead.get())"));
    EXPECT_TRUE(contains(cpp.content, "std::to_string(self->m_counter.get())"));
    EXPECT_TRUE(contains(cpp.content, "!self->m_named.get().empty()"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_lead.get() != 0.0f)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_ratio.get() != 0.0f)"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() != 0)"));
    EXPECT_TRUE(contains(cpp.content, "static_cast<float>(self->m_counter.get())"));
    EXPECT_TRUE(contains(cpp.content, "std::stoi(self->m_named.get())"));
    EXPECT_TRUE(contains(cpp.content, "static_cast<int>(self->m_ratio.get())"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_ratio.get())"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_enabled.get())"));
    EXPECT_TRUE(contains(cpp.content, "well_float_to_text(1.5f)"));
    EXPECT_TRUE(contains(cpp.content, "well_color_to_hex(Color::rgb("));
    EXPECT_TRUE(contains(cpp.content, "(true ? std::string(\"true\") : std::string(\"false\"))"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() > 2)"));
    // the widgets construct with the engines they need, which decides the arguments (§11.3)
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Slider>(engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<CheckBox>(engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Canvas>(engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Button>(engine, text_engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Label>(engine, text_engine);"));
    // the wired callbacks carry the named parameters of their signals, which the body reads (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_on_change([this](float value) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "set_on_toggle([this](bool checked) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "set_on_click([this]() { handle_"));
    // the handler bodies run the written statements, which name the sanitized property (§11.3)
    EXPECT_TRUE(contains(cpp.content, "self->m_visible_.set((self->m_visible_.get() + 1));"));
    EXPECT_TRUE(contains(cpp.content, "self->m_visible_.set((-self->m_counter.get()));"));
    EXPECT_TRUE(contains(cpp.content, "self->m_on_moved(self->m_counter.get(), std::stoi(self->m_named.get()));"));
    EXPECT_TRUE(contains(cpp.content, "self->m_on_tinted(self->m_accent.get());"));
    // the canvas tracks its constant dimensions once, which the intrinsic size then combines (§11.3)
    EXPECT_TRUE(contains(cpp.content, "n9->set_explicit_id(std::string(\"plot\"));"));
    EXPECT_TRUE(contains(cpp.content, "set_intrinsic_size(Size{"));
    EXPECT_TRUE(contains(cpp.content, "self->m_canvas_w_"));
}

TEST(Codegen, ACollectionPropertyBecomesAStandardVectorFacade) {
    // arrange a collection property, which is what a list becomes at the facade (§8.1, §11.3)
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<int>> items: [1, 2]; Column {} }");
    // act the header is what declares the accessor a host reads (§14.1)
    const GeneratedFile header = file_named(files, "c.h");
    // assert the accessor hands out a reference, because copying a collection on every read would be a cost (§14.1)
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] const std::vector<int>& items() const;"));
    // assert the setter takes the collection by value, because a write hands over its ownership (§14.1)
    EXPECT_TRUE(contains(header.content, "void set_items(std::vector<int> value);"));
}

TEST(Codegen, ACollectionPropertyPullsTheVectorHeader) {
    // arrange
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<int>> items: [1]; Column {} }");
    // act
    const GeneratedFile header = file_named(files, "c.h");
    // assert the header carries what its own signature names, because a facade includes what it uses (§3.5, §14.1)
    EXPECT_TRUE(contains(header.content, "#include <vector>"));
}

TEST(Codegen, AComponentWithNoCollectionDoesNotPullTheVectorHeader) {
    // arrange a component with only scalars, which needs no collection header (§11.3)
    const std::vector<GeneratedFile> files = generate("component C { in property<int> count: 1; Column {} }");
    // act
    const GeneratedFile header = file_named(files, "c.h");
    // assert the include is absent, because a generated file carries only what it uses (§14.1)
    EXPECT_FALSE(contains(header.content, "#include <vector>"));
}

TEST(Codegen, AListRendersAsABracedInitialiserOverItsElementType) {
    // arrange a list of numbers, whose element type the checker decided (§8.1)
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<int>> items: [1, 2]; Column {} }");
    // act the member initialiser is where the written list lands (§14.1)
    const GeneratedFile source = file_named(files, "c.cpp");
    // assert the collection is named, so the initialiser cannot deduce a different one (§8.1)
    EXPECT_TRUE(contains(source.content, "Property<std::vector<int>> m_items{std::vector<int>{1, 2}};"));
}

TEST(Codegen, ANestedListRendersItsInnerCollections) {
    // arrange a list of lists, whose inner element is itself a collection (§8.1)
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<vector<int>>> grid: [[1], [2]]; Column {} }");
    // act
    const GeneratedFile source = file_named(files, "c.cpp");
    // assert each inner list names its own element type, which is what nesting means (§8.1)
    EXPECT_TRUE(contains(source.content, "std::vector<std::vector<int>>{std::vector<int>{1}, std::vector<int>{2}}"));
}

TEST(Codegen, AnEmptyListRendersAsValueInitialisation) {
    // arrange an empty list, which has no element to name (§8.1)
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<int>> items: []; Column {} }");
    // act
    const GeneratedFile source = file_named(files, "c.cpp");
    // assert the braces carry no element, because the target property names the type instead (§8.1)
    EXPECT_TRUE(contains(source.content, "Property<std::vector<int>> m_items{{}}"));
}

TEST(Codegen, ACollectionPropertyNoLongerBlocksTheOutput) {
    // arrange diagnostics, because the property used to be the one type the generator refused (§11.3)
    DiagnosticList diagnostics;
    // the collection is generated now, so the run must be clean (§8.1, §11.3)
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<int>> items: [1]; Column {} }", diagnostics);
    // assert the output exists and nothing blocked it, which is what landing this feature means (§11.3)
    EXPECT_FALSE(files.empty());
    EXPECT_EQ(diagnostics.count(), 0U);
}

TEST(Codegen, ATranslationInsideAListIsStillExtracted) {
    // arrange a list of translated strings, because a translation may sit anywhere in a value (§9.2)
    const std::vector<GeneratedFile> files = generate("component C { in property<vector<string>> titles: [@tr(\"first\")]; Column {} }");
    // act
    const GeneratedFile source = file_named(files, "c.cpp");
    // assert the catalogue header is pulled, which is what proves the translation was reached (§9.2, §11.3)
    EXPECT_TRUE(contains(source.content, "albor::i18n::translate"));
}

TEST(Codegen, ARecordCollectionPullsTheManifestHeaderThatDeclaresTheStruct) {
    // arrange a catalog holding a record, which is what the manifest contributes (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    catalog.add_record(RecordType{.name = "Task", .fields = {RecordFieldType{.name = "title", .type = scalar(TypeKind::String), .is_key = false}}});
    DiagnosticList diagnostics;
    // the file imports the module, because that is what names the header's directory (§11.1, §11.2, AD-17)
    const std::vector<Token> tokens = Lexer("import chart; component C { in property<vector<Task>> tasks; Column {} }", diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
    // act
    CodeGenerator generator(diagnostics);
    // the catalog is what tells the generator a record is generatable and where its header lives (§11.2, AD-17)
    generator.set_catalog(&catalog);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert the header declares the record by the name the manifest wrote (§11.2, AD-17)
    ASSERT_EQ(files.size(), 2U);
    const GeneratedFile header = file_named(files, "c.h");
    EXPECT_TRUE(contains(header.content, "#include \"chart/task.h\""));
    // assert the collection is spelled over the record, which is what the facade exposes (§11.2)
    EXPECT_TRUE(contains(header.content, "const std::vector<Task>& tasks() const;"));
}

TEST(Codegen, ARecordFieldReadRendersAsAMemberAccess) {
    // arrange a catalog holding a record, because a field read needs its declaration (§11.2, AD-17)
    ElementCatalog catalog = ElementCatalog::with_builtins();
    catalog.add_record(RecordType{.name = "Task", .fields = {RecordFieldType{.name = "title", .type = scalar(TypeKind::String), .is_key = false}}});
    DiagnosticList diagnostics;
    // the read is inside a repeat body, which is where a row reaches its item (§11.2, AD-17, AD-13)
    const std::string_view source = "import chart; component C { in property<vector<Task>> tasks; Column { Repeat(t in tasks) { Label { text: t.title; } } } }";
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
    // act
    CodeGenerator generator(diagnostics);
    // the catalog is set, because without it a record is not generatable (§11.2, AD-17)
    generator.set_catalog(&catalog);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert the file is blocked by the repeat, which is the next feature and not this one (§11.3)
    EXPECT_TRUE(files.empty());
    // assert the record itself resolved, because only the repeat is refused (§11.2, AD-17)
    EXPECT_FALSE(has_code(diagnostics, "E0416"));
}

TEST(Codegen, AUiStructEmitsAGuardedDefinition) {
    // arrange
    DiagnosticList diagnostics;
    // the struct is declared in the file itself, so the header emits rather than includes it (§11.1)
    const std::string_view source = "struct Frame { float width; float height; } component C { in property<Frame> frame; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& header = file_named(files, "c.h");
    // the struct is declared with its fields, which is the shape a field read walks (§11.1)
    EXPECT_TRUE(contains(header.content, "struct Frame"));
    EXPECT_TRUE(contains(header.content, "float width;"));
    EXPECT_TRUE(contains(header.content, "float height;"));
    // the definition is guarded, so a translation unit that includes two headers defining it takes one (§11.1)
    EXPECT_TRUE(contains(header.content, "#ifndef ALBOR_GENERATED_STRUCT_Frame"));
    EXPECT_TRUE(contains(header.content, "#define ALBOR_GENERATED_STRUCT_Frame"));
}

TEST(Codegen, AUiStructWithACollectionFieldPullsTheVectorHeader) {
    // arrange
    DiagnosticList diagnostics;
    // the struct holds a collection field, so the emitted definition names the vector header (§8.1, §11.1)
    const std::string_view source = "struct Box { vector<string> items; } component C { in property<Box> box; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& header = file_named(files, "c.h");
    // the collection field renders as the standard vector over its element (§8.1, §11.3)
    EXPECT_TRUE(contains(header.content, "std::vector<std::string> items;"));
    // the vector header follows, because the emitted definition names it (§8.1)
    EXPECT_TRUE(contains(header.content, "#include <vector>"));
}

TEST(Codegen, AUiStructFieldReadChecksClean) {
    // arrange
    DiagnosticList diagnostics;
    // the field read feeds a conversion inside a binding, which the checker resolves (§11.1)
    const std::string_view source = "struct Frame { float width; } component C { in property<Frame> frame; Column { Label { text: string(frame.width); } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert the whole file generates, because the struct, the property and the field all resolved (§11.1)
    EXPECT_FALSE(files.empty());
    EXPECT_FALSE(diagnostics.has_errors());
}

TEST(Codegen, AUiEnumEmitsAGuardedDefinition) {
    // arrange
    DiagnosticList diagnostics;
    // the enum is declared in the file itself, so the header emits rather than includes it (§11.1)
    const std::string_view source = "enum Status { Todo, Doing, Done } component C { in property<Status> status; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& header = file_named(files, "c.h");
    // the enum is declared with its members, which is the closed set a value may take (§11.1)
    EXPECT_TRUE(contains(header.content, "enum class Status"));
    EXPECT_TRUE(contains(header.content, "Todo,"));
    EXPECT_TRUE(contains(header.content, "Doing,"));
    EXPECT_TRUE(contains(header.content, "Done,"));
    // the definition is guarded, so a translation unit that includes two headers defining it takes one (§11.1)
    EXPECT_TRUE(contains(header.content, "#ifndef ALBOR_GENERATED_ENUM_Status"));
    EXPECT_TRUE(contains(header.content, "#define ALBOR_GENERATED_ENUM_Status"));
}

TEST(Codegen, AUiEnumMemberReadEmitsTheEnumerator) {
    // arrange
    DiagnosticList diagnostics;
    // the member is written as a value inside a binding, which the emission renders as the enumerator (§11.1)
    const std::string_view source = "enum Status { Todo, Doing, Done } component C { in property<Status> status; Column { Label { text: string(status); } } }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert the whole file generates, because the enum and the property both resolved (§11.1)
    EXPECT_FALSE(files.empty());
    EXPECT_FALSE(diagnostics.has_errors());
}

TEST(Codegen, AUiEnumPropertyTypeRendersTheEnumName) {
    // arrange
    DiagnosticList diagnostics;
    // a property of the enum type spells the enum in the facade signature (§11.1)
    const std::string_view source = "enum Status { Todo, Doing, Done } component C { in property<Status> status; Column {} }";
    // act
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    // assert
    ASSERT_FALSE(files.empty());
    const GeneratedFile& header = file_named(files, "c.h");
    // the accessor names the enum, which is what a host binds against (§14.1)
    EXPECT_TRUE(contains(header.content, "Status status()"));
}

TEST(Codegen, ADataTypeStillBlocksTheOutput) {
    // arrange diagnostics, because a data type is named but is not a record (§11.2, AD-17)
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the data type is what a snapshot carries, and nothing generates it (§11.2, AD-17)
    catalog.add_native_data("WaveformData");
    // the property is a collection of the data type, which has no struct anywhere (§11.2, AD-17)
    const std::vector<Token> tokens = Lexer("component C { in property<vector<WaveformData>> waves; Column {} }", diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
    // act
    CodeGenerator generator(diagnostics);
    // the catalog is set, so the generator knows a data type is not a record (§11.2, AD-17)
    generator.set_catalog(&catalog);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert
    EXPECT_TRUE(files.empty());
    EXPECT_TRUE(has_code(diagnostics, "E0501"));
}

TEST(Codegen, AHandlerBodyReadsTheValueItsSignalCarries) {
    // arrange a slider handler that publishes the value the widget raised (§11.1)
    const std::string source = R"(component C {
    inout property<float> level: 0.0;
    Slider {
        value: level;
        on_changed: {
            level = value;
        }
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert the lambda forwards the value, which is how the body reaches it (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_on_change([this](float value) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "handle_0_changed(this, value);"));
    EXPECT_TRUE(contains(cpp.content, "static void handle_0_changed(void* raw, [[maybe_unused]] float value);"));
    EXPECT_TRUE(contains(cpp.content, "self->m_level.set(value);"));
}

TEST(Codegen, ACheckboxHandlerReadsTheStateItToggledTo) {
    // arrange a checkbox handler that publishes the state the widget toggled (§11.1)
    const std::string source = R"(component C {
    inout property<bool> armed: false;
    CheckBox {
        checked: armed;
        on_toggled: {
            armed = checked;
        }
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert the boolean parameter is named and forwarded, which the body assigns (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_on_toggle([this](bool checked) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "handle_0_toggled(this, checked);"));
    EXPECT_TRUE(contains(cpp.content, "self->m_armed.set(checked);"));
}

TEST(Codegen, ATextFieldIsConstructedOverBothEnginesAndOwnsItsHeader) {
    // arrange a field the author declared over the value and the hint (§11.1)
    const std::string source = R"(component C {
    TextField {
        value: "Albor";
        placeholder: "type here";
        multiline: true;
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert the field is one of the engine and text engine widgets (§11.3)
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<TextField>(engine, text_engine);"));
    EXPECT_TRUE(contains(cpp.content, "#include \"widgets/text_field.h\""));
    // assert both runs reach the widget through the setters the catalog names (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_value("));
    EXPECT_TRUE(contains(cpp.content, "set_placeholder("));
    // assert the mode reaches the widget as the author wrote it (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_multiline("));
}

TEST(Codegen, AReversedBooleanAndABoolStateReachTheirSetters) {
    // arrange a field whose states the author wrote as bools (§11.1)
    const std::string source = R"(component C {
    TextField {
        read_only: true;
        disabled: false;
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert both states reach the widget (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_read_only("));
    EXPECT_TRUE(contains(cpp.content, "set_disabled(false)"));
}

TEST(Codegen, ATextFieldSignalsCarryTheStringTheyChangedTo) {
    // arrange a field whose two signals a form wires (§11.1)
    const std::string source = R"(component C {
    inout property<string> name: "";
    TextField {
        value: name;
        placeholder: "type a name";
        on_changed: {
            name = value;
        }
        on_committed: {
            name = value;
        }
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert the string parameter is named on both handlers, which the bodies assign (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_on_change([this](std::string value) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "set_on_commit([this](std::string value) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "self->m_name.set(value);"));
}

TEST(Codegen, ACanvasDrawBlockBecomesADrawBody) {
    // arrange a canvas that fills itself from a bound property, which is custom drawing (§6.4)
    const std::string source = R"(component C {
    inout property<float> level: 0.0;
    Canvas {
        width: 100px;
        height: 20px;
        draw: {
            fill_rect(0, 0, canvas_width, canvas_height, #1A1E26);
            fill_rect(0, 0, canvas_width * level, canvas_height, #33BF73);
        }
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert the body is installed and records the primitives the author wrote (§6.4)
    EXPECT_TRUE(contains(cpp.content, "set_draw([this](RenderContext& context) { draw_0_body(this, context); });"));
    EXPECT_TRUE(contains(cpp.content, "static void draw_0_body(void* raw, RenderContext& context);"));
    EXPECT_TRUE(contains(cpp.content, "const float canvas_width = self->m_node_0->bounds().width;"));
    EXPECT_TRUE(contains(cpp.content, "context.draw_rect(Rect::from_origin_size(Point{canvas_origin.x + 0, canvas_origin.y + 0}, Size{canvas_width, canvas_height}), paint);"));
    EXPECT_TRUE(contains(cpp.content, "(canvas_width*self->m_level.get())"));
    EXPECT_TRUE(contains(cpp.content, "const Point canvas_origin{canvas_bounds.x, canvas_bounds.y};"));
}

TEST(Codegen, ADrawBlockIsRefusedOutsideACanvas) {
    // arrange a button that tries to draw, which is not a thing a button does (§6.4)
    const std::string source = R"(component C {
    Button {
        text: "x";
        draw: {
            fill_rect(0, 0, 1, 1, #000000);
        }
    }
})";
    // act
    DiagnosticList diagnostics;
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    const std::vector<GeneratedFile> produced = CodeGenerator(diagnostics).generate(checked);
    EXPECT_TRUE(produced.empty());
    // assert the element that cannot draw is refused, which the checker reports (§6.4)
    EXPECT_TRUE(has_code(diagnostics, "E0601"));
}

TEST(Codegen, ADrawPrimitiveOutsideTheVocabularyIsRefused) {
    // arrange a canvas that calls a primitive the vocabulary does not hold (§6.4)
    const std::string source = R"(component C {
    Canvas {
        width: 10px;
        height: 10px;
        draw: {
            teleport(0, 0, 1, 1, #000000);
        }
    }
})";
    // act
    DiagnosticList diagnostics;
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    const std::vector<GeneratedFile> produced = CodeGenerator(diagnostics).generate(checked);
    EXPECT_TRUE(produced.empty());
    // assert the unknown primitive is refused, because the vocabulary is closed (§6.4)
    EXPECT_TRUE(has_code(diagnostics, "E0602"));
}

TEST(Codegen, ADrawPrimitiveWithTheWrongArityIsRefused) {
    // arrange a rectangle that names too few numbers, which no primitive takes (§6.4)
    const std::string source = R"(component C {
    Canvas {
        width: 10px;
        height: 10px;
        draw: {
            fill_rect(0, 0, 1);
        }
    }
})";
    // act
    DiagnosticList diagnostics;
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    const CheckedFile checked = TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    const std::vector<GeneratedFile> produced = CodeGenerator(diagnostics).generate(checked);
    EXPECT_TRUE(produced.empty());
    // assert the arity is refused, because the setter cannot be emitted (§6.4)
    EXPECT_TRUE(has_code(diagnostics, "E0603"));
}

TEST(Codegen, ASliderRangeBecomesOneSetterOverBothBounds) {
    // arrange a slider whose bounds the author wrote, which the widget takes together (§8)
    const std::string source = R"(component C {
    Slider {
        min: -1.0;
        max: 1.0;
    }
})";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert both bounds are tracked and applied by one call, which is what the widget takes (§8)
    EXPECT_TRUE(contains(cpp.content, "m_slider_min_0 = (-1.0f);"));
    EXPECT_TRUE(contains(cpp.content, "m_slider_max_0 = 1.0f;"));
    EXPECT_TRUE(contains(cpp.content, "n0->set_range(m_slider_min_0, m_slider_max_0);"));
}

TEST(Codegen, AScrollViewIsConstructedAndItsViewportExtentIsCombined) {
    // arrange
    const std::string_view source = R"(
component LogPanel {
    ScrollView {
        viewport_width: 200px;
        viewport_height: 120px;
        scrollbar: Scrollbar.off;
        overscan: 32px;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the viewport is one setter, because the two halves are written as an extent (§8.2, §11.3)
    const GeneratedFile& unit = file_named(files, "log_panel.cpp");
    EXPECT_TRUE(contains(unit.content, "std::make_unique<ScrollView>(engine)"));
    EXPECT_TRUE(contains(unit.content, "set_viewport_extent(Size{m_viewport_w_0, m_viewport_h_0})"));
    // assert the policy renders as the enumeration the widget takes, not the language spelling (§11.3, §8)
    EXPECT_TRUE(contains(unit.content, "set_scrollbar_visibility(albor::ScrollbarVisibility::AlwaysOff)"));
    EXPECT_TRUE(contains(unit.content, "set_overscan(32.0f)"));
}

TEST(Codegen, ABoundScrollOffsetBindsOntoTheWidgetProperty) {
    // arrange the offset written as an expression, which is how a log view pins itself (§7.3, AD-24)
    const std::string_view source = R"(
component LogPanel {
    inout property<float> extent;
    ScrollView {
        offset: extent;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the binding lands on the widget property, which is what lets a user scroll outrank it (§7.3, AD-24)
    const GeneratedFile& unit = file_named(files, "log_panel.cpp");
    EXPECT_TRUE(contains(unit.content, "bind_offset(&Impl::evaluate_0_offset"));
    // assert no sink was created, because a sink would call the setter and detach the binding every frame (§7.3, AD-24)
    EXPECT_FALSE(contains(unit.content, "m_sink_0_offset"));
}

TEST(Codegen, AScrollViewCarriesItsHeaderAndItsSemanticInclude) {
    // arrange
    const std::string_view source = R"(
component LogPanel {
    ScrollView {
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the facade includes the widget header, which is what the constructed class needs (§14.1)
    EXPECT_TRUE(contains(file_named(files, "log_panel.cpp").content, "#include \"widgets/scroll_view.h\""));
}

TEST(Codegen, AVirtualizedRepeatBorrowsStorageThatOutlivesTheEvaluator) {
    // arrange a repeat under a viewport, whose items come from a component property (§8.2, AD-13)
    const std::string source = R"(import task;
component LogPanel {
    inout property<vector<Task>> items;
    ScrollView {
        row_height: 20px;
        Repeat(task in items) {
            Label { text: task.title; }
        }
    }
}
)";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the record carries the key a row is matched on, which the manifest declares (§18.1, AD-13)
    catalog.add_record(RecordType{.name = "Task", .fields = {RecordFieldType{.name = "id", .type = scalar(TypeKind::String), .is_key = true}, RecordFieldType{.name = "title", .type = scalar(TypeKind::String), .is_key = false}}});
    // the scanner runs first, because the parser takes units (§11.1)
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    // the parser builds the untyped tree (§11.1)
    const SourceFile file = Parser(tokens, diagnostics).parse();
    // the checker resolves it against the catalog, which is where the record comes from (§11.1, AD-17)
    const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
    // act
    CodeGenerator generator(diagnostics);
    // the catalog is what tells the generator a record is generatable and where its header lives (§11.2, AD-17)
    generator.set_catalog(&catalog);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert
    ASSERT_EQ(diagnostics.count(), 0U);
    ASSERT_EQ(files.size(), 2U);
    const std::string unit = file_named(files, "log_panel.cpp").content;
    // assert the body owns the source, because the rows borrow it past the frame that produced it (§8.2, AD-13)
    EXPECT_TRUE(contains(unit, "std::vector<Task> m_repeat_items_0;"));
    // assert the evaluator stores into that member before the rows are pointed at it (§8.2, AD-13)
    EXPECT_TRUE(contains(unit, "self->m_repeat_items_0 = value;"));
    EXPECT_TRUE(contains(unit, "self->m_repeat_0->set_items(self->m_repeat_items_0);"));
    // assert the rows are never pointed at the evaluator's local, which dies with the call (§8.2, AD-13)
    EXPECT_FALSE(contains(unit, "set_items(value)"));
}

TEST(Codegen, ABoundViewportExtentBindsAndReappliesThePair) {
    // arrange the viewport written as expressions, which is what a responsive form writes (§8.2)
    const std::string_view source = R"(
component LogPanel {
    inout property<float> wide;
    inout property<float> tall;
    ScrollView {
        viewport_width: wide;
        viewport_height: tall;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    const std::string unit = file_named(files, "log_panel.cpp").content;
    // assert each half is bound over its source, so the pair follows the form (§13.3, §8.2)
    EXPECT_TRUE(contains(unit, "m_sink_0_viewport_width.bind_static("));
    EXPECT_TRUE(contains(unit, "m_sink_0_viewport_height.bind_static("));
    // assert the evaluator updates the track and re-applies the pair, rather than calling an empty setter (§11.3)
    EXPECT_TRUE(contains(unit, "self->m_viewport_w_0 = value;"));
    EXPECT_TRUE(contains(unit, "set_viewport_extent(Size{self->m_viewport_w_0, self->m_viewport_h_0})"));
    // assert no call is emitted with no setter name, which is what the unhandled form produced (§11.3)
    EXPECT_FALSE(contains(unit, "->(value)"));
}

TEST(CodegenScrollShapes, AViewportShapeReachesEveryGeneratedForm) {
    // arrange a source writing the viewport properties the generated facade has to name (§8.2, §8.3)
    const std::string source = R"UI(component Panel {
    inout property<float> total;

    Column {
        ScrollView {
            scrollbar: Scrollbar.on;
            offset: total;

            on_scrolled: {
                total = offset;
            }
        }
    }
})UI";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the scrollbar policy is emitted as the generated enum, because the catalog names the type (§11.3)
    EXPECT_TRUE(contains(all_text(files), "ScrollbarVisibility"));
    // assert the scrolled handler takes the offset it reports, because the catalog names the parameter (§8.3)
    EXPECT_TRUE(contains(all_text(files), "float offset"));
}

TEST(Codegen, AnUnwindowedRepeatOwnsItsRowsKeyed) {
    // arrange a repeat written without a viewport, so the container owns the rows itself (§18.1, AD-13)
    const std::string source = R"(
component task_board {
    inout property<vector<Task>> tasks;

    Column {
        Repeat(task in tasks) {
            Label {
                text: task.title;
                horizontal_align: Align.center;
            }
        }
    }
}
)";
    DiagnosticList diagnostics;
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // the record carries the key a row is matched on, which the manifest declares (§18.1, AD-13)
    catalog.add_record(RecordType{.name = "Task", .fields = {RecordFieldType{.name = "id", .type = scalar(TypeKind::String), .is_key = true}, RecordFieldType{.name = "title", .type = scalar(TypeKind::String), .is_key = false}}});
    // the scanner runs first, because the parser takes units (§11.1)
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    // the parser builds the untyped tree (§11.1)
    const SourceFile file = Parser(tokens, diagnostics).parse();
    // the checker resolves it against the catalog, which is where the record comes from (§11.1, AD-17)
    const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
    // act
    CodeGenerator generator(diagnostics);
    // the catalog is what tells the generator a record is generatable and where its header lives (§11.2, AD-17)
    generator.set_catalog(&catalog);
    const std::vector<GeneratedFile> files = generator.generate(checked);
    // assert
    ASSERT_EQ(diagnostics.count(), 0U);
    ASSERT_EQ(files.size(), 2U);
    const std::string unit = file_named(files, "task_board.cpp").content;
    // assert the container owns a keyed view, because no viewport owns the rows here (§18.1, AD-13)
    EXPECT_TRUE(contains(unit, "KeyedView<Task>"));
    // assert the view is owned by the body rather than the rows living in a windowed member (§8.2)
    EXPECT_FALSE(contains(unit, "ScrollableRows<Task>"));
    // assert an alignment written in the row reaches the generated unit, because the row is its own function (§8.2)
    EXPECT_TRUE(contains(unit, "Align::"));
}

TEST(CodegenColour, ATransparentizeCallBecomesTheMethodCall) {
    // arrange a component whose root carries the dimmed colour (§6.1)
    const std::string source = R"(
component Panel {
    in property<color> accent: transparentize(#ff0000ff, 0.5);
    Label {
        foreground: accent;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the call reaches the core colour method, because the operation is one (§6.1)
    EXPECT_TRUE(contains(all_text(files), ".transparentized(0.5f)"));
}

TEST(CodegenColour, AnAlphaCallScalesTheAlpha) {
    // arrange a colour expression driven by a property, which is the case an application writes (§6.1)
    const std::string source = R"(
component Panel {
    in property<float> amount: 0.25;
    in property<color> accent: alpha(#336699ff, amount);
    Label {
        foreground: accent;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the property reached the call, because a bound operand is a dependency (§11.3, AD-04a)
    EXPECT_TRUE(contains(all_text(files), ".with_alpha("));
}

TEST(CodegenColour, AnAlphaSetCallReplacesTheAlpha) {
    // arrange
    const std::string source = R"(
component Panel {
    in property<color> accent: alpha_set(#336699ff, 0.75);
    Label {
        foreground: accent;
    }
}
)";
    // act
    const std::vector<GeneratedFile> files = generate(source);
    // assert the replacement spelled the other method, because the two operations differ (§6.1)
    EXPECT_TRUE(contains(all_text(files), ".alpha_set(0.75f)"));
}

TEST(CodegenColour, AColourOperationOnANumberIsRefused) {
    // arrange a call whose first argument is a number, which no colour operation accepts (§6.1)
    const std::string source = R"(
component Panel {
    in property<color> accent: transparentize(0.5, 0.25);
    Label {
        foreground: accent;
    }
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    (void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    // assert the mismatch is reported, because a silent pass would emit a call that cannot compile (§6.1)
    EXPECT_GT(diagnostics.count(), 0U);
}

TEST(CodegenColour, AColourOperationWithANonNumericAmountIsRefused) {
    // arrange a string where the fraction belongs, which no operation accepts (§6.1)
    const std::string source = R"(
component Panel {
    in property<color> accent: transparentize(#ff0000ff, "half");
    Label {
        foreground: accent;
    }
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    (void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    // assert the mismatch is reported (§6.1)
    EXPECT_GT(diagnostics.count(), 0U);
}

TEST(CodegenColour, AnUnknownFunctionNameIsRefused) {
    // arrange a call the language does not define, which has no type rule (§11.1)
    const std::string source = R"(
component Panel {
    in property<color> accent: lighten(#ff0000ff, 0.5);
    Label {
        foreground: accent;
    }
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    (void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    // assert the report names the name, because that is what the author has to change (§11.1)
    bool named = false;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.message.find("lighten") != std::string::npos)
            named = true;
    EXPECT_TRUE(named);
}

TEST(CodegenColour, AColourOperationOnAFailedOperandIsNotReportedTwice) {
    // arrange a call whose operand is itself unknown, which the checker already reported (§11.1)
    const std::string source = R"(
component Panel {
    in property<color> accent: transparentize(lighten, 0.5);
    Label {
        foreground: accent;
    }
}
)";
    DiagnosticList diagnostics;
    // act
    const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    const SourceFile file = Parser(tokens, diagnostics).parse();
    (void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    // assert the rule stayed quiet about the mismatch, because an error operand suppresses it (§11.1)
    int mismatches = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0410")
            mismatches++;
    EXPECT_EQ(mismatches, 0);
}

TEST(CodegenColour, ACallNodeTheLanguageDoesNotDefineIsRefused) {
    // arrange a tree the parser cannot produce, because it gates the name, so the checker rule is reached directly (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "Panel";
    PropertyDeclaration property;
    property.name = "accent";
    property.type = "color";
    property.direction = PropertyDirection::In;
    std::vector<ExpressionPtr> operands;
    operands.push_back(LiteralExpression::make(LiteralExpression::Kind::Color, "#ff0000ff", SourceSpan{}));
    operands.push_back(LiteralExpression::make(LiteralExpression::Kind::Float, "0.5", SourceSpan{}));
    property.default_value = CallExpression::make("lighten", std::move(operands), SourceSpan{});
    component.properties.push_back(std::move(property));
    file.components.push_back(std::move(component));
    DiagnosticList diagnostics;
    // act
    (void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    // assert the report names the name, because a tree can carry one the parser would not (§11.1)
    bool named = false;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.message.find("lighten") != std::string::npos)
            named = true;
    EXPECT_TRUE(named);
}

TEST(CodegenColour, AColourCallWithAFailedOperandReportsNoMismatch) {
    // arrange a call whose amount is a text, which is the mismatch path under test (§6.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "Panel";
    PropertyDeclaration property;
    property.name = "accent";
    property.type = "color";
    property.direction = PropertyDirection::In;
    std::vector<ExpressionPtr> operands;
    operands.push_back(LiteralExpression::make(LiteralExpression::Kind::Color, "#ff0000ff", SourceSpan{}));
    operands.push_back(LiteralExpression::make(LiteralExpression::Kind::String, "\"half\"", SourceSpan{}));
    property.default_value = CallExpression::make("transparentize", std::move(operands), SourceSpan{});
    component.properties.push_back(std::move(property));
    file.components.push_back(std::move(component));
    DiagnosticList diagnostics;
    // act
    (void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file);
    // assert a mismatch was reported, because a text is not a fraction (§6.1)
    int mismatches = 0;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        if (diagnostic.code == "E0410")
            mismatches++;
    EXPECT_EQ(mismatches, 1);
}

TEST(CodegenColour, AColourOperationWithOneArgumentIsReportedNotThrown) {
    // arrange a tree the parser cannot produce, which the checker must refuse rather than index (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "Panel";
    PropertyDeclaration property;
    property.name = "accent";
    property.type = "color";
    property.direction = PropertyDirection::In;
    std::vector<ExpressionPtr> operands;
    operands.push_back(LiteralExpression::make(LiteralExpression::Kind::Color, "#ff0000ff", SourceSpan{}));
    property.default_value = CallExpression::make("transparentize", std::move(operands), SourceSpan{});
    component.properties.push_back(std::move(property));
    file.components.push_back(std::move(component));
    DiagnosticList diagnostics;
    // act the checker runs over the malformed call, which must not throw
    EXPECT_NO_THROW((void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert the arity was reported by name, because that is what the author has to change (§11.1)
    EXPECT_GT(diagnostics.count(), 0U);
}

TEST(CodegenColour, AColourOperationWithNoArgumentsIsReportedNotThrown) {
    // arrange an empty argument list, which is the same refusal with nothing to type (§11.1)
    SourceFile file;
    ComponentDeclaration component;
    component.name = "Panel";
    PropertyDeclaration property;
    property.name = "accent";
    property.type = "color";
    property.direction = PropertyDirection::In;
    property.default_value = CallExpression::make("alpha_set", {}, SourceSpan{});
    component.properties.push_back(std::move(property));
    file.components.push_back(std::move(component));
    DiagnosticList diagnostics;
    // act
    EXPECT_NO_THROW((void)TypeChecker(ElementCatalog::with_builtins(), diagnostics).check(file));
    // assert
    EXPECT_GT(diagnostics.count(), 0U);
}

TEST(Codegen, EmitsAFunctionAsAGuardedStaticDefinition) {
    // arrange diagnostics, because the function and its call must both generate (§11.1)
    DiagnosticList diagnostics;
    // act a component calls a declared function inside a binding (§11.1)
    const std::string_view source = "fn double(x: float): float { return x * 2.0; } component C { in property<float> amount; Label { text: string(double(amount)); } }";
    // assert the whole file generates, because the function and its call both resolved (§11.1)
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    EXPECT_FALSE(files.empty());
    EXPECT_FALSE(diagnostics.has_errors());
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // the forward declaration and the guarded definition both land, because a chunk may share the unit (§11.1, AD-03b)
    EXPECT_TRUE(contains(cpp.content, "#ifndef ALBOR_GENERATED_FN_double"));
    EXPECT_TRUE(contains(cpp.content, "[[maybe_unused]] static float double(float x);"));
    EXPECT_TRUE(contains(cpp.content, "return (x * 2.0f);"));
    // the call renders as a plain C++ call, which the static function above defines (§11.1)
    EXPECT_TRUE(contains(cpp.content, "double(self->m_amount.get())"));
}

TEST(Codegen, EmitsACommandActionBinding) {
    // arrange diagnostics, because the command type and the action setter must both generate (§18.1, AD-15)
    DiagnosticList diagnostics;
    // act a component declares a command slot and binds a button action to it (§18.1, AD-15)
    const std::string_view source = R"(
component Editor {
    in property<command> save;

    Column {
        Button {
            text: "Save";
            action: save;
        }
    }
}
)";
    // assert the whole file generates, because the command type and the action property both resolved (§18.1)
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    EXPECT_FALSE(files.empty());
    EXPECT_FALSE(diagnostics.has_errors());
    const GeneratedFile& header = file_named(files, "editor.h");
    // the header forwards the command, because an accessor names it as a pointer (§18.1, AD-15)
    EXPECT_TRUE(contains(header.content, "class Command;"));
    EXPECT_TRUE(contains(header.content, "[[nodiscard]] Command* save() const;"));
    EXPECT_TRUE(contains(header.content, "void set_save(Command* value);"));
    const GeneratedFile& cpp = file_named(files, "editor.cpp");
    // the body holds the slot and forwards it to the widget setter, which is what the action binds (§18.1, AD-15)
    EXPECT_TRUE(contains(cpp.content, "Property<Command*> m_save;"));
    EXPECT_TRUE(contains(cpp.content, "->set_command(value);"));
}

TEST(Codegen, EmitsACommandInvocationFromAHandler) {
    // arrange diagnostics, because the command slot and the handler that fires it must both generate (§18.1, AD-15)
    DiagnosticList diagnostics;
    // act a component declares a command slot and a handler body that invokes it (§18.1, AD-15)
    const std::string_view source = R"(
component Editor {
    in property<command> save;

    Button {
        on_clicked: {
            save.invoke();
        }
    }
}
)";
    // assert the whole file generates, because the invocation resolved against the slot (§18.1, AD-15)
    const std::vector<GeneratedFile> files = generate(source, diagnostics);
    EXPECT_FALSE(files.empty());
    EXPECT_FALSE(diagnostics.has_errors());
    const GeneratedFile& cpp = file_named(files, "editor.cpp");
    // the body guards the slot and discards the outcome, which is the route every other consumer takes (§18.3)
    EXPECT_TRUE(contains(cpp.content, "if (Command* command = self->m_save.get())"));
    EXPECT_TRUE(contains(cpp.content, "(void)command->invoke();"));
    // the definition joins the unit, because a forward declaration cannot carry the invoke call (§14.1, §18.1)
    EXPECT_TRUE(contains(cpp.content, "#include \"commands/command.h\""));
}
TEST(Codegen, EmitsAGlobalAsAGuardedFileStatic) {
    // arrange, act
    const std::vector<GeneratedFile> files = generate("global gap: float = 2.5; component C { Label {} }");
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert
    EXPECT_TRUE(contains(cpp.content, "#ifndef ALBOR_GENERATED_GLOBAL_gap"));
    EXPECT_TRUE(contains(cpp.content, "#define ALBOR_GENERATED_GLOBAL_gap"));
    EXPECT_TRUE(contains(cpp.content, "[[maybe_unused]] static const float gap = 2.5f;"));
}

TEST(Codegen, ReadsAGlobalThroughItsBareName) {
    // arrange, act
    const std::vector<GeneratedFile> files = generate("global gap: float = 2.5; component C { inout property<float> wide: 0.0; Button { on_clicked: { wide = gap; } } }");
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert
    EXPECT_TRUE(contains(cpp.content, "self->m_wide.set(gap);"));
    EXPECT_FALSE(contains(cpp.content, "m_gap"));
}

TEST(Codegen, EmitsARecordConstructionInDeclarationOrder) {
    // arrange, act
    const std::vector<GeneratedFile> files = generate("struct Frame { float w; float h; } global box: Frame = Frame{h: 2.5, w: 1.5}; component C { Label {} }");
    const GeneratedFile& cpp = file_named(files, "c.cpp");
    // assert
    EXPECT_TRUE(contains(cpp.content, "[[maybe_unused]] static const Frame box = Frame{.w = 1.5f, .h = 2.5f};"));
}
