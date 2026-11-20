#include "albor/platform/linux/wayland/wayland_connection.linux.h"

#include <cstring>

#include <poll.h>

namespace albor::wayland
{
#ifdef ALBOR_WAYLAND_LIBDECOR
    namespace
    {
        // the libdecor error the context reports, which the adapter keeps quiet because the fallback covers it (§5.3)
        void on_libdecor_error(libdecor*, libdecor_error, const char*) {
            // a decoration failure leaves the configured path to its next event, which needs no report (§5.3)
        }

        // the interface every libdecor context is built with, which only the error callback is read from (§5.3)
        constexpr libdecor_interface LIBDECOR_INTERFACE = {&on_libdecor_error};
    } // namespace
#endif
    namespace
    {
        // the registry global the compositor announced, which the connection binds (§5.3)
        void on_registry_global(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) {
            // the listener was installed with the connection, which is the only receiver (§5.3)
            static_cast<WaylandConnection*>(data)->bind_global(registry, name, interface, version);
        }

        // the registry global the compositor removed, which the connection forgets (§5.3)
        void on_registry_remove(void* data, wl_registry*, std::uint32_t name) {
            // the listener was installed with the connection, which is the only receiver (§5.3)
            static_cast<WaylandConnection*>(data)->drop_global(name);
        }

        // the mode one output announced, which carries the refresh rate in millihertz (§4.3)
        void on_output_mode(void* data, wl_output* output, std::uint32_t, std::int32_t, std::int32_t, std::int32_t refresh) {
            // the listener was installed with the connection, which is the only receiver (§4.3)
            static_cast<WaylandConnection*>(data)->note_output_refresh(output, static_cast<double>(refresh) / 1000.0);
        }

        // the scale one output announced, which separates logical pixels from physical ones (AD-27)
        void on_output_scale(void* data, wl_output* output, std::int32_t factor) {
            // the listener was installed with the connection, which is the only receiver (AD-27)
            static_cast<WaylandConnection*>(data)->note_output_scale(output, static_cast<int>(factor));
        }

        // the geometry one output announced, which carries nothing the adapter reads (§4.3)
        void on_output_geometry(void*, wl_output*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, const char*, const char*, std::int32_t) {
            // the facts the adapter needs arrive on the mode and the scale events alone (AD-27)
        }

        // the done event one output announced, which carries nothing the adapter reads (§4.3)
        void on_output_done(void*, wl_output*) {
            // the facts the adapter needs arrive on the mode and the scale events alone (AD-27)
        }

        // the name and the description events carry nothing the adapter reads (§4.3)
        void on_output_string(void*, wl_output*, const char*) {
            // the label is for the user, which no framework path reads (§4.3)
        }

        // the capabilities one seat announced, which name the input devices the windows take (§8.3)
        void on_seat_capabilities(void* data, wl_seat*, std::uint32_t capabilities) {
            // the listener was installed with the connection, which is the only receiver (§8.3)
            static_cast<WaylandConnection*>(data)->note_seat_capabilities(capabilities);
        }

        // the seat name the compositor labels it with, which no framework path reads (§8.3)
        void on_seat_name(void*, wl_seat*, const char*) {
            // the label is for the user, which no framework path reads (§8.3)
        }

        // the liveness check the shell sends, which a client that never answers is marked unresponsive for (§4.1, §5.3)
        void on_shell_ping(void*, xdg_wm_base* shell, std::uint32_t serial) {
            // the pong is what keeps the window responsive, which kwin desaturates and titles unresponsive without (§5.3)
            xdg_wm_base_pong(shell, serial);
        }

        // the one event the shell carries, which the connection answers from the ui thread (§5.3)
        constexpr xdg_wm_base_listener SHELL_LISTENER = {&on_shell_ping};
    } // namespace

    std::map<std::string, std::weak_ptr<WaylandConnection>>& WaylandConnection::cache() {
        // the connections live as long as their owners, and the cache holds only weak references (§5.3)
        static std::map<std::string, std::weak_ptr<WaylandConnection>> connections;
        return connections;
    }

