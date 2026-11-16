#include "albor/automation/host/input_commands.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/focus/focus_manager.h"
#include "albor/semantics/semantic_provider.h"
#include "albor/ui/caret_target.h"
#include "albor/ui/ui_event.h"
#include "albor/ui/ui_node.h"

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

        // parse one coordinate, which must consume the whole argument and name a finite point (§10.3)
        bool parse_float(std::string_view text, float& value) {
            // the stream is classic, because a host locale must not turn a decimal point into a comma (§10.3)
            std::istringstream stream{std::string(text)};
            // the classic locale is the one the wire spells numbers in (§10.3)
            stream.imbue(std::locale::classic());
            // the conversion reads the whole token, because a suffix would name a different point (§10.3)
            float parsed = 0.0f;
            stream >> parsed;
            // a failed conversion or a leftover suffix is a client mistake (§10.3)
            if (stream.fail() || stream.peek() != std::char_traits<char>::eof())
                return false;
            // an infinite or nan coordinate is not a point in the window (§10.3)
            if (!std::isfinite(parsed))
                return false;
            // the parsed value is handed back, which is the whole of the conversion (§10.3)
            value = parsed;
            return true;
        }

        // map the documented key name onto the portable key a normalized event carries (§8.3, §10.3)
        bool key_from_name(std::string_view name, Key& key) {
            // a single letter or digit is the common accelerator, so its enum range answers it (§18.5)
            if (name.size() == 1) {
                // a lower case letter maps onto the consecutive letter keys (§18.5)
                if (name.front() >= 'a' && name.front() <= 'z') {
                    key = static_cast<Key>(static_cast<int>(Key::A) + (name.front() - 'a'));
                    return true;
                }
                // a digit maps onto the consecutive digit keys (§18.5)
                if (name.front() >= '0' && name.front() <= '9') {
                    key = static_cast<Key>(static_cast<int>(Key::Zero) + (name.front() - '0'));
                    return true;
                }
            }
            // a function key is f followed by one or two digits, which is the spelling a shortcut names (§18.5)
            if (name.size() >= 2 && name.front() == 'f') {
                int number = 0;
                const std::from_chars_result parsed = std::from_chars(name.data() + 1, name.data() + name.size(), number);
                // the whole name must be a function key from one to twelve (§18.5)
                if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() && number >= 1 && number <= 12) {
                    key = static_cast<Key>(static_cast<int>(Key::F1) + (number - 1));
                    return true;
                }
            }
            // the named keys are a fixed vocabulary, so each one is spelled exactly once (§10.3)
            if (name == "tab") {
                key = Key::Tab;
                return true;
            }
            if (name == "return") {
                key = Key::Return;
                return true;
            }
            if (name == "escape") {
                key = Key::Escape;
                return true;
            }
            if (name == "space") {
                key = Key::Space;
                return true;
            }
            if (name == "delete") {
                key = Key::Delete;
                return true;
            }
            if (name == "backspace") {
                key = Key::Backspace;
                return true;
            }
            if (name == "home") {
                key = Key::Home;
                return true;
            }
            if (name == "end") {
                key = Key::End;
                return true;
            }
            if (name == "page_up") {
                key = Key::PageUp;
                return true;
            }
            if (name == "page_down") {
                key = Key::PageDown;
                return true;
            }
            if (name == "menu") {
                key = Key::Menu;
                return true;
            }
            if (name == "left") {
                key = Key::ArrowLeft;
                return true;
            }
            if (name == "right") {
                key = Key::ArrowRight;
                return true;
            }
            if (name == "up") {
                key = Key::ArrowUp;
                return true;
            }
            if (name == "down") {
                key = Key::ArrowDown;
                return true;
            }
            return false;
        }

        // parse the plus separated modifier list, which is the spelling a shortcut uses (§18.5, §10.3)
        bool modifiers_from_name(std::string_view names, Modifiers& modifiers) {
            // an absent list is no modifier at all, which is the common key (§10.3)
            modifiers = Modifiers{};
            // an empty argument is the same as no list, because the wire may carry an empty field (§10.3)
            if (names.empty())
                return true;
            // each segment runs to the next plus, which is the separator the vocabulary uses (§18.5)
            std::size_t start = 0;
            while (start <= names.size()) {
                // the next plus ends the current modifier, and its absence ends the list (§18.5)
                const std::size_t plus = names.find('+', start);
                // the segment names one modifier, which must be one of the four (§18.5)
                const std::string_view part = names.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start);
                // shift is the modifier a traversal reads (§8.3)
                if (part == "shift")
                    modifiers.shift = true;
                // control is the modifier a shortcut on both desktop platforms can carry (§18.5)
                else if (part == "control")
                    modifiers.control = true;
                // option is the alt key the portable event names (§8.3)
                else if (part == "option")
                    modifiers.option = true;
                // command is the meta key the portable event names (§8.3)
                else if (part == "command")
                    modifiers.command = true;
                // an unknown segment is a client mistake, which is refused rather than ignored (§10.3)
                else
                    return false;
                // the list ends when no plus follows the segment (§18.5)
                if (plus == std::string_view::npos)
                    break;
                // the next segment starts past the plus (§18.5)
                start = plus + 1;
            }
            return true;
        }

        // resolve the window a node belongs to, which is where a pointer or key event is delivered (§8.3, §10.3)
        bool resolve_window(Application& application, const UiNode& node, const std::string& id, WindowHandle& handle, std::string& error) {
            // the stage that owns the root names the window the event reaches (§5.1)
            handle = application.window_for_node(node);
            // a detached node has no window, which is refused with its selector (§10.3)
            if (!handle.valid()) {
                error = "the node belongs to no window: " + id;
                return false;
            }
            // a modal window blocks every input, so a command must not claim an event it refused (§5.1)
            if (application.input_blocked(handle)) {
                error = "a modal window blocks the node: " + id;
                return false;
            }
            // a window without a stage has no tree to route into (§6.1)
            if (application.stage(handle) == nullptr) {
                error = "the node belongs to no window: " + id;
                return false;
            }
            return true;
        }

        // find a point the node accepts as a hit, which is where a click aims (§8.3)
        bool click_point(const UiNode& node, Point& point) {
            // the arranged rect is the area a control occupies, which is what a click targets (§8.2)
            const Rect area = node.bounds();
            // an empty rect has no point to aim at (§8.2)
            if (area.empty())
                return false;
            // the centre is the ordinary target, because most controls hit their whole plate (§8.3)
            const Point centre{.x = area.x + area.width * 0.5f, .y = area.y + area.height * 0.5f};
            // a control that accepts the centre needs no further probe (§8.3)
            if (node.hit_test(centre)) {
                point = centre;
                return true;
            }
            // a control whose hit area is smaller than its bounds is probed on its edges and centre lines (§8.3)
            const Point candidates[] = {
                Point{.x = area.x + 1.0f, .y = area.y + area.height * 0.5f}, Point{.x = area.x + area.width * 0.5f, .y = area.y + 1.0f}, Point{.x = area.x + area.width - 1.0f, .y = area.y + area.height * 0.5f}, Point{.x = area.x + area.width * 0.5f, .y = area.y + area.height - 1.0f}, Point{.x = area.x + 1.0f, .y = area.y + 1.0f}, Point{.x = area.x + area.width - 1.0f, .y = area.y + 1.0f}, Point{.x = area.x + 1.0f, .y = area.y + area.height - 1.0f}, Point{.x = area.x + area.width - 1.0f, .y = area.y + area.height - 1.0f},
            };
            // the first candidate the control accepts is the point the click uses (§8.3)
            for (const Point& candidate : candidates) {
                // a candidate the control accepts is the point the click aims at (§8.3)
                if (node.hit_test(candidate)) {
                    point = candidate;
                    return true;
                }
            }
            // a control that accepts none of the probes has no clickable point (§8.3)
            return false;
        }
    } // namespace

    std::vector<std::string> input_command_names() {
        // the names are fixed rather than discovered, because a host allow-lists before any tree exists (§10.3, AD-10)
        return {std::string(FAMILY) + "type", std::string(FAMILY) + "click", std::string(FAMILY) + "pointer", std::string(FAMILY) + "key"};
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
            stage->focus().request_focus(node, FocusReason::Accessibility);
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

        // the click command presses a control at a point it accepts, which is the ordinary pointer route (§8.3, §10.3)
        registry.register_command(std::string(FAMILY) + "click", [root](const AutomationRequest& request) {
            // a host that supplied no root cannot answer, which is refused rather than answered empty (§10.3)
            if (!root)
                return AutomationResult::refused("the host supplied no input target");
            // the live tree is taken once, because the application may rebuild it between lines (§10.3)
            UiNode* active = root();
            if (active == nullptr)
                return AutomationResult::refused("no window carries a tree");
            // the id is required, because a click targets one control (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("click needs an id");
            // a click takes one selector, so a second argument is a client mistake (§10.3)
            if (request.arguments.size() > 1)
                return AutomationResult::refused("click takes an id");
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            UiNode* node = node_by_id(*active, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // the click aims at a point the control accepts, which is its centre when the whole plate hits (§8.3)
            Point point{};
            // a control with no hit point cannot be clicked, which is refused rather than guessed (§8.3)
            if (!click_point(*node, point))
                return AutomationResult::refused("the node has no clickable point: " + *id);
            // the window is what the pointer event reaches, because focus and capture belong to the stage (§8.3)
            Application& application = Application::instance();
            WindowHandle handle{};
            std::string error;
            // a detached or modal-blocked node is refused before an event is built (§5.1, §10.3)
            if (!resolve_window(application, *node, *id, handle, error))
                return AutomationResult::refused(error);
            // the pointer arrives at the click point, which is a move before the press so hover is current (§8.3)
            Event move{.kind = EventKind::PointerMove, .position = point};
            // a window that refuses the move cannot take the click, which is stated rather than hidden (§10.3)
            if (!application.deliver_input(handle, move))
                return AutomationResult::refused("the window refused the input");
            // the press is the primary button, which is the one a control activates on (§8.3)
            Event press{.kind = EventKind::PointerDown, .position = point, .button = PointerButton::Left};
            // a refused press is stated, because a click that never happened must not answer success (§10.3)
            if (!application.deliver_input(handle, press))
                return AutomationResult::refused("the window refused the input");
            // the release completes the click at the same point, which is what a press and release mean (§8.3)
            Event release{.kind = EventKind::PointerUp, .position = point, .button = PointerButton::Left};
            // a refused release is stated, because the gesture did not complete (§10.3)
            if (!application.deliver_input(handle, release))
                return AutomationResult::refused("the window refused the input");
            // the click reports nothing, because its effect is what the client observes in the next dump (§10.3)
            return AutomationResult::answered("");
        });

        // the pointer command delivers one raw event, which is what a hover or a drag needs (§8.3, §10.3)
        registry.register_command(std::string(FAMILY) + "pointer", [root](const AutomationRequest& request) {
            // a host that supplied no root cannot answer, which is refused rather than answered empty (§10.3)
            if (!root)
                return AutomationResult::refused("the host supplied no input target");
            // the live tree is taken once, because the application may rebuild it between lines (§10.3)
            UiNode* active = root();
            if (active == nullptr)
                return AutomationResult::refused("no window carries a tree");
            // the id is required, because it names the window and the default point (§10.3, AD-11)
            const auto id = argument_at(request, 0);
            // a missing id is refused rather than defaulted (§10.3, AD-11)
            if (!id)
                return AutomationResult::refused("pointer needs an id");
            // the action is required, because it is the event kind the command delivers (§8.3)
            const auto action = argument_at(request, 1);
            // a missing action is refused, because guessing one would deliver the wrong event (§10.3)
            if (!action)
                return AutomationResult::refused("pointer needs an action");
            // a pointer call is the id, the action, and the optional point, so a longer call is a mistake (§10.3)
            if (request.arguments.size() > 4)
                return AutomationResult::refused("pointer takes an id, an action, and optional x and y");
            // the action selects the event kind, which is the portable pointer vocabulary (§8.3)
            EventKind kind = EventKind::PointerMove;
            if (*action == "press")
                kind = EventKind::PointerDown;
            else if (*action == "move")
                kind = EventKind::PointerMove;
            else if (*action == "release")
                kind = EventKind::PointerUp;
            else if (*action == "cancel")
                kind = EventKind::PointerCancel;
            else
                return AutomationResult::refused("unknown pointer action: " + *action);
            // the node is looked up by identity, which is the only selector automation offers (§18.1, AD-11)
            UiNode* node = node_by_id(*active, *id);
            // an absent id is refused with its name, so a client knows which selector failed (§10.3)
            if (node == nullptr)
                return AutomationResult::refused("no node with id: " + *id);
            // the window is what the pointer event reaches, because focus and capture belong to the stage (§8.3)
            Application& application = Application::instance();
            WindowHandle handle{};
            std::string error;
            // a detached or modal-blocked node is refused before an event is built (§5.1, §10.3)
            if (!resolve_window(application, *node, *id, handle, error))
                return AutomationResult::refused(error);
            // the point defaults to the node centre, which is the ordinary target a control offers (§8.2)
            const Rect area = node->bounds();
            // a pointer event with no rect and no explicit point has nowhere to land (§8.2)
            if (request.arguments.size() < 4 && area.empty())
                return AutomationResult::refused("the node has no area: " + *id);
            // the centre is the default the explicit pair may replace (§8.2)
            Point point{.x = area.x + area.width * 0.5f, .y = area.y + area.height * 0.5f};
            // an explicit pair overrides the centre, which is how a drag leaves the control (§8.3)
            if (request.arguments.size() == 4) {
                // both coordinates start at zero, because the parser fills them or refuses (§10.3)
                float x = 0.0f;
                float y = 0.0f;
                // a coordinate that is not a finite number is a client mistake (§10.3)
                if (!parse_float(request.arguments[2], x) || !parse_float(request.arguments[3], y))
                    return AutomationResult::refused("the pointer coordinates are not numbers");
                // the pair replaces the centre, which is the point the event carries (§10.3)
                point = Point{x, y};
            }
            // a single coordinate is not a point, so x without y is refused rather than defaulted (§10.3)
            else if (request.arguments.size() == 3)
                return AutomationResult::refused("pointer takes both x and y");
            // the event carries the left button, which is the pointer automation drives (§8.3)
            Event event{.kind = kind, .position = point, .button = PointerButton::Left};
            // a cancel names the loss of the capture, because there is no platform gesture to name (§8.3)
            if (kind == EventKind::PointerCancel)
                event.cancel_reason = PointerCancelReason::CaptureLost;
            // the event is delivered through the same normalized path a platform uses (§8.3)
            if (!application.deliver_input(handle, event))
                return AutomationResult::refused("the window refused the input");
            // the pointer reports nothing, because its effect is what the client observes in the next dump (§10.3)
            return AutomationResult::answered("");
        });

        // the key command delivers a normalized key pair, which is the route a control and a shortcut see (§8.3, §10.3)
        registry.register_command(std::string(FAMILY) + "key", [root](const AutomationRequest& request) {
            // a host that supplied no root cannot answer, which is refused rather than answered empty (§10.3)
            if (!root)
                return AutomationResult::refused("the host supplied no input target");
            // the live tree is taken once, because the application may rebuild it between lines (§10.3)
            UiNode* active = root();
            if (active == nullptr)
                return AutomationResult::refused("no window carries a tree");
            // the key is required, because a key press names one key (§10.3)
            const auto name = argument_at(request, 0);
            // a missing key is refused, because guessing one would press the wrong control (§10.3)
            if (!name)
                return AutomationResult::refused("key needs a key");
            // a key call is the key and the optional modifier list, so a longer call is a mistake (§10.3)
            if (request.arguments.size() > 2)
                return AutomationResult::refused("key takes a key and optional modifiers");
            // the name is mapped onto the portable key, which is what the normalized event carries (§8.3)
            Key key = Key::None;
            // an unknown name is refused rather than defaulted, because a key is a selector (§10.3)
            if (!key_from_name(*name, key))
                return AutomationResult::refused("unknown key: " + *name);
            // an absent modifier argument is no modifier, which is the common key (§10.3)
            Modifiers modifiers{};
            // a modifier list that names an unknown key is a client mistake (§10.3)
            if (request.arguments.size() == 2 && !modifiers_from_name(request.arguments[1], modifiers))
                return AutomationResult::refused("unknown modifier: " + request.arguments[1]);
            // the window is the active tree's window, because a key belongs to the key window (§8.3)
            Application& application = Application::instance();
            const WindowHandle handle = application.window_for_node(*active);
            // a detached active tree has no window, which is refused with the tree it named (§10.3)
            if (!handle.valid())
                return AutomationResult::refused("no window carries a tree");
            // a modal window blocks every input, so a command must not claim a key it refused (§5.1)
            if (application.input_blocked(handle))
                return AutomationResult::refused("a modal window blocks the tree");
            // a window without a stage has no focus or accelerator to route into (§6.1)
            if (application.stage(handle) == nullptr)
                return AutomationResult::refused("no window carries a tree");
            // the press is delivered first, which is where traversal, activation and accelerators run (§8.3)
            Event down{.kind = EventKind::KeyDown, .key = key, .modifiers = modifiers};
            // a refused press is stated, because a key that never arrived must not answer success (§10.3)
            if (!application.deliver_input(handle, down))
                return AutomationResult::refused("the window refused the input");
            // the release follows the press, because a key is a pair and a handler may read either half (§8.3)
            Event up{.kind = EventKind::KeyUp, .key = key, .modifiers = modifiers};
            // a window closed by the press drops its release, which is not a client mistake (§4.2)
            (void)application.deliver_input(handle, up);
            // the key reports nothing, because its effect is what the client observes in the next dump (§10.3)
            return AutomationResult::answered("");
        });
    }
} // namespace albor::automation
