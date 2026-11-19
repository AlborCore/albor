// the XDG portal file dialog and appearance, which a fake portal service on a private bus proves (§5.3, §8.4)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include <gtest/gtest.h>

#include "albor/application/file_dialog.h"
#include "albor/core/color_scheme.h"
#include "albor/platform/linux/wayland/platform_window.h"
#include "albor/platform/linux/wayland/portal.h"
#include "albor/window/platform_window.h"
#include "albor/window/system_appearance.h"
#include "albor/window/window_options.h"

using namespace albor;
using albor::wayland::PortalClient;

namespace
{
    // the portal service the suite drives, which runs on a private bus the test spawns (§5.3)
    class FakePortal
    {
    public:
        // start a private bus and claim the portal name, which a bare bus leaves unclaimed (§5.3)
        explicit FakePortal(bool claim_portal = true) {
            // the library is initialized before a connection crosses two threads (§5.3)
            dbus_threads_init_default();
            // the private bus is what lets the fake own the portal name the session already answers (§5.3)
            if (!start_daemon())
                return;
            // a bare bus serves the suite's portal-less host, which no service claims a name on (§5.3)
            if (!claim_portal) {
                // the bus alone is what a host without a portal exports (§5.3)
                m_ready = true;
                return;
            }
            // the connection is what serves the portal interfaces (§5.3)
            if (!open_connection())
                return;
            // the service runs on its own thread, which a request reaches while the test waits (§5.3)
            m_thread = std::thread([this] { serve(); });
            // a ready service is what every test asserts before it arranges a request (§5.3)
            m_ready = true;
        }

        ~FakePortal() {
            // the thread is asked to stop before the connection it reads is released (§5.3)
            m_running = false;
            // the thread is joined, so no message is read through a released connection (§5.3)
            if (m_thread.joinable())
                m_thread.join();
            // the connection is closed before it is released, which a private connection needs (§5.3)
            if (m_connection != nullptr) {
                dbus_connection_close(m_connection);
                dbus_connection_unref(m_connection);
                m_connection = nullptr;
            }
            // the private bus is stopped with the service it hosted (§5.3)
            if (m_daemon > 0) {
                kill(m_daemon, SIGTERM);
                waitpid(m_daemon, nullptr, 0);
                m_daemon = -1;
            }
            // the config is released with the bus that read it (§5.3)
            if (!m_config_path.empty()) {
                unlink(m_config_path.c_str());
                m_config_path.clear();
            }
        }

        FakePortal(const FakePortal&) = delete;
        FakePortal& operator=(const FakePortal&) = delete;

        // report whether the private bus and the service are up (§5.3)
        [[nodiscard]] bool ready() const { return m_ready; }

        // hand out the bus address the client connects to, which the test exports (§5.3)
        [[nodiscard]] const std::string& address() const { return m_address; }

        // answer the next panel with one file uri (§5.3)
        void answer_with(std::string uri) {
            // the lock keeps the service thread from reading a half written answer (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the outcome is a success, which carries the uri (§5.3)
            m_outcome = 0;
            // the uri is what the panel hands out (§5.3)
            m_uri = std::move(uri);
        }

        // answer the next panel with a cancel, which names no file (§5.3)
        void answer_cancelled() {
            // the lock keeps the service thread from reading a half written answer (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // one is the outcome the portal names as cancelled (§5.3)
            m_outcome = 1;
            // a cancelled panel names no uri (§5.3)
            m_uri.reset();
        }

        // answer the next panel with a success that carries no uri (§5.3)
        void answer_empty() {
            // the lock keeps the service thread from reading a half written answer (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // a success without a uri is not a path, which the client must refuse (§5.3)
            m_outcome = 0;
            // the empty result names no file (§5.3)
            m_uri.reset();
        }

        // set the colour scheme a settings read answers (§8.4)
        void set_color_scheme(std::uint32_t scheme) {
            // the lock keeps the service thread from reading a half written value (§8.4)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the value is what the next read answers (§8.4)
            m_scheme = scheme;
        }

        // report a colour scheme change, which the service thread sends on its own connection (§8.4)
        void emit_color_scheme(std::uint32_t scheme) {
            // the lock keeps the service thread from reading a half written value (§8.4)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the value is what the next read answers (§8.4)
            m_scheme = scheme;
            // the change is sent by the service thread, so two threads never share one bus connection (§8.4)
            m_change_pending = true;
        }

        // answer the settings read as a version 1 portal, which lacks the newer method (§8.4)
        void refuse_read_one() {
            // the lock keeps the service thread from reading a half written flag (§8.4)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the flag makes the newer method fail, which leaves the deprecated one to answer (§8.4)
            m_refuse_read_one = true;
        }

        // answer the next panel on a handle the portal names, ignoring the token it was given (§5.3)
        void ignore_handle_token() {
            // the lock keeps the service thread from reading a half written flag (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the flag makes the fake name its own handle, which the client learns from the reply (§5.3)
            m_ignore_token = true;
        }

        // stop the bus while a panel runs, which a desktop restart does (§5.3)
        void stop_bus() {
            // the daemon is stopped once, which the fixture ignores afterwards (§5.3)
            if (m_daemon > 0) {
                kill(m_daemon, SIGTERM);
                waitpid(m_daemon, nullptr, 0);
                m_daemon = -1;
            }
        }

        // stop the bus when the next panel request arrives, which a desktop restart does (§5.3)
        void stop_bus_with_next_panel() {
            // the lock keeps the service thread from reading a half written flag (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // the flag makes the next request end the bus it arrived on (§5.3)
            m_stop_with_panel = true;
        }

        // read the parent window the last panel named (§5.3)
        [[nodiscard]] std::string parent() const {
            // the lock reads the value the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_parent;
        }

        // read the title the last panel carried (§5.3)
        [[nodiscard]] std::string title() const {
            // the lock reads the value the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_title;
        }

        // read the initial folder the last panel carried, as utf8 (§5.3)
        [[nodiscard]] std::string folder() const {
            // the lock reads the value the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_folder;
        }

        // read the default name the last save panel carried (§5.3)
        [[nodiscard]] std::string default_name() const {
            // the lock reads the value the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_name;
        }

