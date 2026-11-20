#include "albor/platform/linux/wayland/portal.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dbus/dbus.h>

#include "albor/core/env.h"

namespace albor::wayland
{
    // the portal state, defined here so that no dbus type reaches the header (§3.5, AD-05)
    struct PortalState
    {
        // the private session bus connection, which the client closes with the state (§5.3)
        DBusConnection* connection = nullptr;

        // the bus address the client was built for, which the process cache keys on (§5.3)
        std::string address;

        // the unique name the bus assigned, which the request path is built from (§5.3)
        std::string unique_name;

        // the observers the colour scheme is reported to, which a window installs (§8.4)
        std::map<std::uint64_t, std::function<void(ColorScheme)>> observers;

        // the identifier the next observer is registered under (§8.4)
        std::uint64_t next_observer = 1;

        // the counter that names the next request token, which keeps two panels apart (§5.3)
        std::uint64_t next_token = 1;

        // the colour scheme the desktop last reported, which the portable dark scheme stands in for (§8.4)
        ColorScheme scheme = ColorScheme::Dark;
    };
} // namespace albor::wayland

namespace albor::wayland
{
    namespace
    {
        // the bus name the desktop portal publishes, which every request addresses (§5.3)
        constexpr const char* PORTAL_NAME = "org.freedesktop.portal.Desktop";

        // the object every portal interface lives on (§5.3)
        constexpr const char* PORTAL_PATH = "/org/freedesktop/portal/desktop";

        // the interface that runs a file panel (§5.3)
        constexpr const char* FILE_CHOOSER_IFACE = "org.freedesktop.portal.FileChooser";

        // the interface that carries the desktop settings (§8.4)
        constexpr const char* SETTINGS_IFACE = "org.freedesktop.portal.Settings";

        // the interface a panel answers on, which carries the response signal (§5.3)
        constexpr const char* REQUEST_IFACE = "org.freedesktop.portal.Request";

        // the namespace the colour scheme lives under (§8.4)
        constexpr const char* APPEARANCE_NAMESPACE = "org.freedesktop.appearance";

        // the key the colour scheme is read by (§8.4)
        constexpr const char* COLOR_SCHEME_KEY = "color-scheme";

        // the number of milliseconds a panel request waits for its handle, which the portal answers at once (§5.3)
        constexpr int REQUEST_REPLY_TIMEOUT_MS = 25000;

        // the number of milliseconds a settings read waits, which a portal without the interface fails fast (§8.4)
        constexpr int SETTINGS_REPLY_TIMEOUT_MS = 2000;

        // the number of milliseconds one read waits, which bounds the loop while a panel runs (§5.3)
        constexpr int READ_TIMEOUT_MS = 20;

        // initialize libdbus for a process that also renders on another thread, which the library asks for once (§5.3)
        void init_threads() {
            // the once flag keeps the library initialization to a single call (§5.3)
            static std::once_flag once;
            // the call installs the default locks before any connection is shared across threads (§5.3)
            std::call_once(once, [] { dbus_threads_init_default(); });
        }

        // read the bus address the session resolves to, which an unset variable leaves empty (§5.3)
        std::string session_address() {
            // the environment names the bus a desktop session exports (§5.3)
            const std::optional<std::string> address = albor::core::get_environment_variable("DBUS_SESSION_BUS_ADDRESS");
            // an unset address leaves libdbus to resolve the session bus itself (§5.3)
            return address.value_or("");
        }

        // hold the clients already built, keyed by the bus address they connected to (§5.3)
        std::map<std::string, std::weak_ptr<PortalClient>>& client_cache() {
            // the cache lives for the process, so a second window shares the first connection (§5.3)
            static std::map<std::string, std::weak_ptr<PortalClient>> cache;
            return cache;
        }

        // open the private connection the portal traffic runs over, which an unreachable bus refuses (§5.3)
        DBusConnection* open_session_bus(const std::string& address, std::string& unique_name) {
            // the error is filled by a connection the bus refused (§5.3)
            DBusError error;
            dbus_error_init(&error);
            // the connection is built from the address the host exported, which the library would otherwise cache (§5.3)
            DBusConnection* connection = nullptr;
            // a host that exports an address is reached directly, so a restarted desktop bus is followed (§5.3)
            if (!address.empty()) {
                // a private connection keeps the portal messages away from other libdbus users in the process (§5.3)
                connection = dbus_connection_open_private(address.c_str(), &error);
                // a bus connection registers before it owns the unique name the request path is built from (§5.3)
                if (connection != nullptr && !dbus_bus_register(connection, &error)) {
                    // the connection is closed and released, because it cannot address the bus (§5.3)
                    dbus_connection_close(connection);
                    // the connection is released once it is closed (§5.3)
                    dbus_connection_unref(connection);
                    // a refused registration leaves nothing to talk over (§5.3)
                    connection = nullptr;
                }
            }
            else {
                // a host that exports no address leaves the library to resolve the session bus (§5.3)
                connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
            }
            // a bus that refused the connection leaves nothing to talk over (§5.3)
            if (connection == nullptr) {
                // the error is released and the refusal is reported by the null connection (§5.3)
                dbus_error_free(&error);
                return nullptr;
            }
            // the error is released whether the bus answered or refused (§5.3)
            if (dbus_error_is_set(&error))
                dbus_error_free(&error);
            // a dropped bus must not terminate the application, which a desktop restart can cause (§5.3)
            dbus_connection_set_exit_on_disconnect(connection, FALSE);
            // the unique name is what the request path is built from (§5.3)
            const char* unique = dbus_bus_get_unique_name(connection);
            // a bus that names no unique name still serves the connection (§5.3)
            if (unique != nullptr)
                unique_name = unique;
            return connection;
        }

