// the linux half of the accessibility bridge: at-spi over d-bus on a private bus (§10.2, BDG-10)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include <gtest/gtest.h>

#include "accessibility_corpus.h"

#include "albor/accessibility/accessibility_adapter.h"
#include "albor/accessibility/accessibility_platform.linux.h"
#include "albor/runtime/binding_engine.h"
#include "albor/semantics/semantic_node.h"
#include "albor/semantics/semantic_provider.h"
#include "albor/ui/semantic_tree_builder.h"

using namespace albor;

namespace
{
    // the interface names the suite addresses, which are the at-spi contracts the bridge answers (§10.2)
    constexpr const char* ACCESSIBLE = "org.a11y.atspi.Accessible";

    // the component interface carries the extents, which a reader places an announcement with (§10.2)
    constexpr const char* COMPONENT = "org.a11y.atspi.Component";

    // the action interface carries the registered actions, which a reader invokes (§10.2)
    constexpr const char* ACTION = "org.a11y.atspi.Action";

    // the value interface carries the range a slider and a stepper publish (§10.2)
    constexpr const char* VALUE = "org.a11y.atspi.Value";

    // the text interface carries the field text and its selection (§10.2)
    constexpr const char* TEXT = "org.a11y.atspi.Text";

    // the editable text interface carries the text write (§10.2)
    constexpr const char* EDITABLE_TEXT = "org.a11y.atspi.EditableText";

    // the selection interface carries the radio group choice (§10.2)
    constexpr const char* SELECTION = "org.a11y.atspi.Selection";

    // the application interface only the root answers (§10.2)
    constexpr const char* APPLICATION = "org.a11y.atspi.Application";

    // the properties interface every property read crosses, which libatspi reads Accessible through (§10.2)
    constexpr const char* PROPERTIES = "org.freedesktop.DBus.Properties";

    // the event interface a structure change is announced on (§10.2)
    constexpr const char* EVENT_OBJECT = "org.a11y.atspi.Event.Object";

    // the introspection interface an object describes itself through, which a bus tool reads (§10.2)
    constexpr const char* INTROSPECT = "org.freedesktop.DBus.Introspectable";

    // the socket interface the registry embeds the application through (§10.2)
    constexpr const char* SOCKET = "org.a11y.atspi.Socket";

    // the launcher interface the accessibility bus address is asked of (§10.2)
    constexpr const char* LAUNCHER = "org.a11y.Bus";

    // a private bus daemon, which keeps the suite off the host session (§10.2)
    class PrivateBus
    {
    public:
        PrivateBus() {
            // the library is initialized once before a connection crosses two threads (§10.2)
            dbus_threads_init_default();
            // the daemon is started with a configuration the suite owns, so nothing on the host is reached (§10.2)
            m_ready = start_daemon();
        }

        ~PrivateBus() {
            // the daemon is stopped after the connections that used it are gone (§10.2)
            if (m_daemon > 0) {
                kill(m_daemon, SIGTERM);
                waitpid(m_daemon, nullptr, 0);
                m_daemon = -1;
            }
            // the configuration is released with the bus that read it (§10.2)
            if (!m_config_path.empty())
                unlink(m_config_path.c_str());
        }

        PrivateBus(const PrivateBus&) = delete;
        PrivateBus& operator=(const PrivateBus&) = delete;

        // report whether the private bus is up, which a host without the daemon leaves false (§10.2)
        [[nodiscard]] bool ready() const { return m_ready; }

        // hand out the address the clients connect to, which the suite exports as the session (§10.2)
        [[nodiscard]] const std::string& address() const { return m_address; }

    private:
        // start the daemon and read the address it prints, which is the one line it announces (§10.2)
        bool start_daemon() {
            // the configuration listens on a private socket and enables no service directory (§10.2)
            char config_path[] = "/tmp/albor-atspi-bus-XXXXXX";
            const int config_fd = mkstemp(config_path);
            // a refused configuration descriptor leaves no bus to start (§10.2)
            if (config_fd < 0)
                return false;
            // the policy lets the suite send, receive and own any name on its own private bus (§10.2)
            static constexpr const char* CONFIG = "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n<busconfig>\n  <type>session</type>\n  <listen>unix:tmpdir=/tmp</listen>\n  <auth>EXTERNAL</auth>\n  <policy context=\"default\">\n    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n    <allow eavesdrop=\"true\"/>\n    <allow own=\"*\"/>\n  </policy>\n</busconfig>\n";
            // the bytes are written once, which the daemon reads when it starts (§10.2)
            const ssize_t written = write(config_fd, CONFIG, std::strlen(CONFIG));
            close(config_fd);
            // a short write leaves no configuration for the daemon to read (§10.2)
            if (written != static_cast<ssize_t>(std::strlen(CONFIG)))
                return false;
            m_config_path = config_path;
            // the pipe carries the address the daemon prints before it serves (§10.2)
            int descriptors[2] = {-1, -1};
            if (pipe(descriptors) != 0)
                return false;
            // the daemon runs in its own process, which the fixture stops when the suite ends (§10.2)
            const pid_t child = fork();
            // a refused fork closes the pipe and reports no bus (§10.2)
            if (child < 0) {
                close(descriptors[0]);
                close(descriptors[1]);
                return false;
            }
            // the child becomes the daemon, which prints its address on the inherited pipe (§10.2)
            if (child == 0) {
                dup2(descriptors[1], STDOUT_FILENO);
                close(descriptors[0]);
                close(descriptors[1]);
                execlp("dbus-daemon", "dbus-daemon", "--config-file", m_config_path.c_str(), "--nofork", "--print-address=1", nullptr);
                _exit(127);
            }
            close(descriptors[1]);
            // the address is one line, which the parent reads before the daemon accepts a client (§10.2)
            std::string line;
            char byte = '\0';
            while (read(descriptors[0], &byte, 1) == 1 && byte != '\n') {
                // a carriage return is not part of the address (§10.2)
                if (byte != '\r')
                    line.push_back(byte);
            }
            close(descriptors[0]);
            m_daemon = child;
            m_address = line;
            return !m_address.empty();
        }

        // the address the daemon printed, which every client in the suite connects to (§10.2)
        std::string m_address;

        // the configuration the daemon read, which the fixture removes when it stops (§10.2)
        std::string m_config_path;

        // the daemon process, which the fixture owns (§10.2)
        pid_t m_daemon = -1;

        // whether the daemon answered with an address (§10.2)
        bool m_ready = false;
    };

    // the at-spi client the suite drives the bridge through, which is what assistive technology is (§10.2)
    class AtspiClient
    {
    public:
        ~AtspiClient() {
            // the connection is closed before it is released, which a private connection needs (§10.2)
            if (m_connection != nullptr) {
                dbus_connection_close(m_connection);
                dbus_connection_unref(m_connection);
            }
        }

        // open a private connection to one bus address, which is where the bridge announced itself (§10.2)
        bool open(const std::string& address) {
            DBusError error;
            dbus_error_init(&error);
            m_connection = dbus_connection_open_private(address.c_str(), &error);
            // a bus that refused the connection leaves the client unusable (§10.2)
            if (m_connection == nullptr) {
                dbus_error_free(&error);
                return false;
            }
            // a bus connection registers before it may address another client (§10.2)
            if (!dbus_bus_register(m_connection, &error)) {
                dbus_error_free(&error);
                return false;
            }
            dbus_error_free(&error);
            // a dropped bus must not terminate the suite, which the fixture stops on purpose (§10.2)
            dbus_connection_set_exit_on_disconnect(m_connection, FALSE);
            return true;
        }

        // ask the bus to deliver the structure signals, which the bridge emits on publish (§10.2)
        void listen_for_events() const {
            DBusError error;
            dbus_error_init(&error);
            const std::string rule = std::string("type='signal',interface='") + EVENT_OBJECT + "'";
            dbus_bus_add_match(m_connection, rule.c_str(), &error);
            dbus_connection_flush(m_connection);
            dbus_error_free(&error);
        }

        // pop the next signal within a bounded wait, which the suite reads the announcement from (§10.2)
        DBusMessage* next_signal(int timeout_ms) const {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            while (std::chrono::steady_clock::now() < deadline) {
                // one read pass fills the queue, which the pop then drains (§10.2)
                if (!dbus_connection_read_write(m_connection, 20))
                    return nullptr;
                DBusMessage* message = dbus_connection_pop_message(m_connection);
                // a queued reply or a bus signal is drained until an event signal arrives (§10.2)
                while (message != nullptr) {
                    if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL && dbus_message_has_interface(message, EVENT_OBJECT))
                        return message;
                    dbus_message_unref(message);
                    message = dbus_connection_pop_message(m_connection);
                }
            }
            return nullptr;
        }

