#include "automation/host/input_commands.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "application/application.h"
#include "application/window_stage.h"
#include "focus/focus_manager.h"
#include "semantics/semantic_provider.h"
#include "ui/caret_target.h"
#include "ui/ui_node.h"

namespace albor::automation
{
    namespace
    {
        // the family the typing command sits under, which keeps the allow list readable (§10.3)
        constexpr std::string_view FAMILY = "input.";

        // the argument a command needs, which is refused rather than defaulted (§10.3)
        std::optional<std::string> argument_at(const AutomationRequest& request, std::size_t index) {
            // an index past the end names no argument, which is a client mistake (§10.3)
            if (index >= request.arguments.size())
                return std::nullopt;
            // an empty argument is still an argument a caller may have meant (§10.3)
            return request.arguments[index];
        }

        // find the node carrying an author assigned id, which is the only selector automation offers (AD-11)
        UiNode* node_by_id(UiNode& root, std::string_view id) {
            // the first node carrying the identity is the control, because an author id is unique (AD-11)
            UiNode* found = nullptr;
            root.visit([&found, id](UiNode& node) {
                // a match already found is not replaced, which keeps the first node in tree order (AD-11)
                if (found == nullptr && node.identity().explicit_id == id)
                    found = &node;
            });
            return found;
        }

        // the bytes one utf8 sequence occupies, read from its leading byte (§9.1)
        std::size_t sequence_length(unsigned char lead) {
            // an ascii byte is one code point, which is the common case of typing (§9.1)
            if ((lead & 0x80u) == 0)
                return 1;
            // a two byte sequence starts with 110xxxxx (§9.1)
            if ((lead & 0xE0u) == 0xC0)
                return 2;
            // a three byte sequence starts with 1110xxxx (§9.1)
            if ((lead & 0xF0u) == 0xE0)
                return 3;
            // a four byte sequence starts with 11110xxx, and anything else is taken one byte at a time (§9.1)
            if ((lead & 0xF8u) == 0xF0)
                return 4;
            return 1;
        }
    } // namespace

    std::vector<std::string> input_command_names() {
        // the name is fixed rather than discovered, because a host allow-lists before any tree exists (§10.3, AD-10)
        return {std::string(FAMILY) + "type"};
    }

    void register_input_commands(AutomationRegistry& registry, InputRoot root) {
        // the typing command focuses the node and delivers the text the way a keyboard does (§8.3, §10.3)
        registry.register_command(std::string(FAMILY) + "type", [root](const AutomationRequest& request) {
            // a host that supplied no root cannot answer, which is refused rather than answered empty (§10.3)
            if (!root)
                return AutomationResult::refused("the host supplied no input target");
            // the live tree is taken once, because the application may rebuild it between lines (§10.3)
            UiNode* active = root();
            if (active == nullptr)
                return AutomationResult::refused("no window carries a tree");
            // the id is required, because a typing command targets one node (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("type needs an id");
            // the text is required, and empty text is a client that meant to type nothing (§10.3)
            const auto text = argument_at(request, 1);
            // a missing text is refused, because guessing one would type text nobody asked for (§10.3)
            if (!text)
                return AutomationResult::refused("type needs text");
            // a text that was not escaped arrives as extra arguments, which is refused rather than truncated (§10.3)
            if (request.arguments.size() > 2)
                return AutomationResult::refused("type takes an id and text");
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            UiNode* node = node_by_id(*active, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // a node that edits no document would swallow every typed byte, which is refused rather than typed into (§8.3)
            if (dynamic_cast<const TextInputTarget*>(node) == nullptr)
                return AutomationResult::refused("the node edits no text: " + *id);
            // a read only or disabled field refuses the edit, which the client reads before it types (§10.1)
            if (const auto* provider = dynamic_cast<const SemanticProvider*>(node); provider != nullptr) {
                // the published state is what the field announces, so it is what the command respects (§10.1)
                const SemanticNode published = provider->semantic_node();
                // a disabled control refuses every action, which the client is told apart from read only (§10.1)
                if (published.disabled())
                    return AutomationResult::refused("the node is disabled: " + *id);
                // a read only control takes focus but refuses the edit (§10.1)
                if (published.read_only())
                    return AutomationResult::refused("the node is read only: " + *id);
            }
            // a node that cannot take the caret would swallow nothing, which is refused rather than typed into (§8.3)
            if (!FocusManager::focusable(*node))
                return AutomationResult::refused("the node cannot take focus: " + *id);
            // the window and its stage are what the typing path reaches, because focus belongs to the stage (§8.3)
            Application& application = Application::instance();
            const WindowHandle handle = application.window_for_node(*node);
            // a window a modal constrains receives no input, so typing into it would lie about what happened (§5.1)
            if (application.input_blocked(handle))
                return AutomationResult::refused("a modal window blocks the node: " + *id);
            // a node outside a stage cannot be focused, which a detached tree is (§8.1)
            WindowStage* stage = application.stage(handle);
            if (stage == nullptr)
                return AutomationResult::refused("the node belongs to no window: " + *id);
            // the caret moves to the node, which is what typing means for a client (§8.3)
            stage->focus().request_focus(node);
            // one event per code point is what a keyboard reports, and it keeps the field's real edit path (§8.3)
            for (std::size_t index = 0; index < text->size();) {
                // a continuation byte cannot start a sequence, so its length is one (§9.1)
                const std::size_t length = std::min(sequence_length(static_cast<unsigned char>((*text)[index])), text->size() - index);
                // the event carries the code point as the utf8 the input path expects (§8.3)
                Event event{.kind = EventKind::TextInput, .text = text->substr(index, length)};
                // the application brackets the delivery, so a handler may close the dialog safely (§4.2)
                (void)application.deliver_input(handle, event);
                // the next code point follows (§9.1)
                index += length;
            }
            // the typing reports nothing, because its effect is what the client observes in the next dump (§10.3)
            return AutomationResult::answered("");
        });
    }
} // namespace albor::automation
