#include "albor/accessibility/accessibility_platform.h"
#include "albor/accessibility/accessibility_platform.linux.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <dbus/dbus.h>

#include "albor/core/env.h"

namespace albor::accessibility::platform
{
    namespace
    {
        // the registry service every application embeds into, which the protocol fixes (§10.2)
        constexpr const char* REGISTRY_NAME = "org.a11y.atspi.Registry";

        // the registry object the embed call addresses (§10.2)
        constexpr const char* REGISTRY_PATH = "/org/a11y/atspi/registry";

        // the socket interface that carries the embed and unembed calls (§10.2)
        constexpr const char* SOCKET_INTERFACE = "org.a11y.atspi.Socket";

        // the launcher a desktop session exports, which names the accessibility bus (§10.2)
        constexpr const char* BUS_LAUNCHER_NAME = "org.a11y.Bus";

        // the launcher object the address is asked of (§10.2)
        constexpr const char* BUS_LAUNCHER_PATH = "/org/a11y/bus";

        // the launcher interface that answers the address (§10.2)
        constexpr const char* BUS_LAUNCHER_INTERFACE = "org.a11y.Bus";

        // the path a reference with no parent names, which the protocol reserves (§10.2)
        constexpr const char* NULL_PATH = "/org/a11y/atspi/null";

        // the subtree the bridge serves, which the root path and every node path live under (§10.2)
        constexpr const char* ACCESSIBLE_PREFIX = "/org/a11y/atspi/accessible";

        // the accessible interface every object answers (§10.2)
        constexpr const char* ACCESSIBLE_INTERFACE = "org.a11y.atspi.Accessible";

        // the component interface that carries extents and hit testing (§10.2)
        constexpr const char* COMPONENT_INTERFACE = "org.a11y.atspi.Component";

        // the action interface a control with a registered action answers (§10.2)
        constexpr const char* ACTION_INTERFACE = "org.a11y.atspi.Action";

        // the value interface a range control answers (§10.2)
        constexpr const char* VALUE_INTERFACE = "org.a11y.atspi.Value";

        // the text interface an editable field answers (§10.2)
        constexpr const char* TEXT_INTERFACE = "org.a11y.atspi.Text";

        // the editable text interface that carries a text write (§10.2)
        constexpr const char* EDITABLE_TEXT_INTERFACE = "org.a11y.atspi.EditableText";

        // the selection interface a radio group answers (§10.2)
        constexpr const char* SELECTION_INTERFACE = "org.a11y.atspi.Selection";

        // the application interface only the root object answers (§10.2)
        constexpr const char* APPLICATION_INTERFACE = "org.a11y.atspi.Application";

        // the properties interface every client reads a property through (§10.2)
        constexpr const char* PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";

        // the introspection interface, which a bus tool reads the surface through (§10.2)
        constexpr const char* INTROSPECT_INTERFACE = "org.freedesktop.DBus.Introspectable";

        // the event interface a structure change is announced on (§10.2)
        constexpr const char* EVENT_OBJECT_INTERFACE = "org.a11y.atspi.Event.Object";

        // the number of milliseconds a registry call waits, which a missing registry ends quickly (§10.2)
        constexpr int CALL_TIMEOUT_MS = 3000;

        // the number of milliseconds the launcher address waits, which a host without at-spi ends fast (§10.2)
        constexpr int LAUNCHER_TIMEOUT_MS = 2000;

        // the number of milliseconds one dispatch pass waits, which bounds how long a detach blocks (§10.2)
        constexpr int DISPATCH_TIMEOUT_MS = 50;

        // the at-spi role of a node nothing describes, which atspi-constants.h numbers sixty seven (§10.2)
        constexpr std::uint32_t ROLE_UNKNOWN = 67;

        // the role of a push control (§10.2)
        constexpr std::uint32_t ROLE_BUTTON = 43;

        // the role of a toggle that carries an on and off state (§10.2)
        constexpr std::uint32_t ROLE_CHECK_BOX = 7;

        // the role of a title of a section of the interface (§10.2)
        constexpr std::uint32_t ROLE_HEADING = 83;

        // the role of static descriptive text (§10.2)
        constexpr std::uint32_t ROLE_LABEL = 29;

        // the role of a continuous value the user drags (§10.2)
        constexpr std::uint32_t ROLE_SLIDER = 51;

        // the role of a read only value that shows how far a task has advanced (§10.2)
        constexpr std::uint32_t ROLE_PROGRESS_BAR = 42;

        // the role of a container that owns the single selection of its pages (§10.2)
        constexpr std::uint32_t ROLE_PAGE_TAB_LIST = 38;

        // the role of one page of a tab view (§10.2)
        constexpr std::uint32_t ROLE_PAGE_TAB = 37;

        // the role a group of any kind announces, which at-spi has no radio group for (§10.2)
        constexpr std::uint32_t ROLE_GROUPING = 99;

        // the role of one choice in a radio group (§10.2)
        constexpr std::uint32_t ROLE_RADIO_BUTTON = 44;

        // the role of a bounded numeric value with an increment and a decrement (§10.2)
        constexpr std::uint32_t ROLE_SPIN_BUTTON = 52;

        // the role of a drawing surface the user may interact with (§10.2)
        constexpr std::uint32_t ROLE_CANVAS = 6;

        // the role of a single line editable field (§10.2)
        constexpr std::uint32_t ROLE_ENTRY = 79;

        // the role of an image that carries an accessible name (§10.2)
        constexpr std::uint32_t ROLE_IMAGE = 27;

        // the role of a row of a list (§10.2)
        constexpr std::uint32_t ROLE_LIST_ITEM = 32;

        // the role of one row of a menu (§10.2)
        constexpr std::uint32_t ROLE_MENU_ITEM = 35;

        // the role of a container of list items that share one selection (§10.2)
        constexpr std::uint32_t ROLE_LIST = 31;

        // the role of a compact choice control that opens a popup list (§10.2)
        constexpr std::uint32_t ROLE_COMBO_BOX = 11;

        // the role of a viewport over content larger than it shows (§10.2)
        constexpr std::uint32_t ROLE_SCROLL_PANE = 49;

        // the role of a native window of the application (§10.2)
        constexpr std::uint32_t ROLE_WINDOW = 69;

        // the role only the application root announces (§10.2)
        constexpr std::uint32_t ROLE_APPLICATION = 75;

        // the checked state of a checkable node, which atspi-constants.h numbers four (§10.2)
        constexpr std::uint32_t STATE_CHECKED = 4;

        // the state that marks a text field as accepting edits (§10.2)
        constexpr std::uint32_t STATE_EDITABLE = 7;

        // the state that marks a node as accepting input (§10.2)
        constexpr std::uint32_t STATE_ENABLED = 8;

        // the state that marks a popup control as openable (§10.2)
        constexpr std::uint32_t STATE_EXPANDABLE = 9;

        // the state that marks a popup control as open (§10.2)
        constexpr std::uint32_t STATE_EXPANDED = 10;

        // the state that marks a node as accepting keyboard focus (§10.2)
        constexpr std::uint32_t STATE_FOCUSABLE = 11;

        // the state that marks a node as holding keyboard focus (§10.2)
        constexpr std::uint32_t STATE_FOCUSED = 12;

        // the state that marks a text field as editing more than one line (§10.2)
        constexpr std::uint32_t STATE_MULTI_LINE = 17;

        // the state that marks a node as one choice of a container (§10.2)
        constexpr std::uint32_t STATE_SELECTABLE = 22;

        // the state that marks a node as the chosen choice of its container (§10.2)
        constexpr std::uint32_t STATE_SELECTED = 23;

        // the state that marks a node as sensitive to user interaction (§10.2)
        constexpr std::uint32_t STATE_SENSITIVE = 24;

        // the state that marks a node as drawn on screen (§10.2)
        constexpr std::uint32_t STATE_SHOWING = 25;

        // the state that marks a node as visible to the user (§10.2)
        constexpr std::uint32_t STATE_VISIBLE = 30;

        // the state that marks a node whose remaining work is unknown (§10.2)
        constexpr std::uint32_t STATE_INDETERMINATE = 32;

        // the state that marks a text field as carrying selectable text (§10.2)
        constexpr std::uint32_t STATE_SELECTABLE_TEXT = 38;

        // the state that marks a node as checkable, which lives in the second state word (§10.2)
        constexpr std::uint32_t STATE_CHECKABLE = 41;

        // the state that marks a popup control as carrying a popup (§10.2)
        constexpr std::uint32_t STATE_HAS_POPUP = 42;

        // the state that marks a text field as refusing edits while still taking focus (§10.2)
        constexpr std::uint32_t STATE_READ_ONLY = 43;

        // the layer a widget sits in, which the component query answers (§10.2)
        constexpr std::uint32_t LAYER_WIDGET = 3;

        // one immutable publication, so a reply keeps its tree after a rebuild (§10.2)
        struct Publication
        {
            // the tree as published, which every reply reads and none writes (§10.2)
            SemanticTree tree;

            // the nodes without a parent, which the application root announces as its children (§10.2)
            std::vector<std::size_t> roots;
        };

        // one request the dispatch thread accepted, which the ui thread runs at its next publish (§10.2)
        struct Invocation
        {
            // the three writes a client can ask for, which the dispatch thread replies to and the ui thread runs (§10.2)
            enum class Kind
            {
                Action,
                WriteText,
                WriteRange
            };

            // the node the request names, which is an index into the next publication (§10.2)
            std::size_t index = 0;

            // what the request does, which the dispatch thread decided before it replied (§10.2)
            Kind kind = Kind::Action;

            // the action identifier, which the node numbered when it registered it (§10.1)
            SemanticActionId action = 0;

            // the text a text write carries, which the ui thread hands to the node (§10.2)
            std::string text;

            // the number a range write carries, which the node clamps into its bounds (§10.2)
            double number = 0.0;
        };

        // one object reference, which the protocol carries as a bus name and a path pair (§10.2)
        using Reference = std::pair<std::string, std::string>;

        // the at-spi role a semantic role announces, which is a one to one mapping (§10.1, §10.2)
        std::uint32_t role_for(SemanticRole role) {
            // a switch covers every role, because the semantic enumeration is closed (§10.1, §10.2)
            switch (role) {
            case SemanticRole::None:
                return ROLE_UNKNOWN;
            case SemanticRole::Button:
                return ROLE_BUTTON;
            case SemanticRole::Checkbox:
                return ROLE_CHECK_BOX;
            case SemanticRole::Heading:
                return ROLE_HEADING;
            case SemanticRole::Label:
                return ROLE_LABEL;
            case SemanticRole::Slider:
                return ROLE_SLIDER;
            case SemanticRole::ProgressBar:
                return ROLE_PROGRESS_BAR;
            case SemanticRole::TabView:
                return ROLE_PAGE_TAB_LIST;
            case SemanticRole::TabItem:
                return ROLE_PAGE_TAB;
            case SemanticRole::RadioGroup:
                return ROLE_GROUPING;
            case SemanticRole::RadioButton:
                return ROLE_RADIO_BUTTON;
            case SemanticRole::NumericStepper:
                return ROLE_SPIN_BUTTON;
            case SemanticRole::Canvas:
                return ROLE_CANVAS;
            case SemanticRole::TextField:
                return ROLE_ENTRY;
            case SemanticRole::Image:
                return ROLE_IMAGE;
            case SemanticRole::ListItem:
                return ROLE_LIST_ITEM;
            case SemanticRole::MenuItem:
                return ROLE_MENU_ITEM;
            case SemanticRole::List:
                return ROLE_LIST;
            case SemanticRole::ComboBox:
                return ROLE_COMBO_BOX;
            case SemanticRole::Group:
                return ROLE_GROUPING;
            case SemanticRole::ScrollView:
                return ROLE_SCROLL_PANE;
            case SemanticRole::Window:
                return ROLE_WINDOW;
            }
            // a role outside the enumeration announces nothing, which the quiet default states (§10.2)
            return ROLE_UNKNOWN;
        }