        // read the role number a node announces (§10.2)
        std::optional<std::uint32_t> role(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, ACCESSIBLE, "GetRole");
            const std::optional<std::uint32_t> value = read_uint32(reply);
            release(reply);
            return value;
        }

        // read the role name a node announces beside its number (§10.2)
        std::optional<std::string> role_name(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, ACCESSIBLE, "GetRoleName");
            const std::optional<std::string> value = read_string(reply);
            release(reply);
            return value;
        }

        // read the state words a node announces (§10.2)
        std::optional<std::vector<std::uint32_t>> state(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, ACCESSIBLE, "GetState");
            const std::optional<std::vector<std::uint32_t>> value = read_uint32_array(reply);
            release(reply);
            return value;
        }

        // read the interfaces a node answers, which the bridge advertises rather than guesses (§10.2)
        std::optional<std::vector<std::string>> interfaces(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, ACCESSIBLE, "GetInterfaces");
            const std::optional<std::vector<std::string>> value = read_string_array(reply);
            release(reply);
            return value;
        }

        // read the document an object describes itself with, which a bus tool introspects (§10.2)
        std::optional<std::string> introspect(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, INTROSPECT, "Introspect");
            const std::optional<std::string> value = read_string(reply);
            release(reply);
            return value;
        }

        // read the children a node announces, which the suite walks the hierarchy through (§10.2)
        std::optional<std::vector<std::pair<std::string, std::string>>> children(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, ACCESSIBLE, "GetChildren");
            const std::optional<std::vector<std::pair<std::string, std::string>>> value = read_references(reply);
            release(reply);
            return value;
        }

        // read one child by index, which the protocol words as a reference or the null path (§10.2)
        std::optional<std::pair<std::string, std::string>> child_at(const std::string& destination, const std::string& path, std::int32_t index) const {
            DBusMessage* reply = call_int(destination, path, ACCESSIBLE, "GetChildAtIndex", index);
            const std::optional<std::pair<std::string, std::string>> value = read_reference(reply);
            release(reply);
            return value;
        }

        // read where a node sits among its siblings, which the hierarchy query answers (§10.2)
        std::optional<std::int32_t> index_in_parent(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call(destination, path, ACCESSIBLE, "GetIndexInParent");
            const std::optional<std::int32_t> value = read_int32(reply);
            release(reply);
            return value;
        }

        // read the accessible name a node announces, which a reader speaks (§10.1, §10.2)
        std::optional<std::string> name(const std::string& destination, const std::string& path) const { return property_string(destination, path, ACCESSIBLE, "Name"); }

        // read the description a node announces, which a reader speaks beside the name (§10.1, §10.2)
        std::optional<std::string> description(const std::string& destination, const std::string& path) const { return property_string(destination, path, ACCESSIBLE, "Description"); }

        // read one typed property, which the properties interface carries (§10.2)
        std::optional<std::string> property_string(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name) const {
            DBusMessage* reply = get_property(destination, path, interface, name);
            const std::optional<std::string> value = read_property_string(reply);
            release(reply);
            return value;
        }

        // read one signed property, which an action count and a child count carry (§10.2)
        std::optional<std::int32_t> property_int(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name) const {
            DBusMessage* reply = get_property(destination, path, interface, name);
            const std::optional<std::int32_t> value = read_property_int(reply);
            release(reply);
            return value;
        }

        // read one unsigned property, which the interface version carries (§10.2)
        std::optional<std::uint32_t> property_uint(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name) const {
            DBusMessage* reply = get_property(destination, path, interface, name);
            const std::optional<std::uint32_t> value = read_property_uint(reply);
            release(reply);
            return value;
        }

        // read one floating point property, which a range carries (§10.2)
        std::optional<double> property_double(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name) const {
            DBusMessage* reply = get_property(destination, path, interface, name);
            const std::optional<double> value = read_property_double(reply);
            release(reply);
            return value;
        }

        // read one object property, which a parent reference carries (§10.2)
        std::optional<std::pair<std::string, std::string>> property_reference(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name) const {
            DBusMessage* reply = get_property(destination, path, interface, name);
            const std::optional<std::pair<std::string, std::string>> value = read_property_reference(reply);
            release(reply);
            return value;
        }

        // write one floating point property, which a range write is (§10.2)
        bool write_double(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name, double value) const {
            const char* interface_text = interface.c_str();
            const char* name_text = name.c_str();
            DBusMessage* message = dbus_message_new_method_call(destination.c_str(), path.c_str(), PROPERTIES, "Set");
            // a call that was never built reports the refused write (§10.2)
            if (message == nullptr)
                return false;
            DBusMessageIter iterator;
            dbus_message_iter_init_append(message, &iterator);
            dbus_message_iter_append_basic(&iterator, DBUS_TYPE_STRING, &interface_text);
            dbus_message_iter_append_basic(&iterator, DBUS_TYPE_STRING, &name_text);
            DBusMessageIter variant;
            dbus_message_iter_open_container(&iterator, DBUS_TYPE_VARIANT, "d", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &value);
            dbus_message_iter_close_container(&iterator, &variant);
            DBusMessage* reply = send(message);
            const bool accepted = is_return(reply);
            release(reply);
            return accepted;
        }

        // run one action by index, which the protocol reports as performed or refused (§10.2)
        std::optional<bool> do_action(const std::string& destination, const std::string& path, std::int32_t index) const {
            DBusMessage* reply = call_int(destination, path, ACTION, "DoAction", index);
            const std::optional<bool> value = read_bool(reply);
            release(reply);
            return value;
        }

        // write the whole text of a field, which the editable text interface carries (§10.2)
        std::optional<bool> set_text(const std::string& destination, const std::string& path, const std::string& text) const {
            DBusMessage* reply = call_string(destination, path, EDITABLE_TEXT, "SetTextContents", text);
            const std::optional<bool> value = read_bool(reply);
            release(reply);
            return value;
        }

        // read a range of text in characters, which the text interface counts (§10.2)
        std::optional<std::string> text(const std::string& destination, const std::string& path, std::int32_t start, std::int32_t end) const {
            DBusMessage* reply = call_two_int(destination, path, TEXT, "GetText", start, end);
            const std::optional<std::string> value = read_string(reply);
            release(reply);
            return value;
        }

        // read one selection range of a field, which the text interface answers in characters (§10.2)
        std::optional<std::pair<std::int32_t, std::int32_t>> selection(const std::string& destination, const std::string& path, std::int32_t index) const {
            DBusMessage* reply = call_int(destination, path, TEXT, "GetSelection", index);
            const std::optional<std::pair<std::int32_t, std::int32_t>> value = read_offsets(reply);
            release(reply);
            return value;
        }

        // read the selected choice of a radio group, which the selection interface names (§10.2)
        std::optional<std::pair<std::string, std::string>> selected_child(const std::string& destination, const std::string& path, std::int32_t index) const {
            DBusMessage* reply = call_int(destination, path, SELECTION, "GetSelectedChild", index);
            const std::optional<std::pair<std::string, std::string>> value = read_reference(reply);
            release(reply);
            return value;
        }

        // read whether a child is the chosen choice, which the selection interface answers (§10.2)
        std::optional<bool> is_child_selected(const std::string& destination, const std::string& path, std::int32_t index) const {
            DBusMessage* reply = call_int(destination, path, SELECTION, "IsChildSelected", index);
            const std::optional<bool> value = read_bool(reply);
            release(reply);
            return value;
        }

        // select one child, which a radio group takes as its new choice (§10.2)
        std::optional<bool> select_child(const std::string& destination, const std::string& path, std::int32_t index) const {
            DBusMessage* reply = call_int(destination, path, SELECTION, "SelectChild", index);
            const std::optional<bool> value = read_bool(reply);
            release(reply);
            return value;
        }

        // read whether a point falls inside the published bounds, which the component interface answers (§10.2)
        std::optional<bool> contains(const std::string& destination, const std::string& path, std::int32_t x, std::int32_t y) const {
            DBusMessage* reply = call_point(destination, path, COMPONENT, "Contains", x, y, 0);
            const std::optional<bool> value = read_bool(reply);
            release(reply);
            return value;
        }

        // read the rectangle a node occupies, which the component interface answers (§10.2)
        std::optional<std::array<std::int32_t, 4>> extents(const std::string& destination, const std::string& path) const {
            DBusMessage* reply = call_uint(destination, path, COMPONENT, "GetExtents", 0);
            const std::optional<std::array<std::int32_t, 4>> value = read_rectangle(reply);
            release(reply);
            return value;
        }

        // report the error name the last call answered with, which tells a refusal from a dead bridge (§10.2)
        [[nodiscard]] const std::string& last_error() const { return m_last_error; }

        // report whether a reply carried a value rather than a d-bus error (§10.2)
        static bool is_return(DBusMessage* reply) { return reply != nullptr && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN; }

        // report whether a state word set carries one numbered bit §10.2)
        static bool has_state(const std::vector<std::uint32_t>& words, std::uint32_t bit) {
            const std::size_t word = bit / 32;
            // a bit past the words the reply carried is unset, which the quiet default states (§10.2)
            if (word >= words.size())
                return false;
            return (words[word] & (1U << (bit % 32))) != 0;
        }

        // release one reply, which every read does after it decoded the body (§10.2)
        static void release(DBusMessage* message) {
            if (message != nullptr)
                dbus_message_unref(message);
        }

    private:
        // build a call to one object, which a peer that left has no destination for (§10.2)
        DBusMessage* new_call(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member) const {
            // a call with no destination names no peer, which libdbus refuses to build (§10.2)
            if (destination.empty() || path.empty() || interface.empty() || member.empty())
                return nullptr;
            return dbus_message_new_method_call(destination.c_str(), path.c_str(), interface.c_str(), member.c_str());
        }

        // issue one method call and release the built message, which every read shares (§10.2)
        DBusMessage* send(DBusMessage* message) const {
            // a message that was never built has no reply to wait for (§10.2)
            if (message == nullptr || m_connection == nullptr)
                return nullptr;
            DBusError error;
            dbus_error_init(&error);
            DBusMessage* reply = dbus_connection_send_with_reply_and_block(m_connection, message, 3000, &error);
            // the error name is kept, because a test reads which refusal the bridge answered with (§10.2)
            m_last_error = error.name == nullptr ? "" : error.name;
            dbus_message_unref(message);
            dbus_error_free(&error);
            return reply;
        }

        // build a call with no argument (§10.2)
        DBusMessage* call(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member) const { return send(new_call(destination, path, interface, member)); }

        // build a call carrying one signed number (§10.2)
        DBusMessage* call_int(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member, std::int32_t value) const {
            DBusMessage* message = new_call(destination, path, interface, member);
            // a call that was never built carries nothing to send (§10.2)
            if (message == nullptr)
                return nullptr;
            dbus_int32_t argument = value;
            dbus_message_append_args(message, DBUS_TYPE_INT32, &argument, DBUS_TYPE_INVALID);
            return send(message);
        }

        // build a call carrying two signed numbers, which a text range carries (§10.2)
        DBusMessage* call_two_int(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member, std::int32_t first, std::int32_t second) const {
            DBusMessage* message = new_call(destination, path, interface, member);
            // a call that was never built carries nothing to send (§10.2)
            if (message == nullptr)
                return nullptr;
            dbus_int32_t first_argument = first;
            dbus_int32_t second_argument = second;
            dbus_message_append_args(message, DBUS_TYPE_INT32, &first_argument, DBUS_TYPE_INT32, &second_argument, DBUS_TYPE_INVALID);
            return send(message);
        }

        // build a call carrying one unsigned number, which an extent type carries (§10.2)
        DBusMessage* call_uint(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member, std::uint32_t value) const {
            DBusMessage* message = new_call(destination, path, interface, member);
            // a call that was never built carries nothing to send (§10.2)
            if (message == nullptr)
                return nullptr;
            dbus_uint32_t argument = value;
            dbus_message_append_args(message, DBUS_TYPE_UINT32, &argument, DBUS_TYPE_INVALID);
            return send(message);
        }

        // build a call carrying one string, which a text write carries (§10.2)
        DBusMessage* call_string(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member, const std::string& value) const {
            DBusMessage* message = new_call(destination, path, interface, member);
            // a call that was never built carries nothing to send (§10.2)
            if (message == nullptr)
                return nullptr;
            const char* argument = value.c_str();
            dbus_message_append_args(message, DBUS_TYPE_STRING, &argument, DBUS_TYPE_INVALID);
            return send(message);
        }

        // build a call carrying a point and its coordinate type, which the hit test carries (§10.2)
        DBusMessage* call_point(const std::string& destination, const std::string& path, const std::string& interface, const std::string& member, std::int32_t x, std::int32_t y, std::uint32_t coordinate) const {
            DBusMessage* message = new_call(destination, path, interface, member);
            // a call that was never built carries nothing to send (§10.2)
            if (message == nullptr)
                return nullptr;
            dbus_int32_t x_argument = x;
            dbus_int32_t y_argument = y;
            dbus_uint32_t coordinate_argument = coordinate;
            dbus_message_append_args(message, DBUS_TYPE_INT32, &x_argument, DBUS_TYPE_INT32, &y_argument, DBUS_TYPE_UINT32, &coordinate_argument, DBUS_TYPE_INVALID);
            return send(message);
        }

        // build a property read, which the properties interface carries (§10.2)
        DBusMessage* get_property(const std::string& destination, const std::string& path, const std::string& interface, const std::string& name) const {
            DBusMessage* message = dbus_message_new_method_call(destination.c_str(), path.c_str(), PROPERTIES, "Get");
            // a call that was never built carries nothing to send (§10.2)
            if (message == nullptr)
                return nullptr;
            const char* interface_text = interface.c_str();
            const char* name_text = name.c_str();
            dbus_message_append_args(message, DBUS_TYPE_STRING, &interface_text, DBUS_TYPE_STRING, &name_text, DBUS_TYPE_INVALID);
            return send(message);
        }

        // read one string from a reply body (§10.2)
        static std::optional<std::string> read_string(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusError error;
            dbus_error_init(&error);
            const char* text = nullptr;
            const dbus_bool_t read = dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            // a body that does not carry a string names nothing (§10.2)
            if (read == FALSE || text == nullptr)
                return std::nullopt;
            return std::string(text);
        }

        // read one boolean from a reply body (§10.2)
        static std::optional<bool> read_bool(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusError error;
            dbus_error_init(&error);
            dbus_bool_t flag = FALSE;
            const dbus_bool_t read = dbus_message_get_args(reply, &error, DBUS_TYPE_BOOLEAN, &flag, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            // a body that does not carry a boolean answers nothing (§10.2)
            if (read == FALSE)
                return std::nullopt;
            return flag != FALSE;
        }

        // read one signed number from a reply body (§10.2)
        static std::optional<std::int32_t> read_int32(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusError error;
            dbus_error_init(&error);
            dbus_int32_t value = 0;
            const dbus_bool_t read = dbus_message_get_args(reply, &error, DBUS_TYPE_INT32, &value, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            // a body that does not carry a number answers nothing (§10.2)
            if (read == FALSE)
                return std::nullopt;
            return value;
        }

        // read one unsigned number from a reply body (§10.2)
        static std::optional<std::uint32_t> read_uint32(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusError error;
            dbus_error_init(&error);
            dbus_uint32_t value = 0;
            const dbus_bool_t read = dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &value, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            // a body that does not carry a number answers nothing (§10.2)
            if (read == FALSE)
                return std::nullopt;
            return value;
        }

        // read one object reference from a reply body (§10.2)
        static std::optional<std::pair<std::string, std::string>> read_reference(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusMessageIter iterator;
            // an empty body names no reference (§10.2)
            if (!dbus_message_iter_init(reply, &iterator))
                return std::nullopt;
            // a body that is not a structure names no reference (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_STRUCT)
                return std::nullopt;
            DBusMessageIter structure;
            dbus_message_iter_recurse(&iterator, &structure);
            // the first member is the bus name, which a reference always carries (§10.2)
            if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_STRING)
                return std::nullopt;
            const char* bus = nullptr;
            dbus_message_iter_get_basic(&structure, &bus);
            dbus_message_iter_next(&structure);
            // the second member is the path, which the protocol carries as an object path (§10.2)
            if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_OBJECT_PATH)
                return std::nullopt;
            const char* path = nullptr;
            dbus_message_iter_get_basic(&structure, &path);
            return std::make_pair(std::string(bus), std::string(path));
        }

        // read a list of object references from a reply body (§10.2)
        static std::optional<std::vector<std::pair<std::string, std::string>>> read_references(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusMessageIter iterator;
            // an empty body names no child set (§10.2)
            if (!dbus_message_iter_init(reply, &iterator))
                return std::nullopt;
            // a body that is not an array names no child set (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_ARRAY)
                return std::nullopt;
            std::vector<std::pair<std::string, std::string>> references;
            DBusMessageIter array;
            dbus_message_iter_recurse(&iterator, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
                DBusMessageIter structure;
                dbus_message_iter_recurse(&array, &structure);
                // the bus name and the path are the two members a reference carries (§10.2)
                if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_STRING) {
                    const char* bus = nullptr;
                    dbus_message_iter_get_basic(&structure, &bus);
                    dbus_message_iter_next(&structure);
                    // the path is the object the reference names, which the protocol words as an object path (§10.2)
                    if (dbus_message_iter_get_arg_type(&structure) == DBUS_TYPE_OBJECT_PATH) {
                        const char* path = nullptr;
                        dbus_message_iter_get_basic(&structure, &path);
                        references.emplace_back(bus, path);
                    }
                }
                dbus_message_iter_next(&array);
            }
            return references;
        }

        // read the state words, which the protocol carries as an array of numbers (§10.2)
        static std::optional<std::vector<std::uint32_t>> read_uint32_array(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusMessageIter iterator;
            // an empty body carries no state word (§10.2)
            if (!dbus_message_iter_init(reply, &iterator))
                return std::nullopt;
            // a body that is not an array carries no state word (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_ARRAY)
                return std::nullopt;
            std::vector<std::uint32_t> words;
            DBusMessageIter array;
            dbus_message_iter_recurse(&iterator, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_UINT32) {
                dbus_uint32_t word = 0;
                dbus_message_iter_get_basic(&array, &word);
                words.push_back(word);
                dbus_message_iter_next(&array);
            }
            return words;
        }

        // read the interface names, which the interface query answers (§10.2)
        static std::optional<std::vector<std::string>> read_string_array(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusMessageIter iterator;
            // an empty body names no interface (§10.2)
            if (!dbus_message_iter_init(reply, &iterator))
                return std::nullopt;
            // a body that is not an array names no interface (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_ARRAY)
                return std::nullopt;
            std::vector<std::string> names;
            DBusMessageIter array;
            dbus_message_iter_recurse(&iterator, &array);
            while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
                const char* name = nullptr;
                dbus_message_iter_get_basic(&array, &name);
                names.emplace_back(name);
                dbus_message_iter_next(&array);
            }
            return names;
        }

        // read two signed numbers, which a text selection carries (§10.2)
        static std::optional<std::pair<std::int32_t, std::int32_t>> read_offsets(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusError error;
            dbus_error_init(&error);
            dbus_int32_t start = 0;
            dbus_int32_t end = 0;
            const dbus_bool_t read = dbus_message_get_args(reply, &error, DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
            dbus_error_free(&error);
            // a body that does not carry two numbers answers nothing (§10.2)
            if (read == FALSE)
                return std::nullopt;
            return std::make_pair(start, end);
        }

        // read one rectangle, which an extent carries (§10.2)
        static std::optional<std::array<std::int32_t, 4>> read_rectangle(DBusMessage* reply) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return std::nullopt;
            DBusMessageIter iterator;
            // an empty body carries no rectangle (§10.2)
            if (!dbus_message_iter_init(reply, &iterator))
                return std::nullopt;
            // a body that is not a structure carries no rectangle (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_STRUCT)
                return std::nullopt;
            DBusMessageIter structure;
            dbus_message_iter_recurse(&iterator, &structure);
            std::array<std::int32_t, 4> values{};
            for (std::size_t index = 0; index < values.size(); index++) {
                // a member that is not a number leaves the rectangle incomplete (§10.2)
                if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_INT32)
                    return std::nullopt;
                dbus_int32_t value = 0;
                dbus_message_iter_get_basic(&structure, &value);
                values[index] = value;
                dbus_message_iter_next(&structure);
            }
            return values;
        }

        // read a variant body, which every property read carries (§10.2)
        static bool enter_variant(DBusMessage* reply, DBusMessageIter& variant) {
            // a reply that carried an error has no body to read (§10.2)
            if (!is_return(reply))
                return false;
            DBusMessageIter iterator;
            // an empty body carries no property (§10.2)
            if (!dbus_message_iter_init(reply, &iterator))
                return false;
            // a body that is not a variant carries no property (§10.2)
            if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_VARIANT)
                return false;
            dbus_message_iter_recurse(&iterator, &variant);
            return true;
        }

        // read a string property, which the variant body carries (§10.2)
        static std::optional<std::string> read_property_string(DBusMessage* reply) {
            DBusMessageIter variant;
            // a body that is not a variant carries no property (§10.2)
            if (!enter_variant(reply, variant))
                return std::nullopt;
            // a variant of another type carries no string (§10.2)
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRING)
                return std::nullopt;
            const char* text = nullptr;
            dbus_message_iter_get_basic(&variant, &text);
            return std::string(text);
        }

        // read a signed property, which the variant body carries (§10.2)
        static std::optional<std::int32_t> read_property_int(DBusMessage* reply) {
            DBusMessageIter variant;
            // a body that is not a variant carries no property (§10.2)
            if (!enter_variant(reply, variant))
                return std::nullopt;
            // a variant of another type carries no signed number (§10.2)
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_INT32)
                return std::nullopt;
            dbus_int32_t value = 0;
            dbus_message_iter_get_basic(&variant, &value);
            return value;
        }

        // read an unsigned property, which the variant body carries (§10.2)
        static std::optional<std::uint32_t> read_property_uint(DBusMessage* reply) {
            DBusMessageIter variant;
            // a body that is not a variant carries no property (§10.2)
            if (!enter_variant(reply, variant))
                return std::nullopt;
            // a variant of another type carries no unsigned number (§10.2)
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_UINT32)
                return std::nullopt;
            dbus_uint32_t value = 0;
            dbus_message_iter_get_basic(&variant, &value);
            return value;
        }

        // read a floating point property, which the variant body carries (§10.2)
        static std::optional<double> read_property_double(DBusMessage* reply) {
            DBusMessageIter variant;
            // a body that is not a variant carries no property (§10.2)
            if (!enter_variant(reply, variant))
                return std::nullopt;
            // a variant of another type carries no floating point number (§10.2)
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_DOUBLE)
                return std::nullopt;
            double value = 0.0;
            dbus_message_iter_get_basic(&variant, &value);
            return value;
        }

        // read an object property, which the variant body carries (§10.2)
        static std::optional<std::pair<std::string, std::string>> read_property_reference(DBusMessage* reply) {
            DBusMessageIter variant;
            // a body that is not a variant carries no property (§10.2)
            if (!enter_variant(reply, variant))
                return std::nullopt;
            // a variant of another type carries no reference (§10.2)
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRUCT)
                return std::nullopt;
            DBusMessageIter structure;
            dbus_message_iter_recurse(&variant, &structure);
            // the bus name and the path are the two members a reference carries (§10.2)
            if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_STRING)
                return std::nullopt;
            const char* bus = nullptr;
            dbus_message_iter_get_basic(&structure, &bus);
            dbus_message_iter_next(&structure);
            // the path is the object the reference names, which the protocol words as an object path (§10.2)
            if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_OBJECT_PATH)
                return std::nullopt;
            const char* path = nullptr;
            dbus_message_iter_get_basic(&structure, &path);
            return std::make_pair(std::string(bus), std::string(path));
        }

        // the error name the last call answered with, which a refusal test reads (§10.2)
        mutable std::string m_last_error;

        // the connection the suite addresses the bridge through (§10.2)
        DBusConnection* m_connection = nullptr;
    };

    // the registry the bridge embeds itself into, which the suite fakes to prove the handshake (§10.2)
    class FakeRegistry
    {
    public:
        ~FakeRegistry() {
            // the thread is asked to stop before the connection it reads is released (§10.2)
            m_running.store(false);
            if (m_thread.joinable())
                m_thread.join();
            // the connection is closed before it is released, which a private connection needs (§10.2)
            if (m_connection != nullptr) {
                dbus_connection_close(m_connection);
                dbus_connection_unref(m_connection);
            }
        }

        // claim the registry name and answer the socket calls on it (§10.2)
        bool open(const std::string& address) {
            DBusError error;
            dbus_error_init(&error);
            m_connection = dbus_connection_open_private(address.c_str(), &error);
            // a bus that refused the connection leaves no registry to answer (§10.2)
            if (m_connection == nullptr) {
                dbus_error_free(&error);
                return false;
            }
            // a bus connection registers before it may own a name (§10.2)
            if (!dbus_bus_register(m_connection, &error)) {
                dbus_error_free(&error);
                return false;
            }
            // the registry name is what the bridge addresses, so the fake has to own it (§10.2)
            const int result = dbus_bus_request_name(m_connection, "org.a11y.atspi.Registry", DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
            dbus_error_free(&error);
            // a name another process owns leaves this fake unable to answer for it (§10.2)
            if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
                return false;
            dbus_connection_set_exit_on_disconnect(m_connection, FALSE);
            // the filter answers the socket calls, which is the whole registry contract the bridge needs (§10.2)
            dbus_connection_add_filter(m_connection, &FakeRegistry::filter, this, nullptr);
            m_running.store(true);
            m_thread = std::thread([this] {
                // the loop answers calls until the fixture stops it (§10.2)
                while (m_running.load())
                    if (!dbus_connection_read_write_dispatch(m_connection, 20))
                        break;
            });
            return true;
        }

        // wait for the embed call, which the bridge makes while it attaches (§10.2)
        bool wait_for_embed() {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_arrived.wait_for(lock, std::chrono::seconds(3), [this] { return !m_embedded_bus.empty(); });
        }

        // wait for the unembed call, which the bridge makes while it detaches (§10.2)
        bool wait_for_unembed() {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_departed.wait_for(lock, std::chrono::seconds(3), [this] { return m_unembedded; });
        }

        // read the reference the bridge embedded, which names its root object (§10.2)
        std::pair<std::string, std::string> embedded() const {
            const std::lock_guard<std::mutex> guard(m_mutex);
            return {m_embedded_bus, m_embedded_path};
        }

    private:
        // route one incoming message to the handler, which the filter installs (§10.2)
        static DBusHandlerResult filter(DBusConnection*, DBusMessage* message, void* data) { return static_cast<FakeRegistry*>(data)->handle(message); }

        // answer one registry call, which is the embed and the unembed the bridge sends (§10.2)
        DBusHandlerResult handle(DBusMessage* message) {
            // an embed carries the application root, which the fake records and answers (§10.2)
            if (dbus_message_is_method_call(message, SOCKET, "Embed")) {
                DBusMessageIter iterator;
                // a call with no body names no application (§10.2)
                if (!dbus_message_iter_init(message, &iterator))
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                DBusMessageIter structure;
                dbus_message_iter_recurse(&iterator, &structure);
                const char* bus = nullptr;
                dbus_message_iter_get_basic(&structure, &bus);
                dbus_message_iter_next(&structure);
                const char* path = nullptr;
                dbus_message_iter_get_basic(&structure, &path);
                {
                    const std::lock_guard<std::mutex> guard(m_mutex);
                    m_embedded_bus = bus;
                    m_embedded_path = path;
                }
                m_arrived.notify_all();
                DBusMessage* reply = dbus_message_new_method_return(message);
                DBusMessageIter body;
                dbus_message_iter_init_append(reply, &body);
                DBusMessageIter echoed;
                dbus_message_iter_open_container(&body, DBUS_TYPE_STRUCT, nullptr, &echoed);
                dbus_message_iter_append_basic(&echoed, DBUS_TYPE_STRING, &bus);
                // the path is the object the application answers on, which the protocol words as an object path (§10.2)
                dbus_message_iter_append_basic(&echoed, DBUS_TYPE_OBJECT_PATH, &path);
                dbus_message_iter_close_container(&body, &echoed);
                dbus_connection_send(m_connection, reply, nullptr);
                dbus_connection_flush(m_connection);
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            // an unembed names the application that is leaving, which the fake records (§10.2)
            if (dbus_message_is_method_call(message, SOCKET, "Unembed")) {
                {
                    const std::lock_guard<std::mutex> guard(m_mutex);
                    m_unembedded = true;
                }
                m_departed.notify_all();
                DBusMessage* reply = dbus_message_new_method_return(message);
                dbus_connection_send(m_connection, reply, nullptr);
                dbus_connection_flush(m_connection);
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        // the connection the fake answers on, which the fixture owns (§10.2)
        DBusConnection* m_connection = nullptr;

        // the thread that reads and dispatches the socket calls (§10.2)
        std::thread m_thread;

        // the flag that asks the thread to stop, which the destructor clears (§10.2)
        std::atomic<bool> m_running{false};

        // the lock that keeps the recorded reference whole across threads (§10.2)
        mutable std::mutex m_mutex;

        // the signal that wakes the suite when the embed arrives (§10.2)
        std::condition_variable m_arrived;

        // the signal that wakes the suite when the unembed arrives (§10.2)
        std::condition_variable m_departed;

        // the bus name the bridge embedded, which names its connection (§10.2)
        std::string m_embedded_bus;

        // the root path the bridge embedded, which is the application object (§10.2)
        std::string m_embedded_path;

        // whether the bridge has said it is leaving (§10.2)
        bool m_unembedded = false;
    };

    // the launcher a desktop session exports, which names the accessibility bus (§10.2)
    class FakeLauncher
    {
    public:
        ~FakeLauncher() {
            // the thread is asked to stop before the connection it reads is released (§10.2)
            m_running.store(false);
            if (m_thread.joinable())
                m_thread.join();
            // the connection is closed before it is released, which a private connection needs (§10.2)
            if (m_connection != nullptr) {
                dbus_connection_close(m_connection);
                dbus_connection_unref(m_connection);
            }
        }

        // claim the launcher name and answer the address it was given (§10.2)
        bool open(const std::string& address, std::string target) {
            m_target = std::move(target);
            DBusError error;
            dbus_error_init(&error);
            m_connection = dbus_connection_open_private(address.c_str(), &error);
            // a bus that refused the connection leaves no launcher to answer (§10.2)
            if (m_connection == nullptr) {
                dbus_error_free(&error);
                return false;
            }
            // a bus connection registers before it may own a name (§10.2)
            if (!dbus_bus_register(m_connection, &error)) {
                dbus_error_free(&error);
                return false;
            }
            // the launcher name is what the bridge addresses, so the fake has to own it (§10.2)
            const int result = dbus_bus_request_name(m_connection, LAUNCHER, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
            dbus_error_free(&error);
            // a name another process owns leaves this fake unable to answer for it (§10.2)
            if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
                return false;
            dbus_connection_set_exit_on_disconnect(m_connection, FALSE);
            // the filter answers the address query, which is the whole launcher contract the bridge needs (§10.2)
            dbus_connection_add_filter(m_connection, &FakeLauncher::filter, this, nullptr);
            m_running.store(true);
            m_thread = std::thread([this] {
                // the loop answers calls until the fixture stops it (§10.2)
                while (m_running.load())
                    if (!dbus_connection_read_write_dispatch(m_connection, 20))
                        break;
            });
            return true;
        }

    private:
        // route one incoming message to the handler, which the filter installs (§10.2)
        static DBusHandlerResult filter(DBusConnection*, DBusMessage* message, void* data) { return static_cast<FakeLauncher*>(data)->handle(message); }

        // answer the address query, which is the one call the bridge makes here (§10.2)
        DBusHandlerResult handle(DBusMessage* message) const {
            if (!dbus_message_is_method_call(message, LAUNCHER, "GetAddress"))
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            DBusMessage* reply = dbus_message_new_method_return(message);
            const char* address = m_target.c_str();
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &address, DBUS_TYPE_INVALID);
            dbus_connection_send(m_connection, reply, nullptr);
            dbus_connection_flush(m_connection);
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        // the connection the fake answers on, which the fixture owns (§10.2)
        DBusConnection* m_connection = nullptr;

        // the accessibility bus address the launcher answers with (§10.2)
        std::string m_target;

        // the thread that reads and dispatches the address queries (§10.2)
        std::thread m_thread;

        // the flag that asks the thread to stop, which the destructor clears (§10.2)
        std::atomic<bool> m_running{false};
    };

    // the fixture every at-spi check runs over, which owns one private bus and one bridge (§10.2)
    class AtspiBridge : public ::testing::Test
    {
    protected:
        void SetUp() override {
            // a host without the bus daemon cannot run the suite, which the test reports rather than fails (§10.2)
            if (!m_bus.ready())
                GTEST_SKIP() << "dbus-daemon is not available";
            // the address is captured before the suite replaces it, so the host session is restored afterwards (§10.2)
            const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
            m_had_environment = previous != nullptr;
            if (previous != nullptr)
                m_environment = previous;
            // the private bus stands in for the session, so the suite never talks to the host's session bus (§10.2)
            setenv("DBUS_SESSION_BUS_ADDRESS", m_bus.address().c_str(), 1);
            // the bridge is built without a connection, which attach opens (§10.2)
            m_adapter = accessibility::create_adapter();
            ASSERT_NE(m_adapter, nullptr);
            ASSERT_TRUE(m_client.open(m_bus.address()));
        }

        void TearDown() override {
            // the bridge leaves before the bus it announced on stops (§10.2)
            if (m_adapter != nullptr)
                m_adapter->detach_window();
            m_adapter.reset();
            // the host environment is restored, so a suite that follows sees the session it expects (§10.2)
            if (m_had_environment)
                setenv("DBUS_SESSION_BUS_ADDRESS", m_environment.c_str(), 1);
            else
                unsetenv("DBUS_SESSION_BUS_ADDRESS");
        }

        // attach the bridge, which opens the bus and announces the application (§10.2)
        bool attach() {
            m_adapter->attach_window(NativeWindowHandle{});
            // an announced name is what a client addresses, so an empty one means the bus refused (§10.2)
            return !accessibility::platform::atspi_bus_name_for(*m_adapter).empty();
        }

        // hand out the name the bridge announced, which every call addresses (§10.2)
        std::string bus_name() const { return accessibility::platform::atspi_bus_name_for(*m_adapter); }

        // hand out the application root path, which the embed contract fixes (§10.2)
        static std::string root_path() { return std::string(accessibility::platform::ATSPI_ROOT_PATH); }

        // hand out the path a published node answers on, which the index names (§10.2)
        static std::string node_path(std::size_t index) { return accessibility::platform::atspi_object_path(index); }

        // the private bus the suite owns, which it stops when the fixture ends (§10.2)
        PrivateBus m_bus;

        // the client the suite drives the bridge through, which connects to the same bus (§10.2)
        AtspiClient m_client;

        // the bridge under test, which is what the suite publishes into (§10.2)
        std::unique_ptr<accessibility::AccessibilityAdapter> m_adapter;

        // whether the host exported a session address, which the fixture restores (§10.2)
        bool m_had_environment = false;

        // the session address the host exported, which the fixture restores (§10.2)
        std::string m_environment;
    };
} // namespace

TEST_F(AtspiBridge, TheAnnouncedRootCarriesTheApplicationRole) {
    // arrange an attached bridge with nothing published (§10.2)
    ASSERT_TRUE(attach());
    // assert the root announces the application role, which is what a client starts from (§10.2)
    const std::optional<std::uint32_t> role = m_client.role(bus_name(), root_path());
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(*role, 75U);
    // assert the root names the toolkit, which a reader speaks (§10.2)
    const std::optional<std::string> name = m_client.name(bus_name(), root_path());
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Albor");
    // assert the root carries the application interface, which only it answers (§10.2)
    const std::optional<std::string> toolkit = m_client.property_string(bus_name(), root_path(), APPLICATION, "ToolkitName");
    ASSERT_TRUE(toolkit.has_value());
    EXPECT_EQ(*toolkit, "Albor");
}

TEST_F(AtspiBridge, TheAnnouncedTreeCarriesEveryChild) {
    // arrange a tree of a viewport over one label, which is the smallest hierarchy a reader walks (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t viewport = tree.add(SemanticNode{SemanticRole::ScrollView});
    tree.mutable_at(viewport)->set_name("Log");
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label}, viewport);
    tree.mutable_at(label)->set_name("first row");
    // act
    m_adapter->publish(tree);
    // assert the root announces the published root, which is what makes the tree reachable (§10.2)
    const std::optional<std::vector<std::pair<std::string, std::string>>> roots = m_client.children(bus_name(), root_path());
    ASSERT_TRUE(roots.has_value());
    ASSERT_EQ(roots->size(), 1U);
    EXPECT_EQ((*roots)[0].second, node_path(viewport));
    // assert the viewport announces its child, which the hierarchy walk reaches (§10.2)
    const std::optional<std::vector<std::pair<std::string, std::string>>> children = m_client.children(bus_name(), node_path(viewport));
    ASSERT_TRUE(children.has_value());
    ASSERT_EQ(children->size(), 1U);
    EXPECT_EQ((*children)[0].second, node_path(label));
    // assert the child carries its role and name, which a reader speaks (§10.1, §10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(viewport)), 49U);
    EXPECT_EQ(m_client.name(bus_name(), node_path(label)), "first row");
    // assert the query by index answers the same child, which a reader selected (§10.2)
    const std::optional<std::pair<std::string, std::string>> child = m_client.child_at(bus_name(), node_path(viewport), 0);
    ASSERT_TRUE(child.has_value());
    EXPECT_EQ(child->second, node_path(label));
    // assert the index and the parent link agree, which makes the walk reversible (§10.2)
    EXPECT_EQ(m_client.index_in_parent(bus_name(), node_path(label)), 0);
    const std::optional<std::pair<std::string, std::string>> parent = m_client.property_reference(bus_name(), node_path(label), ACCESSIBLE, "Parent");
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->second, node_path(viewport));
    // assert the child names the application root, which is where the walk started (§10.2)
    const std::optional<std::pair<std::string, std::string>> root = m_client.property_reference(bus_name(), node_path(viewport), ACCESSIBLE, "Parent");
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root->second, root_path());
}

