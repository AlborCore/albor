#include "devrt/element_table.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/align.h"
#include "core/paint.h"
#include "layout/column_layout.h"
#include "layout/grid_layout.h"
#include "layout/padding_layout.h"
#include "layout/row_layout.h"
#include "layout/stack_layout.h"
#include "runtime/binding_engine.h"
#include "text/text_engine.h"
#include "ui/ui_node.h"
#include "widgets/button.h"
#include "widgets/canvas.h"
#include "widgets/checkbox.h"
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

namespace albor::devrt
{
    namespace
    {
        // read a float out of a serialized assignment, reporting a value the language never wrote (§12.1)
        bool read_float(std::string_view value, float& out) {
            // the text is trimmed, because an assignment carries the spacing of the definition it was written in (§11.1)
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
            // an empty assignment is a value the language never wrote, so the property keeps what it had (§11.1)
            if (value.empty())
                return false;
            // the text is copied, because the c library reads a terminated buffer rather than a view (§12.1)
            const std::string text(value);
            char* end = nullptr;
            // the parse reports a range the language never writes through the error number, which starts clean (§12.1)
            errno = 0;
            // the parse reads the c locale, which is the locale the serializer writes its decimal point in (§12.1)
            const float parsed = std::strtof(text.c_str(), &end);
            // a parse counts only when it consumed the whole text and stayed in range (§12.1)
            const bool complete = errno != ERANGE && end == text.c_str() + text.size();
            // the caller's value is replaced only when the whole text was read, so a failed parse changes nothing (§12.1)
            out = complete ? parsed : out;
            return complete;
        }

        // read a bool out of a serialized assignment, where the language writes the two words and nothing else (§11.1)
        bool read_bool(std::string_view value, bool& out) {
            // the word decides, so a value outside the set leaves the property as it was (§11.1)
            if (value == "true") {
                out = true;
                return true;
            }
            if (value == "false") {
                out = false;
                return true;
            }
            return false;
        }