    std::shared_ptr<WaylandConnection> WaylandConnection::connect(std::string_view name) {
        // the key is the name the caller asked for, which the cache reads by
        const std::string key(name);
        // a connection that is still alive is reused, because one display carries one connection (§5.3)
        const std::map<std::string, std::weak_ptr<WaylandConnection>>::const_iterator found = cache().find(key);
        if (found != cache().end()) {
            // a weak reference that still locks is the live connection (§5.3)
            if (std::shared_ptr<WaylandConnection> alive = found->second.lock())
                return alive;
        }
        // the socket name is handed over as a nul terminated string, where an empty name means the default (§5.3)
        const std::string socket(name);
        wl_display* display = wl_display_connect(socket.empty() ? nullptr : socket.c_str());
        // a display that will not connect reports no connection, which the caller refuses on (§5.3)
        if (display == nullptr)
            return nullptr;
        // the connection owns the display from here on, which its destructor takes down (§5.3)
        std::shared_ptr<WaylandConnection> connection(new WaylandConnection());
        connection->m_display = display;
        // the registry is what the globals are bound through, installed before the first roundtrip (§5.3)
        connection->m_registry = wl_display_get_registry(display);
        // the registry listener carries the connection, which the globals bind into (§5.3)
        static constexpr wl_registry_listener REGISTRY_LISTENER = {&on_registry_global, &on_registry_remove};
        wl_registry_add_listener(connection->m_registry, &REGISTRY_LISTENER, connection.get());
        // the first roundtrip delivers the globals, and the second delivers their initial events (§5.3)
        if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0)
            return nullptr;
#ifdef ALBOR_WAYLAND_LIBDECOR
        // the libdecor context is built once per display, which every decorated window shares (§5.3)
        connection->m_decoration_context = libdecor_new(display, const_cast<libdecor_interface*>(&LIBDECOR_INTERFACE));
#endif
        // the connection is cached under its name, which a later connect reuses (§5.3)
        cache()[key] = connection;
        return connection;
    }

    WaylandConnection::~WaylandConnection() {
        // a connection that never connected has nothing to tear down (§5.3)
        if (m_display == nullptr)
            return;
        // the seat is released with its own request where the bound version carries it (§8.3)
        if (m_seat != nullptr) {
            // a seat bound at version five or later carries the release request (§8.3)
            if (m_seat_version >= 5)
                wl_seat_release(m_seat);
            else
                wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_seat));
            m_seat = nullptr;
        }
        // every output is released with its own request where the bound version carries it (§4.3)
        for (const Output& output : m_outputs) {
            // an output bound at version three or later carries the release request (§4.3)
            if (output.version >= 3)
                wl_output_release(output.output);
            else
                wl_proxy_destroy(reinterpret_cast<wl_proxy*>(output.output));
        }
        m_outputs.clear();
        // the shm pool is released by destroying its proxy, because older wayland-client has no release request (§6.2)
        if (m_shm != nullptr) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_shm));
            m_shm = nullptr;
        }
        // the managers and the shell carry destroy requests at every version the adapter binds (§5.3)
        if (m_data_devices != nullptr) {
            wl_data_device_manager_destroy(m_data_devices);
            m_data_devices = nullptr;
        }
        if (m_text_inputs != nullptr) {
            zwp_text_input_manager_v3_destroy(m_text_inputs);
            m_text_inputs = nullptr;
        }
        if (m_decorations != nullptr) {
            zxdg_decoration_manager_v1_destroy(m_decorations);
            m_decorations = nullptr;
        }
        // the exporter is released, which the per window exports no longer reference (§5.3)
        if (m_exporter != nullptr) {
            zxdg_exporter_v2_destroy(m_exporter);
            m_exporter = nullptr;
        }
        if (m_activation != nullptr) {
            xdg_activation_v1_destroy(m_activation);
            m_activation = nullptr;
        }
        // the fractional scale manager is released, which the per surface objects no longer reference (AD-27)
        if (m_fractional_scales != nullptr) {
            wp_fractional_scale_manager_v1_destroy(m_fractional_scales);
            m_fractional_scales = nullptr;
        }
        // the viewporter is released, which the per surface viewports no longer reference (AD-27)
        if (m_viewporter != nullptr) {
            wp_viewporter_destroy(m_viewporter);
            m_viewporter = nullptr;
        }