        // read the filters the last panel carried, each spelled as "name;glob;glob" (§5.3)
        [[nodiscard]] std::vector<std::string> filters() const {
            // the lock reads the values the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_filters;
        }

        // report whether the last panel allowed a multiple selection (§5.3)
        [[nodiscard]] bool multiple() const {
            // the lock reads the value the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_multiple;
        }

        // count the panels the service ran (§5.3)
        [[nodiscard]] std::size_t panels() const {
            // the lock reads the value the service thread wrote (§5.3)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return m_panel_count;
        }

    private:
        // the bus name the portal owns, which the client addresses (§5.3)
        static constexpr const char* PORTAL_NAME = "org.freedesktop.portal.Desktop";

        // the object every portal interface lives on (§5.3)
        static constexpr const char* PORTAL_PATH = "/org/freedesktop/portal/desktop";

        // the interface a panel answers on (§5.3)
        static constexpr const char* REQUEST_IFACE = "org.freedesktop.portal.Request";

        // the interface that carries the desktop settings (§8.4)
        static constexpr const char* SETTINGS_IFACE = "org.freedesktop.portal.Settings";

        // start the private session bus, which the service owns the portal name on (§5.3)
        bool start_daemon() {
            // the config is written first, because the host session config would activate its real portal (§5.3)
            char config_path[] = "/tmp/albor-portal-bus-XXXXXX";
            const int config_fd = mkstemp(config_path);
            // a config the host refused leaves no bus to start (§5.3)
            if (config_fd < 0)
                return false;
            // the config listens on a private socket and declares no service directory, so nothing is activated (§5.3)
            static constexpr const char* CONFIG = "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n<busconfig>\n  <type>session</type>\n  <listen>unix:tmpdir=/tmp</listen>\n  <auth>EXTERNAL</auth>\n  <policy context=\"default\">\n    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n    <allow eavesdrop=\"true\"/>\n    <allow own=\"*\"/>\n  </policy>\n</busconfig>\n";
            // the config bytes are written in one call, which the daemon reads at startup (§5.3)
            const ssize_t written = write(config_fd, CONFIG, std::strlen(CONFIG));
            // the descriptor is released once the config is written (§5.3)
            close(config_fd);
            // a config that could not be written leaves no bus to start (§5.3)
            if (written != static_cast<ssize_t>(std::strlen(CONFIG)))
                return false;
            // the path is kept, because the daemon reads it when it starts (§5.3)
            m_config_path = config_path;
            // the pipe carries the address the daemon prints (§5.3)
            int descriptors[2] = {-1, -1};
            // a pipe the host refused leaves no bus to start (§5.3)
            if (pipe(descriptors) != 0)
                return false;
            // the daemon runs in its own process, which the test stops when it ends (§5.3)
            const pid_t child = fork();
            // a fork the host refused closes the pipe and reports no bus (§5.3)
            if (child < 0) {
                close(descriptors[0]);
                close(descriptors[1]);
                return false;
            }
            // the child becomes the bus daemon, which prints its address on the inherited pipe (§5.3)
            if (child == 0) {
                dup2(descriptors[1], STDOUT_FILENO);
                close(descriptors[0]);
                close(descriptors[1]);
                execlp("dbus-daemon", "dbus-daemon", "--config-file", m_config_path.c_str(), "--nofork", "--print-address=1", nullptr);
                _exit(127);
            }
            // the parent reads the address, so the write end is no longer needed (§5.3)
            close(descriptors[1]);
            // the address is one line the daemon prints before it serves (§5.3)
            std::string line;
            // the bytes are read until the line ends (§5.3)
            char byte = '\0';
            while (read(descriptors[0], &byte, 1) == 1 && byte != '\n') {
                // a carriage return is not part of the address (§5.3)
                if (byte != '\r')
                    line.push_back(byte);
            }
            // the read end is released once the address arrived (§5.3)
            close(descriptors[0]);
            // the daemon is kept for the service and stopped with it (§5.3)
            m_daemon = child;
            // the address is what the client connects to (§5.3)
            m_address = line;
            return !m_address.empty();
        }

