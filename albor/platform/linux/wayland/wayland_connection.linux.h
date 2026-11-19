#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <wayland-client.h>

#include "fractional-scale-client-protocol.h"
#include "text-input-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-activation-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "xdg-foreign-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#ifdef ALBOR_WAYLAND_LIBDECOR
#include <libdecor.h>
#endif

namespace albor::wayland
{
    // the shared wayland connection, which one process holds one of per display name (§5.3, AD-05)
    class WaylandConnection
    {
    public:
        // take the display and the bound globals down
        ~WaylandConnection();

        WaylandConnection(const WaylandConnection&) = delete;
        WaylandConnection& operator=(const WaylandConnection&) = delete;
        WaylandConnection(WaylandConnection&&) = delete;
        WaylandConnection& operator=(WaylandConnection&&) = delete;

        // connect to the named display, reusing one connection per name, and report null when it is unreachable (§5.3)
        [[nodiscard]] static std::shared_ptr<WaylandConnection> connect(std::string_view name);

        // hand out the file descriptor the loop polls
        [[nodiscard]] int fd() const;

        // hand out the display every roundtrip flushes through
        [[nodiscard]] wl_display* display() const;

        // hand out the compositor every surface comes from, which is null where the compositor has none (§5.3)
        [[nodiscard]] wl_compositor* compositor() const;

        // hand out the version the compositor global was bound at, which caps the surface version (AD-27)
        [[nodiscard]] std::uint32_t compositor_version() const;

        // hand out the shm pool the initial buffers come from, which is null where the compositor has none (§6.2)
        [[nodiscard]] wl_shm* shm() const;

        // hand out the shell every window comes from, which is null where the compositor has none (§5.3)
        [[nodiscard]] xdg_wm_base* shell() const;

        // hand out the seat the input devices come from, which is null where the compositor has none (§8.3)
        [[nodiscard]] wl_seat* seat() const;

        // hand out the device capabilities the seat announced, which name the input devices (§8.3)
        [[nodiscard]] std::uint32_t seat_capabilities() const;

        // hand out the version the seat was bound at, which caps the pointer and the keyboard version (§8.3)
        [[nodiscard]] std::uint32_t seat_version() const;

        // hand out the decoration manager, which is null where the compositor draws none (§5.3)
        [[nodiscard]] zxdg_decoration_manager_v1* decorations() const;

        // hand out the exporter that names a window to the portal, null where the compositor has no xdg-foreign (§5.3)
        [[nodiscard]] zxdg_exporter_v2* exporter() const;

        // hand out the activation protocol, which is null where the compositor cannot raise a window (§5.1)
        [[nodiscard]] xdg_activation_v1* activation() const;

        // hand out the fractional scale manager, which is null where the compositor prefers no fractional scale (AD-27)
        [[nodiscard]] wp_fractional_scale_manager_v1* fractional_scales() const;

        // hand out the viewporter, which is null where the compositor cannot map a buffer onto a logical extent (AD-27)
        [[nodiscard]] wp_viewporter* viewporter() const;

#ifdef ALBOR_WAYLAND_LIBDECOR
        // hand out the libdecor context, which is null where the library could not start (§5.3)
        [[nodiscard]] libdecor* decoration_context() const;

        // hand out the descriptor libdecor dispatches on, which the loop polls alongside the display (§5.3)
        [[nodiscard]] int decoration_fd() const;

        // dispatch the libdecor queue, which carries its own configure events (§5.3)
        void dispatch_decorations();
#endif

        // hand out the text input manager, which is null where no input method exists (§8.3)
        [[nodiscard]] zwp_text_input_manager_v3* text_inputs() const;

        // hand out the data device manager, which is null where no clipboard exists (§3.5)
        [[nodiscard]] wl_data_device_manager* data_devices() const;

        // hand out the refresh rate the outputs report, which is the pacing target (§4.3)
        [[nodiscard]] double refresh_hz() const;

        // hand out the scale the first output reports, which a surface version before six falls back on (AD-27)
        [[nodiscard]] int output_scale() const;

        // dispatch every event that already arrived, which a caller drives between its own work (§4.1)
        void dispatch_pending();

        // prepare the display read before the poll, which keeps another thread's queue from owning it (§4.1)
        [[nodiscard]] bool prepare_read();

        // read the display a prepared read owns, which the poll found readable (§4.1)
        void read_display();

        // cancel a prepared read the poll found nothing for, so another thread may read (§4.1)
        void cancel_read();

        // read the display for the events the poll reported and dispatch them, which is the loop's read half (§4.1)
        void dispatch();

        // marshal and flush are safe beside a dispatch, because libwayland locks the display (§10.5)

        // send every queued request, which the compositor answers on its own thread (§4.1)
        void flush();

        // bind one announced global at the version the adapter knows, which the registry listener calls (§5.3)
        void bind_global(wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version);

        // drop one removed global, which the registry listener calls (§5.3)
        void drop_global(std::uint32_t name);

        // record the refresh rate one output event reported, which the mode event names in millihertz (§4.3)
        void note_output_refresh(wl_output* output, double refresh_mhz);

        // record the scale one output event reported, which the scale event names (AD-27)
        void note_output_scale(wl_output* output, int scale);

        // record the device capabilities one seat event reported, which the windows read (§8.3)
        void note_seat_capabilities(std::uint32_t capabilities);

        // record the serial of the last input event, which a clipboard copy and an activation token share (§3.5, §5.1)
        void note_input_serial(std::uint32_t serial);