        // read a whole number out of a serialized assignment, which a selected index is written as (§11.1)
        bool read_int(std::string_view value, long long& out) {
            // the text is trimmed, because an assignment carries the spacing of the definition it was written in (§11.1)
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
            // an empty assignment is a value the language never wrote, so the property keeps what it had (§11.1)
            if (value.empty())
                return false;
            // the parse is over the trimmed view, because the library wants a contiguous range (§12.1)
            const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), out);
            // a trailing remainder means the text was not a number, which is reported rather than half applied (§12.1)
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
        }

        // read a colour out of a serialized assignment, which the language writes as three channel floats (§11.1)
        bool read_color(std::string_view value, Color& out) {
            // the channels are collected first, because a partial triple is not a colour (§11.1)
            std::vector<std::string_view> parts;
            std::size_t start = 0;
            while (true) {
                const std::size_t comma = value.find(',', start);
                if (comma == std::string_view::npos) {
                    parts.push_back(value.substr(start));
                    break;
                }
                parts.push_back(value.substr(start, comma - start));
                start = comma + 1;
            }
            // the language writes exactly three channels, so anything else is a value it never wrote (§11.1)
            if (parts.size() != 3u)
                return false;
            // the channels are read in order, because the position is the channel (§11.1)
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            if (!read_float(parts[0], red) || !read_float(parts[1], green) || !read_float(parts[2], blue))
                return false;
            // the factory clamps into the unit range, which is what every fill in the framework carries (§6.3)
            out = Color::rgb(red, green, blue);
            return true;
        }

        // read an alignment out of a serialized assignment, which the language writes as the member name (§8.2)
        bool read_align(std::string_view value, Align& out) {
            // the closed set is scanned, because the name is the member (§8.2)
            const std::pair<std::string_view, Align> members[] = {
                {"inherit", Align::Inherit}, {"start", Align::Start}, {"center", Align::Center}, {"end", Align::End}, {"stretch", Align::Stretch},
            };
            for (const std::pair<std::string_view, Align>& member : members) {
                if (member.first == value) {
                    out = member.second;
                    return true;
                }
            }
            return false;
        }

        // read a scrollbar policy out of a serialized assignment, which is a named member of §8
        bool read_scrollbar(std::string_view value, ScrollbarVisibility& out) {
            // the closed set is scanned, because the name is the member (§8)
            const std::pair<std::string_view, ScrollbarVisibility> members[] = {
                {"as_needed", ScrollbarVisibility::AsNeeded},
                {"always_off", ScrollbarVisibility::AlwaysOff},
                {"always_on", ScrollbarVisibility::AlwaysOn},
            };
            for (const std::pair<std::string_view, ScrollbarVisibility>& member : members) {
                if (member.first == value) {
                    out = member.second;
                    return true;
                }
            }
            return false;
        }

        // apply a property only when the node is the element that declares it, because the name is per element (§11.1)
        template <class Widget, class Setter>
        PropertySetter setter(Setter set) {
            // the type decides whether the assignment belongs, which is what keeps one table from aliasing another (§11.1)
            return [set](UiNode& node, std::string_view value) {
                // the dynamic type is the element the definition named, and a mismatch is a value for nothing (§11.1)
                auto* widget = dynamic_cast<Widget*>(&node);
                if (widget == nullptr)
                    return false;
                // the key is the element and the property, because the name alone is ambiguous across elements (§11.1)
                return set(*widget, value);
            };
        }

        // the closed builtin set of §8, which a Dev build resolves by name and a Release never carries (AD-01)
        const std::unordered_map<std::string_view, NodeFactory>& factories() {
            // the table is built once, because the builtin set is closed (§8)
            static const std::unordered_map<std::string_view, NodeFactory> table{
                // the containers carry no engine, because they only arrange the children they are given (§8.2)
                {"Column", [](std::string_view, BindingEngine&, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<ColumnLayout>()); }},
                {"Row", [](std::string_view, BindingEngine&, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<RowLayout>()); }},
                {"Padding", [](std::string_view, BindingEngine&, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<PaddingLayout>()); }},
                {"Stack", [](std::string_view, BindingEngine&, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<StackLayout>()); }},
                {"Grid", [](std::string_view, BindingEngine&, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<GridLayout>()); }},
                // a label measures its text, so it takes both engines (§9.1)
                {"Label", [](std::string_view, BindingEngine& engine, TextEngine& text) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<Label>(&engine, &text)); }},
                // a button measures its label and reacts, so it takes both engines (§8.3, §9.1)
                {"Button", [](std::string_view, BindingEngine& engine, TextEngine& text) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<Button>(&engine, &text)); }},
                // a node outside the closed set has no element, which a caller treats as unnameable (§8)
                {"Slider", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<Slider>(&engine)); }},
                {"ProgressBar", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<ProgressBar>(&engine)); }},
                {"CheckBox", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<CheckBox>(&engine)); }},
                {"Canvas", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<Canvas>(&engine)); }},
                {"ScrollView", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<ScrollView>(&engine)); }},
                // a list hosts its rows and owns one selection, so it takes the engine (§8, §18.3)
                {"ListBox", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<ListBox>(&engine)); }},
                // a field measures and edits text, so it takes both engines (§8, §9.1)
                {"TextField", [](std::string_view, BindingEngine& engine, TextEngine& text) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<TextField>(&engine, &text)); }},
                // a tab view owns one selected page and draws its labels, so it takes the engine (§8)
                {"TabView", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<TabView>(&engine)); }},
                // a tab page measures its label, so it takes both engines (§8, §9.1)
                {"Tab", [](std::string_view, BindingEngine& engine, TextEngine& text) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<TabPage>(&engine, &text)); }},
                // a tab title is a container for the strip content, so it takes the engine (§8)
                {"TabTitle", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<TabTitle>(&engine)); }},
                // a radio group owns one selected choice and lays out its children (§8)
                {"RadioGroup", [](std::string_view, BindingEngine& engine, TextEngine&) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<RadioGroup>(&engine)); }},
                // a radio button measures its label, so it takes both engines (§8, §9.1)
                {"RadioButton", [](std::string_view, BindingEngine& engine, TextEngine& text) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<RadioButton>(&engine, &text)); }},
                // a numeric stepper measures its value, so it takes both engines (§8, §9.1)
                {"NumericStepper", [](std::string_view, BindingEngine& engine, TextEngine& text) -> std::unique_ptr<UiNode> { return std::unique_ptr<UiNode>(std::make_unique<NumericStepper>(&engine, &text)); }},
            };
            return table;
        }

        // the per element property catalogue of §8, which only a Dev build resolves by name (§12.1, AD-01)
        const std::unordered_map<std::string, PropertySetter>& setters() {
            // the key is the element and the property, because the name alone is ambiguous across elements (§11.1)
            static const std::unordered_map<std::string, PropertySetter> table{
                // the containers arrange, so their properties are the gaps and insets of §8.2
                {"Column.spacing", setter<ColumnLayout>([](ColumnLayout& node, std::string_view value) {
                     float gap = 0.0f;
                     if (!read_float(value, gap))
                         return false;
                     node.set_gap(gap);
                     return true;
                 })},
                {"Column.padding", setter<ColumnLayout>([](ColumnLayout& node, std::string_view value) {
                     float padding = 0.0f;
                     if (!read_float(value, padding))
                         return false;
                     node.set_padding(padding);
                     return true;
                 })},
                {"Row.spacing", setter<RowLayout>([](RowLayout& node, std::string_view value) {
                     float gap = 0.0f;
                     if (!read_float(value, gap))
                         return false;
                     node.set_gap(gap);
                     return true;
                 })},
                {"Grid.spacing", setter<GridLayout>([](GridLayout& node, std::string_view value) {
                     float gap = 0.0f;
                     if (!read_float(value, gap))
                         return false;
                     node.set_gap(gap);
                     return true;
                 })},
                {"Padding.padding", setter<PaddingLayout>([](PaddingLayout& node, std::string_view value) {
                     float padding = 0.0f;
                     if (!read_float(value, padding))
                         return false;
                     node.set_padding(padding);
                     return true;
                 })},
                // a label presents text, so its properties are the text and the two type attributes of §9.1
                {"Label.text", setter<Label>([](Label& node, std::string_view value) {
                     node.set_text(std::string(value));
                     return true;
                 })},
                {"Label.foreground", setter<Label>([](Label& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_foreground(color);
                     return true;
                 })},
                {"Label.font_size", setter<Label>([](Label& node, std::string_view value) {
                     float size = 0.0f;
                     if (!read_float(value, size))
                         return false;
                     node.set_font_size(size);
                     return true;
                 })},
                // a button presents a label and a fill, which is what §8.4 styles (§8)
                {"Button.text", setter<Button>([](Button& node, std::string_view value) {
                     node.set_label(std::string(value));
                     return true;
                 })},
                {"Button.background", setter<Button>([](Button& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_background(color);
                     return true;
                 })},
                {"Button.disabled", setter<Button>([](Button& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                // a slider presents a value and two fills, which is what its track and thumb are painted with (§8)
                {"Slider.value", setter<Slider>([](Slider& node, std::string_view value) {
                     float next = 0.0f;
                     if (!read_float(value, next))
                         return false;
                     node.set_value(next);
                     return true;
                 })},
                {"Slider.disabled", setter<Slider>([](Slider& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                {"Slider.track_color", setter<Slider>([](Slider& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_track_color(color);
                     return true;
                 })},
                {"Slider.thumb_color", setter<Slider>([](Slider& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_thumb_color(color);
                     return true;
                 })},
                // a progress bar shows a fraction against two fills (§8)
                {"ProgressBar.value", setter<ProgressBar>([](ProgressBar& node, std::string_view value) {
                     float next = 0.0f;
                     if (!read_float(value, next))
                         return false;
                     node.set_value(next);
                     return true;
                 })},
                {"ProgressBar.disabled", setter<ProgressBar>([](ProgressBar& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                {"ProgressBar.track_color", setter<ProgressBar>([](ProgressBar& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_track_color(color);
                     return true;
                 })},
                {"ProgressBar.fill_color", setter<ProgressBar>([](ProgressBar& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_fill_color(color);
                     return true;
                 })},
                // a tab view owns one selected page, which the author may write first (§8)
                {"TabView.selected", setter<TabView>([](TabView& node, std::string_view value) {
                     long long index = 0;
                     if (!read_int(value, index))
                         return false;
                     node.set_selected(static_cast<int>(index));
                     return true;
                 })},
                // a tab view runs its strip down the left side when the author asks for it (§8)
                {"TabView.vertical", setter<TabView>([](TabView& node, std::string_view value) {
                     bool vertical = false;
                     if (!read_bool(value, vertical))
                         return false;
                     node.set_vertical(vertical);
                     return true;
                 })},
                // a tab page carries the label its strip draws (§8)
                {"Tab.label", setter<TabPage>([](TabPage& node, std::string_view value) {
                     node.set_label(std::string(value));
                     return true;
                 })},
                // a radio group owns one selected value and lays out its choices (§8)
                {"RadioGroup.selected", setter<RadioGroup>([](RadioGroup& node, std::string_view value) {
                     long long selected = 0;
                     if (!read_int(value, selected))
                         return false;
                     node.set_selected(static_cast<int>(selected));
                     return true;
                 })},
                {"RadioGroup.vertical", setter<RadioGroup>([](RadioGroup& node, std::string_view value) {
                     bool vertical = false;
                     if (!read_bool(value, vertical))
                         return false;
                     node.set_vertical(vertical);
                     return true;
                 })},
                {"RadioGroup.spacing", setter<RadioGroup>([](RadioGroup& node, std::string_view value) {
                     float spacing = 0.0f;
                     if (!read_float(value, spacing))
                         return false;
                     node.set_spacing(spacing);
                     return true;
                 })},
                // a radio choice carries a value, a label and a disabled state (§8)
                {"RadioButton.value", setter<RadioButton>([](RadioButton& node, std::string_view value) {
                     long long choice = 0;
                     if (!read_int(value, choice))
                         return false;
                     node.set_value(static_cast<int>(choice));
                     return true;
                 })},
                {"RadioButton.label", setter<RadioButton>([](RadioButton& node, std::string_view value) {
                     node.set_label(std::string(value));
                     return true;
                 })},
                {"RadioButton.disabled", setter<RadioButton>([](RadioButton& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                // a numeric stepper carries a value, a range and a step (§8)
                {"NumericStepper.value", setter<NumericStepper>([](NumericStepper& node, std::string_view value) {
                     float next = 0.0f;
                     if (!read_float(value, next))
                         return false;
                     node.set_value(next);
                     return true;
                 })},
                {"NumericStepper.min", setter<NumericStepper>([](NumericStepper& node, std::string_view value) {
                     float minimum = 0.0f;
                     if (!read_float(value, minimum))
                         return false;
                     node.set_minimum(minimum);
                     return true;
                 })},
                {"NumericStepper.max", setter<NumericStepper>([](NumericStepper& node, std::string_view value) {
                     float maximum = 0.0f;
                     if (!read_float(value, maximum))
                         return false;
                     node.set_maximum(maximum);
                     return true;
                 })},
                {"NumericStepper.step", setter<NumericStepper>([](NumericStepper& node, std::string_view value) {
                     float step = 0.0f;
                     if (!read_float(value, step))
                         return false;
                     node.set_step(step);
                     return true;
                 })},
                {"NumericStepper.disabled", setter<NumericStepper>([](NumericStepper& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                // a checkbox presents its state and two fills, which is what §8.4 styles (§8)
                {"CheckBox.checked", setter<CheckBox>([](CheckBox& node, std::string_view value) {
                     bool checked = false;
                     if (!read_bool(value, checked))
                         return false;
                     node.set_checked(checked);
                     return true;
                 })},
                {"CheckBox.disabled", setter<CheckBox>([](CheckBox& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                {"CheckBox.outline", setter<CheckBox>([](CheckBox& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_outline(color);
                     return true;
                 })},
                {"CheckBox.check_fill", setter<CheckBox>([](CheckBox& node, std::string_view value) {
                     Color color;
                     if (!read_color(value, color))
                         return false;
                     node.set_check_fill(color);
                     return true;
                 })},
                // a viewport presents its scrollbar and the rows it measures, which is what §8.2 arranges (§8)
                {"ScrollView.overscan", setter<ScrollView>([](ScrollView& node, std::string_view value) {
                     float pixels = 0.0f;
                     if (!read_float(value, pixels))
                         return false;
                     node.set_overscan(pixels);
                     return true;
                 })},
                {"ScrollView.scrollbar", setter<ScrollView>([](ScrollView& node, std::string_view value) {
                     ScrollbarVisibility visibility;
                     if (!read_scrollbar(value, visibility))
                         return false;
                     node.set_scrollbar_visibility(visibility);
                     return true;
                 })},
                {"ScrollView.row_height", setter<ScrollView>([](ScrollView& node, std::string_view value) {
                     float pixels = 0.0f;
                     if (!read_float(value, pixels))
                         return false;
                     node.set_row_height(pixels);
                     return true;
                 })},
                // a list owns one selection and shapes its rows, which is what §8.3 navigates (§8, §18.3)
                {"ListBox.selected", setter<ListBox>([](ListBox& node, std::string_view value) {
                     long long index = 0;
                     if (!read_int(value, index))
                         return false;
                     // a negative index is no selection at all, so it clears rather than clamps (§18.3)
                     if (index < 0) {
                         node.clear_selection();
                         return true;
                     }
                     node.set_selected_index(static_cast<std::size_t>(index));
                     return true;
                 })},
                {"ListBox.row_height", setter<ListBox>([](ListBox& node, std::string_view value) {
                     float pixels = 0.0f;
                     if (!read_float(value, pixels))
                         return false;
                     node.set_row_height(pixels);
                     return true;
                 })},
                {"ListBox.overscan", setter<ListBox>([](ListBox& node, std::string_view value) {
                     float pixels = 0.0f;
                     if (!read_float(value, pixels))
                         return false;
                     node.set_overscan(pixels);
                     return true;
                 })},
                {"ListBox.scrollbar", setter<ListBox>([](ListBox& node, std::string_view value) {
                     ScrollbarVisibility visibility;
                     if (!read_scrollbar(value, visibility))
                         return false;
                     node.set_scrollbar_visibility(visibility);
                     return true;
                 })},
                {"ListBox.disabled", setter<ListBox>([](ListBox& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                // a field edits text, so its properties are the value and the editing attributes of §8
                {"TextField.value", setter<TextField>([](TextField& node, std::string_view value) {
                     node.set_value(std::string(value));
                     return true;
                 })},
                {"TextField.placeholder", setter<TextField>([](TextField& node, std::string_view value) {
                     node.set_placeholder(std::string(value));
                     return true;
                 })},
                {"TextField.read_only", setter<TextField>([](TextField& node, std::string_view value) {
                     bool read_only = false;
                     if (!read_bool(value, read_only))
                         return false;
                     node.set_read_only(read_only);
                     return true;
                 })},
                {"TextField.multiline", setter<TextField>([](TextField& node, std::string_view value) {
                     bool multiline = false;
                     if (!read_bool(value, multiline))
                         return false;
                     node.set_multiline(multiline);
                     return true;
                 })},
                {"TextField.disabled", setter<TextField>([](TextField& node, std::string_view value) {
                     bool disabled = false;
                     if (!read_bool(value, disabled))
                         return false;
                     node.set_disabled(disabled);
                     return true;
                 })},
                {"TextField.font_size", setter<TextField>([](TextField& node, std::string_view value) {
                     float size = 0.0f;
                     if (!read_float(value, size))
                         return false;
                     node.set_font_size(size);
                     return true;
                 })},
                // the two alignments every element declares, so they resolve for any element rather than per one (§8.2)
                {"*.horizontal_align",
                 [](UiNode& node, std::string_view value) {
                     Align align;
                     if (!read_align(value, align))
                         return false;
                     node.set_horizontal_align(align);
                     return true;
                 }},
                {"*.vertical_align",
                 [](UiNode& node, std::string_view value) {
                     Align align;
                     if (!read_align(value, align))
                         return false;
                     node.set_vertical_align(align);
                     return true;
                 }},
            };
            return table;
        }
    } // namespace

    std::unique_ptr<UiNode> create_node(std::string_view element, BindingEngine& engine, TextEngine& text) {
        // the closed set decides, because a name outside it is a definition the runtime never wrote (§8)
        const auto found = factories().find(element);
        // an unknown element is refused rather than guessed, because a guess would replace a subtree silently (§12.1)
        if (found == factories().end())
            return nullptr;
        // the factory builds over the engines the window owns, because a widget borrows them for the whole run (§8.1)
        return found->second(element, engine, text);
    }

    bool known_element(std::string_view element) {
        // the closed set decides, because the builtin elements are the ones a definition may name (§8)
        return factories().find(element) != factories().end();
    }

    bool apply_property(UiNode& node, std::string_view element, std::string_view property, std::string_view value) {
        // the per element entry is tried first, because a property belongs to the element that declares it (§11.1)
        const std::string key = std::string(element) + "." + std::string(property);
        const auto found = setters().find(key);
        if (found != setters().end()) {
            // the setter rejects a node whose dynamic type is not the element, so nothing is applied by name alone (§11.1)
            return found->second(node, value);
        }
        // the common entry is a wildcard, because the two alignments resolve for every element of §8.2
        const auto common = setters().find(std::string("*.") + std::string(property));
        // a name outside both sets is a property no element declares, which is reported rather than ignored (§11.1)
        if (common == setters().end())
            return false;
        // the wildcard setter answers whether the value was one the alignment holds (§11.1)
        return common->second(node, value);
    }

    std::string_view element_of(const UiNode& node) {
        // the dynamic type decides, because a node is one element of the closed set and nothing else (§8)
        const std::pair<const UiNode*, std::string_view> elements[] = {
            {dynamic_cast<const ColumnLayout*>(&node), "Column"}, {dynamic_cast<const RowLayout*>(&node), "Row"}, {dynamic_cast<const PaddingLayout*>(&node), "Padding"}, {dynamic_cast<const StackLayout*>(&node), "Stack"}, {dynamic_cast<const GridLayout*>(&node), "Grid"}, {dynamic_cast<const Label*>(&node), "Label"}, {dynamic_cast<const Button*>(&node), "Button"}, {dynamic_cast<const Slider*>(&node), "Slider"}, {dynamic_cast<const CheckBox*>(&node), "CheckBox"}, {dynamic_cast<const Canvas*>(&node), "Canvas"}, {dynamic_cast<const ScrollView*>(&node), "ScrollView"}, {dynamic_cast<const ListBox*>(&node), "ListBox"}, {dynamic_cast<const TabView*>(&node), "TabView"}, {dynamic_cast<const TabPage*>(&node), "Tab"}, {dynamic_cast<const TabTitle*>(&node), "TabTitle"}, {dynamic_cast<const RadioGroup*>(&node), "RadioGroup"}, {dynamic_cast<const RadioButton*>(&node), "RadioButton"}, {dynamic_cast<const NumericStepper*>(&node), "NumericStepper"}, {dynamic_cast<const TextField*>(&node), "TextField"},
        };
        // the first match wins, because the widget types above are disjoint classes (§8)
        for (const std::pair<const UiNode*, std::string_view>& element : elements) {
            if (element.first != nullptr)
                return element.second;
        }
        // a node outside the closed set has no element, which a caller treats as unnameable (§8)
        return std::string_view{};
    }
} // namespace albor::devrt
