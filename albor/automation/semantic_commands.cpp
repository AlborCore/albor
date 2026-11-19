#include "albor/automation/semantic_commands.h"

#include <cmath>
#include <optional>
#include <sstream>
#include <string>

#include "albor/core/json.h"

namespace albor::automation
{
    namespace
    {
        // the family every semantic command sits under, which keeps the allow list readable (§10.3)
        constexpr std::string_view FAMILY = "semantic.";

        // how a role spells in a dump, which is what a client parses (§10.1, §10.3)
        std::string role_name(SemanticRole role) {
            // a switch covers every role, because a dump must name them all (§10.1)
            switch (role) {
            case SemanticRole::None:
                return "none";
            case SemanticRole::Button:
                return "button";
            case SemanticRole::Checkbox:
                return "checkbox";
            case SemanticRole::Heading:
                return "heading";
            case SemanticRole::Label:
                return "label";
            case SemanticRole::Slider:
                return "slider";
            case SemanticRole::ProgressBar:
                return "progress_bar";
            case SemanticRole::TabView:
                return "tab_view";
            case SemanticRole::TabItem:
                return "tab_item";
            case SemanticRole::RadioGroup:
                return "radio_group";
            case SemanticRole::RadioButton:
                return "radio_button";
            case SemanticRole::NumericStepper:
                return "numeric_stepper";
            case SemanticRole::Canvas:
                return "canvas";
            case SemanticRole::TextField:
                return "textfield";
            case SemanticRole::Image:
                return "image";
            case SemanticRole::ListItem:
                return "listitem";
            case SemanticRole::List:
                return "list";
            case SemanticRole::MenuItem:
                return "menu_item";
            case SemanticRole::ComboBox:
                return "combo_box";
            case SemanticRole::Group:
                return "group";
            case SemanticRole::ScrollView:
                return "scroll_view";
            case SemanticRole::Window:
                return "window";
            }
            // a role outside the enumeration dumps as none, which is the quiet default (§10.1)
            return "none";
        }

        // find the node carrying an author assigned id, which is the only selector automation offers (AD-11)
        SemanticNode* node_by_id(SemanticTree& tree, const std::string& id) {
            // every node is scanned, because the tree is flat and the ids are the only key (§10.1)
            for (std::size_t index = 0; index < tree.size(); index++) {
                // the node is read for writing, because a state command must land on the live tree (§10.1, §10.3)
                SemanticNode* node = tree.mutable_at(index);
                // a node past the end names nothing, so it cannot match (§10.1)
                if (node == nullptr)
                    continue;
                // a node with no author id is unaddressable, because automation selects by identity (AD-11)
                if (!node->identified())
                    continue;
                // the ids match exactly, because a prefix or a case fold would select the wrong node (§18.1, AD-11)
                if (node->explicit_id() == id)
                    return node;
            }
            // no node carries that id, which the caller reports as a refusal (§10.3)
            return nullptr;
        }

        // the argument a command needs, which is refused rather than defaulted (§10.3)
        std::optional<std::string> argument_at(const AutomationRequest& request, std::size_t index) {
            // an index past the end names no argument, which is a client mistake (§10.3)
            if (index >= request.arguments.size())
                return std::nullopt;
            // the argument is present and empty is still an id a caller may have meant, so it is returned (§10.3)
            return request.arguments[index];
        }

        // the text a client parses out of one dump line, which may not carry a line break of its own (§10.3)
        std::string escape_for_line(std::string_view text) {
            // the escaped form is built once and only grows when a character needs it (§10.3)
            std::string escaped;
            escaped.reserve(text.size());
            // every byte is walked, because a multi byte character passes through as itself (§10.3)
            for (const char c : text) {
                // a backslash is escaped first, because it is the escape character itself (§10.3)
                if (c == '\\') {
                    escaped += "\\\\";
                    continue;
                }
                // the three control characters a line cannot hold each get their own spelling (§10.3)
                if (c == '\n') {
                    escaped += "\\n";
                    continue;
                }
                if (c == '\r') {
                    escaped += "\\r";
                    continue;
                }
                if (c == '\t') {
                    escaped += "\\t";
                    continue;
                }
                // every other byte is the byte itself, so plain text stays byte identical (§10.3)
                escaped += c;
            }
            return escaped;
        }
    } // namespace