TEST_F(AtspiBridge, TheRootCarriesNothingBeforeAnythingIsPublished) {
    // arrange an attached bridge with nothing published, because a window may appear before its tree (§10.2)
    ASSERT_TRUE(attach());
    // assert the root answers but announces no child, because there is no tree to reach (§10.2)
    const std::optional<std::vector<std::pair<std::string, std::string>>> children = m_client.children(bus_name(), root_path());
    ASSERT_TRUE(children.has_value());
    EXPECT_TRUE(children->empty());
    // assert the child count agrees with the empty set, which a caching client reads (§10.2)
    EXPECT_EQ(m_client.property_int(bus_name(), root_path(), ACCESSIBLE, "ChildCount"), 0);
}

TEST_F(AtspiBridge, ADetachedBridgeTakesItsTreeOutOfTheBus) {
    // arrange a published tree on an attached bridge (§10.2)
    ASSERT_TRUE(attach());
    const std::string announced = bus_name();
    SemanticTree tree;
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(label)->set_name("first row");
    m_adapter->publish(tree);
    ASSERT_EQ(m_client.name(announced, node_path(label)), "first row");
    // act
    m_adapter->detach_window();
    // assert the name the bridge answered on is gone, because a reader must not see a dead tree (§5.2, §10.2)
    EXPECT_FALSE(m_client.role(announced, root_path()).has_value());
    EXPECT_EQ(m_client.last_error(), DBUS_ERROR_SERVICE_UNKNOWN);
    // assert the detached bridge reports nothing published, which the portable contract states (§10.2)
    EXPECT_EQ(m_adapter->published_node_count(), 0U);
}