        // the at-spi role name a semantic role announces, which a client reads when it wants words (§10.2)
        std::string_view role_name(SemanticRole role) {
            // a switch covers every role, because the semantic enumeration is closed (§10.1, §10.2)
            switch (role) {
            case SemanticRole::None:
                return "unknown";
            case SemanticRole::Button:
                return "push button";
            case SemanticRole::Checkbox:
                return "check box";
            case SemanticRole::Heading:
                return "heading";
            case SemanticRole::Label:
                return "label";
            case SemanticRole::Slider:
                return "slider";
            case SemanticRole::ProgressBar:
                return "progress bar";
            case SemanticRole::TabView:
                return "page tab list";
            case SemanticRole::TabItem:
                return "page tab";
            case SemanticRole::RadioGroup:
                return "grouping";
            case SemanticRole::RadioButton:
                return "radio button";
            case SemanticRole::NumericStepper:
                return "spin button";
            case SemanticRole::Canvas:
                return "canvas";
            case SemanticRole::TextField:
                return "entry";
            case SemanticRole::Image:
                return "image";
            case SemanticRole::ListItem:
                return "list item";
            case SemanticRole::MenuItem:
                return "menu item";
            case SemanticRole::List:
                return "list";
            case SemanticRole::ComboBox:
                return "combo box";
            case SemanticRole::Group:
                return "grouping";
            case SemanticRole::ScrollView:
                return "scroll pane";
            case SemanticRole::Window:
                return "window";
            }
            // a role outside the enumeration names nothing, which the quiet default states (§10.2)
            return "unknown";
        }

        // report whether assistive technology may focus a role, which the focusable state asks (§10.2)
        bool focusable_role(SemanticRole role) {
            // the roles that take keyboard input are the controls a user operates (§10.1)
            switch (role) {
            case SemanticRole::Button:
            case SemanticRole::Checkbox:
            case SemanticRole::Slider:
            case SemanticRole::TextField:
            case SemanticRole::ListItem:
            case SemanticRole::MenuItem:
            case SemanticRole::TabItem:
            case SemanticRole::RadioButton:
            case SemanticRole::NumericStepper:
            case SemanticRole::ComboBox:
                return true;
            default:
                return false;
            }
        }

        // report whether a role is one choice of a selection container, which the selectable state asks (§10.2)
        bool selectable_role(SemanticRole role) {
            // a row, a page and a radio choice are the three choices the semantic tree carries (§10.1)
            return role == SemanticRole::ListItem || role == SemanticRole::TabItem || role == SemanticRole::RadioButton;
        }

        // build the object path a published node answers on, which the index and one name it (§10.2)
        std::string object_path_for(std::size_t index) {
            // the one based number keeps the root path, which is a word, out of the node range (§10.2)
            return std::string(ACCESSIBLE_PREFIX) + "/" + std::to_string(index + 1);
        }

        // count the characters before a byte offset, because at-spi counts characters and the node counts bytes (§10.2)
        std::size_t character_offset(std::string_view text, std::size_t byte_offset) {
            // a lead byte is every byte that does not continue a sequence, which is how utf-8 counts characters (§10.2)
            std::size_t characters = 0;
            // only the bytes before the offset are counted, which clamps an offset past the end (§10.2)
            const std::size_t limit = std::min(byte_offset, text.size());
            for (std::size_t index = 0; index < limit; index++)
                if ((static_cast<unsigned char>(text[index]) & 0xC0) != 0x80)
                    characters++;
            return characters;
        }

        // resolve a character offset to a byte offset, which is where a text request starts or ends (§10.2)
        std::size_t byte_offset(std::string_view text, std::size_t characters) {
            // the walk counts lead bytes until the requested character is reached (§10.2)
            std::size_t seen = 0;
            for (std::size_t index = 0; index < text.size(); index++) {
                // only a lead byte starts a character, because a continuation byte is part of the one before (§10.2)
                if ((static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
                    continue;
                if (seen == characters)
                    return index;
                seen++;
            }
            // an offset past the last character names the end of the text (§10.2)
            return text.size();
        }

        // read the text between two character offsets, which a text request answers with (§10.2)
        std::string text_range(std::string_view text, std::size_t start, std::size_t end) {
            // a reversed range names no text, because the field never publishes one (§10.1)
            if (end < start)
                return {};
            // the byte offsets are resolved once, because the conversion walks the text (§10.2)
            const std::size_t begin_byte = byte_offset(text, start);
            const std::size_t end_byte = byte_offset(text, end);
            return std::string(text.substr(begin_byte, end_byte - begin_byte));
        }

        // append one object reference, which a child, a parent and an embed argument all carry (§10.2)
        void append_object_reference(DBusMessageIter* iterator, const std::string& bus_name, const std::string& path) {
            DBusMessageIter structure;
            dbus_message_iter_open_container(iterator, DBUS_TYPE_STRUCT, nullptr, &structure);
            // the bus name is the unique name the bridge answers on, which the protocol carries as a string (§10.2)
            const char* bus_text = bus_name.c_str();
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_STRING, &bus_text);
            // the path is the object the reference names, which the protocol carries as an object path (§10.2)
            const char* path_text = path.c_str();
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_OBJECT_PATH, &path_text);
            dbus_message_iter_close_container(iterator, &structure);
        }

        // send one message and release it, which every reply and signal does (§10.2)
        void send_message(DBusConnection* connection, DBusMessage* message) {
            // a message that was never built has nothing to send (§10.2)
            if (message == nullptr)
                return;
            // the message is queued rather than flushed, because a flush contends for the dispatch thread's io path (§10.2)
            dbus_connection_send(connection, message, nullptr);
            dbus_message_unref(message);
        }

        // close and release one private connection, which an unreachable bus leaves null (§10.2)
        void close_connection(DBusConnection* connection) {
            // a connection the bridge never opened is not closed (§10.2)
            if (connection == nullptr)
                return;
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
        }

        // open a private connection to one bus address, which the session and the accessibility bus share (§10.2)
        DBusConnection* open_connection(const std::string& address, std::string& unique_name) {
            // the error is filled by a bus that refused the connection (§10.2)
            DBusError error;
            dbus_error_init(&error);
            // the connection is built from the address the host exported, or the session the library resolves (§10.2)
            DBusConnection* connection = address.empty() ? dbus_bus_get_private(DBUS_BUS_SESSION, &error) : dbus_connection_open_private(address.c_str(), &error);
            if (connection == nullptr) {
                dbus_error_free(&error);
                return nullptr;
            }
            // a bus connection registers before it owns the unique name every reference carries (§10.2)
            if (!dbus_bus_register(connection, &error)) {
                dbus_error_free(&error);
                close_connection(connection);
                return nullptr;
            }
            dbus_error_free(&error);
            // a dropped bus must not terminate the application, which a desktop restart can cause (§10.2)
            dbus_connection_set_exit_on_disconnect(connection, FALSE);
            // the unique name is what every child and parent reference names (§10.2)
            const char* name = dbus_bus_get_unique_name(connection);
            if (name != nullptr)
                unique_name = name;
            return connection;
        }