    std::vector<std::string> semantic_command_names() {
        // the names are fixed rather than discovered, because a host allow-lists before any tree exists (§10.3, AD-10)
        return {std::string(FAMILY) + "tree", std::string(FAMILY) + "find", std::string(FAMILY) + "value", std::string(FAMILY) + "bounds", std::string(FAMILY) + "read", std::string(FAMILY) + "invoke", std::string(FAMILY) + "actions", std::string(FAMILY) + "set_value", std::string(FAMILY) + "set_focused"};
    }

    void register_semantic_commands(AutomationRegistry& registry, SemanticSnapshot snapshot) {
        // the dump reports every node with its role, id and name, which is what a client navigates by (§10.1, §10.3)
        registry.register_command(std::string(FAMILY) + "tree", [snapshot](const AutomationRequest&) {
            // a host that supplied no snapshot cannot answer, which is refused rather than answered empty (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the dump accumulates line by line, which is what a client parses (§10.3)
            std::ostringstream out;
            // every node contributes one line, in the tree's depth first order (§10.1)
            for (std::size_t index = 0; index < tree.size(); index++) {
                // the node at this index is read for the dump only, which never writes (§10.1)
                const SemanticNode* node = tree.at(index);
                // a missing node is skipped rather than dumped as a hole (§10.1)
                if (node == nullptr)
                    continue;
                // an unidentified node still appears, because a screen reader reaches it though automation cannot (AD-11)
                const std::string id = node->identified() ? node->explicit_id() : std::string("-");
                // the line carries role, id and name, which is what identifies a control to a reader (§10.1)
                out << role_name(node->role()) << ' ' << id << ' ' << escape_for_line(node->name());
                // a description appends the hover text a reader announces beside the name (§8.3, §10.1)
                if (!node->description().empty())
                    out << " description=" << escape_for_line(node->description());
                // an editable node appends the text it carries, which is what a client reads back (§10.1)
                if (!node->value().empty())
                    out << " value=" << escape_for_line(node->value());
                // a writable node marks itself, because a client must know a write is offered (§10.1, §10.3)
                if (node->has_value_action() && !node->disabled() && !node->read_only())
                    out << " writable";
                // a selection appends its half open byte range, which a client reads beside the value (§10.1)
                if (node->selection_start() != node->selection_end())
                    out << " selection=" << node->selection_start() << ':' << node->selection_end();
                // a read only node marks itself, because the state is what a client must respect (§10.1)
                if (node->read_only())
                    out << " read_only";
                // a multi line node marks itself, because the state is what a reader announces with (§10.1)
                if (node->multiline())
                    out << " multiline";
                // an expanded popup marks itself, because the state is what a client opens against (§10.1)
                if (node->expanded())
                    out << " expanded";
                // an indeterminate bar marks itself, because its remaining length is unknown (§10.1)
                if (node->indeterminate())
                    out << " indeterminate";
                // a disabled node marks itself, because a client must not drive one (§10.1)
                if (node->disabled())
                    out << " disabled";
                // a selected node marks itself, because the selection is what a list client reads back (§10.1)
                if (node->selected())
                    out << " selected";
                // a focused node marks itself, because the focus is where the keyboard lands (§10.1)
                if (node->focused())
                    out << " focused";
                // the line ends, which is what separates one node from the next (§10.3)
                out << '\n';
            }
            // the whole dump is one payload, which the transport frames as a single line (§10.3)
            return AutomationResult::answered(out.str());
        });

        // the lookup reports whether an id is present, which is how a client asserts on a tree (§10.3)
        registry.register_command(std::string(FAMILY) + "find", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a lookup with no key would be a scan (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused, because guessing one would select the wrong node (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("find needs an id");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // a found node answers with its role and name, which is what the client wanted to confirm (§10.3)
            return AutomationResult::answered(role_name(node->role()) + " " + node->name());
        });

        // the value command answers the node's primary value with its kind, which a typed test reads (§10.1, §10.3)
        registry.register_command(std::string(FAMILY) + "value", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a value belongs to one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("value needs an id");
            // a value query takes one selector, so a second argument is a client mistake (§10.3)
            if (request.arguments.size() > 1)
                return AutomationResult::refused("value takes an id");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // a bounded number is the value a slider or stepper publishes, which a client reads as a number (§10.1)
            if (node->has_range()) {
                // a non finite number has no decimal spelling, so it is refused rather than answered as text (§10.3)
                if (!std::isfinite(node->range().value))
                    return AutomationResult::refused("the value is not a finite number: " + *id);
                // the number is spelled as one, which is what the number kind carries (§10.3)
                return AutomationResult::answered(ValueKind::Number, core::Json(static_cast<double>(node->range().value)).to_string());
            }
            // a checkable node publishes a tri state, where the settled half is a boolean (§10.1)
            if (node->check() == CheckState::Checked)
                return AutomationResult::answered(ValueKind::Boolean, "yes");
            // an unchecked node is the false half of the same boolean (§10.1)
            if (node->check() == CheckState::Unchecked)
                return AutomationResult::answered(ValueKind::Boolean, "no");
            // an indeterminate check has no boolean answer, so it is named rather than forced into one (§10.1)
            if (node->check() == CheckState::Indeterminate)
                return AutomationResult::answered(ValueKind::Text, "indeterminate");
            // an editable or text carrying node publishes a string, which is the value a form test reads (§10.1)
            if (!node->value().empty() || node->role() == SemanticRole::TextField)
                return AutomationResult::answered(ValueKind::Text, node->value());
            // a node with no value state has nothing to answer, which is refused rather than answered empty (§10.1)
            return AutomationResult::refused("no value on node: " + *id);
        });

        // the bounds command answers the arranged rect, which a client reads before it aims a pointer (§8.2, §10.3)
        registry.register_command(std::string(FAMILY) + "bounds", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a rect belongs to one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("bounds needs an id");
            // a bounds query takes one selector, so a second argument is a client mistake (§10.3)
            if (request.arguments.size() > 1)
                return AutomationResult::refused("bounds takes an id");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // the rect is one object, because a client reads the four fields together (§10.3)
            core::Json bounds;
            // the x is the logical window coordinate the layout assigned (§8.2)
            bounds.set("x", core::Json(static_cast<double>(node->bounds().x)));
            // the y is the second logical window coordinate (§8.2)
            bounds.set("y", core::Json(static_cast<double>(node->bounds().y)));
            // the width is the arranged extent, which a pointer target needs (§8.2)
            bounds.set("width", core::Json(static_cast<double>(node->bounds().width)));
            // the height is the other extent, which adjacency reads (§8.2)
            bounds.set("height", core::Json(static_cast<double>(node->bounds().height)));
            // the object is answered as json, which is the typed shape a rect takes (§10.3)
            return AutomationResult::answered(ValueKind::Json, bounds.to_string());
        });