TEST_F(AtspiBridge, ATextFieldAnnouncesItsValue) {
    // arrange a field carrying text, which is what a reader edits (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t field = tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(field)->set_name("Name");
    tree.mutable_at(field)->set_value("Albor");
    // act
    m_adapter->publish(tree);
    // assert the field announces the entry role and its name (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(field)), 79U);
    EXPECT_EQ(m_client.name(bus_name(), node_path(field)), "Name");
    // assert the text interface reads the value back, which is what a reader announces (§10.2)
    EXPECT_EQ(m_client.text(bus_name(), node_path(field), 0, -1), "Albor");
    // assert the character count matches the text, which the reader trusts for its offsets (§10.2)
    EXPECT_EQ(m_client.property_int(bus_name(), node_path(field), TEXT, "CharacterCount"), 5);
    // assert the field announces the read only state, which every adapter reports without a write path (§10.2)
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(field));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 43U));
    EXPECT_FALSE(AtspiClient::has_state(*state, 7U));
}

TEST_F(AtspiBridge, ATextFieldAcceptsAValueWrite) {
    // arrange a field whose write records what it was given (§10.1, §10.2)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t field = tree.add(SemanticNode{SemanticRole::TextField});
    std::string written;
    tree.mutable_at(field)->set_value("Albor");
    tree.mutable_at(field)->set_value_action([&written](std::string_view text, SemanticValueState& state) {
        // the control reports the text it now holds, which the publication follows (§10.2)
        written = text;
        state.value = text;
        state.selection_start = text.size();
        state.selection_end = text.size();
        return true;
    });
    m_adapter->publish(tree);
    // act
    const std::optional<bool> accepted = m_client.set_text(bus_name(), node_path(field), "edited");
    // the write is queued for the ui thread, which publish is where it runs (§10.2)
    m_adapter->publish(tree);
    // assert the write reached the node, which is what assistive technology asks for (§10.2)
    ASSERT_TRUE(accepted.has_value());
    EXPECT_TRUE(*accepted);
    EXPECT_EQ(written, "edited");
    // assert the write is readable through the protocol, because it landed on the publication the client reads (§10.2)
    EXPECT_EQ(m_client.text(bus_name(), node_path(field), 0, -1), "edited");
}