        // open the connection the service answers on and claim the portal name (§5.3)
        bool open_connection() {
            // the error carries a connection or a name the bus refused (§5.3)
            DBusError error;
            dbus_error_init(&error);
            // the private connection keeps the service away from the client's connection (§5.3)
            m_connection = dbus_connection_open_private(m_address.c_str(), &error);
            // a bus that refused the connection leaves nothing to serve (§5.3)
            if (m_connection == nullptr) {
                dbus_error_free(&error);
                return false;
            }
            // a dropped bus must not terminate the test process (§5.3)
            dbus_connection_set_exit_on_disconnect(m_connection, FALSE);
            // the bus registers the connection, which gives it the unique name it serves under (§5.3)
            if (!dbus_bus_register(m_connection, &error)) {
                dbus_error_free(&error);
                return false;
            }
            // the portal name is what the client addresses, which only the service owns (§5.3)
            const int request = dbus_bus_request_name(m_connection, PORTAL_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
            // a name another service owns leaves no portal to prove (§5.3)
            if (request != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                dbus_error_free(&error);
                return false;
            }
            // the name is flushed, so a client that follows is answered by this service (§5.3)
            dbus_connection_flush(m_connection);
            return true;
        }

        // serve the portal interfaces until the fixture is destroyed (§5.3)
        void serve() {
            // the loop runs for the life of the fixture (§5.3)
            while (m_running.load()) {
                // the bus is read with a bound, so the thread wakes even without traffic (§5.3)
                if (!dbus_connection_read_write(m_connection, 20))
                    break;
                // every message that arrived is handled in order (§5.3)
                for (DBusMessage* message = dbus_connection_pop_message(m_connection); message != nullptr; message = dbus_connection_pop_message(m_connection)) {
                    // the method call is what the client asked for (§5.3)
                    handle(message);
                    // the message is released once it is handled (§5.3)
                    dbus_message_unref(message);
                }
                // a change the test asked for is reported on this thread, which owns the connection (§8.4)
                send_pending_change();
                // a response the portal deferred follows the reply, which a named handle needs (§5.3)
                send_deferred_response();
            }
        }

        // send the response a portal that named its own handle deferred by one pass (§5.3)
        void send_deferred_response() {
            // the handle is read under the lock the request wrote through (§5.3)
            std::optional<std::string> path;
            // the lock keeps the request from writing while the handle is read (§5.3)
            {
                // the lock guards the deferred response (§5.3)
                const std::lock_guard<std::mutex> guard(m_mutex);
                // only a deferred response is sent, which leaves the steady state quiet (§5.3)
                if (!m_deferred_response.has_value())
                    return;
                // the pass that queued the response is one behind, which leaves the client time to subscribe (§5.3)
                if (m_deferred_delay > 0) {
                    m_deferred_delay--;
                    return;
                }
                // the handle is what the deferred response answers on (§5.3)
                path = m_deferred_response;
                // the response is cleared before it is sent, so it is reported once (§5.3)
                m_deferred_response.reset();
            }
            // the response is what ends the panel the client runs (§5.3)
            emit_response(*path);
        }

        // send the colour scheme change the test queued, on the thread that owns the connection (§8.4)
        void send_pending_change() {
            // the value is read under the lock the test writes through (§8.4)
            std::optional<std::uint32_t> scheme;
            // the lock keeps the test from writing while the change is read (§8.4)
            {
                // the lock guards the queued change (§8.4)
                const std::lock_guard<std::mutex> guard(m_mutex);
                // only a queued change is sent, which leaves the steady state quiet (§8.4)
                if (!m_change_pending)
                    return;
                // the scheme is what the pending change reports (§8.4)
                scheme = m_scheme;
                // the change is cleared before it is sent, so it is reported once (§8.4)
                m_change_pending = false;
            }
            // the signal names the namespace, the key and the value (§8.4)
            DBusMessage* signal = dbus_message_new_signal(PORTAL_PATH, SETTINGS_IFACE, "SettingChanged");
            // a message that could not be built reports no change (§8.4)
            if (signal == nullptr)
                return;
            // the arguments are opened on the signal (§8.4)
            DBusMessageIter arguments;
            // the signal carries the setting it changes (§8.4)
            dbus_message_iter_init_append(signal, &arguments);
            // the namespace is the appearance one, which a window reads (§8.4)
            const char* space = "org.freedesktop.appearance";
            // the namespace is appended first (§8.4)
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &space);
            // the key names the colour scheme (§8.4)
            const char* key = "color-scheme";
            // the key is appended second (§8.4)
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &key);
            // the value is the numeric scheme the desktop now prefers (§8.4)
            DBusMessageIter value;
            // the variant is opened on the signal (§8.4)
            dbus_message_iter_open_container(&arguments, DBUS_TYPE_VARIANT, "u", &value);
            // the scheme the test queued is what the observers mirror (§8.4)
            const std::uint32_t reported = *scheme;
            // the number is appended to the variant (§8.4)
            dbus_message_iter_append_basic(&value, DBUS_TYPE_UINT32, &reported);
            // the variant is closed before the signal is sent (§8.4)
            dbus_message_iter_close_container(&arguments, &value);
            // the signal is sent on the bus the client listens to (§8.4)
            dbus_connection_send(m_connection, signal, nullptr);
            // the signal is flushed, so the client reads it on its next dispatch (§8.4)
            dbus_connection_flush(m_connection);
            // the signal is released once it is sent (§8.4)
            dbus_message_unref(signal);
        }

        // answer one message, which is either a panel request or a settings read (§5.3, §8.4)
        void handle(DBusMessage* message) {
            // a message that is not a method call names no request (§5.3)
            if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
                return;
            // the interface names the service the client asked (§5.3)
            const char* interface = dbus_message_get_interface(message);
            // the member names the request itself (§5.3)
            const char* member = dbus_message_get_member(message);
            // a message without an interface or a member names nothing (§5.3)
            if (interface == nullptr || member == nullptr)
                return;
            // the interface is what decides how the request is answered (§5.3)
            const std::string_view name(interface);
            // a file chooser request runs one panel (§5.3)
            if (name == "org.freedesktop.portal.FileChooser" && (std::string_view(member) == "OpenFile" || std::string_view(member) == "SaveFile")) {
                run_panel(message);
                return;
            }
            // a settings read answers the configured scheme (§8.4)
            if (name == SETTINGS_IFACE) {
                // the method names the shape the value is answered in (§8.4)
                const std::string_view method(member);
                // whether this fake answers as a version 1 portal, which lacks the newer method (§8.4)
                bool refuses_read_one = false;
                // the lock keeps the test from writing while the flag is read (§8.4)
                {
                    // the lock guards the version flag (§8.4)
                    const std::lock_guard<std::mutex> guard(m_mutex);
                    // the flag decides whether the newer method fails (§8.4)
                    refuses_read_one = m_refuse_read_one;
                }
                // a version 1 portal reports the newer method as missing (§8.4)
                if (refuses_read_one && method == "ReadOne") {
                    refuse_method(message);
                    return;
                }
                // one of the two read methods answers the scheme (§8.4)
                if (method == "ReadOne" || method == "Read")
                    answer_scheme(message);
            }
        }

        // answer the newer settings read with the error a version 1 portal reports (§8.4)
        void refuse_method(DBusMessage* message) {
            // the error names the method this interface lacks (§8.4)
            DBusMessage* reply = dbus_message_new_error(message, "org.freedesktop.DBus.Error.UnknownMethod", "the settings interface has no such method");
            // a reply that could not be built answers nothing (§8.4)
            if (reply == nullptr)
                return;
            // the reply is sent on the bus the client reads (§8.4)
            dbus_connection_send(m_connection, reply, nullptr);
            // the reply is flushed, so the client learns the method is missing (§8.4)
            dbus_connection_flush(m_connection);
            // the reply is released once it is sent (§8.4)
            dbus_message_unref(reply);
        }