        // send one message and read what arrives until its reply, which leaves every signal to the caller (§5.3)
        DBusMessage* wait_for_reply(PortalState& state, DBusMessage* message, int timeout_ms, const std::function<void()>& pump, const std::function<void(DBusMessage*)>& on_message) {
            // the serial names the reply this wait ends on (§5.3)
            dbus_uint32_t serial = 0;
            // a message the bus refused to queue leaves no reply to wait for (§5.3)
            if (!dbus_connection_send(state.connection, message, &serial))
                return nullptr;
            // the request is sent before the wait, so the portal may answer it (§5.3)
            dbus_connection_flush(state.connection);
            // the deadline bounds the wait, which a portal that never answers ends (§5.3)
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            // the loop ends on the reply, the deadline or a connection that went away (§5.3)
            while (std::chrono::steady_clock::now() < deadline) {
                // the caller pumps its own work, which a compositor ping needs answered while the portal runs (§5.3)
                if (pump)
                    pump();
                // the bus is read with a bound, so the deadline is rechecked without a long block (§5.3)
                if (!dbus_connection_read_write(state.connection, READ_TIMEOUT_MS))
                    return nullptr;
                // every message that arrived is read in order (§5.3)
                for (DBusMessage* incoming = dbus_connection_pop_message(state.connection); incoming != nullptr; incoming = dbus_connection_pop_message(state.connection)) {
                    // the reply to this call is handed back to the caller, which releases it (§5.3)
                    if (dbus_message_get_type(incoming) != DBUS_MESSAGE_TYPE_SIGNAL && dbus_message_get_reply_serial(incoming) == serial)
                        return incoming;
                    // every other message reaches the caller, so no signal is dispatched behind its back (§5.3)
                    if (on_message)
                        on_message(incoming);
                    // the message is released once the caller read it (§5.3)
                    dbus_message_unref(incoming);
                }
            }
            return nullptr;
        }

        // build the match rule that routes one panel response to this connection (§5.3)
        std::string response_rule(const std::string& path) {
            // the rule names the portal, the request interface and the handle the response answers on (§5.3)
            return std::string("type='signal',sender='") + PORTAL_NAME + "',interface='" + REQUEST_IFACE + "',member='Response',path='" + path + "'";
        }

        // build the object path a request answers on, which the portal derives from the token (§5.3)
        std::string request_path(const std::string& unique_name, const std::string& token) {
            // the sender is the unique name without its colon and with its dots replaced (§5.3)
            std::string sender = unique_name;
            // a leading colon is what the unique name carries and the path does not (§5.3)
            if (!sender.empty() && sender.front() == ':')
                sender.erase(sender.begin());
            // a dot is not a path element, so it becomes an underscore (§5.3)
            std::replace(sender.begin(), sender.end(), '.', '_');
            // the request path is the fixed prefix and the token the request named (§5.3)
            return std::string(PORTAL_PATH) + "/request/" + sender + "/" + token;
        }

        // append one string option to the options dictionary the portal reads (§5.3)
        void append_string_option(DBusMessageIter& options, const char* key, const std::string& value) {
            // one dictionary entry carries the key and its variant (§5.3)
            DBusMessageIter entry;
            // the entry is opened inside the options array (§5.3)
            dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            // the key names the option the portal reads (§5.3)
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            // the value is a string variant, which is the type the portal names (§5.3)
            DBusMessageIter variant;
            // the variant is opened inside the entry (§5.3)
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
            // the text is what the portal shows or reads (§5.3)
            const char* text = value.c_str();
            // the string is appended to the variant (§5.3)
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &text);
            // the variant is closed before its entry (§5.3)
            dbus_message_iter_close_container(&entry, &variant);
            // the entry is closed before the options array continues (§5.3)
            dbus_message_iter_close_container(&options, &entry);
        }