TEST_F(AtspiBridge, AReadOnlyTextFieldRefusesAValueWrite) {
    // arrange a read only field, which still takes focus and permits a copy (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t field = tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(field)->set_value("Albor");
    tree.mutable_at(field)->set_read_only(true);
    tree.mutable_at(field)->set_value_action([](std::string_view, SemanticValueState&) { return true; });
    m_adapter->publish(tree);
    // act
    const std::optional<bool> accepted = m_client.set_text(bus_name(), node_path(field), "edited");
    // assert the write is refused, which the node decided rather than the bridge (§10.1, §10.2)
    ASSERT_TRUE(accepted.has_value());
    EXPECT_FALSE(*accepted);
    // assert the field announces the read only state, which a reader speaks beside the value (§10.2)
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(field));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 43U));
}

TEST_F(AtspiBridge, ACheckboxAnnouncesItsCheckStateAndToggles) {
    // arrange a checked box whose toggle counts the invocations (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t box = tree.add(SemanticNode{SemanticRole::Checkbox});
    int toggles = 0;
    tree.mutable_at(box)->set_name("Armed");
    tree.mutable_at(box)->set_check(CheckState::Checked);
    (void)tree.mutable_at(box)->add_action("toggle", [&toggles] { toggles++; });
    m_adapter->publish(tree);
    // assert the box announces the check box role, which a reader recognises (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(box)), 7U);
    // assert the box announces that it is checked and checkable, which a reader speaks (§10.2)
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(box));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 4U));
    EXPECT_TRUE(AtspiClient::has_state(*state, 41U));
    // assert the action interface advertises the toggle, which is the shared portable table (§10.2)
    EXPECT_EQ(m_client.property_int(bus_name(), node_path(box), ACTION, "NActions"), 1);
    // act
    const std::optional<bool> invoked = m_client.do_action(bus_name(), node_path(box), 0);
    // the action is queued for the ui thread, which publish is where it runs (§10.2)
    m_adapter->publish(tree);
    // assert the toggle action ran, which is the whole point of announcing it (§10.2)
    ASSERT_TRUE(invoked.has_value());
    EXPECT_TRUE(*invoked);
    EXPECT_EQ(toggles, 1);
}