        // read the request options and answer the panel (§5.3)
        void run_panel(DBusMessage* message) {
            // a change the test queued is reported before the panel answers, which the modal wait dispatches (§8.4)
            send_pending_change();
            // the arguments carry the parent, the title and the options (§5.3)
            DBusMessageIter arguments;
            // a request with no arguments names no panel (§5.3)
            if (!dbus_message_iter_init(message, &arguments))
                return;
            // the parent is the first string the request carries (§5.3)
            const char* parent = nullptr;
            // the parent is read at the start of the argument list (§5.3)
            if (dbus_message_iter_get_arg_type(&arguments) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&arguments, &parent);
            // the title follows the parent (§5.3)
            if (!dbus_message_iter_next(&arguments) || dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_STRING)
                return;
            // the title is the second string the request carries (§5.3)
            const char* title = nullptr;
            // the title text is read from the request (§5.3)
            dbus_message_iter_get_basic(&arguments, &title);
            // the options follow the title (§5.3)
            if (!dbus_message_iter_next(&arguments) || dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_ARRAY)
                return;
            // the token names the path the response is emitted on (§5.3)
            std::string token;
            // whether the portal names its own handle, which the output options decide (§5.3)
            bool ignore_token = false;
            // whether the bus is stopped as soon as the request arrives (§5.3)
            bool stop_with_panel = false;
            // the parent, the title and the recorded options are replaced by this request (§5.3)
            {
                // the lock keeps the test from reading a half written request (§5.3)
                const std::lock_guard<std::mutex> guard(m_mutex);
                // the parent is what the client named (§5.3)
                m_parent = parent != nullptr ? parent : "";
                // the title is what the client named (§5.3)
                m_title = title != nullptr ? title : "";
                // the folder, the name and the filters are rebuilt by the options walk (§5.3)
                m_folder.clear();
                // the proposed name is rebuilt by the options walk (§5.3)
                m_name.clear();
                // the filters are rebuilt by the options walk (§5.3)
                m_filters.clear();
                // the multiple selection flag is rebuilt by the options walk (§5.3)
                m_multiple = false;
                // the panel is counted once its request arrived (§5.3)
                m_panel_count++;
                // the flag decides whether the token is honoured (§5.3)
                ignore_token = m_ignore_token;
                // the flag decides whether this request ends the bus (§5.3)
                stop_with_panel = m_stop_with_panel;
            }
            // a bus that goes away leaves the panel unanswered, which the client reads as a refusal (§5.3)
            if (stop_with_panel) {
                // the daemon is stopped before the panel answers, which no reply reaches (§5.3)
                stop_bus();
                return;
            }
            // the options are a dictionary the request walk reads (§5.3)
            DBusMessageIter options;
            // the dictionary starts at its first entry (§5.3)
            dbus_message_iter_recurse(&arguments, &options);
            // every entry names one option the panel received (§5.3)
            while (dbus_message_iter_get_arg_type(&options) == DBUS_TYPE_DICT_ENTRY) {
                // one entry carries the key and its variant (§5.3)
                DBusMessageIter entry;
                // the entry is opened at its key (§5.3)
                dbus_message_iter_recurse(&options, &entry);
                // the key names the option the client wrote (§5.3)
                const char* key = nullptr;
                // a key that is not a string names no option (§5.3)
                if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&entry, &key);
                // the value follows the key, where an entry ever carries one (§5.3)
                if (key != nullptr && dbus_message_iter_next(&entry) && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                    // the variant holds the value the option names (§5.3)
                    DBusMessageIter value;
                    // the value is opened at its payload (§5.3)
                    dbus_message_iter_recurse(&entry, &value);
                    // the option key decides how the value is read (§5.3)
                    const std::string_view option(key);
                    // the token is what the response path is built from (§5.3)
                    if (option == "handle_token" && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
                        // the token text is read from the request (§5.3)
                        const char* text = nullptr;
                        // the token is the string the client wrote (§5.3)
                        dbus_message_iter_get_basic(&value, &text);
                        // a token that arrived is what names the request path (§5.3)
                        if (text != nullptr)
                            token = text;
                    }
                    // the multiple flag is what the single path contract refuses (§5.3)
                    else if (option == "multiple" && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BOOLEAN) {
                        // the flag is read from the request (§5.3)
                        dbus_bool_t flag = FALSE;
                        // the number is what the client wrote (§5.3)
                        dbus_message_iter_get_basic(&value, &flag);
                        // the flag is recorded for the assertion (§5.3)
                        const std::lock_guard<std::mutex> guard(m_mutex);
                        // the client must refuse a multiple selection (§5.3)
                        m_multiple = flag != FALSE;
                    }
                    // the folder is a nul terminated byte string (§5.3)
                    else if (option == "current_folder" && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_ARRAY) {
                        // the bytes carry the utf8 path and its terminator (§5.3)
                        DBusMessageIter bytes;
                        // the array starts at its first byte (§5.3)
                        dbus_message_iter_recurse(&value, &bytes);
                        // the folder is rebuilt from the bytes (§5.3)
                        std::string folder;
                        // every byte until the terminator belongs to the path (§5.3)
                        while (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE) {
                            // the byte is one character of the path (§5.3)
                            unsigned char byte = 0;
                            // the byte is read from the array (§5.3)
                            dbus_message_iter_get_basic(&bytes, &byte);
                            // the terminator ends the path (§5.3)
                            if (byte == 0)
                                break;
                            // the byte is appended to the path (§5.3)
                            folder.push_back(static_cast<char>(byte));
                            // the next byte may still belong to the path (§5.3)
                            dbus_message_iter_next(&bytes);
                        }
                        // the lock keeps the test from reading a half written path (§5.3)
                        const std::lock_guard<std::mutex> guard(m_mutex);
                        // the folder is what the panel opens at (§5.3)
                        m_folder = std::move(folder);
                    }
                    // the proposed name is a string (§5.3)
                    else if (option == "current_name" && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
                        // the name text is read from the request (§5.3)
                        const char* text = nullptr;
                        // the name is the string the client wrote (§5.3)
                        dbus_message_iter_get_basic(&value, &text);
                        // the lock keeps the test from reading a half written name (§5.3)
                        const std::lock_guard<std::mutex> guard(m_mutex);
                        // the name is what the save panel proposes (§5.3)
                        m_name = text != nullptr ? text : "";
                    }
                    // the filters are one row per filter, each with its patterns (§5.3)
                    else if (option == "filters" && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_ARRAY) {
                        // the rows are what the panel offers (§5.3)
                        DBusMessageIter rows;
                        // the array starts at its first row (§5.3)
                        dbus_message_iter_recurse(&value, &rows);
                        // the filters are rebuilt from the rows (§5.3)
                        std::vector<std::string> filters;
                        // every row names one filter and its patterns (§5.3)
                        while (dbus_message_iter_get_arg_type(&rows) == DBUS_TYPE_STRUCT) {
                            // the row carries the label and the patterns (§5.3)
                            DBusMessageIter row;
                            // the struct starts at its label (§5.3)
                            dbus_message_iter_recurse(&rows, &row);
                            // the label is what the panel shows (§5.3)
                            const char* label = nullptr;
                            // the label is the first string in the row (§5.3)
                            if (dbus_message_iter_get_arg_type(&row) == DBUS_TYPE_STRING)
                                dbus_message_iter_get_basic(&row, &label);
                            // the spelled filter starts with the label (§5.3)
                            std::string spelled = label != nullptr ? label : "";
                            // the patterns follow the label (§5.3)
                            if (dbus_message_iter_next(&row) && dbus_message_iter_get_arg_type(&row) == DBUS_TYPE_ARRAY) {
                                // the patterns are what the filter accepts (§5.3)
                                DBusMessageIter patterns;
                                // the array starts at its first pattern (§5.3)
                                dbus_message_iter_recurse(&row, &patterns);
                                // every pattern is one kind and one glob (§5.3)
                                while (dbus_message_iter_get_arg_type(&patterns) == DBUS_TYPE_STRUCT) {
                                    // the pattern carries the kind and the glob (§5.3)
                                    DBusMessageIter pattern;
                                    // the struct starts at its kind (§5.3)
                                    dbus_message_iter_recurse(&patterns, &pattern);
                                    // the glob follows the kind (§5.3)
                                    if (dbus_message_iter_next(&pattern) && dbus_message_iter_get_arg_type(&pattern) == DBUS_TYPE_STRING) {
                                        // the glob is what the panel matches (§5.3)
                                        const char* glob = nullptr;
                                        // the glob is the string the client wrote (§5.3)
                                        dbus_message_iter_get_basic(&pattern, &glob);
                                        // the separator is what the assertion reads (§5.3)
                                        spelled += glob != nullptr ? ";" : "";
                                        // the glob is appended to the filter (§5.3)
                                        spelled += glob != nullptr ? glob : "";
                                    }
                                    // the next pattern may belong to the same filter (§5.3)
                                    dbus_message_iter_next(&patterns);
                                }
                            }
                            // the spelled filter is one row the test compares (§5.3)
                            filters.push_back(std::move(spelled));
                            // the next row may name another filter (§5.3)
                            dbus_message_iter_next(&rows);
                        }
                        // the lock keeps the test from reading half written filters (§5.3)
                        const std::lock_guard<std::mutex> guard(m_mutex);
                        // the filters are what the panel offers (§5.3)
                        m_filters = std::move(filters);
                    }
                }
                // the next entry may name another option (§5.3)
                dbus_message_iter_next(&options);
            }
            // the request path is what the portal derives from the token and the sender (§5.3)
            const char* sender = dbus_message_get_sender(message);
            // the sender is the unique name the request came from (§5.3)
            std::string sender_key = sender != nullptr ? sender : "";
            // a leading colon is what the unique name carries and the path does not (§5.3)
            if (!sender_key.empty() && sender_key.front() == ':')
                sender_key.erase(sender_key.begin());
            // a dot is not a path element, so it becomes an underscore (§5.3)
            for (char& character : sender_key) {
                // a dot is the only character the path replaces (§5.3)
                if (character == '.')
                    character = '_';
            }
            // the path is the fixed prefix, the sender and the token (§5.3)
            const std::string path = std::string(PORTAL_PATH) + "/request/" + sender_key + "/" + (ignore_token ? "portalhandle" + std::to_string(m_panel_count) : token);
            // the reply names the request handle the client waits on (§5.3)
            DBusMessage* reply = dbus_message_new_method_return(message);
            // a handle that could not be built leaves no request to answer (§5.3)
            if (reply == nullptr)
                return;
            // the path text is what the reply carries (§5.3)
            const char* path_text = path.c_str();
            // the handle is appended to the reply (§5.3)
            dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path_text, DBUS_TYPE_INVALID);
            // the reply is sent before the response, so the client knows the path already (§5.3)
            dbus_connection_send(m_connection, reply, nullptr);
            // the reply is flushed, so the client reads it on its blocking call (§5.3)
            dbus_connection_flush(m_connection);
            // the reply is released once it is sent (§5.3)
            dbus_message_unref(reply);
            // a handle the portal named itself is answered after the client subscribed to it (§5.3)
            if (ignore_token) {
                // the lock keeps the test from reading a half written handle (§5.3)
                const std::lock_guard<std::mutex> guard(m_mutex);
                // the response follows on the next pass, which leaves the client time to subscribe (§5.3)
                m_deferred_response = path;
                // the pass that queued the response is the one to skip (§5.3)
                m_deferred_delay = 1;
                return;
            }
            // the response is what ends the panel the client runs (§5.3)
            emit_response(path);
        }

        // emit the outcome the test configured for the panel (§5.3)
        void emit_response(const std::string& path) {
            // the outcome and the uri are read under the lock the test writes through (§5.3)
            std::uint32_t outcome = 0;
            // the uri is what a successful panel names (§5.3)
            std::optional<std::string> uri;
            // the lock keeps the test from writing while the response is built (§5.3)
            {
                // the lock guards the configured answer (§5.3)
                const std::lock_guard<std::mutex> guard(m_mutex);
                // the outcome is what the test configured (§5.3)
                outcome = m_outcome;
                // the uri is what a successful panel names (§5.3)
                uri = m_uri;
            }
            // the signal is the response the client matches (§5.3)
            DBusMessage* signal = dbus_message_new_signal(path.c_str(), REQUEST_IFACE, "Response");
            // a signal that could not be built answers nothing (§5.3)
            if (signal == nullptr)
                return;
            // the arguments carry the outcome and the results (§5.3)
            DBusMessageIter arguments;
            // the signal starts at its outcome (§5.3)
            dbus_message_iter_init_append(signal, &arguments);
            // the outcome is the first number the response carries (§5.3)
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &outcome);
            // the results are the dictionary a success fills (§5.3)
            DBusMessageIter results;
            // the dictionary is opened on the arguments (§5.3)
            dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &results);
            // a success names the chosen uri, which a cancel or an empty answer leaves out (§5.3)
            if (outcome == 0 && uri.has_value()) {
                // one entry carries the uris the panel chose (§5.3)
                DBusMessageIter entry;
                // the entry is opened inside the results (§5.3)
                dbus_message_iter_open_container(&results, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
                // the key names the uri list the client reads (§5.3)
                const char* key = "uris";
                // the key is appended to the entry (§5.3)
                dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
                // the value is the uri list, which the client decodes (§5.3)
                DBusMessageIter variant;
                // the variant is opened inside the entry (§5.3)
                dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
                // the list carries one uri, which is the single selection contract (§5.3)
                DBusMessageIter uris;
                // the array is opened inside the variant (§5.3)
                dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &uris);
                // the uri text is what the panel chose (§5.3)
                const char* text = uri->c_str();
                // the uri is appended to the list (§5.3)
                dbus_message_iter_append_basic(&uris, DBUS_TYPE_STRING, &text);
                // the list is closed before its variant (§5.3)
                dbus_message_iter_close_container(&variant, &uris);
                // the variant is closed before its entry (§5.3)
                dbus_message_iter_close_container(&entry, &variant);
                // the entry is closed before the results continue (§5.3)
                dbus_message_iter_close_container(&results, &entry);
            }
            // the results are closed before the signal is sent (§5.3)
            dbus_message_iter_close_container(&arguments, &results);
            // the signal is sent on the bus the client listens to (§5.3)
            dbus_connection_send(m_connection, signal, nullptr);
            // the signal is flushed, so the client reads it on its next dispatch (§5.3)
            dbus_connection_flush(m_connection);
            // the signal is released once it is sent (§5.3)
            dbus_message_unref(signal);
        }

        // answer one settings read with the configured scheme (§8.4)
        void answer_scheme(DBusMessage* message) {
            // the value is read under the lock the test writes through (§8.4)
            std::uint32_t scheme = 0;
            // the lock keeps the test from writing while the reply is built (§8.4)
            {
                // the lock guards the configured scheme (§8.4)
                const std::lock_guard<std::mutex> guard(m_mutex);
                // the scheme is what the test configured (§8.4)
                scheme = m_scheme;
            }
            // the reply is what a settings read answers (§8.4)
            DBusMessage* reply = dbus_message_new_method_return(message);
            // a reply that could not be built answers nothing (§8.4)
            if (reply == nullptr)
                return;
            // the arguments carry the value as one variant (§8.4)
            DBusMessageIter arguments;
            // the reply starts at its value (§8.4)
            dbus_message_iter_init_append(reply, &arguments);
            // the deprecated read wraps its value in a second variant, which the portal documents (§8.4)
            const bool nested = std::string_view(dbus_message_get_member(message)) == "Read";
            // the outer variant is opened only where the deprecated method answers (§8.4)
            DBusMessageIter outer;
            // the deprecated method names its outer variant as a variant (§8.4)
            if (nested)
                dbus_message_iter_open_container(&arguments, DBUS_TYPE_VARIANT, "v", &outer);
            // the value is the numeric scheme the desktop prefers (§8.4)
            DBusMessageIter variant;
            // the variant is opened on the outer one, or on the arguments for the newer method (§8.4)
            dbus_message_iter_open_container(nested ? &outer : &arguments, DBUS_TYPE_VARIANT, "u", &variant);
            // the number is what the window reports (§8.4)
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &scheme);
            // the variant is closed before its outer one (§8.4)
            dbus_message_iter_close_container(nested ? &outer : &arguments, &variant);
            // the outer variant is closed before the reply is sent (§8.4)
            if (nested)
                dbus_message_iter_close_container(&arguments, &outer);
            // the reply is sent on the bus the client reads (§8.4)
            dbus_connection_send(m_connection, reply, nullptr);
            // the reply is flushed, so the client reads it on its blocking call (§8.4)
            dbus_connection_flush(m_connection);
            // the reply is released once it is sent (§8.4)
            dbus_message_unref(reply);
        }

        // the private bus process, which the fixture stops when it ends (§5.3)
        pid_t m_daemon = -1;

        // the address the private bus printed, which the client connects to (§5.3)
        std::string m_address;

        // the config the private bus was started with, which names no service directory (§5.3)
        std::string m_config_path;

        // the connection the service answers on (§5.3)
        DBusConnection* m_connection = nullptr;

        // the thread the service runs on (§5.3)
        std::thread m_thread;

        // whether the service is up, which a test asserts before it arranges a request (§5.3)
        bool m_ready = false;

        // whether the service thread keeps serving, which the fixture clears when it ends (§5.3)
        std::atomic<bool> m_running{true};

        // the lock the test and the service share their values through (§5.3)
        mutable std::mutex m_mutex;

        // the outcome the next panel reports, where one is a cancel (§5.3)
        std::uint32_t m_outcome = 0;

        // the uri the next successful panel names, where it names one (§5.3)
        std::optional<std::string> m_uri;

        // the colour scheme a settings read answers (§8.4)
        std::uint32_t m_scheme = 0;

        // whether the settings read answers as a version 1 portal, which lacks the newer method (§8.4)
        bool m_refuse_read_one = false;

        // whether the portal names its own handle, ignoring the token the client sent (§5.3)
        bool m_ignore_token = false;

        // whether the next panel request stops the bus it arrived on (§5.3)
        bool m_stop_with_panel = false;

        // the handle a deferred response answers on, which a named handle needs (§5.3)
        std::optional<std::string> m_deferred_response;

        // how many service passes are left before a deferred response is sent (§5.3)
        int m_deferred_delay = 0;

        // whether the test queued a scheme change, which the service thread sends (§8.4)
        bool m_change_pending = false;

        // the parent the last panel carried (§5.3)
        std::string m_parent;

        // the title the last panel carried (§5.3)
        std::string m_title;

        // the initial folder the last panel carried (§5.3)
        std::string m_folder;

        // the proposed name the last save panel carried (§5.3)
        std::string m_name;

        // the filters the last panel carried, each spelled as "name;glob;glob" (§5.3)
        std::vector<std::string> m_filters;

        // whether the last panel allowed a multiple selection (§5.3)
        bool m_multiple = false;

        // how many panels the service ran (§5.3)
        std::size_t m_panel_count = 0;
    };

    // the listener a window reports its appearance through, which the framework never owns (§5.3)
    class AppearanceListener final : public PlatformWindowListener
    {
    public:
        // every appearance change the platform reported, which a setting change raises (§8.4)
        std::size_t appearance_changes = 0;

        // the scheme of the last change, which the test compares (§8.4)
        ColorScheme last_scheme = ColorScheme::Dark;

        // override: count the resize, which a show raises (§5.3)
        void on_platform_resize(const WindowMetrics&) override {}

        // override: grant every close a compositor asks for (§5.2)
        bool on_platform_close_request() override { return true; }

        // override: ignore the close, which this suite does not drive (§5.2)
        void on_platform_closed() override {}

        // override: ignore the frame, which this suite does not pace (§4.3)
        void on_platform_frame() override {}

        // override: ignore the input, which this suite does not inject (§8.3)
        void on_platform_event(const Event&) override {}

        // override: ignore the focus loss, which this suite does not drive (§8.3)
        void on_platform_focus_lost() override {}

        // override: ignore the pointer exit, which this suite does not drive (§8.3)
        void on_platform_pointer_exited() override {}

        // override: count the appearance change and record its scheme (§8.4)
        void on_platform_appearance_changed(const SystemAppearance& appearance) override {
            appearance_changes++;
            last_scheme = appearance.scheme;
        }
    };
} // namespace

