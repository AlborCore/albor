#include "preview/preview_host.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "compiler/ast.h"
#include "compiler/codegen/codegen.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/static_expression.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"
#include "i18n/catalog.h"
#include "layout/column_layout.h"
#include "layout/grid_layout.h"
#include "layout/padding_layout.h"
#include "layout/row_layout.h"
#include "layout/stack_layout.h"
#include "render/image_resource.h"
#include "resources/embedded_image.h"
#include "resources/utf8_path.h"
#include "text/text_engine.h"
#include "widgets/button.h"
#include "widgets/canvas.h"
#include "widgets/checkbox.h"
#include "widgets/image.h"
#include "widgets/label.h"
#include "widgets/list_box.h"
#include "widgets/numeric_stepper.h"
#include "widgets/progress_bar.h"
#include "widgets/radio_button.h"
#include "widgets/radio_group.h"
#include "widgets/scroll_view.h"
#include "widgets/slider.h"
#include "widgets/tab_page.h"
#include "widgets/tab_title.h"
#include "widgets/tab_view.h"
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

        // the number an expression spells, including a written negation or grouping, which a preview can resolve (§12.2)
        bool number_of_expression(const compiler::Expression& expression, double& result) {
            // a literal is the common case, because a preview shows the author's static values (§12.2)
            if (const auto* literal = dynamic_cast<const compiler::LiteralExpression*>(&expression); literal != nullptr) {
                // the literal text is the number, with any length suffix removed by the shared reader (§12.2)
                result = number_of(*literal);
                return true;
            }
            // a negation flips the operand, because the parser keeps the sign outside the literal (§11.1)
            if (const auto* unary = dynamic_cast<const compiler::UnaryExpression*>(&expression); unary != nullptr && unary->op == compiler::UnaryOperator::Negate)
                return number_of_expression(*unary->value, result) ? (result = -result, true) : false;
            // a grouping is transparent to the number, because the parentheses change no value (§11.1)
            if (const auto* grouping = dynamic_cast<const compiler::GroupingExpression*>(&expression); grouping != nullptr)
                return number_of_expression(*grouping->inner, result);
            // anything else is a binding, which the preview does not evaluate (§12.2)
            return false;
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

        // resolve an embedded asset beside the source, which is where a saved ui file keeps its images (§12.2, §6.4)
        std::filesystem::path asset_path(const compiler::ImageExpression& image, const std::filesystem::path& source_path) {
            // the literal path is locale independent, because it arrived as utf-8 bytes (§6.4)
            const std::filesystem::path path = resources::path_from_utf8(image.path);
            // an unsaved source has no directory, so the path is resolved as written (§12.2)
            if (source_path.empty())
                return path;
            // a saved source resolves the asset relative to itself, which is what the build does (§6.4)
            return (source_path.parent_path() / path).lexically_normal();
        }

        // build one node from a checked element, which instantiates the real widget (§12.2)
        std::unique_ptr<UiNode> instantiate(const compiler::CheckedNode& node, BindingEngine& engine, TextEngine* text_engine, const std::filesystem::path& source_path) {
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
            else if (node.element_type == "ProgressBar")
                built = std::make_unique<ProgressBar>(&engine);
            else if (node.element_type == "CheckBox")
                built = std::make_unique<CheckBox>(&engine);
            else if (node.element_type == "Canvas")
                built = std::make_unique<Canvas>(&engine);
            else if (node.element_type == "Image")
                built = std::make_unique<Image>(&engine);
            else if (node.element_type == "ScrollView")
                built = std::make_unique<ScrollView>(&engine);
            else if (node.element_type == "ListBox")
                built = std::make_unique<ListBox>(&engine);
            else if (node.element_type == "TextField")
                built = std::make_unique<TextField>(&engine, text_engine);
            else if (node.element_type == "TabView")
                built = std::make_unique<TabView>(&engine);
            else if (node.element_type == "Tab")
                built = std::make_unique<TabPage>(&engine, text_engine);
            else if (node.element_type == "TabTitle")
                built = std::make_unique<TabTitle>(&engine);
            else if (node.element_type == "RadioGroup")
                built = std::make_unique<RadioGroup>(&engine);
            else if (node.element_type == "RadioButton")
                built = std::make_unique<RadioButton>(&engine, text_engine);
            else if (node.element_type == "NumericStepper")
                built = std::make_unique<NumericStepper>(&engine, text_engine);
            else {
                // an element this build does not know produces nothing, which the diagnostics already reported (§12.2)
                return nullptr;
            }
            // the author identity is applied, because a preview addresses by it the way automation does (§18.1, AD-11)
            if (!node.explicit_id.empty())
                built->set_explicit_id(node.explicit_id);
            // a stepper takes its bounds before its value, because a clamp against defaults would lose it (§8)
            if (auto* stepper = dynamic_cast<NumericStepper*>(built.get()); stepper != nullptr) {
                for (const compiler::CheckedAssignment& assignment : node.assignments) {
                    // only a static bound can be applied here, because a preview evaluates no binding (§12.2)
                    double number = 0.0;
                    if (!number_of_expression(*assignment.value, number) || (assignment.name != "min" && assignment.name != "max" && assignment.name != "step"))
                        continue;
                    // the authored range and step land before the value they contain (§8)
                    if (assignment.name == "min")
                        stepper->set_minimum(static_cast<float>(number));
                    else if (assignment.name == "max")
                        stepper->set_maximum(static_cast<float>(number));
                    else
                        stepper->set_step(static_cast<float>(number));
                }
            }
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
                    // a radio group takes its gap between choices (§8.2)
                    else if (auto* group = dynamic_cast<RadioGroup*>(built.get()))
                        group->set_spacing(static_cast<float>(number_of(*numeric)));
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
                // the size a run was written at, which a label and a field both carry (§9.1)
                else if (assignment.name == "font_size" && numeric != nullptr) {
                    // the label that shows text takes the size it was written at (§9.1)
                    if (auto* label = dynamic_cast<Label*>(built.get()))
                        label->set_font_size(static_cast<float>(number_of(*numeric)));
                    // the field that edits text takes the same size, because one number names one widget (§9.1)
                    else if (auto* field = dynamic_cast<TextField*>(built.get()))
                        field->set_font_size(static_cast<float>(number_of(*numeric)));
                }
                // a progress fraction, which a preview shows as the author wrote it (§8)
                else if (auto* progress = dynamic_cast<ProgressBar*>(built.get()); progress != nullptr && assignment.name == "value" && numeric != nullptr) {
                    // the progress bar is the read only widget that carries the fraction (§8)
                    progress->set_value(static_cast<float>(number_of(*numeric)));
                }
                // an image source, name or explicit size, which the preview shows as the author wrote it (§6.4, §8)
                else if (auto* image = dynamic_cast<Image*>(built.get()); image != nullptr && (assignment.name == "source" || assignment.name == "description" || assignment.name == "width" || assignment.name == "height")) {
                    // a written asset resolves beside its source file, which is where the build finds it (§6.4)
                    if (assignment.name == "source") {
                        // only an embedded asset can be resolved, because the preview evaluates no binding (§12.2)
                        if (const compiler::ImageExpression* embedded = compiler::static_image(*assignment.value); embedded != nullptr)
                            image->set_source(resources::ImageRegistry::load_file(asset_path(*embedded, source_path)));
                    }
                    // a description reaches the widget, because the preview publishes the same tree (§10.1)
                    else if (assignment.name == "description") {
                        image->set_description(text_of(*assignment.value));
                    }
                    // a written dimension resolves like any number, where zero keeps the natural extent (§8.2)
                    else {
                        double number = 0.0;
                        if (!number_of_expression(*assignment.value, number))
                            continue;
                        if (assignment.name == "width")
                            image->set_width(static_cast<float>(number));
                        else
                            image->set_height(static_cast<float>(number));
                    }
                }
                // a numeric value, which a stepper or a radio choice carries (§8)
                else if (auto* value_stepper = dynamic_cast<NumericStepper*>(built.get()); value_stepper != nullptr && assignment.name == "value") {
                    // a static value, including a written negation, reaches the stepper (§8, §12.2)
                    double number = 0.0;
                    if (number_of_expression(*assignment.value, number))
                        value_stepper->set_value(static_cast<float>(number));
                }
                else if (auto* value_radio = dynamic_cast<RadioButton*>(built.get()); value_radio != nullptr && assignment.name == "value") {
                    // a static value, including a written negation, reaches the choice (§8, §12.2)
                    double number = 0.0;
                    if (number_of_expression(*assignment.value, number))
                        value_radio->set_value(static_cast<int>(number));
                }
                // a numeric range, which only a stepper carries (§8)
                else if (auto* range_stepper = dynamic_cast<NumericStepper*>(built.get()); range_stepper != nullptr && numeric != nullptr && (assignment.name == "min" || assignment.name == "max" || assignment.name == "step")) {
                    // the lower bound, the upper bound and the step are each one number (§8)
                    if (assignment.name == "min")
                        range_stepper->set_minimum(static_cast<float>(number_of(*numeric)));
                    else if (assignment.name == "max")
                        range_stepper->set_maximum(static_cast<float>(number_of(*numeric)));
                    else if (assignment.name == "step")
                        range_stepper->set_step(static_cast<float>(number_of(*numeric)));
                }
                // a selected tab or radio choice, which a preview shows as the author wrote it (§8)
                else if (assignment.name == "selected") {
                    // the static number is resolved through grouping and negation, as the checker accepted it (§12.2)
                    double number = 0.0;
                    if (!number_of_expression(*assignment.value, number))
                        continue;
                    // the tab view and the radio group are the two widgets that carry a single selection (§8)
                    if (auto* tabs = dynamic_cast<TabView*>(built.get()))
                        tabs->set_selected(static_cast<int>(number));
                    else if (auto* group = dynamic_cast<RadioGroup*>(built.get()))
                        group->set_selected(static_cast<int>(number));
                }
                // a vertical strip or group, which a preview shows as the author wrote it (§8)
                else if (assignment.name == "vertical" && numeric != nullptr) {
                    // the tab view and the radio group are the two widgets that carry an orientation (§8)
                    if (auto* tabs = dynamic_cast<TabView*>(built.get()))
                        tabs->set_vertical(numeric->text == "true");
                    else if (auto* group = dynamic_cast<RadioGroup*>(built.get()))
                        group->set_vertical(numeric->text == "true");
                }
                // a tab title or a radio label, which the strip or the choice draws (§8)
                else if (assignment.name == "label") {
                    // the page carries the strip title, and the choice carries its row label (§8)
                    if (auto* tab = dynamic_cast<TabPage*>(built.get()))
                        tab->set_label(text_of(*assignment.value));
                    else if (auto* label_radio = dynamic_cast<RadioButton*>(built.get()))
                        label_radio->set_label(text_of(*assignment.value));
                }
                // a disabled state, which a radio choice or a stepper carries (§8)
                else if (assignment.name == "disabled" && numeric != nullptr) {
                    // the choice and the stepper refuse input when the author disables them (§8)
                    if (auto* disabled_radio = dynamic_cast<RadioButton*>(built.get()))
                        disabled_radio->set_disabled(numeric->text == "true");
                    else if (auto* disabled_stepper = dynamic_cast<NumericStepper*>(built.get()))
                        disabled_stepper->set_disabled(numeric->text == "true");
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
                // a read only state, which a preview shows as written (§8)
                else if (assignment.name == "read_only" && numeric != nullptr) {
                    // the field is the only widget that carries it, and one assignment touches one state (§8)
                    if (auto* field = dynamic_cast<TextField*>(built.get()))
                        field->set_read_only(numeric->text == "true");
                }
                // a multi line state, which a preview shows as written beside the read only one (§8)
                else if (assignment.name == "multiline" && numeric != nullptr) {
                    // the field is the only widget that carries it, and writing it leaves read only alone (§8)
                    if (auto* field = dynamic_cast<TextField*>(built.get()))
                        field->set_multiline(numeric->text == "true");
                }
            }
            // each child is built and adopted, which is the preorder a compiled build uses (§11.3)
            for (const std::shared_ptr<compiler::CheckedNode>& child : node.children) {
                // the child is built by the same rule, which is the whole of the recursion (§12.2)
                std::unique_ptr<UiNode> built_child = instantiate(*child, engine, text_engine, source_path);
                // a child that could not be built is skipped, which the diagnostics already reported (§12.2)
                if (built_child == nullptr)
                    continue;
                // a radio group reapplies its selection when a choice is added, because a preview does no layout (§8)
                if (auto* group = dynamic_cast<RadioGroup*>(built.get())) {
                    group->add_child(std::move(built_child));
                    continue;
                }
                // the child is adopted, which takes ownership (§8.1)
                built->add_child(std::move(built_child));
            }
            // the node is returned whole (§12.2)
            return built;
        }
    } // namespace

    PreviewResult build(const std::string& source, BindingEngine& engine, const std::filesystem::path& source_path) {
        // the diagnostics accumulate, because a preview window shows them rather than swallowing them (§12.2)
        compiler::DiagnosticList diagnostics;
        // the front end runs in process, which makes this a design time tool and not a shipped path (§12.2)
        const std::vector<compiler::Token> tokens = compiler::Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const compiler::SourceFile file = compiler::Parser(tokens, diagnostics).parse();
        // the checker resolves it, which is what carries the property types (§11.1)
        const compiler::CheckedFile checked = compiler::TypeChecker(compiler::ElementCatalog::with_builtins(), diagnostics).check(file);
        // the generator runs too, because a diagnostic may come only from codegen (§11.3)
        compiler::CodeGenerator generator(diagnostics);
        // a saved source resolves its assets beside itself, which is what the build does (§12.2, §6.4)
        if (!source_path.empty())
            generator.set_source_path(source_path);
        // the generated output is discarded, because the preview only needs the diagnostics (§11.3)
        (void)generator.generate(checked);
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
        result.root = instantiate(*component.root, engine, nullptr, source_path);
        // the result is returned whole (§12.2)
        return result;
    }
} // namespace albor::preview