        // the generic read answers one typed property of one node, which is the read a state assertion needs (§10.3)
        registry.register_command(std::string(FAMILY) + "read", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a property belongs to one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("read needs an id");
            // the property is required, because one generic read serves every state (§10.3)
            const auto property = argument_at(request, 1);
            // a missing property is refused, because guessing one would read the wrong state (§10.3)
            if (!property)
                return AutomationResult::refused("read needs a property");
            // a read takes an id and a property, so a third argument is a client mistake (§10.3)
            if (request.arguments.size() > 2)
                return AutomationResult::refused("read takes an id and a property");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // a focused node answers the boolean it holds, which is the keyboard focus (§10.1)
            if (*property == "focused")
                return AutomationResult::answered(ValueKind::Boolean, node->focused() ? "yes" : "no");
            // a selected node answers the boolean it holds, which a list client reads (§10.1)
            if (*property == "selected")
                return AutomationResult::answered(ValueKind::Boolean, node->selected() ? "yes" : "no");
            // a disabled node answers the boolean it holds, which a client must not drive against (§10.1)
            if (*property == "disabled")
                return AutomationResult::answered(ValueKind::Boolean, node->disabled() ? "yes" : "no");
            // a read only node answers the boolean it holds, which still takes focus and permits a copy (§10.1)
            if (*property == "read_only")
                return AutomationResult::answered(ValueKind::Boolean, node->read_only() ? "yes" : "no");
            // a multiline node answers the boolean it holds, which a reader announces (§10.1)
            if (*property == "multiline")
                return AutomationResult::answered(ValueKind::Boolean, node->multiline() ? "yes" : "no");
            // an expanded node answers the boolean it holds, which a popup control reports (§10.1)
            if (*property == "expanded")
                return AutomationResult::answered(ValueKind::Boolean, node->expanded() ? "yes" : "no");
            // an indeterminate node answers the boolean it holds, which a bar with unknown work reports (§10.1)
            if (*property == "indeterminate")
                return AutomationResult::answered(ValueKind::Boolean, node->indeterminate() ? "yes" : "no");
            // a checkable node publishes a tri state, where the settled halves are booleans (§10.1)
            if (*property == "check") {
                // a checked node is the true half, which the boolean kind spells (§10.1)
                if (node->check() == CheckState::Checked)
                    return AutomationResult::answered(ValueKind::Boolean, "yes");
                // an unchecked node is the false half of the same boolean (§10.1)
                if (node->check() == CheckState::Unchecked)
                    return AutomationResult::answered(ValueKind::Boolean, "no");
                // only a checkable node carries the third state, so a plain button has none to answer (§10.1)
                if (node->check() != CheckState::Indeterminate)
                    return AutomationResult::refused("no check on node: " + *id);
                // an indeterminate check has no boolean answer, so it is named rather than forced into one (§10.1)
                return AutomationResult::answered(ValueKind::Text, "indeterminate");
            }
            // a selection is the half open byte range a field publishes, which a client reads as start:end (§10.1)
            if (*property == "selection")
                return AutomationResult::answered(ValueKind::Text, std::to_string(node->selection_start()) + ":" + std::to_string(node->selection_end()));
            // the role is the word the dump names, so a client reads the same spelling both ways (§10.1, §10.3)
            if (*property == "role")
                return AutomationResult::answered(ValueKind::Text, role_name(node->role()));
            // the name is the accessible label, which a client asserts without parsing the dump (§10.1)
            if (*property == "name")
                return AutomationResult::answered(ValueKind::Text, node->name());
            // a range is the four numbers a bounded control publishes, which only a range carrying node answers (§10.1)
            if (*property == "range") {
                // a node with no range has nothing to answer, which is refused rather than answered as zeros (§10.1)
                if (!node->has_range())
                    return AutomationResult::refused("no range on node: " + *id);
                // a non finite bound has no decimal spelling, so it is refused rather than serialized as one (§10.3)
                const SemanticRange& range = node->range();
                if (!std::isfinite(range.value) || !std::isfinite(range.minimum) || !std::isfinite(range.maximum) || !std::isfinite(range.step))
                    return AutomationResult::refused("the range is not finite: " + *id);
                // the four fields travel as one object, because a client reads a range as a whole (§10.3)
                core::Json object;
                // the current value is what a write moves, which a client asserts after a gesture (§10.1)
                object.set("value", core::Json(range.value));
                // the minimum is the bottom the value never falls below (§10.1)
                object.set("minimum", core::Json(range.minimum));
                // the maximum is the top the value never rises above (§10.1)
                object.set("maximum", core::Json(range.maximum));
                // the step is the distance one increment moves the value (§10.1)
                object.set("step", core::Json(range.step));
                // the object is answered as json, which is the typed shape a range takes (§10.3)
                return AutomationResult::answered(ValueKind::Json, object.to_string());
            }
            // a word no branch above answered is a client mistake, which is refused with the name it wrote (§10.3)
            return AutomationResult::refused("no such property: " + *property);
        });

        // the listing reports what a node can be asked to do, which a client reads before acting (§10.3)
        registry.register_command(std::string(FAMILY) + "actions", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a listing needs a node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("actions needs an id");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // the names accumulate, because a node may expose more than one action (§10.1)
            std::string names;
            // each action is listed by name, and identifiers are one based because zero names no action (§10.1)
            for (SemanticActionId action = 1; action <= node->action_count(); action++) {
                // the separator joins the names, which is what makes them one payload (§10.3)
                if (!names.empty())
                    names.push_back(' ');
                // the name is appended, which is the whole of the listing (§10.3)
                names += node->action_name(action);
            }
            // the listing is answered, and an empty listing is still an answer (§10.3)
            return AutomationResult::answered(names);
        });

        // the invocation runs one registered action, which is how automation presses a control (§10.3)
        registry.register_command(std::string(FAMILY) + "invoke", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because an invocation targets one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("invoke needs an id");
            // the action name is required, because a node may expose several (§10.1)
            const auto action = argument_at(request, 1);
            // a missing action is refused, because guessing one would press the wrong control (§10.3)
            if (!action)
                return AutomationResult::refused("invoke needs an action name");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // the action runs, which is the only path into application behaviour (§10.3, AD-20)
            if (!node->invoke(*action))
                return AutomationResult::refused("no such action: " + *action);
            // an invoked action reports nothing, because its effect is what the client observes (§10.3)
            return AutomationResult::answered("");
        });

        // the value command writes the text an editable node edits, which a fill operation needs (§10.1, §10.3)
        registry.register_command(std::string(FAMILY) + "set_value", [snapshot](const AutomationRequest& request) {
            // a host that supplied no snapshot cannot answer, which is refused rather than answered empty (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a write targets one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("set_value needs an id");
            // the value is required, and an empty value is a value a client may have meant (§10.3)
            const auto value = argument_at(request, 1);
            // a missing value is refused, because guessing one would write text nobody asked for (§10.3)
            if (!value)
                return AutomationResult::refused("set_value needs a value");
            // a value that was not escaped arrives as extra arguments, which is refused rather than truncated (§10.3)
            if (request.arguments.size() > 2)
                return AutomationResult::refused("set_value takes an id and a value");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // a node with no write carries no editable value, which is refused rather than silently ignored (§10.1)
            if (!node->has_value_action())
                return AutomationResult::refused("no writable value on node: " + *id);
            // the write runs, and the control decides whether it accepted the text (§10.1)
            if (!node->write_value(*value))
                return AutomationResult::refused("the node refused the value: " + *id);
            // the write reports nothing, because its effect is what the client observes in the next dump (§10.3)
            return AutomationResult::answered("");
        });

        // the focus command records what assistive technology reported, which automation asserts against (§10.1, §10.3)
        registry.register_command(std::string(FAMILY) + "set_focused", [snapshot](const AutomationRequest& request) {
            // a host with no snapshot cannot answer, which is refused (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no semantic snapshot");
            // the id is required, because a state change targets one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("set_focused needs an id");
            // the value is required, because a guess would move focus the wrong way (§10.3)
            const auto value = argument_at(request, 1);
            // a missing value is refused rather than defaulted (§10.3)
            if (!value)
                return AutomationResult::refused("set_focused needs 0 or 1");
            // the tree is taken once, because the application may rebuild it between lines (§10.3)
            SemanticTree& tree = *snapshot();
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            SemanticNode* node = node_by_id(tree, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // only the two spellings are accepted, because anything else is a client mistake (§10.3)
            if (*value != "0" && *value != "1")
                return AutomationResult::refused("set_focused takes 0 or 1");
            // the state is recorded on the node, which is what assistive technology reads back (§10.1)
            node->mutable_states().focused = *value == "1";
            // the change is answered, so a client knows it landed (§10.3)
            return AutomationResult::answered("");
        });
    }
} // namespace albor::automation