        // hand out the serial of the last input event, which a copy sets the selection with (§3.5)
        [[nodiscard]] std::uint32_t last_input_serial() const;

    private:
        // build the empty connection, which connect() fills
        WaylandConnection() = default;

        // one bound output, which carries the facts a window and the pacing read (§4.3, AD-27)
        struct Output
        {
            // the registry name the output was advertised under, which a removal clears by
            std::uint32_t name = 0;

            // the output the listener reads geometry and modes from
            wl_output* output = nullptr;

            // the scale the output announced, which the scale event carries (AD-27)
            int scale = 1;

            // the refresh rate the mode events carried, in millihertz (§4.3)
            double refresh_mhz = 0.0;

            // the version the output was bound at, which decides whether a release request exists (§4.3)
            std::uint32_t version = 0;
        };

        // report whether an interface matches one of the names the adapter binds (§5.3)
        [[nodiscard]] static bool interface_is(const char* interface, const char* name);

        // bind the seat once, which the capabilities arrive on its own listener (§8.3)
        void bind_seat(wl_registry* registry, std::uint32_t name, std::uint32_t version);

        // bind one output and read its facts, which the mode and scale events carry (§4.3, AD-27)
        void bind_output(wl_registry* registry, std::uint32_t name, std::uint32_t version);

        // forget one output, whose facts no longer count (§4.3)
        void drop_output(std::uint32_t name);

        // find the record one output was bound under, which the event thunks resolve through
        [[nodiscard]] Output* find_output(wl_output* output);

        // the display the compositor connection runs on, which is null until connect succeeds
        wl_display* m_display = nullptr;

        // the registry the globals are bound through, which lives as long as the display
        wl_registry* m_registry = nullptr;

        // the compositor every surface comes from
        wl_compositor* m_compositor = nullptr;

        // the version the compositor was bound at, which caps the surface version (AD-27)
        std::uint32_t m_compositor_version = 0;

        // the registry name the compositor was advertised under, which a removal clears by
        std::uint32_t m_compositor_name = 0;

        // the shm pool the initial buffers come from
        wl_shm* m_shm = nullptr;

        // the registry name the shm was advertised under, which a removal clears by
        std::uint32_t m_shm_name = 0;

        // the version the shm was bound at, which decides whether a release request exists (§6.2)
        std::uint32_t m_shm_version = 0;

        // the shell every window comes from (§5.3)
        xdg_wm_base* m_shell = nullptr;

        // the registry name the shell was advertised under, which a removal clears by
        std::uint32_t m_shell_name = 0;

        // the seat the input devices come from (§8.3)
        wl_seat* m_seat = nullptr;

        // the registry name the seat was advertised under, which a removal clears by
        std::uint32_t m_seat_name = 0;

        // the version the seat was bound at, which decides whether a release request exists (§8.3)
        std::uint32_t m_seat_version = 0;

        // the device capabilities the seat announced, which the capability event carries (§8.3)
        std::uint32_t m_seat_capabilities = 0;

        // the serial of the last input event, which a copy and an activation token share (§3.5, §5.1)
        std::uint32_t m_last_input_serial = 0;

        // the decoration manager, which is null where the compositor draws none (§5.3)
        zxdg_decoration_manager_v1* m_decorations = nullptr;

        // the registry name the decoration manager was advertised under, which a removal clears by
        std::uint32_t m_decorations_name = 0;

        // the exporter that names a window to the portal, which is null where the compositor has no xdg-foreign (§5.3)
        zxdg_exporter_v2* m_exporter = nullptr;

        // the registry name the exporter was advertised under, which a removal clears by
        std::uint32_t m_exporter_name = 0;

        // the activation protocol, which is null where the compositor cannot raise a window (§5.1)
        xdg_activation_v1* m_activation = nullptr;

        // the registry name the activation protocol was advertised under, which a removal clears by
        std::uint32_t m_activation_name = 0;

        // the fractional scale manager, which is null where the compositor prefers no fractional scale (AD-27)
        wp_fractional_scale_manager_v1* m_fractional_scales = nullptr;

        // the registry name the fractional scale manager was advertised under, which a removal clears by
        std::uint32_t m_fractional_scales_name = 0;

        // the viewporter, which is null where the compositor cannot map a buffer onto a logical extent (AD-27)
        wp_viewporter* m_viewporter = nullptr;

        // the registry name the viewporter was advertised under, which a removal clears by
        std::uint32_t m_viewporter_name = 0;

#ifdef ALBOR_WAYLAND_LIBDECOR
        // the client side decoration context, which is null where libdecor could not start (§5.3)
        libdecor* m_decoration_context = nullptr;
#endif

        // the text input manager, which is null where no input method exists (§8.3)
        zwp_text_input_manager_v3* m_text_inputs = nullptr;

        // the registry name the text input manager was advertised under, which a removal clears by
        std::uint32_t m_text_inputs_name = 0;

        // the data device manager, which is null where no clipboard exists (§3.5)
        wl_data_device_manager* m_data_devices = nullptr;

        // the registry name the data device manager was advertised under, which a removal clears by
        std::uint32_t m_data_devices_name = 0;

        // the outputs the registry bound, which the removals prune (§4.3)
        std::vector<Output> m_outputs;

        // the connections already open, keyed by the display name they were asked for
        static std::map<std::string, std::weak_ptr<WaylandConnection>>& cache();
    };
} // namespace albor::wayland