TEST_F(AtspiBridge, AProgressBarAnnouncesIndeterminate) {
    // arrange a bar whose remaining work is unknown, which assistive technology announces (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t bar = tree.add(SemanticNode{SemanticRole::ProgressBar});
    tree.mutable_at(bar)->set_indeterminate(true);
    // act
    m_adapter->publish(tree);
    // assert the bar announces the progress bar role and the indeterminate state (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(bar)), 42U);
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(bar));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 32U));
}

TEST_F(AtspiBridge, ASliderExposesTheRangeItsNodePublished) {
    // arrange a slider over the unit interval, which is a range a reader announces (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t slider = tree.add(SemanticNode{SemanticRole::Slider});
    tree.mutable_at(slider)->set_range(SemanticRange{.value = 0.5, .minimum = 0.0, .maximum = 1.0, .step = 0.1}, [](double) {});
    // act
    m_adapter->publish(tree);
    // assert the slider announces its role and its three range bounds (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(slider)), 51U);
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(slider), VALUE, "MinimumValue"), 0.0);
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(slider), VALUE, "MaximumValue"), 1.0);
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(slider), VALUE, "MinimumIncrement"), 0.1);
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(slider), VALUE, "CurrentValue"), 0.5);
}

TEST_F(AtspiBridge, ASliderRangeWriteReachesItsNode) {
    // arrange a slider whose write records the clamped value it was given (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t slider = tree.add(SemanticNode{SemanticRole::Slider});
    double written = -1.0;
    tree.mutable_at(slider)->set_range(SemanticRange{.value = 0.5, .minimum = 0.0, .maximum = 1.0, .step = 0.1}, [&written](double value) { written = value; });
    m_adapter->publish(tree);
    // act
    const bool accepted = m_client.write_double(bus_name(), node_path(slider), VALUE, "CurrentValue", 0.75);
    // the write is queued for the ui thread, which publish is where it runs (§10.2)
    m_adapter->publish(tree);
    // assert the write reached the range work, which is what a platform SetValue asks for (§10.2)
    EXPECT_TRUE(accepted);
    EXPECT_DOUBLE_EQ(written, 0.75);
    // assert the number is readable through the protocol, because it landed on the publication the client reads (§10.2)
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(slider), VALUE, "CurrentValue"), 0.75);
}

TEST_F(AtspiBridge, ADisabledSliderRefusesTheRangeWrite) {
    // arrange a disabled slider, which refuses every action including a range write (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t slider = tree.add(SemanticNode{SemanticRole::Slider});
    tree.mutable_at(slider)->set_disabled(true);
    tree.mutable_at(slider)->set_range(SemanticRange{.value = 0.5, .minimum = 0.0, .maximum = 1.0, .step = 0.1}, [](double) {});
    m_adapter->publish(tree);
    // act
    const bool accepted = m_client.write_double(bus_name(), node_path(slider), VALUE, "CurrentValue", 0.75);
    // assert the write is refused, which the node decided (§10.1)
    EXPECT_FALSE(accepted);
    // assert the slider does not announce the enabled state, which a reader speaks (§10.2)
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(slider));
    ASSERT_TRUE(state.has_value());
    EXPECT_FALSE(AtspiClient::has_state(*state, 8U));
}

TEST_F(AtspiBridge, AComboBoxExposesExpandAndCollapse) {
    // arrange a combo box carrying both popup actions, which the shared table declares (§10.2)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t combo = tree.add(SemanticNode{SemanticRole::ComboBox});
    tree.mutable_at(combo)->set_value("One");
    int expansions = 0;
    int collapses = 0;
    (void)tree.mutable_at(combo)->add_action("expand", [&expansions] { expansions++; });
    (void)tree.mutable_at(combo)->add_action("collapse", [&collapses] { collapses++; });
    m_adapter->publish(tree);
    // assert the combo box announces the combo box role and the expandable state (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(combo)), 11U);
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(combo));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 9U));
    EXPECT_TRUE(AtspiClient::has_state(*state, 42U));
    // assert the combo box announces its text, which the text interface answers for every platform (§10.2)
    EXPECT_EQ(m_client.text(bus_name(), node_path(combo), 0, -1), "One");
    // act
    ASSERT_TRUE(m_client.do_action(bus_name(), node_path(combo), 0).value_or(false));
    ASSERT_TRUE(m_client.do_action(bus_name(), node_path(combo), 1).value_or(false));
    // the actions are queued for the ui thread, which publish is where they run (§10.2)
    m_adapter->publish(tree);
    // assert both popup actions ran, which is what a reader asks for (§10.2)
    EXPECT_EQ(expansions, 1);
    EXPECT_EQ(collapses, 1);
}

TEST_F(AtspiBridge, AListAnnouncesAsAList) {
    // arrange a list over one row, which assistive technology groups (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t list = tree.add(SemanticNode{SemanticRole::List});
    const std::size_t row = tree.add(SemanticNode{SemanticRole::ListItem}, list);
    tree.mutable_at(row)->set_name("first row");
    tree.mutable_at(row)->set_selected(true);
    // act
    m_adapter->publish(tree);
    // assert the list and the row announce the roles a reader groups by (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(list)), 31U);
    EXPECT_EQ(m_client.role(bus_name(), node_path(row)), 32U);
    // assert the chosen row announces the selected and selectable states (§10.2)
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(row));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 23U));
    EXPECT_TRUE(AtspiClient::has_state(*state, 22U));
}

TEST_F(AtspiBridge, ATabViewAnnouncesItsTabsAndTheirSelection) {
    // arrange a page tab list over two pages, of which the second is chosen (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t tabs = tree.add(SemanticNode{SemanticRole::TabView});
    const std::size_t first = tree.add(SemanticNode{SemanticRole::TabItem}, tabs);
    tree.mutable_at(first)->set_name("One");
    const std::size_t second = tree.add(SemanticNode{SemanticRole::TabItem}, tabs);
    tree.mutable_at(second)->set_name("Two");
    tree.mutable_at(second)->set_selected(true);
    // act
    m_adapter->publish(tree);
    // assert the strip announces the page tab list and its pages, which a reader walks (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(tabs)), 38U);
    const std::optional<std::vector<std::pair<std::string, std::string>>> pages = m_client.children(bus_name(), node_path(tabs));
    ASSERT_TRUE(pages.has_value());
    ASSERT_EQ(pages->size(), 2U);
    EXPECT_EQ(m_client.role(bus_name(), (*pages)[0].second), 37U);
    // assert the chosen page announces its selection, which the strip owns (§10.2)
    const std::optional<std::vector<std::uint32_t>> state = m_client.state(bus_name(), node_path(second));
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(AtspiClient::has_state(*state, 23U));
}