#ifdef ALBOR_WAYLAND_LIBDECOR
        // the decoration context is released before the display, which its frames reference (§5.3)
        if (m_decoration_context != nullptr) {
            libdecor_unref(m_decoration_context);
            m_decoration_context = nullptr;
        }
#endif
        if (m_shell != nullptr) {
            xdg_wm_base_destroy(m_shell);
            m_shell = nullptr;
        }
        // the compositor and the registry carry no destroy request, so they are unmapped locally (§5.3)
        if (m_compositor != nullptr) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_compositor));
            m_compositor = nullptr;
        }
        if (m_registry != nullptr) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_registry));
            m_registry = nullptr;
        }
        // the display is disconnected last, which closes the socket (§5.3)
        wl_display_disconnect(m_display);
        m_display = nullptr;
    }

    int WaylandConnection::fd() const {
        // the descriptor is what the loop polls on (§4.1)
        return wl_display_get_fd(m_display);
    }

    wl_display* WaylandConnection::display() const {
        // the display is what every roundtrip and every flush go through
        return m_display;
    }

    wl_compositor* WaylandConnection::compositor() const {
        // the compositor is what every surface comes from (§5.3)
        return m_compositor;
    }

    std::uint32_t WaylandConnection::compositor_version() const {
        // the version caps the surface version, which decides whether a preferred scale arrives (AD-27)
        return m_compositor_version;
    }

    wl_shm* WaylandConnection::shm() const {
        // the shm is what the initial buffers come from (§6.2)
        return m_shm;
    }

    xdg_wm_base* WaylandConnection::shell() const {
        // the shell is what every window comes from (§5.3)
        return m_shell;
    }

    wl_seat* WaylandConnection::seat() const {
        // the seat is what every input device comes from (§8.3)
        return m_seat;
    }

    std::uint32_t WaylandConnection::seat_capabilities() const {
        // the capabilities are what the seat announced at bind time (§8.3)
        return m_seat_capabilities;
    }

    std::uint32_t WaylandConnection::seat_version() const {
        // the version caps the pointer and the keyboard version, which the devices inherit (§8.3)
        return m_seat_version;
    }

    zxdg_decoration_manager_v1* WaylandConnection::decorations() const {
        // the manager is null where the compositor draws no decorations (§5.3)
        return m_decorations;
    }

    xdg_activation_v1* WaylandConnection::activation() const {
        // the protocol is null where the compositor cannot raise a window (§5.1)
        return m_activation;
    }

    zxdg_exporter_v2* WaylandConnection::exporter() const {
        // the exporter is null where the compositor cannot name a window to the portal (§5.3)
        return m_exporter;
    }

    wp_fractional_scale_manager_v1* WaylandConnection::fractional_scales() const {
        // the manager is null where the compositor prefers an integer scale alone (AD-27)
        return m_fractional_scales;
    }

    wp_viewporter* WaylandConnection::viewporter() const {
        // the viewporter is null where the compositor cannot map a buffer onto a logical extent (AD-27)
        return m_viewporter;
    }

#ifdef ALBOR_WAYLAND_LIBDECOR
    libdecor* WaylandConnection::decoration_context() const {
        // the context is null where libdecor could not start, which the fallback covers (§5.3)
        return m_decoration_context;
    }

    int WaylandConnection::decoration_fd() const {
        // a context that never started has no descriptor to poll (§5.3)
        if (m_decoration_context == nullptr)
            return -1;
        // the descriptor is libdecor's own, which the loop polls alongside the display (§5.3)
        return libdecor_get_fd(m_decoration_context);
    }

    void WaylandConnection::dispatch_decorations() {
        // a context that never started has no queue to dispatch (§5.3)
        if (m_decoration_context == nullptr)
            return;
        // a zero timeout dispatches what already arrived without blocking (§5.3)
        (void)libdecor_dispatch(m_decoration_context, 0);
    }