        // ask the launcher for the accessibility bus, which a host without at-spi leaves empty (§10.2)
        std::string query_accessibility_bus(DBusConnection* session) {
            // the launcher is addressed on the session bus, because that is where a desktop starts it (§10.2)
            DBusMessage* call = dbus_message_new_method_call(BUS_LAUNCHER_NAME, BUS_LAUNCHER_PATH, BUS_LAUNCHER_INTERFACE, "GetAddress");
            if (call == nullptr)
                return {};
            DBusError error;
            dbus_error_init(&error);
            // the reply names the accessibility bus, and a missing launcher answers an error (§10.2)
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(session, call, LAUNCHER_TIMEOUT_MS, &error);
            dbus_message_unref(call);
            std::string address;
            if (reply != nullptr) {
                const char* text = nullptr;
                if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID) && text != nullptr)
                    address = text;
                dbus_message_unref(reply);
            }
            dbus_error_free(&error);
            return address;
        }

        // register the application root with the registry, which a host without at-spi answers with an error (§10.2)
        void embed_application(DBusConnection* connection, const std::string& bus_name) {
            // the embed call carries the application root, which the registry then announces (§10.2)
            DBusMessage* call = dbus_message_new_method_call(REGISTRY_NAME, REGISTRY_PATH, SOCKET_INTERFACE, "Embed");
            if (call == nullptr)
                return;
            DBusMessageIter iterator;
            dbus_message_iter_init_append(call, &iterator);
            append_object_reference(&iterator, bus_name, std::string(ATSPI_ROOT_PATH));
            DBusError error;
            dbus_error_init(&error);
            // the reply is read only to release it, because the call itself is what registers the application (§10.2)
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, call, CALL_TIMEOUT_MS, &error);
            if (reply != nullptr)
                dbus_message_unref(reply);
            dbus_error_free(&error);
            dbus_message_unref(call);
        }

        // tell the registry the application is leaving, which keeps its list of applications current (§10.2)
        void unembed_application(DBusConnection* connection, const std::string& bus_name) {
            // the unembed call carries the root the embed named (§10.2)
            DBusMessage* call = dbus_message_new_method_call(REGISTRY_NAME, REGISTRY_PATH, SOCKET_INTERFACE, "Unembed");
            if (call == nullptr)
                return;
            DBusMessageIter iterator;
            dbus_message_iter_init_append(call, &iterator);
            append_object_reference(&iterator, bus_name, std::string(ATSPI_ROOT_PATH));
            DBusError error;
            dbus_error_init(&error);
            // the reply is read only to release it, because the call itself is what deregisters the application (§10.2)
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, call, CALL_TIMEOUT_MS, &error);
            if (reply != nullptr)
                dbus_message_unref(reply);
            dbus_error_free(&error);
            dbus_message_unref(call);
        }

        // append a rectangle, which an extent and a position both answer with (§10.2)
        void append_rectangle(DBusMessageIter* iterator, const Rect& bounds) {
            // the logical pixels cross as whole numbers, which is what the protocol carries (§10.2)
            dbus_int32_t x = static_cast<dbus_int32_t>(std::lround(bounds.x));
            dbus_int32_t y = static_cast<dbus_int32_t>(std::lround(bounds.y));
            dbus_int32_t width = static_cast<dbus_int32_t>(std::lround(bounds.width));
            dbus_int32_t height = static_cast<dbus_int32_t>(std::lround(bounds.height));
            DBusMessageIter structure;
            dbus_message_iter_open_container(iterator, DBUS_TYPE_STRUCT, nullptr, &structure);
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &x);
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &y);
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &width);
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &height);
            dbus_message_iter_close_container(iterator, &structure);
        }

        // build a reply carrying one string (§10.2)
        DBusMessage* reply_with_string(DBusMessage* request, const std::string& value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            const char* text = value.c_str();
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one boolean (§10.2)
        DBusMessage* reply_with_bool(DBusMessage* request, bool value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_bool_t flag = value ? TRUE : FALSE;
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &flag, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one signed number (§10.2)
        DBusMessage* reply_with_int32(DBusMessage* request, std::int32_t value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_int32_t number = value;
            dbus_message_append_args(reply, DBUS_TYPE_INT32, &number, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one unsigned number (§10.2)
        DBusMessage* reply_with_uint32(DBusMessage* request, std::uint32_t value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_uint32_t number = value;
            dbus_message_append_args(reply, DBUS_TYPE_UINT32, &number, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one small signed number, which the stacking order answers with (§10.2)
        DBusMessage* reply_with_int16(DBusMessage* request, std::int16_t value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_int16_t number = value;
            dbus_message_append_args(reply, DBUS_TYPE_INT16, &number, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one floating point number (§10.2)
        DBusMessage* reply_with_double(DBusMessage* request, double value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_message_append_args(reply, DBUS_TYPE_DOUBLE, &value, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one object reference (§10.2)
        DBusMessage* reply_with_reference(DBusMessage* request, const std::string& bus_name, const std::string& path) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            append_object_reference(&iterator, bus_name, path);
            return reply;
        }

        // build a reply carrying a list of object references (§10.2)
        DBusMessage* reply_with_references(DBusMessage* request, const std::vector<Reference>& references) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            DBusMessageIter array;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "(so)", &array);
            for (const Reference& reference : references)
                append_object_reference(&array, reference.first, reference.second);
            dbus_message_iter_close_container(&iterator, &array);
            return reply;
        }

        // build a reply carrying a list of names, which the interface query answers with (§10.2)
        DBusMessage* reply_with_string_array(DBusMessage* request, const std::vector<std::string>& names) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            DBusMessageIter array;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "s", &array);
            for (const std::string& name : names) {
                const char* text = name.c_str();
                dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &text);
            }
            dbus_message_iter_close_container(&iterator, &array);
            return reply;
        }

        // build a reply carrying the state words, which the protocol returns as a set of numbers (§10.2)
        DBusMessage* reply_with_state(DBusMessage* request, std::uint32_t first, std::uint32_t second) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            DBusMessageIter array;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "u", &array);
            dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &first);
            dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &second);
            dbus_message_iter_close_container(&iterator, &array);
            return reply;
        }

        // build a reply carrying an empty string dictionary, which the attributes query answers with (§10.2)
        DBusMessage* reply_with_empty_dictionary(DBusMessage* request) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            DBusMessageIter array;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "{ss}", &array);
            dbus_message_iter_close_container(&iterator, &array);
            return reply;
        }

        // build a reply carrying no relation, which a node without relations answers with (§10.2)
        DBusMessage* reply_with_empty_relations(DBusMessage* request) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            DBusMessageIter array;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "(ua(so))", &array);
            dbus_message_iter_close_container(&iterator, &array);
            return reply;
        }

        // build a reply carrying one rectangle, which an extent answers with (§10.2)
        DBusMessage* reply_with_rectangle(DBusMessage* request, const Rect& bounds) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            append_rectangle(&iterator, bounds);
            return reply;
        }

        // build a reply carrying one position, which the position query answers with (§10.2)
        DBusMessage* reply_with_position(DBusMessage* request, const Rect& bounds) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_int32_t x = static_cast<dbus_int32_t>(std::lround(bounds.x));
            dbus_int32_t y = static_cast<dbus_int32_t>(std::lround(bounds.y));
            dbus_message_append_args(reply, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying one size, which the size query answers with (§10.2)
        DBusMessage* reply_with_size(DBusMessage* request, const Rect& bounds) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_int32_t width = static_cast<dbus_int32_t>(std::lround(bounds.width));
            dbus_int32_t height = static_cast<dbus_int32_t>(std::lround(bounds.height));
            dbus_message_append_args(reply, DBUS_TYPE_INT32, &width, DBUS_TYPE_INT32, &height, DBUS_TYPE_INVALID);
            return reply;
        }

        // build a reply carrying two numbers, which a text selection answers with (§10.2)
        DBusMessage* reply_with_offsets(DBusMessage* request, dbus_int32_t start, dbus_int32_t end) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            dbus_message_append_args(reply, DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
            return reply;
        }

        // build the empty reply a write answers with, because its effect is what the client reads next (§10.2)
        DBusMessage* reply_without_body(DBusMessage* request) { return dbus_message_new_method_return(request); }

        // build an error naming a call the object does not answer (§10.2)
        DBusMessage* unknown_member_error(DBusMessage* request, std::string_view interface, std::string_view member) {
            const std::string text = "no member " + std::string(member) + " on " + std::string(interface);
            return dbus_message_new_error(request, DBUS_ERROR_UNKNOWN_METHOD, text.c_str());
        }

        // build an error naming a call whose arguments the object cannot read (§10.2)
        DBusMessage* invalid_arguments_error(DBusMessage* request) { return dbus_message_new_error(request, DBUS_ERROR_INVALID_ARGS, "the call carries the wrong arguments"); }

        // read one signed number argument, which every indexed method carries (§10.2)
        bool read_int32(DBusMessage* message, dbus_int32_t& value) {
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &value, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            return read != FALSE;
        }

        // read two signed number arguments, which a text range carries (§10.2)
        bool read_two_int32(DBusMessage* message, dbus_int32_t& first, dbus_int32_t& second) {
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &first, DBUS_TYPE_INT32, &second, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            return read != FALSE;
        }

        // read a point and its coordinate type, which the contains and extent calls carry (§10.2)
        bool read_point_with_coordinate(DBusMessage* message, dbus_int32_t& x, dbus_int32_t& y, dbus_uint32_t& coordinate) {
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_UINT32, &coordinate, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            return read != FALSE;
        }

        // read a point, which the hit test carries without a coordinate type (§10.2)
        bool read_point(DBusMessage* message, dbus_int32_t& x, dbus_int32_t& y) {
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            return read != FALSE;
        }

        // read the coordinate type an extent call carries, which names the space the rectangle lives in (§10.2)
        bool read_coordinate(DBusMessage* message, dbus_uint32_t& coordinate) {
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &coordinate, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            return read != FALSE;
        }

        // read one string argument, which a text write and an embedded notification carry (§10.2)
        bool read_string(DBusMessage* message, std::string& value) {
            DBusError error;
            dbus_error_init(&error);
            const char* text = nullptr;
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            if (read == FALSE || text == nullptr)
                return false;
            value = text;
            return true;
        }

        // report whether a rectangle holds a point, which the hit test answers with (§10.2)
        bool rectangle_contains(const Rect& bounds, dbus_int32_t x, dbus_int32_t y) {
            // the edges are inclusive, because a pointer on the boundary still points at the control (§10.2)
            return static_cast<double>(x) >= bounds.x && static_cast<double>(x) <= bounds.x + bounds.width && static_cast<double>(y) >= bounds.y && static_cast<double>(y) <= bounds.y + bounds.height;
        }

        // one property value, which a read and a full dictionary both answer with (§10.2)
        struct PropertyValue
        {
            // the d-bus type the variant carries, which decides how the value is appended (§10.2)
            int type = DBUS_TYPE_STRING;

            // the text a string property carries (§10.2)
            std::string text;

            // the number a signed property carries (§10.2)
            std::int32_t integer = 0;

            // the number an unsigned property carries (§10.2)
            std::uint32_t unsigned_integer = 0;

            // the number a floating point property carries (§10.2)
            double number = 0.0;

            // the flag a boolean property carries (§10.2)
            bool flag = false;

            // the bus name an object property carries (§10.2)
            std::string bus;

            // the path an object property carries (§10.2)
            std::string path;
        };

        // build a string property value (§10.2)
        PropertyValue string_value(std::string text) {
            PropertyValue value;
            value.type = DBUS_TYPE_STRING;
            value.text = std::move(text);
            return value;
        }

        // build a signed number property value (§10.2)
        PropertyValue int32_value(std::int32_t number) {
            PropertyValue value;
            value.type = DBUS_TYPE_INT32;
            value.integer = number;
            return value;
        }

        // build an unsigned number property value (§10.2)
        PropertyValue uint32_value(std::uint32_t number) {
            PropertyValue value;
            value.type = DBUS_TYPE_UINT32;
            value.unsigned_integer = number;
            return value;
        }

        // build a floating point property value (§10.2)
        PropertyValue double_value(double number) {
            PropertyValue value;
            value.type = DBUS_TYPE_DOUBLE;
            value.number = number;
            return value;
        }

        // build an object property value, which a parent reference answers with (§10.2)
        PropertyValue reference_value(std::string bus_name, std::string path) {
            PropertyValue value;
            value.type = DBUS_TYPE_STRUCT;
            value.bus = std::move(bus_name);
            value.path = std::move(path);
            return value;
        }

        // append a property value in its own type, which a variant body carries (§10.2)
        void append_property_value(DBusMessageIter* iterator, const PropertyValue& value) {
            // the type decides how the value is appended, because every variant carries exactly one of them (§10.2)
            switch (value.type) {
            case DBUS_TYPE_STRING: {
                const char* text = value.text.c_str();
                dbus_message_iter_append_basic(iterator, DBUS_TYPE_STRING, &text);
                break;
            }
            case DBUS_TYPE_INT32:
                dbus_message_iter_append_basic(iterator, DBUS_TYPE_INT32, &value.integer);
                break;
            case DBUS_TYPE_UINT32:
                dbus_message_iter_append_basic(iterator, DBUS_TYPE_UINT32, &value.unsigned_integer);
                break;
            case DBUS_TYPE_DOUBLE:
                dbus_message_iter_append_basic(iterator, DBUS_TYPE_DOUBLE, &value.number);
                break;
            case DBUS_TYPE_BOOLEAN: {
                dbus_bool_t flag = value.flag ? TRUE : FALSE;
                dbus_message_iter_append_basic(iterator, DBUS_TYPE_BOOLEAN, &flag);
                break;
            }
            case DBUS_TYPE_STRUCT:
                append_object_reference(iterator, value.bus, value.path);
                break;
            default:
                break;
            }
        }

        // append a property value inside a variant, which the properties interface answers with (§10.2)
        void append_variant(DBusMessageIter* iterator, const PropertyValue& value) {
            // a reference carries the structure signature, and every other value carries its single type (§10.2)
            const char* signature = value.type == DBUS_TYPE_STRUCT ? "(so)" : nullptr;
            const char single[2] = {static_cast<char>(value.type), '\0'};
            DBusMessageIter variant;
            dbus_message_iter_open_container(iterator, DBUS_TYPE_VARIANT, signature == nullptr ? single : signature, &variant);
            append_property_value(&variant, value);
            dbus_message_iter_close_container(iterator, &variant);
        }

        // append one dictionary entry, which a full property dictionary carries (§10.2)
        void append_dictionary_entry(DBusMessageIter* dictionary, std::string_view name, const PropertyValue& value) {
            DBusMessageIter entry;
            dbus_message_iter_open_container(dictionary, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            const std::string key(name);
            const char* text = key.c_str();
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &text);
            append_variant(&entry, value);
            dbus_message_iter_close_container(dictionary, &entry);
        }

        // build a reply carrying one property, which the properties read answers with (§10.2)
        DBusMessage* reply_with_property(DBusMessage* request, const PropertyValue& value) {
            DBusMessage* reply = dbus_message_new_method_return(request);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            append_variant(&iterator, value);
            return reply;
        }

        // the introspection document every object carries, which describes the interfaces it answers (§10.2)
        constexpr const char* INTROSPECTION_HEADER = "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n";

        // the accessible interface as the introspection document spells it (§10.2)
        constexpr const char* ACCESSIBLE_XML = R"xml(<interface name="org.a11y.atspi.Accessible">