TEST(PortalFileDialog, AnOpenPanelCarriesTheRequestedOptions) {
    // arrange a private bus with a portal that answers one file (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch", "kicad_pcb"}}, FileFilter{.name = "Any", .extensions = {}}};
    options.initial_directory = "/tmp/netlist";
    portal.answer_with("file:///tmp/netlist/board.kicad_sch");
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert the chosen file reached the caller and every option reached the panel (§5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->string(), "/tmp/netlist/board.kicad_sch");
    EXPECT_EQ(portal.title(), "Open netlist");
    EXPECT_EQ(portal.folder(), "/tmp/netlist");
    ASSERT_EQ(portal.filters().size(), 1U);
    EXPECT_EQ(portal.filters().at(0), "Netlists;*.kicad_sch;*.kicad_pcb");
    EXPECT_FALSE(portal.multiple());
    EXPECT_EQ(portal.parent(), "");
    EXPECT_EQ(portal.panels(), 1U);
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, ASavePanelCarriesTheProposedName) {
    // arrange a private bus with a portal that answers one saved file (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    SaveFileOptions options;
    options.title = "Save netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}};
    options.initial_directory = "/tmp/netlist";
    options.default_name = "board.kicad_sch";
    portal.answer_with("file:///tmp/netlist/board.kicad_sch");
    // act
    const std::optional<std::filesystem::path> chosen = dialog->save(options, NativeWindowHandle{});
    // assert the save panel carried the proposed name and answered the chosen path (§5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->string(), "/tmp/netlist/board.kicad_sch");
    EXPECT_EQ(portal.default_name(), "board.kicad_sch");
    EXPECT_EQ(portal.title(), "Save netlist");
    EXPECT_EQ(portal.folder(), "/tmp/netlist");
    ASSERT_EQ(portal.filters().size(), 1U);
    EXPECT_EQ(portal.filters().at(0), "Netlists;*.kicad_sch");
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, ACancelledPanelReportsNoPath) {
    // arrange a private bus with a portal that reports a cancel (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    portal.answer_cancelled();
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert a cancel is reported as no path rather than an empty one (§5.3)
    EXPECT_FALSE(chosen.has_value());
    EXPECT_EQ(portal.panels(), 1U);
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, AnEmptyResultIsNotAPath) {
    // arrange a private bus with a portal that reports a success without a uri (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    portal.answer_empty();
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert an empty result is refused rather than reported as an empty path (§5.3)
    EXPECT_FALSE(chosen.has_value());
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, APathWithSpacesDecodesFromItsUri) {
    // arrange a private bus with a portal that answers an escaped uri (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    portal.answer_with("file:///tmp/netlist/bo%C3%AErd%20copy.kicad_sch");
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert the escaped bytes reached the caller as the path the user chose (§5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->string(), "/tmp/netlist/bo\xC3\xAErd copy.kicad_sch");
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, AParentedPanelCarriesTheExportedWindow) {
    // arrange a private bus with a portal that answers one file (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the window the panel is owned by is a real wayland toplevel (§5.1)
    WindowOptions window_options;
    window_options.title = "albor-portal-parent";
    window_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(window_options);
    ASSERT_NE(window, nullptr);
    window->show();
    // the identifier is what the compositor exported, which a compositor without xdg-foreign leaves empty (§5.3)
    const std::string parent = portal_parent_handle(window->native_window_handle());
    if (!parent.empty()) {
        EXPECT_EQ(parent.rfind("wayland:", 0), 0U);
    }
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    portal.answer_with("file:///tmp/netlist/board.kicad_sch");
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, window->native_window_handle());
    // assert the panel named exactly the window the compositor exported (§5.3)
    EXPECT_TRUE(chosen.has_value());
    EXPECT_EQ(portal.parent(), parent);
    window->close();
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, AHostWithoutAPortalRefuses) {
    // arrange an address no bus listens on, which reports the portal unavailable (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-albor-portal-bus", 1);
    const std::shared_ptr<PortalClient> portal = PortalClient::shared();
    // assert the portal reports itself unavailable, which a host without one does (§5.3)
    EXPECT_FALSE(portal->available());
    // assert the portable dark scheme stands in for the missing setting (§8.4)
    EXPECT_EQ(portal->color_scheme(), ColorScheme::Dark);
    // the installed backend still refuses a panel rather than crashing (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert the refusal is reported as no path (§5.3)
    EXPECT_FALSE(chosen.has_value());
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, ABusWithoutAPortalRefusesThePanel) {
    // arrange a private bus no portal service claims, which a desktop without one exports (§5.3)
    FakePortal portal(false);
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the bus is reached, so the client is available and the portal decides the request (§5.3)
    const std::shared_ptr<PortalClient> client = PortalClient::shared();
    EXPECT_TRUE(client->available());
    // the panel is requested through the same client, which the missing service refuses (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert the missing portal is reported as no path rather than a crash (§5.3)
    EXPECT_FALSE(chosen.has_value());
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, APortalThatNamesItsOwnHandleStillAnswers) {
    // arrange a private bus whose portal ignores the token it was given (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    portal.ignore_handle_token();
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    portal.answer_with("file:///tmp/netlist/board.kicad_sch");
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert the handle the portal named is followed, which the client learns from the reply (§5.3)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->string(), "/tmp/netlist/board.kicad_sch");
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalFileDialog, ALostBusEndsThePanel) {
    // arrange a private bus whose portal goes away when the request arrives (§5.3)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    portal.stop_bus_with_next_panel();
    // the environment names the private bus, which the client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    // the client is held across the panel, so the disconnect it notices is what the test reads (§5.3)
    const std::shared_ptr<PortalClient> client = PortalClient::shared();
    // act
    const std::optional<std::filesystem::path> chosen = dialog->open(options, NativeWindowHandle{});
    // assert the dead bus ends the panel with a refusal rather than blocking the ui thread (§5.3)
    EXPECT_FALSE(chosen.has_value());
    // assert the dropped bus leaves the client unavailable, which the loop stops polling (§5.3)
    client->dispatch();
    EXPECT_FALSE(client->available());
    EXPECT_EQ(client->fd(), -1);
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalAppearance, APortalWithOnlyTheOldReadReportsTheScheme) {
    // arrange a private bus whose settings interface predates the newer read (§8.4)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    portal.refuse_read_one();
    portal.set_color_scheme(2);
    // the environment names the private bus, which the window's client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the window is a real wayland toplevel, which follows the desktop's scheme (§5.1)
    WindowOptions window_options;
    window_options.title = "albor-portal-old-settings";
    window_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(window_options);
    ASSERT_NE(window, nullptr);
    window->show();
    // act
    const SystemAppearance reported = window->system_appearance();
    // assert the deprecated read is decoded, which a version 1 portal answers with twice a variant (§8.4)
    EXPECT_EQ(reported.scheme, ColorScheme::Light);
    window->close();
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalAppearance, AChangeDuringAPanelReachesTheListener) {
    // arrange a private bus whose portal reports a change while a panel runs (§8.4)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    portal.set_color_scheme(2);
    // the environment names the private bus, which the window's client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the window is a real wayland toplevel, which follows the desktop's scheme (§5.1)
    WindowOptions window_options;
    window_options.title = "albor-portal-panel-change";
    window_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(window_options);
    ASSERT_NE(window, nullptr);
    window->show();
    // the listener counts the change the desktop reports next (§8.4)
    AppearanceListener listener;
    window->set_listener(&listener);
    // the installed backend is the portal one, which the platform installer registered at load (§5.3)
    ASSERT_TRUE(platform_file_dialog_factory());
    const std::unique_ptr<PlatformFileDialog> dialog = platform_file_dialog_factory()();
    ASSERT_NE(dialog, nullptr);
    OpenFileOptions options;
    options.title = "Open netlist";
    portal.answer_with("file:///tmp/netlist/board.kicad_sch");
    portal.emit_color_scheme(1);
    // act the panel blocks the ui thread, which the change is dispatched through (§8.4)
    const std::optional<std::filesystem::path> chosen = dialog->open(options, window->native_window_handle());
    // assert the change raised behind the panel reached the window listener (§8.4)
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(listener.appearance_changes, 1U);
    EXPECT_EQ(listener.last_scheme, ColorScheme::Dark);
    EXPECT_EQ(window->system_appearance().scheme, ColorScheme::Dark);
    window->close();
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalAppearance, TheWindowReportsThePortalColourScheme) {
    // arrange a private bus whose portal prefers the light scheme (§8.4)
    FakePortal portal;
    if (!portal.ready())
        GTEST_SKIP() << "dbus-daemon is not available, so the portal suite cannot run";
    portal.set_color_scheme(2);
    // the environment names the private bus, which the window's client connects to (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", portal.address().c_str(), 1);
    // the window is a real wayland toplevel, which follows the desktop's scheme (§5.1)
    WindowOptions window_options;
    window_options.title = "albor-portal-appearance";
    window_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(window_options);
    ASSERT_NE(window, nullptr);
    window->show();
    // act
    const SystemAppearance reported = window->system_appearance();
    // assert the window reports the scheme the desktop advertised (§8.4)
    EXPECT_EQ(reported.scheme, ColorScheme::Light);
    // the listener counts the change the desktop reports next (§8.4)
    AppearanceListener listener;
    window->set_listener(&listener);
    portal.emit_color_scheme(1);
    // the loop dispatches the portal on the ui thread, which the test drives here (§8.4)
    const std::shared_ptr<PortalClient> client = PortalClient::shared();
    for (int attempt = 0; attempt < 200 && listener.appearance_changes == 0; attempt++) {
        client->dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // assert the change reached the window and the reported scheme followed it (§8.4)
    EXPECT_EQ(listener.appearance_changes, 1U);
    EXPECT_EQ(listener.last_scheme, ColorScheme::Dark);
    EXPECT_EQ(window->system_appearance().scheme, ColorScheme::Dark);
    window->close();
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}

TEST(PortalAppearance, AWindowWithoutAPortalReportsTheDarkDefault) {
    // arrange an address no bus listens on, which leaves the portal unavailable (§5.3)
    const char* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-albor-portal-bus", 1);
    // the window is a real wayland toplevel, which reports the portable default (§5.1)
    WindowOptions window_options;
    window_options.title = "albor-portal-default";
    window_options.size = Size{240.0f, 160.0f};
    const std::unique_ptr<PlatformWindow> window = PlatformWindow::create(window_options);
    ASSERT_NE(window, nullptr);
    // act
    const SystemAppearance reported = window->system_appearance();
    // assert the portable dark scheme stands in, without a crash on the missing portal (§5.3, §8.4)
    EXPECT_EQ(reported.scheme, ColorScheme::Dark);
    window->close();
    // the environment is restored, so a later suite reads the session bus it started with (§5.3)
    if (saved.empty())
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    else
        setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
}