TEST_F(AtspiBridge, ARadioGroupExposesItsSelection) {
    // arrange a radio group over two choices, of which the first is chosen (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t group = tree.add(SemanticNode{SemanticRole::RadioGroup});
    tree.mutable_at(group)->set_name("Mode");
    const std::size_t first = tree.add(SemanticNode{SemanticRole::RadioButton}, group);
    tree.mutable_at(first)->set_name("One");
    tree.mutable_at(first)->set_selected(true);
    tree.mutable_at(first)->set_check(CheckState::Checked);
    const std::size_t second = tree.add(SemanticNode{SemanticRole::RadioButton}, group);
    int activations = 0;
    tree.mutable_at(second)->set_name("Two");
    (void)tree.mutable_at(second)->add_action("activate", [&activations] { activations++; });
    // act
    m_adapter->publish(tree);
    // assert the group announces the grouping role, which is the container a reader selects in (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(group)), 99U);
    // assert the group announces how many children are chosen, which a reader speaks (§10.2)
    EXPECT_EQ(m_client.property_int(bus_name(), node_path(group), SELECTION, "NSelectedChildren"), 1);
    // assert the chosen child is the first, which the selection query answers (§10.2)
    const std::optional<std::pair<std::string, std::string>> chosen = m_client.selected_child(bus_name(), node_path(group), 0);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->second, node_path(first));
    // assert the choices agree about their own selection, which a reader queries per child (§10.2)
    EXPECT_EQ(m_client.is_child_selected(bus_name(), node_path(group), 0), true);
    EXPECT_EQ(m_client.is_child_selected(bus_name(), node_path(group), 1), false);
    // act
    const std::optional<bool> selected = m_client.select_child(bus_name(), node_path(group), 1);
    // the activation is queued for the ui thread, which publish is where it runs (§10.2)
    m_adapter->publish(tree);
    // assert selecting a choice runs its activation, which is the group's only portable path (§10.1, §10.2)
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(*selected);
    EXPECT_EQ(activations, 1);
}

TEST_F(AtspiBridge, ANumericStepperAnnouncesItsValue) {
    // arrange a bounded stepper, which a reader increments (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t stepper = tree.add(SemanticNode{SemanticRole::NumericStepper});
    tree.mutable_at(stepper)->set_range(SemanticRange{.value = 5.0, .minimum = 0.0, .maximum = 10.0, .step = 1.0}, [](double) {});
    // act
    m_adapter->publish(tree);
    // assert the stepper announces the spin button role and the value it holds (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(stepper)), 52U);
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(stepper), VALUE, "CurrentValue"), 5.0);
    EXPECT_EQ(m_client.property_double(bus_name(), node_path(stepper), VALUE, "MinimumIncrement"), 1.0);
}

TEST_F(AtspiBridge, AMenuItemAnnouncesItsRoleAndName) {
    // arrange a menu row, which a reader invokes (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t item = tree.add(SemanticNode{SemanticRole::MenuItem});
    tree.mutable_at(item)->set_name("Save");
    // act
    m_adapter->publish(tree);
    // assert the row announces the menu item role and its name (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(item)), 35U);
    EXPECT_EQ(m_client.name(bus_name(), node_path(item)), "Save");
}

TEST_F(AtspiBridge, AnImageAnnouncesItsRoleNameAndDescription) {
    // arrange an image carrying a name and the description a tooltip supplied (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t image = tree.add(SemanticNode{SemanticRole::Image});
    tree.mutable_at(image)->set_name("Logo");
    tree.mutable_at(image)->set_description("Company logo");
    // act
    m_adapter->publish(tree);
    // assert the image announces the image role, which a reader describes (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(image)), 27U);
    EXPECT_EQ(m_client.name(bus_name(), node_path(image)), "Logo");
    EXPECT_EQ(m_client.description(bus_name(), node_path(image)), "Company logo");
}

TEST_F(AtspiBridge, APlainLabelAnnouncesNoAction) {
    // arrange static text, which carries no action at all (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(label)->set_name("Static text");
    // act
    m_adapter->publish(tree);
    // assert the label announces the label role, which a reader reads (§10.2)
    EXPECT_EQ(m_client.role(bus_name(), node_path(label)), 29U);
    // assert the label does not advertise the action interface, which it cannot honour (§10.2)
    const std::optional<std::vector<std::string>> interfaces = m_client.interfaces(bus_name(), node_path(label));
    ASSERT_TRUE(interfaces.has_value());
    EXPECT_EQ(std::find(interfaces->begin(), interfaces->end(), ACTION), interfaces->end());
}

TEST_F(AtspiBridge, ATextOffsetCountsCharactersRatherThanBytes) {
    // arrange a field whose text carries a multi byte character, which the node measures in bytes (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t field = tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(field)->set_value("h\u00e9llo");
    tree.mutable_at(field)->set_selection(0, 3);
    // act
    m_adapter->publish(tree);
    // assert the character count is the number a reader announces, which the bytes do not match (§10.2)
    EXPECT_EQ(m_client.property_int(bus_name(), node_path(field), TEXT, "CharacterCount"), 5);
    // assert the selection offsets are characters rather than bytes, which is what the protocol counts (§10.2)
    const std::optional<std::pair<std::int32_t, std::int32_t>> selection = m_client.selection(bus_name(), node_path(field), 0);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->first, 0);
    EXPECT_EQ(selection->second, 2);
    // assert a character range reads whole characters, which a reader announces (§10.2)
    EXPECT_EQ(m_client.text(bus_name(), node_path(field), 1, 2), "\u00e9");
}

TEST_F(AtspiBridge, TheComponentInterfaceAnswersThePublishedBounds) {
    // arrange a node at a known place, which a reader places its announcement from (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t button = tree.add(SemanticNode{SemanticRole::Button});
    tree.mutable_at(button)->set_name("Save");
    tree.mutable_at(button)->set_bounds(Rect{.x = 10.0f, .y = 20.0f, .width = 80.0f, .height = 24.0f});
    // act
    m_adapter->publish(tree);
    // assert the extents are the published bounds, which the window coordinates report (§10.2)
    const std::optional<std::array<std::int32_t, 4>> extents = m_client.extents(bus_name(), node_path(button));
    ASSERT_TRUE(extents.has_value());
    EXPECT_EQ((*extents)[0], 10);
    EXPECT_EQ((*extents)[1], 20);
    EXPECT_EQ((*extents)[2], 80);
    EXPECT_EQ((*extents)[3], 24);
    // assert the hit test agrees with the rectangle, which a reader clicks by (§10.2)
    EXPECT_EQ(m_client.contains(bus_name(), node_path(button), 50, 30), true);
    EXPECT_EQ(m_client.contains(bus_name(), node_path(button), 0, 0), false);
}

TEST_F(AtspiBridge, TheRegistryReceivesTheEmbed) {
    // arrange a fake registry on the same bus, which records the call the bridge makes on attach (§10.2)
    FakeRegistry registry;
    ASSERT_TRUE(registry.open(m_bus.address()));
    // act
    ASSERT_TRUE(attach());
    // assert the registry received the application root, which is what announces the tree (§10.2)
    ASSERT_TRUE(registry.wait_for_embed());
    const std::pair<std::string, std::string> embedded = registry.embedded();
    EXPECT_EQ(embedded.first, bus_name());
    EXPECT_EQ(embedded.second, root_path());
    // act
    m_adapter->detach_window();
    // assert the registry is told the application left, which keeps its list current (§10.2)
    EXPECT_TRUE(registry.wait_for_unembed());
}

TEST_F(AtspiBridge, TheLauncherRoutesTheBridgeToTheAccessibilityBus) {
    // arrange a second private bus, which the launcher names as the accessibility bus (§10.2)
    PrivateBus accessibility_bus;
    ASSERT_TRUE(accessibility_bus.ready());
    FakeLauncher launcher;
    ASSERT_TRUE(launcher.open(m_bus.address(), accessibility_bus.address()));
    FakeRegistry registry;
    ASSERT_TRUE(registry.open(accessibility_bus.address()));
    // act
    ASSERT_TRUE(attach());
    // assert the bridge registered on the bus the launcher named, which is what a desktop expects (§10.2)
    ASSERT_TRUE(registry.wait_for_embed());
    EXPECT_EQ(registry.embedded().second, root_path());
    // arrange a client on the accessibility bus, which is where assistive technology connects (§10.2)
    AtspiClient client;
    ASSERT_TRUE(client.open(accessibility_bus.address()));
    SemanticTree tree;
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(label)->set_name("first row");
    m_adapter->publish(tree);
    // assert the tree is reachable on the accessibility bus, which proves the discovery handshake (§10.2)
    EXPECT_EQ(client.name(bus_name(), node_path(label)), "first row");
}

TEST_F(AtspiBridge, PublishingRaisesChildrenChanged) {
    // arrange a listening client and an attached bridge with nothing published (§10.2)
    ASSERT_TRUE(attach());
    m_client.listen_for_events();
    SemanticTree tree;
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(label)->set_name("first row");
    // act
    m_adapter->publish(tree);
    // assert a structure signal arrived, which tells a client to re-read the tree (§10.2)
    DBusMessage* signal = m_client.next_signal(2000);
    ASSERT_NE(signal, nullptr);
    // assert the signal names the root and the add operation, which the protocol carries (§10.2)
    EXPECT_STREQ(dbus_message_get_interface(signal), EVENT_OBJECT);
    EXPECT_STREQ(dbus_message_get_member(signal), "ChildrenChanged");
    EXPECT_STREQ(dbus_message_get_path(signal), root_path().c_str());
    DBusMessageIter iterator;
    ASSERT_TRUE(dbus_message_iter_init(signal, &iterator));
    const char* operation = nullptr;
    dbus_message_iter_get_basic(&iterator, &operation);
    EXPECT_STREQ(operation, "add");
    dbus_message_unref(signal);
}