<property name="Name" type="s" access="read"/>
<property name="Description" type="s" access="read"/>
<property name="Parent" type="(so)" access="read"/>
<property name="ChildCount" type="i" access="read"/>
<property name="Locale" type="s" access="read"/>
<property name="AccessibleId" type="s" access="read"/>
<method name="GetChildAtIndex"><arg direction="in" type="i"/><arg direction="out" type="(so)"/></method>
<method name="GetChildren"><arg direction="out" type="a(so)"/></method>
<method name="GetIndexInParent"><arg direction="out" type="i"/></method>
<method name="GetRelationSet"><arg direction="out" type="a(ua(so))"/></method>
<method name="GetRole"><arg direction="out" type="u"/></method>
<method name="GetRoleName"><arg direction="out" type="s"/></method>
<method name="GetLocalizedRoleName"><arg direction="out" type="s"/></method>
<method name="GetState"><arg direction="out" type="au"/></method>
<method name="GetAttributes"><arg direction="out" type="a{ss}"/></method>
<method name="GetApplication"><arg direction="out" type="(so)"/></method>
<method name="GetInterfaces"><arg direction="out" type="as"/></method>
</interface>
)xml";

        // the component interface as the introspection document spells it (§10.2)
        constexpr const char* COMPONENT_XML = R"xml(<interface name="org.a11y.atspi.Component">
<method name="Contains"><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="in" type="u"/><arg direction="out" type="b"/></method>
<method name="GetAccessibleAtPoint"><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="out" type="(so)"/></method>
<method name="GetExtents"><arg direction="in" type="u"/><arg direction="out" type="(iiii)"/></method>
<method name="GetPosition"><arg direction="in" type="u"/><arg direction="out" type="(ii)"/></method>
<method name="GetSize"><arg direction="out" type="(ii)"/></method>
<method name="GetLayer"><arg direction="out" type="u"/></method>
<method name="GetMDIZOrder"><arg direction="out" type="n"/></method>
<method name="GrabFocus"><arg direction="out" type="b"/></method>
<method name="GetAlpha"><arg direction="out" type="d"/></method>
<method name="SetExtents"><arg direction="in" type="(iiii)"/><arg direction="in" type="u"/><arg direction="out" type="b"/></method>
<method name="SetPosition"><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="in" type="u"/><arg direction="out" type="b"/></method>
<method name="SetSize"><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="in" type="u"/><arg direction="out" type="b"/></method>
<method name="ScrollTo"><arg direction="in" type="u"/><arg direction="out" type="b"/></method>
<method name="ScrollToPoint"><arg direction="in" type="u"/><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
</interface>
)xml";

        // the action interface as the introspection document spells it (§10.2)
        constexpr const char* ACTION_XML = R"xml(<interface name="org.a11y.atspi.Action">
<property name="NActions" type="i" access="read"/>
<method name="GetName"><arg direction="in" type="i"/><arg direction="out" type="s"/></method>
<method name="GetDescription"><arg direction="in" type="i"/><arg direction="out" type="s"/></method>
<method name="GetKeyBinding"><arg direction="in" type="i"/><arg direction="out" type="s"/></method>
<method name="DoAction"><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
</interface>
)xml";

        // the value interface as the introspection document spells it (§10.2)
        constexpr const char* VALUE_XML = R"xml(<interface name="org.a11y.atspi.Value">
<property name="MinimumValue" type="d" access="read"/>
<property name="MaximumValue" type="d" access="read"/>
<property name="MinimumIncrement" type="d" access="read"/>
<property name="CurrentValue" type="d" access="readwrite"/>
<property name="Text" type="s" access="read"/>
</interface>
)xml";

        // the text interface as the introspection document spells it (§10.2)
        constexpr const char* TEXT_XML = R"xml(<interface name="org.a11y.atspi.Text">
<property name="CharacterCount" type="i" access="read"/>
<property name="CaretOffset" type="i" access="read"/>
<method name="GetText"><arg direction="in" type="i"/><arg direction="in" type="i"/><arg direction="out" type="s"/></method>
<method name="GetNSelections"><arg direction="out" type="i"/></method>
<method name="GetSelection"><arg direction="in" type="i"/><arg direction="out" type="(ii)"/></method>
</interface>
)xml";

        // the editable text interface as the introspection document spells it (§10.2)
        constexpr const char* EDITABLE_TEXT_XML = R"xml(<interface name="org.a11y.atspi.EditableText">
<method name="SetTextContents"><arg direction="in" type="s"/><arg direction="out" type="b"/></method>
</interface>
)xml";

        // the selection interface as the introspection document spells it (§10.2)
        constexpr const char* SELECTION_XML = R"xml(<interface name="org.a11y.atspi.Selection">
<property name="NSelectedChildren" type="i" access="read"/>
<method name="GetSelectedChild"><arg direction="in" type="i"/><arg direction="out" type="(so)"/></method>
<method name="SelectChild"><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
<method name="DeselectSelectedChild"><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
<method name="IsChildSelected"><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
<method name="SelectAll"><arg direction="out" type="b"/></method>
<method name="ClearSelection"><arg direction="out" type="b"/></method>
<method name="DeselectChild"><arg direction="in" type="i"/><arg direction="out" type="b"/></method>
</interface>
)xml";

        // the application interface as the introspection document spells it (§10.2)
        constexpr const char* APPLICATION_XML = R"xml(<interface name="org.a11y.atspi.Application">
<property name="ToolkitName" type="s" access="read"/>
<property name="Version" type="s" access="read"/>
<property name="AtspiVersion" type="s" access="read"/>
<property name="InterfaceVersion" type="u" access="read"/>
<property name="Id" type="i" access="readwrite"/>
<method name="GetLocale"><arg direction="in" type="u"/><arg direction="out" type="s"/></method>
<method name="GetApplicationBusAddress"><arg direction="out" type="s"/></method>
</interface>
)xml";

        // the socket interface as the introspection document spells it (§10.2)
        constexpr const char* SOCKET_XML = R"xml(<interface name="org.a11y.atspi.Socket">
<method name="Embedded"><arg direction="in" type="s"/></method>
<method name="Unembed"><arg direction="in" type="(so)"/></method>
</interface>
)xml";

        // the properties interface as the introspection document spells it (§10.2)
        constexpr const char* PROPERTIES_XML = R"xml(<interface name="org.freedesktop.DBus.Properties">
<method name="Get"><arg direction="in" type="s"/><arg direction="in" type="s"/><arg direction="out" type="v"/></method>
<method name="GetAll"><arg direction="in" type="s"/><arg direction="out" type="a{sv}"/></method>
<method name="Set"><arg direction="in" type="s"/><arg direction="in" type="s"/><arg direction="in" type="v"/></method>
</interface>
)xml";

        // the introspection interface as the introspection document spells it (§10.2)
        constexpr const char* INTROSPECT_XML = R"xml(<interface name="org.freedesktop.DBus.Introspectable">