        // append one boolean option to the options dictionary the portal reads (§5.3)
        void append_bool_option(DBusMessageIter& options, const char* key, bool value) {
            // one dictionary entry carries the key and its variant (§5.3)
            DBusMessageIter entry;
            // the entry is opened inside the options array (§5.3)
            dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            // the key names the option the portal reads (§5.3)
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            // the value is a boolean variant, which is the type the portal names (§5.3)
            DBusMessageIter variant;
            // the variant is opened inside the entry (§5.3)
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
            // the flag is what the portal reads, where true is the nonzero value of the wire (§5.3)
            const dbus_bool_t flag = value ? TRUE : FALSE;
            // the flag is appended to the variant (§5.3)
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &flag);
            // the variant is closed before its entry (§5.3)
            dbus_message_iter_close_container(&entry, &variant);
            // the entry is closed before the options array continues (§5.3)
            dbus_message_iter_close_container(&options, &entry);
        }

        // append the nul terminated byte string the portal reads as a folder path (§5.3)
        void append_bytestring_option(DBusMessageIter& options, const char* key, const std::string& value) {
            // one dictionary entry carries the key and its variant (§5.3)
            DBusMessageIter entry;
            // the entry is opened inside the options array (§5.3)
            dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            // the key names the option the portal reads (§5.3)
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            // the value is a byte array variant, which is the type the portal names for a path (§5.3)
            DBusMessageIter variant;
            // the variant is opened inside the entry (§5.3)
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
            // the bytes are the path and its terminating zero, which the portal reads as a c string (§5.3)
            DBusMessageIter bytes;
            // the byte array is opened inside the variant (§5.3)
            dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &bytes);
            // the bytes are appended in one block, which the array accepts (§5.3)
            const char* data = value.c_str();
            // the terminating zero is part of the string the portal expects (§5.3)
            dbus_message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &data, static_cast<int>(value.size() + 1));
            // the array is closed before its variant (§5.3)
            dbus_message_iter_close_container(&variant, &bytes);
            // the variant is closed before its entry (§5.3)
            dbus_message_iter_close_container(&entry, &variant);
            // the entry is closed before the options array continues (§5.3)
            dbus_message_iter_close_container(&options, &entry);
        }

        // append the filter rows the portal shows, which spell each extension as a glob (§5.3)
        bool append_filters_option(DBusMessageIter& options, const std::vector<FileFilter>& filters) {
            // a filter with no extension names any file, which the portal shows by default (§5.3)
            bool any_pattern = false;
            // the first filter that names an extension is enough to send the option (§5.3)
            for (const FileFilter& filter : filters) {
                // a named extension is what the portal matches a file name against (§5.3)
                if (!filter.extensions.empty())
                    any_pattern = true;
            }
            // a request with no pattern leaves the option out, which means any file (§5.3)
            if (!any_pattern)
                return false;
            // one dictionary entry carries the key and its variant (§5.3)
            DBusMessageIter entry;
            // the entry is opened inside the options array (§5.3)
            dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            // the key names the option the portal reads (§5.3)
            const char* key = "filters";
            // the key is appended to the entry (§5.3)
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            // the value is the filter array variant, which is the type the portal names (§5.3)
            DBusMessageIter variant;
            // the variant is opened inside the entry (§5.3)
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
            // the rows are one struct per filter, which carries its name and its patterns (§5.3)
            DBusMessageIter rows;
            // the array is opened inside the variant (§5.3)
            dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &rows);
            // every filter becomes one row, in the order the author wrote them (§5.3)
            for (const FileFilter& filter : filters) {
                // a filter without a pattern names nothing the portal can match (§5.3)
                if (filter.extensions.empty())
                    continue;
                // the row carries the label and the patterns the filter names (§5.3)
                DBusMessageIter row;
                // the struct is opened inside the rows array (§5.3)
                dbus_message_iter_open_container(&rows, DBUS_TYPE_STRUCT, nullptr, &row);
                // the label is what the panel shows next to the pattern set (§5.3)
                const char* name = filter.name.c_str();
                // the label is appended to the row (§5.3)
                dbus_message_iter_append_basic(&row, DBUS_TYPE_STRING, &name);
                // the patterns are one struct per extension, which names its kind and its glob (§5.3)
                DBusMessageIter patterns;
                // the array is opened inside the row (§5.3)
                dbus_message_iter_open_container(&row, DBUS_TYPE_ARRAY, "(us)", &patterns);
                // every extension becomes one glob pattern (§5.3)
                for (const std::string& extension : filter.extensions) {
                    // the pattern carries the kind and the glob (§5.3)
                    DBusMessageIter pattern;
                    // the struct is opened inside the patterns array (§5.3)
                    dbus_message_iter_open_container(&patterns, DBUS_TYPE_STRUCT, nullptr, &pattern);
                    // zero names a glob, which is the kind this adapter writes (§5.3)
                    const dbus_uint32_t kind = 0;
                    // the kind is appended to the pattern (§5.3)
                    dbus_message_iter_append_basic(&pattern, DBUS_TYPE_UINT32, &kind);
                    // the glob is what the portal matches the file name against (§5.3)
                    const std::string glob = "*." + extension;
                    // the text points into the glob for the length of the call (§5.3)
                    const char* text = glob.c_str();
                    // the glob is appended to the pattern (§5.3)
                    dbus_message_iter_append_basic(&pattern, DBUS_TYPE_STRING, &text);
                    // the pattern is closed before the next one (§5.3)
                    dbus_message_iter_close_container(&patterns, &pattern);
                }
                // the patterns are closed before the row (§5.3)
                dbus_message_iter_close_container(&row, &patterns);
                // the row is closed before the next filter (§5.3)
                dbus_message_iter_close_container(&rows, &row);
            }
            // the rows are closed before their variant (§5.3)
            dbus_message_iter_close_container(&variant, &rows);
            // the variant is closed before its entry (§5.3)
            dbus_message_iter_close_container(&entry, &variant);
            // the entry is closed before the options array continues (§5.3)
            dbus_message_iter_close_container(&options, &entry);
            return true;
        }

        // read one hexadecimal digit, where a byte the character cannot name reports a negative value (§5.3)
        int hex_digit(char digit) {
            // a decimal digit is its own value (§5.3)
            if (digit >= '0' && digit <= '9')
                return digit - '0';
            // a lower case letter continues after nine (§5.3)
            if (digit >= 'a' && digit <= 'f')
                return digit - 'a' + 10;
            // an upper case letter names the same values (§5.3)
            if (digit >= 'A' && digit <= 'F')
                return digit - 'A' + 10;
            // a character that is not a hexadecimal digit names no byte (§5.3)
            return -1;
        }

        // decode the percent escapes of a file uri, which the portal spells every path as (§5.3)
        std::optional<std::filesystem::path> path_from_uri(std::string_view uri) {
            // the portal answers file uris, which is the only scheme the framework opens (§5.3)
            constexpr std::string_view PREFIX = "file://";
            // a uri of another scheme names no file path (§5.3)
            if (uri.substr(0, PREFIX.size()) != PREFIX)
                return std::nullopt;
            // the remainder is the authority and the path the uri carries (§5.3)
            std::string_view rest = uri.substr(PREFIX.size());
            // a portal writes an empty authority, so the path starts at the first slash (§5.3)
            if (!rest.empty() && rest.front() != '/') {
                // the authority ends at the first slash, which a host name would follow (§5.3)
                const std::size_t slash = rest.find('/');
                // a uri with no path after its authority names no file (§5.3)
                if (slash == std::string_view::npos)
                    return std::nullopt;
                // the path is what follows the authority (§5.3)
                rest.remove_prefix(slash);
            }
            // the decoded bytes are what the file system path is built from (§5.3)
            std::string decoded;
            // the escapes are decoded so a space or a non ascii byte reaches the caller (§5.3)
            for (std::size_t index = 0; index < rest.size(); index++) {
                // a percent names the two hex digits of one byte (§5.3)
                if (rest[index] == '%' && index + 2 < rest.size()) {
                    // the high digit starts the byte the escape names (§5.3)
                    const int high = hex_digit(rest[index + 1]);
                    // the low digit completes the byte the escape names (§5.3)
                    const int low = hex_digit(rest[index + 2]);
                    // a pair of digits is one byte of the path (§5.3)
                    if (high >= 0 && low >= 0) {
                        // the byte is what the percent escape stood for (§5.3)
                        decoded.push_back(static_cast<char>((high << 4) | low));
                        // the escape consumed three characters, which the loop skips (§5.3)
                        index += 2;
                        continue;
                    }
                }
                // a plain character is part of the path as it stands (§5.3)
                decoded.push_back(rest[index]);
            }
            // a uri that decodes to nothing names no file (§5.3)
            if (decoded.empty())
                return std::nullopt;
            return std::filesystem::path(decoded);
        }

        // read the colour scheme from one variant value, which the settings methods and signal carry (§8.4)
        std::optional<ColorScheme> scheme_from_variant(DBusMessageIter value) {
            // the deprecated read wraps its value in a second variant, which the newer methods omit (§8.4)
            while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_VARIANT) {
                // the payload is the type the variant names (§8.4)
                DBusMessageIter payload;
                // the payload is opened inside the variant (§8.4)
                dbus_message_iter_recurse(&value, &payload);
                // the next layer is what the payload holds, which a nested variant repeats (§8.4)
                value = payload;
            }
            // only the numeric scheme is what the appearance follows (§8.4)
            if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_UINT32)
                return std::nullopt;
            // the value is the scheme the desktop named (§8.4)
            dbus_uint32_t scheme = 0;
            // the number is read from the payload (§8.4)
            dbus_message_iter_get_basic(&value, &scheme);
            // one means the desktop prefers dark, which is the portable default (§8.4)
            if (scheme == 1)
                return ColorScheme::Dark;
            // two means the desktop prefers light, which a theme mirrors (§8.4)
            if (scheme == 2)
                return ColorScheme::Light;
            // no preference leaves the portable default in place (§8.4)
            return std::nullopt;
        }

        // read the colour scheme from a settings reply, which either method answers with a variant (§8.4)
        std::optional<ColorScheme> scheme_from_reply(DBusMessage* reply) {
            // the reply carries the value as its first argument (§8.4)
            DBusMessageIter arguments;
            // a reply with no arguments names no scheme (§8.4)
            if (!dbus_message_iter_init(reply, &arguments))
                return std::nullopt;
            return scheme_from_variant(arguments);
        }

        // report the colour scheme to every observer, which a setting change reaches (§8.4)
        void notify_observers(PortalState& state) {
            // the observers are copied first, because one may release its registration in the callback (§8.4)
            std::vector<std::function<void(ColorScheme)>> observers;
            // every registered observer receives the change (§8.4)
            for (const std::pair<const std::uint64_t, std::function<void(ColorScheme)>>& entry : state.observers)
                observers.push_back(entry.second);
            // the callbacks run in registration order, which no observer's release disturbs (§8.4)
            for (const std::function<void(ColorScheme)>& observer : observers) {
                // an empty slot names no observer (§8.4)
                if (observer)
                    observer(state.scheme);
            }
        }

        // take one setting change, which updates the scheme and reports it once (§8.4)
        void handle_setting_changed(PortalState& state, DBusMessage* message) {
            // only the settings signal carries a scheme change (§8.4)
            if (!dbus_message_is_signal(message, SETTINGS_IFACE, "SettingChanged"))
                return;
            // the signal carries the namespace, the key and the value (§8.4)
            DBusMessageIter arguments;
            // a signal with no arguments names no setting (§8.4)
            if (!dbus_message_iter_init(message, &arguments))
                return;
            // the namespace is the first string the signal carries (§8.4)
            if (dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_STRING)
                return;
            // the namespace text is read before the key follows (§8.4)
            const char* space = nullptr;
            // the namespace is what the appearance lives under (§8.4)
            dbus_message_iter_get_basic(&arguments, &space);
            // the key is the second argument the signal carries (§8.4)
            if (!dbus_message_iter_next(&arguments) || dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_STRING)
                return;
            // the key text is read before the value follows (§8.4)
            const char* key = nullptr;
            // the key is what names the colour scheme (§8.4)
            dbus_message_iter_get_basic(&arguments, &key);
            // a signal that names no setting is ignored (§8.4)
            if (space == nullptr || key == nullptr)
                return;
            // only the appearance namespace and its scheme key belong to this client (§8.4)
            if (std::string_view(space) != APPEARANCE_NAMESPACE || std::string_view(key) != COLOR_SCHEME_KEY)
                return;
            // the value follows the key, which the settings interface spells as a variant (§8.4)
            if (!dbus_message_iter_next(&arguments))
                return;
            // the scheme is what the value names, which an unknown value leaves alone (§8.4)
            const std::optional<ColorScheme> scheme = scheme_from_variant(arguments);
            // a value this client does not know keeps the last scheme (§8.4)
            if (!scheme.has_value())
                return;
            // the scheme is recorded before the observers read it (§8.4)
            state.scheme = *scheme;
            // the observers mirror the change into their appearances (§8.4)
            notify_observers(state);
        }

        // read the colour scheme the desktop advertises, which a portal without the interface leaves unset (§8.4)
        void read_color_scheme(PortalState& state) {
            // the newer method is asked first, because the deprecated one is on its way out (§8.4)
            for (const char* method : {"ReadOne", "Read"}) {
                // one settings read is one method call the portal answers (§8.4)
                DBusMessage* message = dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, SETTINGS_IFACE, method);
                // a message that could not be built cannot be sent (§8.4)
                if (message == nullptr)
                    return;
                // the call carries the namespace and the key, which every settings method takes (§8.4)
                DBusMessageIter arguments;
                // the arguments are opened on the message (§8.4)
                dbus_message_iter_init_append(message, &arguments);
                // the namespace names the appearance settings (§8.4)
                const char* space = APPEARANCE_NAMESPACE;
                // the namespace is appended first (§8.4)
                dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &space);
                // the key names the colour scheme (§8.4)
                const char* key = COLOR_SCHEME_KEY;
                // the key is appended second (§8.4)
                dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &key);
                // the reply carries the value or names the method the portal lacks (§8.4)
                DBusMessage* reply = wait_for_reply(state, message, SETTINGS_REPLY_TIMEOUT_MS, {}, [&state](DBusMessage* incoming) { handle_setting_changed(state, incoming); });
                // the request is released once it is sent (§8.4)
                dbus_message_unref(message);
                // a portal that refused the newer method is asked the older one (§8.4)
                if (reply == nullptr)
                    continue;
                // an error reply names a method this portal lacks, which the older one may answer (§8.4)
                if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
                    // the reply is released once its refusal is read (§8.4)
                    dbus_message_unref(reply);
                    continue;
                }
                // the scheme the reply carries is what the window reports (§8.4)
                const std::optional<ColorScheme> scheme = scheme_from_reply(reply);
                // the reply is released once it is read (§8.4)
                dbus_message_unref(reply);
                // a scheme the desktop named replaces the portable default (§8.4)
                if (scheme.has_value())
                    state.scheme = *scheme;
                return;
            }
        }

        // decode the response of one panel, which names the chosen path or a cancel (§5.3)
        std::optional<std::filesystem::path> decode_response(DBusMessage* message) {
            // the response carries the outcome and the results the panel answered (§5.3)
            DBusMessageIter arguments;
            // a response with no arguments is a failure rather than a cancel (§5.3)
            if (!dbus_message_iter_init(message, &arguments))
                return std::nullopt;
            // the outcome is the first number the response carries (§5.3)
            if (dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_UINT32)
                return std::nullopt;
            // the outcome is what the user decided (§5.3)
            dbus_uint32_t outcome = 2;
            // the number is read from the response (§5.3)
            dbus_message_iter_get_basic(&arguments, &outcome);
            // one means the user cancelled, which is the quiet path (§5.3)
            if (outcome == 1)
                return std::nullopt;
            // any other non success outcome is a portal failure worth reporting (§5.3)
            if (outcome != 0) {
                // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
                std::fprintf(stderr, "albor: the file panel failed with response %u\n", static_cast<unsigned>(outcome));
                return std::nullopt;
            }
            // the results are the second argument, which is a dictionary of variants (§5.3)
            if (!dbus_message_iter_next(&arguments) || dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_ARRAY)
                return std::nullopt;
            // the dictionary is walked entry by entry (§5.3)
            DBusMessageIter results;
            // the entries start at the first element of the dictionary (§5.3)
            dbus_message_iter_recurse(&arguments, &results);
            // every entry may be the one that carries the chosen uris (§5.3)
            while (dbus_message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY) {
                // one entry carries the key and its variant (§5.3)
                DBusMessageIter entry;
                // the entry is opened at its key (§5.3)
                dbus_message_iter_recurse(&results, &entry);
                // the key names the result the panel answered (§5.3)
                const char* key = nullptr;
                // a key that is not a string cannot name a result (§5.3)
                if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&entry, &key);
                // the uris are the result this client reads (§5.3)
                if (key != nullptr && std::string_view(key) == "uris") {
                    // the value follows the key (§5.3)
                    if (!dbus_message_iter_next(&entry) || dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
                        return std::nullopt;
                    // the value is a variant around the uri list (§5.3)
                    DBusMessageIter variant;
                    // the variant is opened at its payload (§5.3)
                    dbus_message_iter_recurse(&entry, &variant);
                    // a payload that is not a list names no uri (§5.3)
                    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY)
                        return std::nullopt;
                    // the list is walked for its first uri, which is the single selection contract (§5.3)
                    DBusMessageIter uris;
                    // the first uri starts at the list element (§5.3)
                    dbus_message_iter_recurse(&variant, &uris);
                    // an empty list names no path, which is a refusal rather than a path (§5.3)
                    if (dbus_message_iter_get_arg_type(&uris) != DBUS_TYPE_STRING)
                        return std::nullopt;
                    // the uri is what the portal answered (§5.3)
                    const char* uri = nullptr;
                    // the uri text is read from the list (§5.3)
                    dbus_message_iter_get_basic(&uris, &uri);
                    // a uri the client cannot decode is a refusal (§5.3)
                    if (uri == nullptr)
                        return std::nullopt;
                    return path_from_uri(uri);
                }
                // the next entry may carry the uris (§5.3)
                dbus_message_iter_next(&results);
            }
            // a response without uris names no path (§5.3)
            return std::nullopt;
        }

        // run one panel until the portal answers it, which pumps the caller's work between reads (§5.3)
        std::optional<std::filesystem::path> run_panel(PortalState& state, const PortalFileRequest& request, const std::function<void()>& pump) {
            // the token names the request in the reply path, which the portal builds the handle from (§5.3)
            const std::string token = "albor" + std::to_string(state.next_token++);
            // the predicted path is what the portal answers on where it keeps the token it was given (§5.3)
            const std::string predicted = request_path(state.unique_name, token);
            // the error carries a match the bus refused (§5.3)
            DBusError error;
            dbus_error_init(&error);
            // the predicted response is matched before the request is sent, because the portal may answer at once (§5.3)
            std::string rule = response_rule(predicted);
            // the match is what routes the response to this connection (§5.3)
            dbus_bus_add_match(state.connection, rule.c_str(), &error);
            // a match the bus refused leaves no panel to wait for (§5.3)
            if (dbus_error_is_set(&error)) {
                // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
                std::fprintf(stderr, "albor: the file panel could not subscribe: %s\n", error.message);
                // the error is released once it is reported (§5.3)
                dbus_error_free(&error);
                return std::nullopt;
            }
            // the match is sent before the request, so the answer cannot arrive unobserved (§5.3)
            dbus_connection_flush(state.connection);
            // one method call asks the portal to run the panel (§5.3)
            DBusMessage* message = dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, FILE_CHOOSER_IFACE, request.save ? "SaveFile" : "OpenFile");
            // a message that could not be built cannot start a panel (§5.3)
            if (message == nullptr) {
                // the match is removed before the call returns (§5.3)
                dbus_bus_remove_match(state.connection, rule.c_str(), nullptr);
                return std::nullopt;
            }
            // the call carries the parent, the title and the options the framework described (§5.3)
            DBusMessageIter arguments;
            // the arguments are opened on the message (§5.3)
            dbus_message_iter_init_append(message, &arguments);
            // the parent names the window the panel is owned by, where empty means unowned (§5.1)
            const char* parent = request.parent.c_str();
            // the parent is the first argument the portal reads (§5.1)
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &parent);
            // the title is what the portal shows on the panel (§5.3)
            const char* title = request.title.c_str();
            // the title is the second argument the portal reads (§5.3)
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &title);
            // the options are the dictionary the two directions share (§5.3)
            DBusMessageIter options;
            // the dictionary is opened on the arguments (§5.3)
            dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &options);
            // the token is what the portal derives the reply path from (§5.3)
            append_string_option(options, "handle_token", token);
            // the framework hands out one path, so the panel refuses a multiple selection (§5.3)
            append_bool_option(options, "multiple", false);
            // the panel is modal to its owner, which keeps the window behind it inactive (§5.1)
            append_bool_option(options, "modal", true);
            // an initial directory is what the panel opens at (§5.3)
            if (!request.initial_directory.empty())
                append_bytestring_option(options, "current_folder", request.initial_directory);
            // a save panel proposes a name, which the user may replace (§5.3)
            if (request.save && !request.default_name.empty())
                append_string_option(options, "current_name", request.default_name);
            // the filters are what the panel offers, in the order the author wrote them (§5.3)
            append_filters_option(options, request.filters);
            // the dictionary is closed before the call is sent (§5.3)
            dbus_message_iter_close_container(&arguments, &options);
            // the panel is answered once the response names it, which a cancel also does (§5.3)
            std::optional<std::filesystem::path> chosen;
            // the flag records the response, which may arrive before the handle (§5.3)
            bool answered = false;
            // the response on the predicted path is read here, so a fast portal cannot answer unobserved (§5.3)
            const auto on_message = [&state, &chosen, &answered, &predicted](DBusMessage* incoming) {
                // only a response on this panel's predicted handle answers it (§5.3)
                if (dbus_message_is_signal(incoming, REQUEST_IFACE, "Response")) {
                    // the signal names the handle it answers on (§5.3)
                    const char* path = dbus_message_get_path(incoming);
                    // a response on the predicted path names the chosen path, a cancel or a failure (§5.3)
                    if (path != nullptr && predicted == path) {
                        chosen = decode_response(incoming);
                        answered = true;
                    }
                    return;
                }
                // a setting that changed while the panel ran is reported at once (§8.4)
                handle_setting_changed(state, incoming);
            };
            // the portal answers with the handle it created, which it may name itself (§5.3)
            DBusMessage* reply = wait_for_reply(state, message, REQUEST_REPLY_TIMEOUT_MS, pump, on_message);
            // the request is released once it is sent (§5.3)
            dbus_message_unref(message);
            // the handle the answer names is copied before the reply is released (§5.3)
            std::string answered_path;
            // the reply carries one object path, which a refusal does not (§5.3)
            if (reply != nullptr && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
                // the path is the handle the portal answers the panel on (§5.3)
                const char* path = nullptr;
                // a reply that carries no path names no panel (§5.3)
                if (dbus_message_get_args(reply, nullptr, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID) && path != nullptr)
                    answered_path = path;
            }
            // the reply is released once its handle is copied (§5.3)
            if (reply != nullptr)
                dbus_message_unref(reply);
            // a response that arrived before the handle is the whole answer, which no handle decides (§5.3)
            if (answered) {
                // the predicted match is removed before the call returns (§5.3)
                dbus_bus_remove_match(state.connection, rule.c_str(), nullptr);
                return chosen;
            }
            // a portal that refused the request leaves no panel to wait for (§5.3)
            if (answered_path.empty()) {
                // the match is removed before the call returns (§5.3)
                dbus_bus_remove_match(state.connection, rule.c_str(), nullptr);
                // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
                std::fprintf(stderr, "albor: the file panel request was refused\n");
                return std::nullopt;
            }
            // a handle the portal chose itself needs its own match, which the predicted rule does not cover (§5.3)
            if (answered_path != predicted) {
                // the predicted match is replaced by the handle the portal named (§5.3)
                dbus_bus_remove_match(state.connection, rule.c_str(), nullptr);
                // the rule names the handle the portal actually answers on (§5.3)
                rule = response_rule(answered_path);
                // the handle match is what routes the response to this connection (§5.3)
                dbus_bus_add_match(state.connection, rule.c_str(), &error);
                // the error is released whether the match was accepted or refused (§5.3)
                if (dbus_error_is_set(&error))
                    dbus_error_free(&error);
                // the handle match is sent before the wait, so the response cannot arrive unobserved (§5.3)
                dbus_connection_flush(state.connection);
            }
            // the panel runs until the portal answers on the handle it named (§5.3)
            while (!answered) {
                // the caller pumps its own connection, which a compositor ping needs answered (§5.3)
                if (pump)
                    pump();
                // the bus is read, which fails when the connection went away (§5.3)
                if (!dbus_connection_read_write(state.connection, READ_TIMEOUT_MS))
                    break;
                // every message that arrived is read in order (§5.3)
                for (DBusMessage* incoming = dbus_connection_pop_message(state.connection); incoming != nullptr; incoming = dbus_connection_pop_message(state.connection)) {
                    // a response names the path it answers, which a signal always carries (§5.3)
                    const char* answered_path_text = dbus_message_get_path(incoming);
                    // a response on this request is what ends the wait (§5.3)
                    if (dbus_message_is_signal(incoming, REQUEST_IFACE, "Response") && answered_path_text != nullptr && answered_path == answered_path_text) {
                        // the response names the chosen path, a cancel or a failure (§5.3)
                        chosen = decode_response(incoming);
                        // the wait is over once the response is read (§5.3)
                        answered = true;
                    }
                    // a setting that changed while the panel ran is reported at once (§8.4)
                    else {
                        // the change is what the windows behind the panel mirror (§8.4)
                        handle_setting_changed(state, incoming);
                    }
                    // the message is released once it is read (§5.3)
                    dbus_message_unref(incoming);
                }
            }
            // the match is removed before the call returns, so a later panel starts clean (§5.3)
            dbus_bus_remove_match(state.connection, rule.c_str(), nullptr);
            // a bus that went away leaves a refusal, which the caller reads as a cancel (§5.3)
            if (!answered)
                std::fprintf(stderr, "albor: the file panel lost its portal connection\n");
            return chosen;
        }
    } // namespace

    PortalSubscription::PortalSubscription(std::shared_ptr<PortalState> state, std::uint64_t id) :
        m_state(std::move(state)), m_id(id) {}

    PortalSubscription::~PortalSubscription() {
        // a state that outlived the client still holds the registration, which is released here (§8.4)
        const std::shared_ptr<PortalState> state = m_state.lock();
        // an empty registration and a dead client have nothing to release (§8.4)
        if (state == nullptr || m_id == 0)
            return;
        // the observer is removed, which a later change no longer reaches (§8.4)
        state->observers.erase(m_id);
    }

    PortalSubscription::PortalSubscription(PortalSubscription&& other) noexcept :
        m_state(std::move(other.m_state)), m_id(other.m_id) {
        // the moved from registration no longer owns an observer (§8.4)
        other.m_id = 0;
    }

    PortalSubscription& PortalSubscription::operator=(PortalSubscription&& other) noexcept {
        // a self assignment leaves the registration as it stands (§8.4)
        if (this == &other)
            return *this;
        // the registration this holder carried is released before it takes the other's (§8.4)
        const std::shared_ptr<PortalState> state = m_state.lock();
        // only a live state with an observer has something to release (§8.4)
        if (state != nullptr && m_id != 0)
            state->observers.erase(m_id);
        // the other registration is what this holder carries from now on (§8.4)
        m_state = std::move(other.m_state);
        // the identifier follows its state (§8.4)
        m_id = other.m_id;
        // the moved from registration no longer owns an observer (§8.4)
        other.m_id = 0;
        return *this;
    }

    std::shared_ptr<PortalClient> PortalClient::shared() {
        // the client is keyed by the bus address, so a private test bus never shares the session bus client (§5.3)
        const std::string address = session_address();
        // the cache is the process wide map the client is looked up in (§5.3)
        std::map<std::string, std::weak_ptr<PortalClient>>& cache = client_cache();
        // a client that is still alive is what the caller gets (§5.3)
        const auto found = cache.find(address);
        // a live entry is what every window and the loop share (§5.3)
        if (found != cache.end()) {
            // a client whose last holder died is built again below (§5.3)
            const std::shared_ptr<PortalClient> client = found->second.lock();
            // the live client is what the caller asked for (§5.3)
            if (client != nullptr)
                return client;
        }
        // a first caller builds the client, which connects the bus and reads the scheme (§5.3)
        const std::shared_ptr<PortalClient> client(new PortalClient());
        // the cache keeps a weak reference, so an unused client releases its bus connection (§5.3)
        cache[address] = client;
        return client;
    }

    PortalClient::PortalClient() :
        m_state(std::make_shared<PortalState>()) {
        // the library is initialized for a process that renders on another thread (§5.3)
        init_threads();
        // the address is what the cache keys on, which the environment names (§5.3)
        m_state->address = session_address();
        // the connection is what every portal call rides, which an unreachable bus leaves missing (§5.3)
        m_state->connection = open_session_bus(m_state->address, m_state->unique_name);
        // a host with no session bus reports the portable defaults rather than failing (§5.3)
        if (m_state->connection == nullptr)
            return;
        // the error carries a match the bus refused (§5.3)
        DBusError error;
        dbus_error_init(&error);
        // the settings signal is matched before the first read, so no change is missed (§8.4)
        const std::string rule = "type='signal',sender='" + std::string(PORTAL_NAME) + "',interface='" + SETTINGS_IFACE + "',member='SettingChanged'";
        // the match is what routes a setting change to this connection (§8.4)
        dbus_bus_add_match(m_state->connection, rule.c_str(), &error);
        // the error is released whether the match was accepted or refused (§8.4)
        if (dbus_error_is_set(&error))
            dbus_error_free(&error);
        // the match is sent before the scheme is read, so a change that follows it is delivered (§8.4)
        dbus_connection_flush(m_state->connection);
        // the scheme the desktop advertises is read once, which starts the portal where the bus activates it (§8.4)
        read_color_scheme(*m_state);
    }

    PortalClient::~PortalClient() {
        // a connection that was opened is closed before it is released, which a private connection needs (§5.3)
        if (m_state->connection != nullptr) {
            // the close hangs the connection up, which releases the socket (§5.3)
            dbus_connection_close(m_state->connection);
            // the connection object is released once it is closed (§5.3)
            dbus_connection_unref(m_state->connection);
            // the state no longer names a connection (§5.3)
            m_state->connection = nullptr;
        }
    }

    bool PortalClient::available() const {
        // a connection that was reached and not dropped is what a caller reads, which a host without a bus reports (§5.3)
        return m_state->connection != nullptr && dbus_connection_get_is_connected(m_state->connection);
    }

    std::optional<std::filesystem::path> PortalClient::choose_file(const PortalFileRequest& request, const std::function<void()>& pump) {
        // a host without a session bus has no panel to run, which the caller reads as a refusal (§5.3)
        if (!available()) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: no D-Bus session bus is available for the file panel\n");
            return std::nullopt;
        }
        // the panel runs until the user answers it or the bus goes away (§5.3)
        return run_panel(*m_state, request, pump);
    }

    ColorScheme PortalClient::color_scheme() const {
        // the scheme the desktop last reported is what a window mirrors (§8.4)
        return m_state->scheme;
    }

    PortalSubscription PortalClient::subscribe_appearance(std::function<void(ColorScheme)> observer) {
        // a host without a session bus has no scheme to report (§8.4)
        if (!available() || !observer)
            return PortalSubscription();
        // the observer is registered under the next identifier (§8.4)
        const std::uint64_t id = m_state->next_observer++;
        // the registration is what a change reaches (§8.4)
        m_state->observers.emplace(id, std::move(observer));
        return PortalSubscription(m_state, id);
    }

    int PortalClient::fd() const {
        // a connection that was never opened has no descriptor to poll (§4.1)
        if (m_state->connection == nullptr)
            return -1;
        // the descriptor is the socket the bus traffic arrives on (§4.1)
        int descriptor = -1;
        // a connection the platform cannot name leaves the loop without a portal descriptor (§4.1)
        if (!dbus_connection_get_unix_fd(m_state->connection, &descriptor))
            return -1;
        return descriptor;
    }

    void PortalClient::dispatch() {
        // a connection that was never opened has nothing to dispatch (§5.3)
        if (m_state->connection == nullptr)
            return;
        // the messages that already arrived are read without waiting (§8.4)
        dbus_connection_read_write(m_state->connection, 0);
        // every message that arrived is read in order (§8.4)
        for (DBusMessage* message = dbus_connection_pop_message(m_state->connection); message != nullptr; message = dbus_connection_pop_message(m_state->connection)) {
            // a setting change is what the appearance follows (§8.4)
            handle_setting_changed(*m_state, message);
            // the message is released once it is read (§8.4)
            dbus_message_unref(message);
        }
    }

    void PortalClient::flush() {
        // a connection that was never opened has nothing to send (§5.3)
        if (m_state->connection == nullptr)
            return;
        // the queued requests are sent to the bus (§5.3)
        dbus_connection_flush(m_state->connection);
    }
} // namespace albor::wayland
