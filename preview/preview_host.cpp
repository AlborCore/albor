#include "preview/preview_host.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "compiler/ast.h"
#include "compiler/codegen/codegen.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"
#include "i18n/catalog.h"
#include "layout/column_layout.h"
#include "layout/grid_layout.h"
#include "layout/padding_layout.h"
#include "layout/row_layout.h"
#include "layout/stack_layout.h"
#include "text/text_engine.h"
#include "widgets/button.h"
#include "widgets/canvas.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/scroll_view.h"
#include "widgets/slider.h"
#include "widgets/text_field.h"

namespace albor::preview
{
    namespace
    {
        // the number a written literal spells, which is the same reading codegen performs (§11.3, AD-27)
        double number_of(const compiler::LiteralExpression& literal) {
            // a length carries a unit tail, which the checker validated and codegen strips (§11.3, AD-27)
            std::string_view text = literal.text;
            // either unit is trimmed, because only the number reaches the runtime (§11.3, AD-27)
            if (text.size() >= 2 && (text.substr(text.size() - 2) == "px" || text.substr(text.size() - 2) == "phx"))
                text.remove_suffix(2);
            // the remainder is the number, which is what a length resolves to (§11.3, AD-27)
            return std::strtod(std::string(text).c_str(), nullptr);
        }

        // report whether a written length used the physical suffix, which the framework refuses (§8.2, AD-27)
        bool physical(const compiler::CheckedAssignment& assignment) {
            // the value is read as a literal, because only a literal carries a unit (§8.2)
            const auto* literal = dynamic_cast<const compiler::LiteralExpression*>(assignment.value.get());
            // a value that is not a literal has no unit (§8.2)
            if (literal == nullptr)
                return false;
            // the physical suffix is what the checker rejects, so a preview must not draw it (§8.2, AD-27)
            return literal->text.size() >= 3 && literal->text.substr(literal->text.size() - 3) == "phx";
        }

        // the text a written value carries, which is what a label shows (§12.2, §9.1)
        std::string text_of(const compiler::Expression& expression) {
            // a plain literal is the common case in a preview, because no binding drives it here (§12.2)
            if (const auto* literal = dynamic_cast<const compiler::LiteralExpression*>(&expression); literal != nullptr)
                // the spelling is the text, because a string literal is written as it reads (§12.2)
                return literal->text;
            // a translation resolves through the catalogue, because that is what the macro means (§9.2, §12.2)
            if (const auto* translation = dynamic_cast<const compiler::TranslationExpression*>(&expression); translation != nullptr)
                // the catalogue decides, which is the same lookup the compiled path performs (§9.2)
                return i18n::translate(translation->text);
            // anything else has no static text, which a preview shows empty rather than guessing (§12.2)
            return {};
        }

