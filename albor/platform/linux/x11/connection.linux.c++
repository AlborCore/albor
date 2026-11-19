#include "albor/platform/linux/x11/connection.linux.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace albor::x11
{
    namespace
    {
        // the connections already open, keyed by the display name they were asked for (§5.3)
        std::unordered_map<std::string, std::weak_ptr<X11Connection>>& connections() {
            // the map is held for the process, because two windows share one display (§5.3)
            static std::unordered_map<std::string, std::weak_ptr<X11Connection>> map;
            return map;
        }

        // the lock that serializes the map, which the ui thread builds the first window on (§10.5)
        std::mutex& connections_mutex() {
            // one mutex guards the map for the process (§10.5)
            static std::mutex mutex;
            return mutex;
        }

        // resolve the screen a connection reports at an index
        [[nodiscard]] xcb_screen_t* screen_at(xcb_connection_t* connection, int index) {
            // the setup carries every screen the display reports (§5.3)
            xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(xcb_get_setup(connection));
            for (int current = 0; current < index && iterator.rem != 0; current++)
                xcb_screen_next(&iterator);
            return iterator.data;
        }
    } // namespace

    X11Connection::X11Connection(xcb_connection_t* connection, xcb_screen_t* screen) :
        m_connection(connection), m_screen(screen) {}

    X11Connection::~X11Connection() {
        // the connection is closed once, because the adapter owns it for the process (§5.3)
        if (m_connection != nullptr)
            xcb_disconnect(m_connection);
    }

    std::shared_ptr<X11Connection> X11Connection::connect(std::string_view name) {
        // the default display is the one xcb resolves from the environment, which a named one overrides (§5.3)
        const std::string key = name.empty() ? std::string(":default") : std::string(name);
        const std::lock_guard<std::mutex> lock(connections_mutex());
        const auto found = connections().find(key);
        // a display already open is reused, which keeps one connection for every window on it (§5.3)
        if (found != connections().end()) {
            if (std::shared_ptr<X11Connection> existing = found->second.lock())
                return existing;
        }
        int screen_index = 0;
        xcb_connection_t* connection = xcb_connect(name.empty() ? nullptr : key.c_str(), &screen_index);
        // a connection the host refuses reports null rather than a half built adapter (§5.3)
        if (connection == nullptr || xcb_connection_has_error(connection) != 0) {
            if (connection != nullptr)
                xcb_disconnect(connection);
            return nullptr;
        }
        // the screen the display reports is the one the window and its renderer use (§5.3)
        xcb_screen_t* screen = screen_at(connection, screen_index);
        if (screen == nullptr) {
            xcb_disconnect(connection);
            return nullptr;
        }
        std::shared_ptr<X11Connection> built(new X11Connection(connection, screen));
        connections()[key] = built;
        return built;
    }

    xcb_connection_t* X11Connection::connection() const {
        // the connection is what every request and event rides (§5.3)
        return m_connection;
    }

    xcb_screen_t* X11Connection::screen() const {
        // the screen names the root and the default visual (§5.3)
        return m_screen;
    }

    int X11Connection::fd() const {
        // the descriptor is what the loop polls for events (§4.1)
        return m_connection != nullptr ? xcb_get_file_descriptor(m_connection) : -1;
    }
} // namespace albor::x11