#endif

    zwp_text_input_manager_v3* WaylandConnection::text_inputs() const {
        // the manager is null where no input method exists (§8.3)
        return m_text_inputs;
    }

    wl_data_device_manager* WaylandConnection::data_devices() const {
        // the manager is null where no clipboard exists (§3.5)
        return m_data_devices;
    }

    double WaylandConnection::refresh_hz() const {
        // the fastest mode wins, because a mixed desk paces to the fastest screen a window can sit on (§4.3)
        double best = 0.0;
        for (const Output& output : m_outputs) {
            // a mode that carried no refresh name is skipped, which the zero millihertz records (§4.3)
            if (output.refresh_mhz > best)
                best = output.refresh_mhz;
        }
        // a desk with no outputs falls back to the documented default, which still paces (§4.3)
        return best > 0.0 ? best : 60.0;
    }

    int WaylandConnection::output_scale() const {
        // the first output is the one a window opens on, which its scale names (AD-27)
        if (!m_outputs.empty())
            return m_outputs.front().scale;
        // a desk with no outputs falls back to the identity scale (AD-27)
        return 1;
    }

    void WaylandConnection::dispatch_pending() {
        // every event that already arrived is dispatched, which is where the listeners run (§4.1)
        wl_display_dispatch_pending(m_display);
#ifdef ALBOR_WAYLAND_LIBDECOR
        // the decoration queue is dispatched after the display, which its frames ride (§5.3)
        dispatch_decorations();
#endif
    }

    void WaylandConnection::dispatch() {
        // the read is taken only when the display still carries bytes, so a caller never waits on another thread (§4.1)
        if (wl_display_prepare_read(m_display) == 0) {
            // the descriptor is checked without waiting, because the read below would block on an idle display (§4.1)
            pollfd display = {wl_display_get_fd(m_display), POLLIN, 0};
            if (poll(&display, 1, 0) > 0 && (display.revents & POLLIN) != 0) {
                // the bytes that arrived are read once, which the queued dispatch then hands to the listeners (§4.1)
                if (wl_display_read_events(m_display) < 0)
                    return;
            }
            else {
                // a display without bytes releases the read, so another thread may prepare one (§4.1)
                wl_display_cancel_read(m_display);
            }
        }
        // the dispatch never waits, because the vulkan wsi reads the same display on the render thread (§6.3, AD-18b)
        wl_display_dispatch_pending(m_display);
#ifdef ALBOR_WAYLAND_LIBDECOR
        // the decoration queue is dispatched after the display, which its frames ride (§5.3)
        dispatch_decorations();
#endif
    }

    bool WaylandConnection::prepare_read() {
        // the read is prepared before the wait, so the loop owns the bytes its poll reports (§4.1)
        return wl_display_prepare_read(m_display) == 0;
    }

    void WaylandConnection::read_display() {
        // a prepared read is always completed, because the display cannot be used otherwise (§4.1)
        if (wl_display_read_events(m_display) < 0)
            return;
        // the read bytes are dispatched now, which keeps the queue this loop owns empty (§4.1)
        wl_display_dispatch_pending(m_display);
    }

    void WaylandConnection::cancel_read() {
        // a read the poll found nothing for is released, so another thread may prepare one (§4.1)
        wl_display_cancel_read(m_display);
    }

    void WaylandConnection::flush() {
        // the queued requests are sent, which the compositor answers on its own thread (§4.1)
        wl_display_flush(m_display);
    }

    bool WaylandConnection::interface_is(const char* interface, const char* name) {
        // the names are compared byte by byte, which is what the protocol guarantees about them (§5.3)
        return std::strcmp(interface, name) == 0;
    }

    void WaylandConnection::bind_seat(wl_registry* registry, std::uint32_t name, std::uint32_t version) {
        // the seat is bound at the newest version the adapter knows, which caps the pointer version (§8.3)
        m_seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version < 8 ? version : 8));
        m_seat_version = version < 8 ? version : 8;
        m_seat_name = name;
        // the seat listener records the capabilities, which the windows read (§8.3)
        static constexpr wl_seat_listener SEAT_LISTENER = {&on_seat_capabilities, &on_seat_name};
        wl_seat_add_listener(m_seat, &SEAT_LISTENER, this);
    }

    void WaylandConnection::bind_output(wl_registry* registry, std::uint32_t name, std::uint32_t version) {
        // the output is bound at the newest version the adapter knows, which carries the release request (§4.3)
        const std::uint32_t bound = version < 3 ? version : 3;
        Output record;
        record.name = name;
        record.version = bound;
        record.output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, bound));
        // an output the registry could not bind carries no record (§4.3)
        if (record.output == nullptr)
            return;
        // the output listener records the mode and the scale facts (§4.3, AD-27)
        static constexpr wl_output_listener OUTPUT_LISTENER = {&on_output_geometry, &on_output_mode, &on_output_done, &on_output_scale, &on_output_string, &on_output_string};
        wl_output_add_listener(record.output, &OUTPUT_LISTENER, this);
        // the record joins the bound set, which the removals prune (§4.3)
        m_outputs.push_back(record);
    }

    void WaylandConnection::drop_output(std::uint32_t name) {
        // the record is found by name, which the removal names (§4.3)
        for (std::size_t index = 0; index < m_outputs.size(); ++index) {
            if (m_outputs[index].name == name) {
                // the proxy is released, which the bound version carries from three on (§4.3)
                if (m_outputs[index].version >= 3)
                    wl_output_release(m_outputs[index].output);
                else
                    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_outputs[index].output));
                // the record is erased, which its facts drop with it (§4.3)
                m_outputs.erase(m_outputs.begin() + static_cast<std::ptrdiff_t>(index));
                return;
            }
        }
    }

    auto WaylandConnection::find_output(wl_output* output) -> Output* {
        // the record is found by proxy, which the event carries
        for (Output& record : m_outputs) {
            if (record.output == output)
                return &record;
        }
        return nullptr;
    }

    void WaylandConnection::note_output_refresh(wl_output* output, double refresh_mhz) {
        // the record is the one the event named, which a removed output leaves without (§4.3)
        Output* record = find_output(output);
        if (record == nullptr)
            return;
        // the refresh is recorded in hertz, which is what the pacing reads (§4.3)
        record->refresh_mhz = refresh_mhz;
    }

    void WaylandConnection::note_output_scale(wl_output* output, int scale) {
        // the record is the one the event named, which a removed output leaves without (AD-27)
        Output* record = find_output(output);
        if (record == nullptr)
            return;
        // the scale is recorded, which is what the fallback metrics read (AD-27)
        record->scale = scale;
    }

    void WaylandConnection::bind_global(wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) {
        // the compositor is what every surface comes from, bound at the newest version the adapter knows (§5.3)
        if (interface_is(interface, wl_compositor_interface.name)) {
            m_compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, version < 6 ? version : 6));
            m_compositor_version = version < 6 ? version : 6;
            m_compositor_name = name;
            return;
        }
        // the shell is what every window comes from, and a compositor without one is refused later (§5.3)
        if (interface_is(interface, xdg_wm_base_interface.name)) {
            m_shell = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, version < 6 ? version : 6));
            m_shell_name = name;
            // the ping listener is what keeps the window responsive, so every shell carries it (§5.3)
            xdg_wm_base_add_listener(m_shell, &SHELL_LISTENER, this);
            return;
        }
        // the shm is what the initial buffers come from (§6.2)
        if (interface_is(interface, wl_shm_interface.name)) {
            m_shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, version < 2 ? version : 2));
            m_shm_version = version < 2 ? version : 2;
            m_shm_name = name;
            return;
        }
        // the seat is bound through its own path, which installs the capability listener (§8.3)
        if (interface_is(interface, wl_seat_interface.name)) {
            bind_seat(registry, name, version);
            return;
        }
        // the outputs are bound through their own path, which installs their listeners (§4.3)
        if (interface_is(interface, wl_output_interface.name)) {
            bind_output(registry, name, version);
            return;
        }
        // the decoration manager is optional, and a compositor without one leaves the default chrome (§5.3)
        if (interface_is(interface, zxdg_decoration_manager_v1_interface.name)) {
            m_decorations = static_cast<zxdg_decoration_manager_v1*>(wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
            m_decorations_name = name;
            return;
        }
        // the activation protocol is optional, and a compositor without one cannot raise a window (§5.1)
        if (interface_is(interface, xdg_activation_v1_interface.name)) {
            m_activation = static_cast<xdg_activation_v1*>(wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1));
            m_activation_name = name;
            return;
        }
        // the exporter is optional, and a compositor without one cannot name a window to the portal (§5.3)
        if (interface_is(interface, zxdg_exporter_v2_interface.name)) {
            m_exporter = static_cast<zxdg_exporter_v2*>(wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, 1));
            m_exporter_name = name;
            return;
        }
        // the fractional scale manager is optional, and a compositor without one keeps the integer scale (AD-27)
        if (interface_is(interface, wp_fractional_scale_manager_v1_interface.name)) {
            m_fractional_scales = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
            m_fractional_scales_name = name;
            return;
        }
        // the viewporter is optional, and a compositor without one cannot map a fractional buffer (AD-27)
        if (interface_is(interface, wp_viewporter_interface.name)) {
            m_viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
            m_viewporter_name = name;
            return;
        }
        // the text input manager is optional, and a compositor without one leaves the portable input defaults (§8.3)
        if (interface_is(interface, zwp_text_input_manager_v3_interface.name)) {
            m_text_inputs = static_cast<zwp_text_input_manager_v3*>(wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1));
            m_text_inputs_name = name;
            return;
        }
        // the data device manager is optional, and a compositor without one leaves the clipboard unreachable (§3.5)
        if (interface_is(interface, wl_data_device_manager_interface.name)) {
            m_data_devices = static_cast<wl_data_device_manager*>(wl_registry_bind(registry, name, &wl_data_device_manager_interface, version < 3 ? version : 3));
            m_data_devices_name = name;
            return;
        }
        // every other global is ignored, which is what an adapter that needs none does (§5.3)
    }

    void WaylandConnection::drop_global(std::uint32_t name) {
        // a global that matched by name is cleared, which its accessor reports as null afterwards (§5.3)
        if (m_compositor_name == name) {
            m_compositor = nullptr;
            m_compositor_version = 0;
            m_compositor_name = 0;
            return;
        }
        if (m_shell_name == name) {
            m_shell = nullptr;
            m_shell_name = 0;
            return;
        }
        if (m_shm_name == name) {
            m_shm = nullptr;
            m_shm_version = 0;
            m_shm_name = 0;
            return;
        }
        if (m_seat_name == name) {
            m_seat = nullptr;
            m_seat_capabilities = 0;
            m_seat_name = 0;
            return;
        }
        if (m_decorations_name == name) {
            m_decorations = nullptr;
            m_decorations_name = 0;
            return;
        }
        if (m_exporter_name == name) {
            m_exporter = nullptr;
            m_exporter_name = 0;
            return;
        }
        if (m_activation_name == name) {
            m_activation = nullptr;
            m_activation_name = 0;
            return;
        }
        if (m_fractional_scales_name == name) {
            m_fractional_scales = nullptr;
            m_fractional_scales_name = 0;
            return;
        }
        if (m_viewporter_name == name) {
            m_viewporter = nullptr;
            m_viewporter_name = 0;
            return;
        }
        if (m_text_inputs_name == name) {
            m_text_inputs = nullptr;
            m_text_inputs_name = 0;
            return;
        }
        if (m_data_devices_name == name) {
            m_data_devices = nullptr;
            m_data_devices_name = 0;
            return;
        }
        // an output is dropped through its own path, which prunes the records (§4.3)
        drop_output(name);
    }

    void WaylandConnection::note_seat_capabilities(std::uint32_t capabilities) {
        // the capabilities are recorded, which is what the windows read (§8.3)
        m_seat_capabilities = capabilities;
    }

    void WaylandConnection::note_input_serial(std::uint32_t serial) {
        // the serial is recorded, which a copy and an activation token name (§3.5, §5.1)
        m_last_input_serial = serial;
    }

    std::uint32_t WaylandConnection::last_input_serial() const {
        // the last serial is what a copy sets the selection with (§3.5)
        return m_last_input_serial;
    }
} // namespace albor::wayland