<method name="Introspect"><arg direction="out" type="s"/></method>
</interface>
)xml";

        // hand out the document fragment one interface answers with, which the introspection document joins (§10.2)
        std::string_view interface_xml(std::string_view interface) {
            // the fragment table is the one place the document is written, so a member cannot be described twice (§10.2)
            if (interface == ACCESSIBLE_INTERFACE)
                return ACCESSIBLE_XML;
            if (interface == COMPONENT_INTERFACE)
                return COMPONENT_XML;
            if (interface == ACTION_INTERFACE)
                return ACTION_XML;
            if (interface == VALUE_INTERFACE)
                return VALUE_XML;
            if (interface == TEXT_INTERFACE)
                return TEXT_XML;
            if (interface == EDITABLE_TEXT_INTERFACE)
                return EDITABLE_TEXT_XML;
            if (interface == SELECTION_INTERFACE)
                return SELECTION_XML;
            if (interface == APPLICATION_INTERFACE)
                return APPLICATION_XML;
            if (interface == SOCKET_INTERFACE)
                return SOCKET_XML;
            // a name the table does not describe answers nothing, which the quiet default states (§10.2)
            return {};
        }

        // name the properties one interface carries, which a full read enumerates (§10.2)
        std::vector<std::string_view> property_names(std::string_view interface) {
            // the accessible interface carries the identity, the name and the hierarchy links (§10.2)
            if (interface == ACCESSIBLE_INTERFACE)
                return {"Name", "Description", "Parent", "ChildCount", "Locale", "AccessibleId"};
            // the application interface carries the toolkit and the identifier the registry assigns (§10.2)
            if (interface == APPLICATION_INTERFACE)
                return {"ToolkitName", "Version", "AtspiVersion", "InterfaceVersion", "Id"};
            // a control with actions announces how many it carries (§10.2)
            if (interface == ACTION_INTERFACE)
                return {"NActions"};
            // a range control carries its bounds, its step and its current value (§10.1, §10.2)
            if (interface == VALUE_INTERFACE)
                return {"MinimumValue", "MaximumValue", "MinimumIncrement", "CurrentValue", "Text"};
            // a field carries its character count and its caret (§10.2)
            if (interface == TEXT_INTERFACE)
                return {"CharacterCount", "CaretOffset"};
            // a selection container carries how many children it holds selected (§10.2)
            if (interface == SELECTION_INTERFACE)
                return {"NSelectedChildren"};
            // an interface with no property answers an empty set, which the quiet default states (§10.2)
            return {};
        }

        // the object a path named, which is either the application root or one published node (§10.2)
        struct ObjectView
        {
            // the publication the view reads, which is immutable once stored (§10.2)
            std::shared_ptr<const Publication> publication;

            // the unique bus name every reference carries, which is copied with the view (§10.2)
            std::string bus_name;

            // the node the path named, which only a node answers with (§10.2)
            std::size_t index = SemanticTree::NO_PARENT;

            // whether the path named the application root rather than a node (§10.2)
            bool root = false;
        };

        // hand out the children of an object, which the root answers with the tree roots (§10.2)
        std::vector<std::size_t> children_of(const ObjectView& view) {
            // a detached bridge holds no publication, so nothing has a child (§10.2)
            if (view.publication == nullptr)
                return {};
            // the application root announces the nodes without a parent, which are its windows (§10.2)
            if (view.root)
                return view.publication->roots;
            return view.publication->tree.children_of(view.index);
        }

        // find the identifier a node registered one action under, which a platform reply names (§10.1)
        SemanticActionId action_id_of(const SemanticNode& node, std::string_view name) {
            for (SemanticActionId id = 1; id <= node.action_count(); id++)
                if (node.action_name(id) == name)
                    return id;
            // a name nothing registered is not an action, which the zero identifier states (§10.1)
            return 0;
        }

        // hand out the semantic node a view named, which the root answers with null (§10.2)
        const SemanticNode* node_of(const ObjectView& view) {
            // the root carries the application, which no published node describes (§10.2)
            if (view.root || view.publication == nullptr)
                return nullptr;
            return view.publication->tree.at(view.index);
        }

        // hand out the parent reference, which the root and a published root answer differently (§10.2)
        Reference parent_of(const ObjectView& view) {
            // the application root has no parent, which the protocol words as the null path (§10.2)
            if (view.root)
                return {view.bus_name, NULL_PATH};
            // a node without a parent hangs off the application root, which the root path names (§10.2)
            const std::size_t parent = view.publication->tree.parent_of(view.index);
            if (parent == SemanticTree::NO_PARENT)
                return {view.bus_name, std::string(ATSPI_ROOT_PATH)};
            return {view.bus_name, object_path_for(parent)};
        }

        // hand out the position of an object among its siblings, which the index query answers with (§10.2)
        dbus_int32_t index_in_parent(const ObjectView& view) {
            // the application root has no siblings, which the protocol words as minus one (§10.2)
            if (view.root || view.publication == nullptr)
                return -1;
            // the parent link decides the sibling set, because a root's siblings are the other roots (§10.2)
            const std::size_t parent = view.publication->tree.parent_of(view.index);
            const std::vector<std::size_t> siblings = parent == SemanticTree::NO_PARENT ? view.publication->roots : view.publication->tree.children_of(parent);
            for (std::size_t position = 0; position < siblings.size(); position++)
                if (siblings[position] == view.index)
                    return static_cast<dbus_int32_t>(position);
            return -1;
        }

        // the accessibility bridge, which serves the published tree over at-spi on a private connection (§10.2)
        class LinuxAccessibilityAdapter final : public AccessibilityAdapter
        {
        public:
            // the bridge holds one connection pair and one dispatch thread, which the destructor releases (§10.2)
            ~LinuxAccessibilityAdapter() override;

            // refuse a copy, because the dispatch thread is bound to one bridge (§10.2)
            LinuxAccessibilityAdapter(const LinuxAccessibilityAdapter&) = delete;

            // refuse an assignment, because the dispatch thread is bound to one bridge (§10.2)
            LinuxAccessibilityAdapter& operator=(const LinuxAccessibilityAdapter&) = delete;

            // build an idle bridge, which opens its connection on the first attach (§10.2)
            LinuxAccessibilityAdapter() = default;

            // attach the window, which opens the accessibility bus and registers the application (§10.2)
            void attach_window(NativeWindowHandle window) override;

            // publish a rebuilt tree, which the cache is replaced with and the registry is told about (§10.2)
            void publish(const SemanticTree& tree) override;

            // detach from the window, which unregisters the application and releases the bus (§5.2, §10.2)
            void detach_window() override;

            // report how many nodes the last publication held, which a coverage test asserts on (§10.2)
            [[nodiscard]] std::size_t published_node_count() const override { return m_count.load(); }

            // hand out the unique bus name the bridge announced, which a client addresses it by (§10.2)
            [[nodiscard]] std::string bus_name() const;

            // answer one client message, which the dispatch thread runs for every call (§10.2)
            DBusHandlerResult dispatch(DBusConnection* connection, DBusMessage* message);

        private:
            // resolve a path to an object, which a stale path leaves unresolved (§10.2)
            std::optional<ObjectView> view_for(std::string_view path) const;

            // describe one property, which a read and a full dictionary both use (§10.2)
            std::optional<PropertyValue> property_for(const ObjectView& view, std::string_view interface, std::string_view name) const;

            // name the interfaces an object answers, which the interface query and the introspection share (§10.2)
            std::vector<std::string> interface_names(const ObjectView& view) const;

            // compute the state words of an object, which the state query answers with (§10.2)
            std::array<std::uint32_t, 2> state_words(const ObjectView& view) const;

            // answer one accessible member, which carries the hierarchy and the role (§10.2)
            DBusMessage* accessible_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one component member, which carries extents and hit testing (§10.2)
            DBusMessage* component_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one action member, which carries the registered actions (§10.2)
            DBusMessage* action_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one value member, which the interface refuses because it carries properties only (§10.2)
            DBusMessage* value_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one text member, which carries the field text and its selection (§10.2)
            DBusMessage* text_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one editable text member, which carries the text write (§10.2)
            DBusMessage* editable_text_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one selection member, which carries the radio group choice (§10.2)
            DBusMessage* selection_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one application member, which only the root answers (§10.2)
            DBusMessage* application_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one socket member, which carries the embed notification the registry sends (§10.2)
            DBusMessage* socket_member(const ObjectView& view, DBusMessage* message, std::string_view member) const;

            // answer one properties member, which carries the property read, the full read and the write (§10.2)
            DBusMessage* properties_member(const ObjectView& view, DBusMessage* message, std::string_view member);

            // answer one introspection member, which describes the object the caller reached (§10.2)
            DBusMessage* introspect_member(const ObjectView& view, DBusMessage* message) const;

            // build an action request for one node, which the dispatch thread queues and the ui thread runs (§10.2)
            Invocation action_request(std::size_t index, SemanticActionId action) const;

            // build a text request for one node, which carries the text a client wrote (§10.2)
            Invocation text_request(std::size_t index, std::string text) const;

            // build a range request for one node, which carries the number a client wrote (§10.2)
            Invocation range_request(std::size_t index, double number) const;

            // queue one request for the ui thread, which the dispatch thread never runs itself (§10.2)
            void enqueue(Invocation invocation) const;

            // run the requests the dispatch thread accepted against the publication about to be stored (§10.2)
            void run_invocations(const std::shared_ptr<Publication>& publication);

            // run the dispatch loop until the bridge detaches or the bus goes away (§10.2)
            void serve(DBusConnection* connection);

            // announce that the children of the root changed, which tells a client to re-read the tree (§10.2)
            void announce_children(DBusConnection* connection, const std::string& bus_name, const std::vector<std::size_t>& previous, const std::vector<std::size_t>& current);

            // send one children changed signal, which carries the operation and the child (§10.2)
            void send_children_signal(DBusConnection* connection, const std::string& bus_name, const char* operation, std::int32_t index, std::size_t child);

            // the lock that keeps a rebuild from tearing a reply, which every read takes (§10.2)
            mutable std::mutex m_mutex;

            // the tree as published, which a rebuild replaces rather than writes (§10.2)
            std::shared_ptr<const Publication> m_publication;

            // the requests the dispatch thread accepted, which the next publish runs (§10.2)
            mutable std::deque<Invocation> m_invocations;

            // how many nodes the last publication held, which a coverage test reads (§10.2)
            std::atomic<std::size_t> m_count{0};

            // the session connection, which the launcher answers on and a host without at-spi keeps serving (§10.2)
            DBusConnection* m_session = nullptr;

            // the accessibility connection, which the tree is announced on (§10.2)
            DBusConnection* m_accessibility = nullptr;

            // the unique name the tree is addressed by, which every reference carries (§10.2)
            std::string m_bus_name;

            // the address the session exported, which the application bus address answers with (§10.2)
            std::string m_session_address;

            // the number the registry assigned the application, which the registry writes (§10.2)
            std::atomic<std::int32_t> m_application_id{0};

            // the dispatch thread, which answers client calls while the window is attached (§10.2)
            std::thread m_thread;

            // the flag that asks the dispatch thread to stop, which a detach clears (§10.2)
            std::atomic<bool> m_running{false};
        };

        // answer one message on the served subtree, which the vtable installs (§10.2)
        DBusHandlerResult object_message(DBusConnection* connection, DBusMessage* message, void* user_data) {
            // the bridge the subtree was registered with is the one that answers (§10.2)
            auto* adapter = static_cast<LinuxAccessibilityAdapter*>(user_data);
            return adapter->dispatch(connection, message);
        }
    } // namespace

    std::string atspi_object_path(std::size_t index) {
        // the object path is the same one the bridge serves, because the index is the only identity it carries (§10.2)
        return object_path_for(index);
    }

    std::string atspi_bus_name_for(AccessibilityAdapter& adapter) {
        // a test addresses the bridge through the name it announced, which the adapter owns (§10.2)
        auto* linux_adapter = dynamic_cast<LinuxAccessibilityAdapter*>(&adapter);
        return linux_adapter == nullptr ? std::string{} : linux_adapter->bus_name();
    }

    LinuxAccessibilityAdapter::~LinuxAccessibilityAdapter() { detach_window(); }

    std::string LinuxAccessibilityAdapter::bus_name() const {
        // the lock reads the name a detach clears, so a test never reads a half written string (§10.2)
        const std::lock_guard<std::mutex> guard(m_mutex);
        return m_bus_name;
    }

    void LinuxAccessibilityAdapter::attach_window(NativeWindowHandle window) {
        // the tree is application scoped on at-spi, so the native handle is not what publishes it (§10.2)
        (void)window;
        // a second attach replaces the first, so the previous bus is released before a new one opens (§10.2)
        detach_window();
        // the library is initialized once before any connection crosses two threads (§10.2)
        static std::once_flag threads_once;
        std::call_once(threads_once, [] { dbus_threads_init_default(); });
        // the exported address is the bus a desktop session reaches, which a test replaces with a private one (§10.2)
        const std::string session_address = core::get_environment_variable("DBUS_SESSION_BUS_ADDRESS").value_or("");
        std::string session_name;
        DBusConnection* session = open_connection(session_address, session_name);
        // a host with no session bus leaves the bridge degraded rather than failing (§10.2)
        if (session == nullptr)
            return;
        // the launcher names the accessibility bus, which a desktop starts beside the session bus (§10.2)
        const std::string accessibility_address = query_accessibility_bus(session);
        DBusConnection* accessibility = session;
        std::string bus_name = session_name;
        if (!accessibility_address.empty() && accessibility_address != session_address) {
            std::string accessibility_name;
            DBusConnection* candidate = open_connection(accessibility_address, accessibility_name);
            // a second bus that refused the connection leaves the session bus serving (§10.2)
            if (candidate != nullptr) {
                accessibility = candidate;
                bus_name = accessibility_name;
            }
        }
        // the fallback subtree serves the root and every node, so one registration covers the publication (§10.2)
        DBusObjectPathVTable vtable{};
        vtable.message_function = &object_message;
        if (!dbus_connection_register_fallback(accessibility, ACCESSIBLE_PREFIX, &vtable, this)) {
            // a connection that refused the path serves nothing, so both connections are released (§10.2)
            close_connection(accessibility);
            if (accessibility != session)
                close_connection(session);
            return;
        }
        // the registry is told the application arrived before the dispatch thread starts, because the call blocks (§10.2)
        embed_application(accessibility, bus_name);
        {
            const std::lock_guard<std::mutex> guard(m_mutex);
            m_session = session;
            m_accessibility = accessibility;
            m_bus_name = bus_name;
            m_session_address = session_address;
            m_running.store(true);
        }
        // the dispatch thread answers every client call while the window is attached (§10.2)
        m_thread = std::thread([this, accessibility] { serve(accessibility); });
    }

    void LinuxAccessibilityAdapter::publish(const SemanticTree& tree) {
        // the publication is built whole before it is stored, because a reply reads one tree at a time (§10.2)
        auto publication = std::make_shared<Publication>();
        publication->tree = tree;
        for (std::size_t index = 0; index < publication->tree.size(); index++)
            if (publication->tree.parent_of(index) == SemanticTree::NO_PARENT)
                publication->roots.push_back(index);
        // the requests a client made run on this publication here, which is where the ui thread marshals them (§10.2)
        run_invocations(publication);
        std::vector<std::size_t> previous;
        DBusConnection* connection = nullptr;
        std::string bus_name;
        {
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the previous roots are copied, because a client is told what went away before what arrived (§10.2)
            if (m_publication != nullptr)
                previous = m_publication->roots;
            m_publication = publication;
            m_count.store(publication->tree.size());
            connection = m_accessibility;
            bus_name = m_bus_name;
        }
        // a detached or disconnected bridge announces nothing, which a rebuild before an attach expects (§10.2)
        if (connection == nullptr || !dbus_connection_get_is_connected(connection))
            return;
        announce_children(connection, bus_name, previous, publication->roots);
    }

    Invocation LinuxAccessibilityAdapter::action_request(std::size_t index, SemanticActionId action) const {
        Invocation invocation;
        invocation.index = index;
        invocation.kind = Invocation::Kind::Action;
        invocation.action = action;
        return invocation;
    }

    Invocation LinuxAccessibilityAdapter::text_request(std::size_t index, std::string text) const {
        Invocation invocation;
        invocation.index = index;
        invocation.kind = Invocation::Kind::WriteText;
        invocation.text = std::move(text);
        return invocation;
    }

    Invocation LinuxAccessibilityAdapter::range_request(std::size_t index, double number) const {
        Invocation invocation;
        invocation.index = index;
        invocation.kind = Invocation::Kind::WriteRange;
        invocation.number = number;
        return invocation;
    }

    void LinuxAccessibilityAdapter::enqueue(Invocation invocation) const {
        const std::lock_guard<std::mutex> guard(m_mutex);
        m_invocations.push_back(std::move(invocation));
    }

    void LinuxAccessibilityAdapter::run_invocations(const std::shared_ptr<Publication>& publication) {
        std::deque<Invocation> pending;
        {
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the queue is taken whole, so a request that arrives while these run waits for the next publish (§10.2)
            pending.swap(m_invocations);
        }
        for (const Invocation& invocation : pending) {
            // the node the request named comes from the publication about to be stored, which no other thread reads (§10.2)
            const SemanticNode* node = publication == nullptr ? nullptr : publication->tree.at(invocation.index);
            // a node the publication does not hold carries no request, which a rebuild can leave (§10.2)
            if (node == nullptr)
                continue;
            switch (invocation.kind) {
            case Invocation::Kind::Action:
                (void)node->invoke(invocation.action);
                break;
            case Invocation::Kind::WriteText:
                (void)node->write_value(invocation.text);
                break;
            case Invocation::Kind::WriteRange:
                (void)node->set_range_value(invocation.number);
                break;
            }
        }
    }

    void LinuxAccessibilityAdapter::detach_window() {
        DBusConnection* session = nullptr;
        DBusConnection* accessibility = nullptr;
        std::string bus_name;
        {
            const std::lock_guard<std::mutex> guard(m_mutex);
            m_running.store(false);
            session = m_session;
            accessibility = m_accessibility;
            bus_name = m_bus_name;
        }
        // the thread is joined before the connections go, so no reply reads a released connection (§10.2)
        if (m_thread.joinable())
            m_thread.join();
        // the registry is told the application left, which keeps its list of applications current (§10.2)
        if (accessibility != nullptr && !bus_name.empty())
            unembed_application(accessibility, bus_name);
        if (accessibility != nullptr && accessibility != session)
            close_connection(accessibility);
        close_connection(session);
        {
            const std::lock_guard<std::mutex> guard(m_mutex);
            m_session = nullptr;
            m_accessibility = nullptr;
            m_bus_name.clear();
            m_session_address.clear();
            m_publication.reset();
            // a queued request belongs to the session that detached, so it is dropped rather than run later (§10.2)
            m_invocations.clear();
            m_count.store(0);
        }
    }

    void LinuxAccessibilityAdapter::serve(DBusConnection* connection) {
        // the loop reads and dispatches until the bridge detaches or the bus goes away (§10.2)
        while (m_running.load())
            if (!dbus_connection_read_write_dispatch(connection, DISPATCH_TIMEOUT_MS))
                break;
    }

    std::optional<ObjectView> LinuxAccessibilityAdapter::view_for(std::string_view path) const {
        const std::lock_guard<std::mutex> guard(m_mutex);
        ObjectView view;
        view.publication = m_publication;
        view.bus_name = m_bus_name;
        // the root path names the application, which answers even before anything is published (§10.2)
        if (path == ATSPI_ROOT_PATH) {
            view.root = true;
            return view;
        }
        // a node path is the prefix followed by a one based number, which the publication names (§10.2)
        const std::string prefix = std::string(ACCESSIBLE_PREFIX) + "/";
        if (!path.starts_with(prefix))
            return std::nullopt;
        const std::string_view tail = path.substr(prefix.size());
        // a tail that is not all digits names no node, which a stale client path produces (§10.2)
        if (tail.empty())
            return std::nullopt;
        std::size_t number = 0;
        for (const char digit : tail) {
            // a character that is not a digit names no node, which the quiet refusal states (§10.2)
            if (digit < '0' || digit > '9')
                return std::nullopt;
            number = number * 10 + static_cast<std::size_t>(digit - '0');
            // an overflowing number names no node, because no publication holds that many (§10.2)
            if (number > 1'000'000'000)
                return std::nullopt;
        }
        // zero names the root path, which is not a node path (§10.2)
        if (number == 0)
            return std::nullopt;
        view.index = number - 1;
        // a publication that holds no such node is a stale path, which the refusal reports (§10.2)
        if (view.publication == nullptr || view.index >= view.publication->tree.size())
            return std::nullopt;
        return view;
    }

    std::vector<std::string> LinuxAccessibilityAdapter::interface_names(const ObjectView& view) const {
        std::vector<std::string> names{ACCESSIBLE_INTERFACE, COMPONENT_INTERFACE};
        // the application root answers the application and socket interfaces, which no node answers (§10.2)
        if (view.root) {
            names.push_back(APPLICATION_INTERFACE);
            names.push_back(SOCKET_INTERFACE);
            return names;
        }
        const SemanticNode* node = node_of(view);
        if (node == nullptr)
            return names;
        // a registered action is what makes the action interface answer, which the node states (§10.1, §10.2)
        if (node->action_count() > 0)
            names.push_back(ACTION_INTERFACE);
        // a bounded value is what makes the value interface answer, which the node states (§10.1, §10.2)
        if (node->has_range())
            names.push_back(VALUE_INTERFACE);
        // a field and a combo box answer the text interfaces, because at-spi reads their text through them (§10.2)
        if (node->role() == SemanticRole::TextField || node->role() == SemanticRole::ComboBox) {
            names.push_back(TEXT_INTERFACE);
            names.push_back(EDITABLE_TEXT_INTERFACE);
        }
        // a radio group answers the selection interface, which is the only container that exposes one (§10.2)
        if (node->role() == SemanticRole::RadioGroup)
            names.push_back(SELECTION_INTERFACE);
        return names;
    }

    std::array<std::uint32_t, 2> LinuxAccessibilityAdapter::state_words(const ObjectView& view) const {
        std::array<std::uint32_t, 2> words{0, 0};
        // the state numbers above thirty one live in the second word, which the protocol returns as a set (§10.2)
        const auto set = [&words](std::uint32_t bit) {
            if (bit < 32)
                words[0] |= 1U << bit;
            else
                words[1] |= 1U << (bit - 32);
        };
        // the application root is an enabled and visible object, which carries no node state (§10.2)
        if (view.root) {
            set(STATE_ENABLED);
            set(STATE_SENSITIVE);
            set(STATE_SHOWING);
            set(STATE_VISIBLE);
            return words;
        }
        const SemanticNode* node = node_of(view);
        if (node == nullptr)
            return words;
        // every published node is enabled and sensitive unless it reports the disabled state (§10.1, §10.2)
        if (!node->disabled()) {
            set(STATE_ENABLED);
            set(STATE_SENSITIVE);
        }
        // every published node is drawn, because the semantic tree only carries what the frame built (§10.2)
        set(STATE_SHOWING);
        set(STATE_VISIBLE);
        // a role the user operates takes focus, which the focusable state announces (§10.2)
        if (focusable_role(node->role()))
            set(STATE_FOCUSABLE);
        // the focus flag is the keyboard focus the window reported (§10.1)
        if (node->focused())
            set(STATE_FOCUSED);
        // the selected flag is the chosen row, page or radio choice (§10.1)
        if (node->selected())
            set(STATE_SELECTED);
        // a choice of a selection container is selectable, which is what the container ranges over (§10.2)
        if (selectable_role(node->role()))
            set(STATE_SELECTABLE);
        // a checkable node carries both the checkable state and its check state (§10.1, §10.2)
        if (node->check() != CheckState::NotCheckable) {
            set(STATE_CHECKABLE);
            if (node->checked())
                set(STATE_CHECKED);
            if (node->check() == CheckState::Indeterminate)
                set(STATE_INDETERMINATE);
        }
        // a progress bar whose work is unknown reports the indeterminate state (§10.1)
        if (node->indeterminate())
            set(STATE_INDETERMINATE);
        // a text field carries selectable text, which every field answers (§10.2)
        if (node->role() == SemanticRole::TextField)
            set(STATE_SELECTABLE_TEXT);
        // a field with a write accepts edits unless it is disabled or read only (§10.1, §10.2)
        if (node->role() == SemanticRole::TextField && node->has_value_action() && !node->disabled() && !node->read_only())
            set(STATE_EDITABLE);
        // a multi line field announces that it carries more than one line (§10.1)
        if (node->multiline())
            set(STATE_MULTI_LINE);
        // a field without a write path is read only, which still takes focus and permits a copy (§10.1, §10.2)
        if (node->role() == SemanticRole::TextField && (!node->has_value_action() || node->read_only()))
            set(STATE_READ_ONLY);
        // a combo box is the one role that opens a popup, which the expandable state announces (§10.1, §10.2)
        if (node->role() == SemanticRole::ComboBox) {
            set(STATE_EXPANDABLE);
            set(STATE_HAS_POPUP);
        }
        // the expanded flag is the popup the control reported open (§10.1)
        if (node->expanded())
            set(STATE_EXPANDED);
        return words;
    }

    std::optional<PropertyValue> LinuxAccessibilityAdapter::property_for(const ObjectView& view, std::string_view interface, std::string_view name) const {
        const SemanticNode* node = node_of(view);
        // the accessible interface carries the identity, the name and the hierarchy links (§10.2)
        if (interface == ACCESSIBLE_INTERFACE) {
            if (name == "Name")
                return string_value(view.root ? "Albor" : node->name());
            if (name == "Description")
                return string_value(view.root ? std::string{} : node->description());
            if (name == "Parent") {
                const Reference parent = parent_of(view);
                return reference_value(parent.first, parent.second);
            }
            if (name == "ChildCount")
                return int32_value(static_cast<std::int32_t>(children_of(view).size()));
            if (name == "Locale")
                return string_value({});
            if (name == "AccessibleId")
                return string_value(view.root ? std::string{} : node->explicit_id());
            return std::nullopt;
        }
        // the application interface carries the toolkit and the identifier the registry assigns (§10.2)
        if (interface == APPLICATION_INTERFACE && view.root) {
            if (name == "ToolkitName")
                return string_value("Albor");
            if (name == "Version")
                return string_value({});
            if (name == "AtspiVersion")
                return string_value("2.1");
            if (name == "InterfaceVersion")
                return uint32_value(1);
            if (name == "Id")
                return int32_value(m_application_id.load());
            return std::nullopt;
        }
        // a node that is not there answers no state property, which a stale read produces (§10.2)
        if (node == nullptr)
            return std::nullopt;
        // the action interface announces how many actions the node exposes (§10.2)
        if (interface == ACTION_INTERFACE && name == "NActions")
            return int32_value(static_cast<std::int32_t>(node->action_count()));
        // the value interface carries the range and the write, which a range control publishes (§10.1, §10.2)
        if (interface == VALUE_INTERFACE) {
            if (name == "MinimumValue")
                return double_value(node->range().minimum);
            if (name == "MaximumValue")
                return double_value(node->range().maximum);
            if (name == "MinimumIncrement")
                return double_value(node->range().step);
            if (name == "CurrentValue")
                return double_value(node->range().value);
            if (name == "Text")
                return string_value({});
            return std::nullopt;
        }
        // the text interface carries the character count and the caret, which a field publishes (§10.2)
        if (interface == TEXT_INTERFACE) {
            if (name == "CharacterCount")
                return int32_value(static_cast<std::int32_t>(character_offset(node->value(), node->value().size())));
            if (name == "CaretOffset")
                return int32_value(static_cast<std::int32_t>(character_offset(node->value(), node->selection_end())));
            return std::nullopt;
        }
        // the selection interface announces how many choices the group holds selected (§10.2)
        if (interface == SELECTION_INTERFACE && name == "NSelectedChildren") {
            std::int32_t selected = 0;
            for (const std::size_t child : children_of(view)) {
                const SemanticNode* candidate = view.publication->tree.at(child);
                if (candidate != nullptr && candidate->selected())
                    selected++;
            }
            return int32_value(selected);
        }
        return std::nullopt;
    }

    DBusMessage* LinuxAccessibilityAdapter::accessible_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        const SemanticNode* node = node_of(view);
        // the child query resolves one sibling, which an index names (§10.2)
        if (member == "GetChildAtIndex") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            const std::vector<std::size_t> children = children_of(view);
            // an index past the end names no child, which the protocol words as the null reference (§10.2)
            if (index < 0 || static_cast<std::size_t>(index) >= children.size())
                return reply_with_reference(message, view.bus_name, NULL_PATH);
            return reply_with_reference(message, view.bus_name, object_path_for(children[static_cast<std::size_t>(index)]));
        }
        // the children query answers the whole sibling set, which a reader walks (§10.2)
        if (member == "GetChildren") {
            std::vector<Reference> references;
            for (const std::size_t child : children_of(view))
                references.emplace_back(view.bus_name, object_path_for(child));
            return reply_with_references(message, references);
        }
        // the position query answers where the object sits among its siblings (§10.2)
        if (member == "GetIndexInParent")
            return reply_with_int32(message, index_in_parent(view));
        // the role query answers the number a client announces (§10.2)
        if (member == "GetRole")
            return reply_with_uint32(message, view.root ? ROLE_APPLICATION : role_for(node->role()));
        // the role name query answers the word a reader speaks beside the number (§10.2)
        if (member == "GetRoleName")
            return reply_with_string(message, view.root ? std::string("application") : std::string(role_name(node->role())));
        // the localized role name is the same word, because the bridge ships no translation table (§10.2)
        if (member == "GetLocalizedRoleName")
            return reply_with_string(message, view.root ? std::string("application") : std::string(role_name(node->role())));
        // the state query answers the two words the states live in (§10.2)
        if (member == "GetState") {
            const std::array<std::uint32_t, 2> words = state_words(view);
            return reply_with_state(message, words[0], words[1]);
        }
        // the attributes query answers an empty dictionary, because the semantic tree carries none (§10.2)
        if (member == "GetAttributes")
            return reply_with_empty_dictionary(message);
        // the application query names the root, which every node belongs to (§10.2)
        if (member == "GetApplication")
            return reply_with_reference(message, view.bus_name, std::string(ATSPI_ROOT_PATH));
        // the interface query answers what this object implements, which the client then calls (§10.2)
        if (member == "GetInterfaces")
            return reply_with_string_array(message, interface_names(view));
        // the relation query answers no relation, because the tree carries none (§10.2)
        if (member == "GetRelationSet")
            return reply_with_empty_relations(message);
        return unknown_member_error(message, ACCESSIBLE_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::component_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        const SemanticNode* node = node_of(view);
        const Rect bounds = node == nullptr ? Rect{} : node->bounds();
        // the extent query answers the published bounds, which the window coordinates name (§10.2)
        if (member == "GetExtents") {
            dbus_uint32_t coordinate = 0;
            if (!read_coordinate(message, coordinate))
                return invalid_arguments_error(message);
            return reply_with_rectangle(message, bounds);
        }
        // the position query answers the origin of the published bounds (§10.2)
        if (member == "GetPosition") {
            dbus_uint32_t coordinate = 0;
            if (!read_coordinate(message, coordinate))
                return invalid_arguments_error(message);
            return reply_with_position(message, bounds);
        }
        // the size query answers the extent of the published bounds (§10.2)
        if (member == "GetSize")
            return reply_with_size(message, bounds);
        // the contains query answers whether the point falls inside the published bounds (§10.2)
        if (member == "Contains") {
            dbus_int32_t x = 0;
            dbus_int32_t y = 0;
            dbus_uint32_t coordinate = 0;
            if (!read_point_with_coordinate(message, x, y, coordinate))
                return invalid_arguments_error(message);
            return reply_with_bool(message, rectangle_contains(bounds, x, y));
        }
        // the point query answers the first child under the point, or the object itself (§10.2)
        if (member == "GetAccessibleAtPoint") {
            dbus_int32_t x = 0;
            dbus_int32_t y = 0;
            if (!read_point(message, x, y))
                return invalid_arguments_error(message);
            for (const std::size_t child : children_of(view)) {
                const SemanticNode* candidate = view.publication->tree.at(child);
                if (candidate != nullptr && rectangle_contains(candidate->bounds(), x, y))
                    return reply_with_reference(message, view.bus_name, object_path_for(child));
            }
            // a point with no child under it names the object the caller reached (§10.2)
            return reply_with_reference(message, view.bus_name, view.root ? std::string(ATSPI_ROOT_PATH) : object_path_for(view.index));
        }
        // the layer query answers the widget layer, which is where a control sits (§10.2)
        if (member == "GetLayer")
            return reply_with_uint32(message, LAYER_WIDGET);
        // the stacking order answers minus one, because the tree carries no stacking (§10.2)
        if (member == "GetMDIZOrder")
            return reply_with_int16(message, -1);
        // the alpha answers fully opaque, because the semantic tree carries no transparency (§10.2)
        if (member == "GetAlpha")
            return reply_with_double(message, 1.0);
        // the focus request queues a focus action when a node registered one, which is the only portable route (§10.2)
        if (member == "GrabFocus") {
            const SemanticActionId action = node == nullptr ? 0 : action_id_of(*node, "focus");
            const bool accepted = action != 0 && !node->disabled();
            if (accepted)
                enqueue(action_request(view.index, action));
            return reply_with_bool(message, accepted);
        }
        // a scroll or a resize is refused, because the framework owns placement and scrolling (§10.2)
        if (member == "SetExtents" || member == "SetPosition" || member == "SetSize" || member == "ScrollTo" || member == "ScrollToPoint")
            return reply_with_bool(message, false);
        return unknown_member_error(message, COMPONENT_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::action_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        const SemanticNode* node = node_of(view);
        if (node == nullptr)
            return unknown_member_error(message, ACTION_INTERFACE, member);
        // the name query answers the action name, which a reader selects it by (§10.1, §10.2)
        if (member == "GetName") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            return reply_with_string(message, node->action_name(static_cast<SemanticActionId>(index) + 1));
        }
        // the description query answers an empty word, because an action carries no description (§10.2)
        if (member == "GetDescription")
            return reply_with_string(message, {});
        // the key binding query answers an empty word, because the semantic tree carries no binding (§10.2)
        if (member == "GetKeyBinding")
            return reply_with_string(message, {});
        // the invocation queues the action the index names, which is one based in the node (§10.1, §10.2)
        if (member == "DoAction") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            const SemanticActionId action = index < 0 ? 0 : static_cast<SemanticActionId>(index) + 1;
            // an action the node does not carry or refuses is not queued, which the reply states (§10.1)
            const bool accepted = action != 0 && !node->action_name(action).empty() && !node->disabled();
            if (accepted)
                enqueue(action_request(view.index, action));
            return reply_with_bool(message, accepted);
        }
        return unknown_member_error(message, ACTION_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::value_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        (void)view;
        // the value interface carries properties only, so a method call names no member of it (§10.2)
        return unknown_member_error(message, VALUE_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::text_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        const SemanticNode* node = node_of(view);
        if (node == nullptr)
            return unknown_member_error(message, TEXT_INTERFACE, member);
        // the text query answers the characters between two offsets, which the field holds (§10.2)
        if (member == "GetText") {
            dbus_int32_t start = 0;
            dbus_int32_t end = -1;
            if (!read_two_int32(message, start, end))
                return invalid_arguments_error(message);
            const std::string& value = node->value();
            const std::size_t characters = character_offset(value, value.size());
            const std::size_t first = start < 0 ? 0 : static_cast<std::size_t>(start);
            const std::size_t last = end < 0 ? characters : static_cast<std::size_t>(end);
            return reply_with_string(message, text_range(value, first, last));
        }
        // a field always carries one selection, which is the caret when nothing is selected (§10.2)
        if (member == "GetNSelections")
            return reply_with_int32(message, 1);
        // the selection query answers the selected range in characters, which the field published in bytes (§10.1, §10.2)
        if (member == "GetSelection") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            // an index other than zero names no selection, which a single selection field refuses (§10.2)
            if (index != 0)
                return reply_with_offsets(message, 0, 0);
            const std::string& value = node->value();
            const dbus_int32_t start = static_cast<dbus_int32_t>(character_offset(value, node->selection_start()));
            const dbus_int32_t end = static_cast<dbus_int32_t>(character_offset(value, node->selection_end()));
            return reply_with_offsets(message, start, end);
        }
        return unknown_member_error(message, TEXT_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::editable_text_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        const SemanticNode* node = node_of(view);
        if (node == nullptr)
            return unknown_member_error(message, EDITABLE_TEXT_INTERFACE, member);
        // the write queues the registered text action, which the ui thread runs at its next publish (§10.1, §10.2)
        if (member == "SetTextContents") {
            std::string text;
            if (!read_string(message, text))
                return invalid_arguments_error(message);
            // a field that is disabled, read only or without a write refuses the request (§10.1)
            const bool accepted = node->has_value_action() && !node->disabled() && !node->read_only();
            if (accepted)
                enqueue(text_request(view.index, std::move(text)));
            return reply_with_bool(message, accepted);
        }
        return unknown_member_error(message, EDITABLE_TEXT_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::selection_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        const std::vector<std::size_t> children = children_of(view);
        // the selected children are collected once, because every selection query walks the same set (§10.2)
        std::vector<std::size_t> selected;
        for (const std::size_t child : children) {
            const SemanticNode* candidate = view.publication->tree.at(child);
            if (candidate != nullptr && candidate->selected())
                selected.push_back(child);
        }
        // the selected child query answers the choice the index names, which a reader announces (§10.2)
        if (member == "GetSelectedChild") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            // a group with no choice selected answers the null reference, which the protocol allows (§10.2)
            if (index < 0 || static_cast<std::size_t>(index) >= selected.size())
                return reply_with_reference(message, view.bus_name, NULL_PATH);
            return reply_with_reference(message, view.bus_name, object_path_for(selected[static_cast<std::size_t>(index)]));
        }
        // the chosen query answers whether the child at an index is the choice (§10.2)
        if (member == "IsChildSelected") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            if (index < 0 || static_cast<std::size_t>(index) >= children.size())
                return reply_with_bool(message, false);
            const SemanticNode* candidate = view.publication->tree.at(children[static_cast<std::size_t>(index)]);
            return reply_with_bool(message, candidate != nullptr && candidate->selected());
        }
        // the select query queues the choice's activation, which is how a radio group takes a new choice (§10.1, §10.2)
        if (member == "SelectChild") {
            dbus_int32_t index = 0;
            if (!read_int32(message, index))
                return invalid_arguments_error(message);
            if (index < 0 || static_cast<std::size_t>(index) >= children.size())
                return reply_with_bool(message, false);
            const std::size_t child = children[static_cast<std::size_t>(index)];
            const SemanticNode* candidate = view.publication->tree.at(child);
            const SemanticActionId action = candidate == nullptr ? 0 : action_id_of(*candidate, "activate");
            const bool accepted = action != 0 && !candidate->disabled();
            if (accepted)
                enqueue(action_request(child, action));
            return reply_with_bool(message, accepted);
        }
        // a deselect is accepted and changes nothing, because a radio group always keeps one choice (§10.2)
        if (member == "DeselectChild" || member == "DeselectSelectedChild")
            return reply_with_bool(message, true);
        // a group selects one choice, so selecting all and clearing the selection are refused (§10.2)
        if (member == "SelectAll" || member == "ClearSelection")
            return reply_with_bool(message, false);
        return unknown_member_error(message, SELECTION_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::application_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        (void)view;
        // the locale query answers nothing, because the bridge ships no locale table (§10.2)
        if (member == "GetLocale")
            return reply_with_string(message, {});
        // the bus address query answers the session address, which is where the application lives (§10.2)
        if (member == "GetApplicationBusAddress")
            return reply_with_string(message, m_session_address);
        return unknown_member_error(message, APPLICATION_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::socket_member(const ObjectView& view, DBusMessage* message, std::string_view member) const {
        (void)view;
        // the registry reports an embedded application, which needs no work because the tree is already served (§10.2)
        if (member == "Embedded")
            return reply_without_body(message);
        // the registry reports a departing application, which releases the tree the client holds (§10.2)
        if (member == "Unembed")
            return reply_without_body(message);
        return unknown_member_error(message, SOCKET_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::properties_member(const ObjectView& view, DBusMessage* message, std::string_view member) {
        // a property read answers one variant, which the client decodes by name (§10.2)
        if (member == "Get") {
            const char* interface = nullptr;
            const char* name = nullptr;
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            if (read == FALSE || interface == nullptr || name == nullptr)
                return invalid_arguments_error(message);
            // an interface the object does not advertise carries no property, which the refusal reports (§10.2)
            const std::vector<std::string> interfaces = interface_names(view);
            if (std::find(interfaces.begin(), interfaces.end(), interface) == interfaces.end())
                return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_INTERFACE, "no such interface on this object");
            const std::optional<PropertyValue> value = property_for(view, interface, name);
            // a property the object does not carry is refused by name, which the protocol names (§10.2)
            if (!value) {
                const std::string text = "no property " + std::string(name) + " on " + interface;
                return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_PROPERTY, text.c_str());
            }
            return reply_with_property(message, *value);
        }
        // a full read answers every property of one interface, which a caching client asks for (§10.2)
        if (member == "GetAll") {
            const char* interface = nullptr;
            DBusError error;
            dbus_error_init(&error);
            const dbus_bool_t read = dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &interface, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            if (read == FALSE || interface == nullptr)
                return invalid_arguments_error(message);
            // an empty interface names every interface the object answers, which the protocol allows (§10.2)
            std::vector<std::string> interfaces;
            if (std::string_view(interface).empty()) {
                interfaces = interface_names(view);
            }
            else {
                // an interface the object does not answer carries no property, which the refusal reports (§10.2)
                const std::vector<std::string> answered = interface_names(view);
                if (std::find(answered.begin(), answered.end(), interface) == answered.end())
                    return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_INTERFACE, "no such interface on this object");
                interfaces.emplace_back(interface);
            }
            DBusMessage* reply = dbus_message_new_method_return(message);
            DBusMessageIter iterator;
            dbus_message_iter_init_append(reply, &iterator);
            DBusMessageIter dictionary;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "{sv}", &dictionary);
            for (const std::string& name : interfaces)
                for (const std::string_view property : property_names(name)) {
                    const std::optional<PropertyValue> value = property_for(view, name, property);
                    if (value)
                        append_dictionary_entry(&dictionary, property, *value);
                }
            dbus_message_iter_close_container(&iterator, &dictionary);
            return reply;
        }
        // a write carries one variant, which only the application id and the range accept (§10.2)
        if (member == "Set") {
            DBusMessageIter iterator;
            if (!dbus_message_iter_init(message, &iterator))
                return invalid_arguments_error(message);
            // the interface and the property name are the first two arguments, which every write carries (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_STRING)
                return invalid_arguments_error(message);
            const char* interface = nullptr;
            dbus_message_iter_get_basic(&iterator, &interface);
            dbus_message_iter_next(&iterator);
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_STRING)
                return invalid_arguments_error(message);
            const char* name = nullptr;
            dbus_message_iter_get_basic(&iterator, &name);
            dbus_message_iter_next(&iterator);
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_VARIANT)
                return invalid_arguments_error(message);
            DBusMessageIter variant;
            dbus_message_iter_recurse(&iterator, &variant);
            // the registry writes the application id, which the protocol assigns after the embed (§10.2)
            if (view.root && std::string_view(interface) == APPLICATION_INTERFACE && std::string_view(name) == "Id" && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_INT32) {
                dbus_int32_t identifier = 0;
                dbus_message_iter_get_basic(&variant, &identifier);
                m_application_id.store(identifier);
                return reply_without_body(message);
            }
            const SemanticNode* node = node_of(view);
            // the client writes the range, which the node clamps when the ui thread runs it (§10.1, §10.2)
            if (node != nullptr && std::string_view(interface) == VALUE_INTERFACE && std::string_view(name) == "CurrentValue" && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_DOUBLE) {
                double value = 0.0;
                dbus_message_iter_get_basic(&variant, &value);
                // a bounded control that is not disabled takes the write, which a detached node refuses (§10.1)
                if (!node->has_range() || node->disabled())
                    return dbus_message_new_error(message, DBUS_ERROR_FAILED, "the value refused the write");
                enqueue(range_request(view.index, value));
                return reply_without_body(message);
            }
            return dbus_message_new_error(message, DBUS_ERROR_PROPERTY_READ_ONLY, "the property is not writable");
        }
        return unknown_member_error(message, PROPERTIES_INTERFACE, member);
    }

    DBusMessage* LinuxAccessibilityAdapter::introspect_member(const ObjectView& view, DBusMessage* message) const {
        // the document names the interfaces and their members, which a bus tool reads to describe the object (§10.2)
        std::string xml(INTROSPECTION_HEADER);
        for (const std::string& name : interface_names(view))
            xml += interface_xml(name);
        xml += PROPERTIES_XML;
        xml += INTROSPECT_XML;
        xml += "</node>";
        return reply_with_string(message, xml);
    }

    DBusHandlerResult LinuxAccessibilityAdapter::dispatch(DBusConnection* connection, DBusMessage* message) {
        // a message that is not a method call needs no answer, which only a call does (§10.2)
        if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        const char* path = dbus_message_get_path(message);
        // a call on a path the publication does not hold names a dead object, which is refused (§10.2)
        const std::optional<ObjectView> view = path == nullptr ? std::nullopt : view_for(path);
        if (!view) {
            send_message(connection, dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_OBJECT, "no such accessible"));
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        const char* interface = dbus_message_get_interface(message);
        const char* member = dbus_message_get_member(message);
        // a call without an interface or a member names nothing, which is refused rather than guessed (§10.2)
        if (interface == nullptr || member == nullptr) {
            send_message(connection, invalid_arguments_error(message));
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        const std::string_view interface_view(interface);
        const std::string_view member_view(member);
        // an object answers only the interfaces it advertises, which the introspection shares (§10.2)
        const std::vector<std::string> interfaces = interface_names(*view);
        const auto answers = [&interfaces](std::string_view name) { return std::find(interfaces.begin(), interfaces.end(), name) != interfaces.end(); };
        DBusMessage* reply = nullptr;
        if (interface_view == PROPERTIES_INTERFACE)
            reply = properties_member(*view, message, member_view);
        else if (interface_view == INTROSPECT_INTERFACE)
            reply = introspect_member(*view, message);
        else if (interface_view == ACCESSIBLE_INTERFACE)
            reply = accessible_member(*view, message, member_view);
        else if (interface_view == COMPONENT_INTERFACE)
            reply = component_member(*view, message, member_view);
        else if (interface_view == ACTION_INTERFACE && answers(ACTION_INTERFACE))
            reply = action_member(*view, message, member_view);
        else if (interface_view == TEXT_INTERFACE && answers(TEXT_INTERFACE))
            reply = text_member(*view, message, member_view);
        else if (interface_view == EDITABLE_TEXT_INTERFACE && answers(EDITABLE_TEXT_INTERFACE))
            reply = editable_text_member(*view, message, member_view);
        else if (interface_view == SELECTION_INTERFACE && answers(SELECTION_INTERFACE))
            reply = selection_member(*view, message, member_view);
        else if (interface_view == VALUE_INTERFACE && answers(VALUE_INTERFACE))
            reply = value_member(*view, message, member_view);
        else if (interface_view == APPLICATION_INTERFACE && answers(APPLICATION_INTERFACE))
            reply = application_member(*view, message, member_view);
        else if (interface_view == SOCKET_INTERFACE && answers(SOCKET_INTERFACE))
            reply = socket_member(*view, message, member_view);
        else
            reply = dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_INTERFACE, "no such interface on this object");
        // a client that asked for no reply still had its call performed, which the absence of a message reports (§10.2)
        if (dbus_message_get_no_reply(message)) {
            if (reply != nullptr)
                dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        send_message(connection, reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    void LinuxAccessibilityAdapter::announce_children(DBusConnection* connection, const std::string& bus_name, const std::vector<std::size_t>& previous, const std::vector<std::size_t>& current) {
        // a publication that kept the same roots announces nothing, because the tree did not change (§10.2)
        if (previous == current)
            return;
        // only a child that went away is removed, because the others did not move (§10.2)
        for (std::size_t index = 0; index < previous.size(); index++) {
            const std::size_t child = previous[index];
            if (std::find(current.begin(), current.end(), child) == current.end())
                send_children_signal(connection, bus_name, "remove", static_cast<std::int32_t>(index), child);
        }
        // only a child that arrived is added at its position, which the root then carries (§10.2)
        for (std::size_t index = 0; index < current.size(); index++) {
            const std::size_t child = current[index];
            if (std::find(previous.begin(), previous.end(), child) == previous.end())
                send_children_signal(connection, bus_name, "add", static_cast<std::int32_t>(index), child);
        }
    }

    void LinuxAccessibilityAdapter::send_children_signal(DBusConnection* connection, const std::string& bus_name, const char* operation, std::int32_t index, std::size_t child) {
        // the signal is emitted by the root object, which is the parent the children changed under (§10.2)
        DBusMessage* signal = dbus_message_new_signal(std::string(ATSPI_ROOT_PATH).c_str(), EVENT_OBJECT_INTERFACE, "ChildrenChanged");
        if (signal == nullptr)
            return;
        DBusMessageIter iterator;
        dbus_message_iter_init_append(signal, &iterator);
        const char* operation_text = operation;
        dbus_message_iter_append_basic(&iterator, DBUS_TYPE_STRING, &operation_text);
        dbus_message_iter_append_basic(&iterator, DBUS_TYPE_INT32, &index);
        dbus_int32_t detail = 0;
        dbus_message_iter_append_basic(&iterator, DBUS_TYPE_INT32, &detail);
        DBusMessageIter variant;
        dbus_message_iter_open_container(&iterator, DBUS_TYPE_VARIANT, "(so)", &variant);
        append_object_reference(&variant, bus_name, object_path_for(child));
        dbus_message_iter_close_container(&iterator, &variant);
        DBusMessageIter properties;
        dbus_message_iter_open_container(&iterator, DBUS_TYPE_ARRAY, "{sv}", &properties);
        dbus_message_iter_close_container(&iterator, &properties);
        send_message(connection, signal);
    }

    bool available() {
        // the bridge is compiled with the platform, so every linux build carries one (§10.2, BDG-10)
        return true;
    }

    std::unique_ptr<AccessibilityAdapter> build() {
        // the adapter opens no connection until a window attaches, which keeps a headless publication bus free (§10.2)
        return std::make_unique<LinuxAccessibilityAdapter>();
    }
} // namespace albor::accessibility::platform
