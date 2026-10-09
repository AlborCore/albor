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
        // the checker resolves it against the catalog, which is where the native types come from (§11.1, AD-17)
        const CheckedFile checked = TypeChecker(catalog, diagnostics).check(file);
        // the generator turns the checked file into the facade files, which is what the driver writes (§11.3)
        return CodeGenerator(diagnostics).generate(checked);
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
    EXPECT_TRUE(contains(source_file.content, "n3->set_on_click([this] { handle_3_clicked(this); });"));
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
    EXPECT_TRUE(contains(cpp.content, "std::to_string(self->m_ratio.get())"));
    EXPECT_TRUE(contains(cpp.content, "std::to_string(self->m_lead.get())"));
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
    EXPECT_TRUE(contains(cpp.content, "std::to_string(1.5f)"));
    EXPECT_TRUE(contains(cpp.content, "well_color_to_hex(Color::rgb("));
    EXPECT_TRUE(contains(cpp.content, "(true ? std::string(\"true\") : std::string(\"false\"))"));
    EXPECT_TRUE(contains(cpp.content, "(self->m_counter.get() > 2)"));
    // the widgets construct with the engines they need, which decides the arguments (§11.3)
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Slider>(engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<CheckBox>(engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Canvas>(engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Button>(engine, text_engine);"));
    EXPECT_TRUE(contains(cpp.content, "std::make_unique<Label>(engine, text_engine);"));
    // the wired callbacks carry the parameter lists of their signals (§11.3)
    EXPECT_TRUE(contains(cpp.content, "set_on_change([this](float) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "set_on_toggle([this](bool) { handle_"));
    EXPECT_TRUE(contains(cpp.content, "set_on_click([this] { handle_"));
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