TEST_F(AtspiBridge, TheWidgetCorpusAnnouncesEveryRoleEveryAdapterCarries) {
    // arrange the shared corpus, which every platform adapter announces through its own api (§13.2, BDG-10)
    ASSERT_TRUE(attach());
    BindingEngine engine;
    AccessibilityCorpus corpus(&engine, nullptr);
    const SemanticTree tree = build_semantic_tree(corpus);
    ASSERT_FALSE(tree.empty());
    // act
    m_adapter->publish(tree);
    // assert the role sequence a client reads matches the tree, which is the conformance the corpus proves (§13.2)
    const auto expected_role = [](SemanticRole role) -> std::uint32_t {
        switch (role) {
        case SemanticRole::None:
            return 67U;
        case SemanticRole::Button:
            return 43U;
        case SemanticRole::Checkbox:
            return 7U;
        case SemanticRole::Heading:
            return 83U;
        case SemanticRole::Label:
            return 29U;
        case SemanticRole::Slider:
            return 51U;
        case SemanticRole::ProgressBar:
            return 42U;
        case SemanticRole::TabView:
            return 38U;
        case SemanticRole::TabItem:
            return 37U;
        case SemanticRole::RadioGroup:
            return 99U;
        case SemanticRole::RadioButton:
            return 44U;
        case SemanticRole::NumericStepper:
            return 52U;
        case SemanticRole::Canvas:
            return 6U;
        case SemanticRole::TextField:
            return 79U;
        case SemanticRole::Image:
            return 27U;
        case SemanticRole::ListItem:
            return 32U;
        case SemanticRole::MenuItem:
            return 35U;
        case SemanticRole::List:
            return 31U;
        case SemanticRole::ComboBox:
            return 11U;
        case SemanticRole::Group:
            return 99U;
        case SemanticRole::ScrollView:
            return 49U;
        case SemanticRole::Window:
            return 69U;
        }
        return 67U;
    };
    const std::optional<std::vector<std::pair<std::string, std::string>>> roots = m_client.children(bus_name(), root_path());
    ASSERT_TRUE(roots.has_value());
    std::vector<std::pair<std::uint32_t, std::string>> announced;
    std::vector<std::string> pending;
    for (auto entry = roots->rbegin(); entry != roots->rend(); ++entry)
        pending.push_back(entry->second);
    std::size_t position = 0;
    while (!pending.empty()) {
        const std::string path = pending.back();
        pending.pop_back();
        // the role and the name are the two announcements every adapter must agree on (§10.1, §13.2)
        const std::optional<std::uint32_t> role = m_client.role(bus_name(), path);
        const std::optional<std::string> name = m_client.name(bus_name(), path);
        ASSERT_TRUE(role.has_value());
        ASSERT_TRUE(name.has_value());
        announced.emplace_back(*role, *name);
        // the announced node is the tree node at this position, because both walks are the same depth first order (§10.1)
        ASSERT_LT(position, tree.size());
        const SemanticNode* node = tree.at(position);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(*role, expected_role(node->role()));
        EXPECT_EQ(*name, node->name());
        // an action a node registers is advertised and counted, which a reader invokes (§10.2)
        if (node->action_count() > 0)
            EXPECT_EQ(m_client.property_int(bus_name(), path, ACTION, "NActions"), static_cast<std::int32_t>(node->action_count()));
        position++;
        // the children are pushed in reverse, so the walk visits them in tree order (§10.1)
        const std::optional<std::vector<std::pair<std::string, std::string>>> children = m_client.children(bus_name(), path);
        ASSERT_TRUE(children.has_value());
        for (auto entry = children->rbegin(); entry != children->rend(); ++entry)
            pending.push_back(entry->second);
    }
    EXPECT_EQ(position, tree.size());
    // assert the corpus reaches the controls the widget set carries, which keeps the corpus from drifting (§13.2)
    EXPECT_GE(tree.count_role(SemanticRole::Button), 1U);
    EXPECT_GE(tree.count_role(SemanticRole::Checkbox), 1U);
    EXPECT_GE(tree.count_role(SemanticRole::RadioGroup), 1U);
    EXPECT_GE(tree.count_role(SemanticRole::RadioButton), 2U);
    EXPECT_GE(tree.count_role(SemanticRole::TabItem), 2U);
    EXPECT_GE(tree.count_role(SemanticRole::Image), 1U);
    EXPECT_GE(tree.count_role(SemanticRole::MenuItem), 1U);
}

TEST_F(AtspiBridge, AnUnchangedRepublishAnnouncesNoStructureChange) {
    // arrange a published tree on a bridge a client listens to (§10.2)
    ASSERT_TRUE(attach());
    m_client.listen_for_events();
    SemanticTree tree;
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(label)->set_name("first row");
    m_adapter->publish(tree);
    // the first publish announces the root, which the client drains before the repeat (§10.2)
    DBusMessage* announced = m_client.next_signal(2000);
    ASSERT_NE(announced, nullptr);
    dbus_message_unref(announced);
    // act
    m_adapter->publish(tree);
    // assert the unchanged tree announces nothing, because a client would re-read data that did not change (§10.2)
    DBusMessage* repeated = m_client.next_signal(300);
    if (repeated != nullptr)
        dbus_message_unref(repeated);
    EXPECT_EQ(repeated, nullptr);
}

TEST_F(AtspiBridge, ADroppedRootAnnouncesItsRemovalWithItsOldIndex) {
    // arrange a tree with two roots, which the application root announces as its children (§10.2)
    ASSERT_TRUE(attach());
    m_client.listen_for_events();
    SemanticTree tree;
    const std::size_t first = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(first)->set_name("first row");
    const std::size_t second = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(second)->set_name("second row");
    m_adapter->publish(tree);
    // the two additions are drained, so the removal is the first signal that follows (§10.2)
    for (int index = 0; index < 2; index++) {
        DBusMessage* added = m_client.next_signal(2000);
        ASSERT_NE(added, nullptr);
        dbus_message_unref(added);
    }
    // act a publication that keeps only the first root, which takes the second away (§10.2)
    SemanticTree updated;
    updated.add(SemanticNode{SemanticRole::Label});
    m_adapter->publish(updated);
    // assert one removal names the child that went away, at the index it held (§10.2)
    DBusMessage* removed = m_client.next_signal(2000);
    ASSERT_NE(removed, nullptr);
    DBusMessageIter iterator;
    ASSERT_TRUE(dbus_message_iter_init(removed, &iterator));
    const char* operation = nullptr;
    dbus_message_iter_get_basic(&iterator, &operation);
    EXPECT_STREQ(operation, "remove");
    dbus_message_iter_next(&iterator);
    dbus_int32_t at = -1;
    dbus_message_iter_get_basic(&iterator, &at);
    EXPECT_EQ(at, 1);
    // the detail word sits between the index and the child, which the protocol fixes (§10.2)
    dbus_message_iter_next(&iterator);
    // the child the removal names is the one that went away, which the variant carries (§10.2)
    dbus_message_iter_next(&iterator);
    DBusMessageIter variant;
    dbus_message_iter_recurse(&iterator, &variant);
    DBusMessageIter structure;
    dbus_message_iter_recurse(&variant, &structure);
    dbus_message_iter_next(&structure);
    const char* path = nullptr;
    dbus_message_iter_get_basic(&structure, &path);
    EXPECT_EQ(std::string(path), node_path(second));
    dbus_message_unref(removed);
    // assert no addition follows, because the surviving root did not move (§10.2)
    DBusMessage* added = m_client.next_signal(300);
    if (added != nullptr)
        dbus_message_unref(added);
    EXPECT_EQ(added, nullptr);
}

TEST_F(AtspiBridge, AStalePathIsRefused) {
    // arrange a publication of two nodes on an attached bridge (§10.2)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t first = tree.add(SemanticNode{SemanticRole::Label});
    const std::size_t second = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(first)->set_name("first row");
    tree.mutable_at(second)->set_name("second row");
    m_adapter->publish(tree);
    ASSERT_TRUE(m_client.role(bus_name(), node_path(second)).has_value());
    // act a shorter publication, which moves the client's path out of the tree (§10.2)
    SemanticTree updated;
    updated.add(SemanticNode{SemanticRole::Label});
    m_adapter->publish(updated);
    // assert the path that names no node is refused, which a client reads as a dead object (§10.2)
    EXPECT_FALSE(m_client.role(bus_name(), node_path(second)).has_value());
    EXPECT_EQ(m_client.last_error(), DBUS_ERROR_UNKNOWN_OBJECT);
}

TEST_F(AtspiBridge, TheIntrospectionDocumentNamesWhatTheObjectAnswers) {
    // arrange a field and a label, which answer different interfaces (§10.1, §10.2)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t field = tree.add(SemanticNode{SemanticRole::TextField});
    tree.mutable_at(field)->set_value("Albor");
    const std::size_t label = tree.add(SemanticNode{SemanticRole::Label});
    tree.mutable_at(label)->set_name("Static text");
    m_adapter->publish(tree);
    // act
    const std::optional<std::string> document = m_client.introspect(bus_name(), node_path(field));
    ASSERT_TRUE(document.has_value());
    // assert the document names the text interfaces the object answers (§10.2)
    EXPECT_NE(document->find("<interface name=\"" + std::string(TEXT) + "\">"), std::string::npos);
    EXPECT_NE(document->find("<interface name=\"" + std::string(EDITABLE_TEXT) + "\">"), std::string::npos);
    // assert the document and the interface query agree on every interface, which a bus tool reads (§10.2)
    const std::optional<std::vector<std::string>> interfaces = m_client.interfaces(bus_name(), node_path(field));
    ASSERT_TRUE(interfaces.has_value());
    for (const std::string& name : *interfaces)
        EXPECT_NE(document->find("<interface name=\"" + name + "\">"), std::string::npos);
    // assert a label advertises no text interface and refuses its members, which the two answers agree on (§10.2)
    const std::optional<std::vector<std::string>> label_interfaces = m_client.interfaces(bus_name(), node_path(label));
    ASSERT_TRUE(label_interfaces.has_value());
    EXPECT_EQ(std::find(label_interfaces->begin(), label_interfaces->end(), TEXT), label_interfaces->end());
    EXPECT_FALSE(m_client.text(bus_name(), node_path(label), 0, -1).has_value());
    // assert the root document names the application methods the root answers (§10.2)
    const std::optional<std::string> root_document = m_client.introspect(bus_name(), root_path());
    ASSERT_TRUE(root_document.has_value());
    EXPECT_NE(root_document->find("GetLocale"), std::string::npos);
    EXPECT_NE(root_document->find("GetApplicationBusAddress"), std::string::npos);
}

TEST_F(AtspiBridge, ADetachedBridgeDropsAQueuedRequest) {
    // arrange a published tree whose toggle records what ran (§10.1)
    ASSERT_TRUE(attach());
    SemanticTree tree;
    const std::size_t box = tree.add(SemanticNode{SemanticRole::Checkbox});
    int toggles = 0;
    (void)tree.mutable_at(box)->add_action("toggle", [&toggles] { toggles++; });
    m_adapter->publish(tree);
    ASSERT_TRUE(m_client.do_action(bus_name(), node_path(box), 0).value_or(false));
    // act the bridge detaches before the request runs, which drops the tree it named (§5.2, §10.2)
    m_adapter->detach_window();
    m_adapter->publish(tree);
    // assert the request from the detached session did not run, because its widgets may be gone (§10.2)
    EXPECT_EQ(toggles, 0);
}