        // build one node from a checked element, which instantiates the real widget (§12.2)
        std::unique_ptr<UiNode> instantiate(const compiler::CheckedNode& node, BindingEngine& engine, TextEngine* text_engine) {
            // the widget is built by element name, which is the whole of the factory (§12.2)
            std::unique_ptr<UiNode> built;
            // the containers are layouts, which take no engine (§8)
            if (node.element_type == "Column")
                built = std::make_unique<ColumnLayout>();
            else if (node.element_type == "Row")
                built = std::make_unique<RowLayout>();
            else if (node.element_type == "Padding")
                built = std::make_unique<PaddingLayout>();
            else if (node.element_type == "Stack")
                built = std::make_unique<StackLayout>();
            else if (node.element_type == "Grid")
                built = std::make_unique<GridLayout>();
            // a widget takes the engine, and text taking widgets take the text engine too (§9.1, AD-14)
            else if (node.element_type == "Label")
                built = std::make_unique<Label>(&engine, text_engine);
            else if (node.element_type == "Button")
                built = std::make_unique<Button>(&engine, text_engine);
            else if (node.element_type == "Slider")
                built = std::make_unique<Slider>(&engine);
            else if (node.element_type == "CheckBox")
                built = std::make_unique<CheckBox>(&engine);
            else if (node.element_type == "Canvas")
                built = std::make_unique<Canvas>(&engine);
            else if (node.element_type == "ScrollView")
                built = std::make_unique<ScrollView>(&engine);
            else if (node.element_type == "TextField")
                built = std::make_unique<TextField>(&engine, text_engine);
            else {
                // an element this build does not know produces nothing, which the diagnostics already reported (§12.2)
                return nullptr;
            }
            // the author identity is applied, because a preview addresses by it the way automation does (§18.1, AD-11)
            if (!node.explicit_id.empty())
                built->set_explicit_id(node.explicit_id);
            // each written assignment is applied, which is what makes the preview show the author's values (§12.2)
            for (const compiler::CheckedAssignment& assignment : node.assignments) {
                // a physical length is skipped, because the checker refuses one (§8.2, AD-27)
                if (physical(assignment))
                    continue;
                // a written number is read once, because every numeric property goes through it (§12.2)
                const auto* numeric = dynamic_cast<const compiler::LiteralExpression*>(assignment.value.get());
                // the spacing a column or row was written with, which is what the layout metrics hold (§8.2)
                if (assignment.name == "spacing" || assignment.name == "gap") {
                    // a non numeric spacing is skipped, because the checker already reported it (§12.2)
                    if (numeric == nullptr)
                        continue;
                    // a column takes its gap, which is the space between children (§8.2)
                    if (auto* column = dynamic_cast<ColumnLayout*>(built.get()))
                        column->set_gap(static_cast<float>(number_of(*numeric)));
                    // a row takes the same gap in the other axis (§8.2)
                    else if (auto* row = dynamic_cast<RowLayout*>(built.get()))
                        row->set_gap(static_cast<float>(number_of(*numeric)));
                    // a grid takes the same gap between its cells (§8.2)
                    else if (auto* grid = dynamic_cast<GridLayout*>(built.get()))
                        grid->set_gap(static_cast<float>(number_of(*numeric)));
                }
                // the padding a container was written with, which wraps its children (§8.2)
                else if (assignment.name == "padding") {
                    // a non numeric padding is skipped, because the checker already reported it (§12.2)
                    if (numeric == nullptr)
                        continue;
                    // a padding layout takes the inset directly (§8.2)
                    if (auto* padding = dynamic_cast<PaddingLayout*>(built.get()))
                        padding->set_padding(static_cast<float>(number_of(*numeric)));
                    // a column takes its padding as an inset around the children (§8.2)
                    else if (auto* column = dynamic_cast<ColumnLayout*>(built.get()))
                        column->set_padding(static_cast<float>(number_of(*numeric)));
                }
                // a written run, which a label shows and a button carries (§9.1, §8)
                else if (assignment.name == "text") {
                    // a label shows the written run (§9.1)
                    if (auto* label = dynamic_cast<Label*>(built.get()))
                        label->set_text(text_of(*assignment.value));
                    // a button carries the written run as its label (§8)
                    else if (auto* button = dynamic_cast<Button*>(built.get()))
                        button->set_label(text_of(*assignment.value));
                }
                // the size a run was written at, which only a label carries (§9.1)
                else if (assignment.name == "font_size" && numeric != nullptr) {
                    // the label that carries a size is the only one that takes it (§9.1)
                    if (auto* label = dynamic_cast<Label*>(built.get()))
                        label->set_font_size(static_cast<float>(number_of(*numeric)));
                }
                // a checked state, which a preview shows as the author wrote it (§8)
                else if (assignment.name == "checked" && numeric != nullptr) {
                    // the checkbox is the only widget that carries the state (§8)
                    if (auto* box = dynamic_cast<CheckBox*>(built.get()))
                        box->set_checked(numeric->text == "true");
                }
                // a written value or hint, which a field shows as the author wrote it (§8)
                else if (assignment.name == "value" || assignment.name == "placeholder") {
                    // the field is the only widget that edits a string (§8)
                    if (auto* field = dynamic_cast<TextField*>(built.get())) {
                        // the value is what the field edits, and the hint is what an empty one shows (§8)
                        if (assignment.name == "value")
                            field->set_value(text_of(*assignment.value));
                        else
                            field->set_placeholder(text_of(*assignment.value));
                    }
                }
                // a read only or multi line state, which a preview shows as written (§8)
                else if ((assignment.name == "read_only" || assignment.name == "multiline") && numeric != nullptr) {
                    // the field is the only widget that carries the states (§8)
                    if (auto* field = dynamic_cast<TextField*>(built.get())) {
                        // read only refuses edits but still takes focus, and multiline wraps (§8)
                        field->set_read_only(assignment.name == "read_only" && numeric->text == "true");
                        field->set_multiline(assignment.name == "multiline" && numeric->text == "true");
                    }
                }
                // a font size, which a field carries beside a label (§9.1)
                else if (assignment.name == "font_size" && numeric != nullptr) {
                    // the label was answered above, so the field takes the size here (§9.1)
                    if (auto* field = dynamic_cast<TextField*>(built.get()))
                        field->set_font_size(static_cast<float>(number_of(*numeric)));
                }
            }
            // each child is built and adopted, which is the preorder a compiled build uses (§11.3)
            for (const std::shared_ptr<compiler::CheckedNode>& child : node.children) {
                // the child is built by the same rule, which is the whole of the recursion (§12.2)
                std::unique_ptr<UiNode> built_child = instantiate(*child, engine, text_engine);
                // a child that could not be built is skipped, which the diagnostics already reported (§12.2)
                if (built_child == nullptr)
                    continue;
                // the child is adopted, which takes ownership (§8.1)
                built->add_child(std::move(built_child));
            }
            // the node is returned whole (§12.2)
            return built;
        }
    } // namespace

    PreviewResult build(const std::string& source, BindingEngine& engine) {
        // the diagnostics accumulate, because a preview window shows them rather than swallowing them (§12.2)
        compiler::DiagnosticList diagnostics;
        // the front end runs in process, which makes this a design time tool and not a shipped path (§12.2)
        const std::vector<compiler::Token> tokens = compiler::Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const compiler::SourceFile file = compiler::Parser(tokens, diagnostics).parse();
        // the checker resolves it, which is what carries the property types (§11.1)
        const compiler::CheckedFile checked = compiler::TypeChecker(compiler::ElementCatalog::with_builtins(), diagnostics).check(file);
        // the generator runs too, because a diagnostic may come only from codegen (§11.3)
        (void)compiler::CodeGenerator(diagnostics).generate(checked);
        // the result is reported even when nothing could be built, because the window shows the count (§12.2)
        PreviewResult result;
        // the count is kept, which is what a designer reads (§12.2)
        result.diagnostics = diagnostics.count();
        // a file with errors builds nothing, because half a component would mislead a designer (§12.2)
        if (diagnostics.count() != 0 || checked.components.empty())
            return result;
        // the first component is built, because a preview shows one component at a time (§12.2)
        const compiler::CheckedComponent& component = checked.components.front();
        // a component with no root describes nothing (§12.2)
        if (component.root == nullptr)
            return result;
        // the root is built with the real widgets, which is what makes the preview trustworthy (§12.2)
        result.root = instantiate(*component.root, engine, nullptr);
        // the result is returned whole (§12.2)
        return result;
    }
} // namespace albor::preview
