#include "albor/platform/linux/wayland/clipboard.h"

#include <cerrno>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "albor/core/clipboard.h"
#include "albor/platform/linux/wayland/wayland_connection.linux.h"

namespace albor::wayland
{
    // the data device state a copy and a paste ride, which the listeners reach (§3.5)
    struct WaylandClipboardState
    {
        // the shared connection the clipboard borrows, which the display and the devices come from (§3.5)
        std::shared_ptr<WaylandConnection> connection;

        // the data device the selection is set and read through (§3.5)
        wl_data_device* device = nullptr;

        // the keyboard the clipboard listens on for the serials a selection needs (§3.5)
        wl_keyboard* keyboard = nullptr;

        // the serial of the last keyboard enter event, which the selection is set with (§3.5)
        std::uint32_t serial = 0;

        // the source this clipboard last set, which the send event writes through (§3.5)
        wl_data_source* own_source = nullptr;

        // the text this clipboard last copied, which the send event writes (§3.5)
        std::string own_text;

        // the offer the selection currently points at, which a paste reads through (§3.5)
        wl_data_offer* offer = nullptr;

        // the flavours the current offer advertises, which a paste checks (§3.5)
        std::vector<std::string> mimes;
    };

    namespace
    {
        // the utf8 flavour every application agrees on, which a copy offers and a paste reads (§3.5)
        constexpr const char* TEXT_MIME = "text/plain;charset=utf-8";

        // the plain text flavour older clients offer, which a paste accepts (§3.5)
        constexpr const char* TEXT_MIME_PLAIN = "text/plain";

        // the x11 style utf8 flavour a compatibility client offers, which a paste accepts (§3.5)
        constexpr const char* TEXT_MIME_UTF8 = "UTF8_STRING";

        // how long a paste waits for the transfer, which a hung compositor must not stall the caller by (§3.5)
        constexpr int PASTE_TIMEOUT_MS = 200;

        // how long one non blocking wait of a paste is, which the dispatches interleave with (§3.5)
        constexpr int PASTE_SLICE_MS = 20;

        // the flavour one offer advertised, which the paste check reads (§3.5)
        void on_offer_offer(void* data, wl_data_offer*, const char* mime) {
            // the state is the clipboard's own, which is the only receiver (§3.5)
            WaylandClipboardState* state = static_cast<WaylandClipboardState*>(data);
            // a flavour the compositor named is recorded, which the paste check reads (§3.5)
            state->mimes.push_back(std::string(mime != nullptr ? mime : ""));
        }

        // the actions a drag and drop announced, which the clipboard carries none of (§3.5)
        void on_offer_action(void*, wl_data_offer*, std::uint32_t) {
            // the clipboard only reads the selection, which no action belongs to (§3.5)
        }

        // the offer the compositor created, which carries the flavours (§3.5)
        void on_device_data_offer(void* data, wl_data_device*, wl_data_offer* offer) {
            // the state is the clipboard's own, which is the only receiver (§3.5)
            WaylandClipboardState* state = static_cast<WaylandClipboardState*>(data);
            // an offer the compositor refused carries none (§3.5)
            if (offer == nullptr)
                return;
            // the offer listener is installed, which the flavours arrive through (§3.5)
            static constexpr wl_data_offer_listener OFFER_LISTENER = {&on_offer_offer, &on_offer_action, &on_offer_action};
            wl_data_offer_add_listener(offer, &OFFER_LISTENER, state);
        }

        // the drag and drop session events, which the clipboard carries none of (§3.5)
        void on_device_enter(void*, wl_data_device*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*) {
            // the clipboard only reads the selection, which no session belongs to (§3.5)
        }

        // the motion of a drag and drop session, which the clipboard carries none of (§3.5)
        void on_device_motion(void*, wl_data_device*, std::uint32_t, wl_fixed_t, wl_fixed_t) {
            // the clipboard only reads the selection, which no session belongs to (§3.5)
        }

        // the remaining data device events, which the clipboard carries none of (§3.5)
        void on_device_nothing(void*, wl_data_device*) {
            // the clipboard only reads the selection, which no remaining event belongs to (§3.5)
        }

        // the selection the compositor announced, which is what a paste reads (§3.5)
        void on_device_selection(void* data, wl_data_device*, wl_data_offer* offer) {
            // the state is the clipboard's own, which is the only receiver (§3.5)
            WaylandClipboardState* state = static_cast<WaylandClipboardState*>(data);
            // the previous offer is destroyed, because a selection replaces rather than adds (§3.5)
            if (state->offer != nullptr) {
                wl_data_offer_destroy(state->offer);
                state->offer = nullptr;
            }
            // the flavours clear, which the new offer's events rebuild (§3.5)
            state->mimes.clear();
            // a cleared selection carries no offer, which an empty paste reads (§3.5)
            if (offer == nullptr)
                return;
            // the offer becomes the current one, which a paste reads (§3.5)
            state->offer = offer;
        }

        // the flavour the compositor asked the source to write, which the own text is (§3.5)
        void on_source_send(void* data, wl_data_source*, const char*, std::int32_t fd) {
            // the state is the clipboard's own, which is the only receiver (§3.5)
            WaylandClipboardState* state = static_cast<WaylandClipboardState*>(data);
            // the text is written once, which the paste reads until EOF (§3.5)
            std::size_t written = 0;
            while (written < state->own_text.size()) {
                // the bytes are written in one go, which the descriptor takes (§3.5)
                const std::ptrdiff_t sent = write(fd, state->own_text.data() + written, state->own_text.size() - written);
                // a write that failed ends the transfer, which the compositor reads as a refused paste (§3.5)
                if (sent <= 0)
                    break;
                // the written count advances, which the loop steers by (§3.5)
                written += static_cast<std::size_t>(sent);
            }
            // the descriptor is closed, because the transfer is done (§3.5)
            close(fd);
        }

        // the source the compositor cancelled, which a newer copy replaced (§3.5)
        void on_source_cancelled(void* data, wl_data_source* source) {
            // the state is the clipboard's own, which is the only receiver (§3.5)
            WaylandClipboardState* state = static_cast<WaylandClipboardState*>(data);
            // an own source that was cancelled is dropped, which a newer copy replaced (§3.5)
            if (state->own_source == source) {
                state->own_source = nullptr;
                state->own_text.clear();
            }
            // the proxy is destroyed, which is the last owner (§3.5)
            wl_data_source_destroy(source);
        }

        // the target of a deprecated negotiation, which no framework path reads (§3.5)
        void on_source_target(void*, wl_data_source*, const char*) {
            // the negotiation is deprecated, which nothing answers (§3.5)
        }

        // the drag and drop outcomes, which the clipboard carries none of (§3.5)
        void on_source_nothing(void*, wl_data_source*) {
            // the clipboard only sets the selection, which no drag belongs to (§3.5)
        }

        // the selected action of a drag and drop, which the clipboard carries none of (§3.5)
        void on_source_action(void*, wl_data_source*, std::uint32_t) {
            // the clipboard only sets the selection, which no drag belongs to (§3.5)
        }

        // the keymap the keyboard announced, which the clipboard translates nothing through (§3.5)
        void on_clipboard_keymap(void*, wl_keyboard*, std::uint32_t, std::int32_t fd, std::uint32_t) {
            // the descriptor is closed, because a leaked one exhausts the table (§3.5)
            close(fd);
        }

        // the focus the keyboard gave, which carries the serial a selection is set with (§3.5)
        void on_clipboard_enter(void* data, wl_keyboard*, std::uint32_t serial, wl_surface*, wl_array*) {
            // the state is the clipboard's own, which is the only receiver (§3.5)
            WaylandClipboardState* state = static_cast<WaylandClipboardState*>(data);
            // the serial is remembered, which the next selection is set with (§3.5)
            state->serial = serial;
        }

        // the focus the keyboard took, which carries no fact the clipboard reads (§3.5)
        void on_clipboard_leave(void*, wl_keyboard*, std::uint32_t, wl_surface*) {
            // the serial of the next enter event applies, which the clipboard waits for (§3.5)
        }

        // the key events, which the clipboard normalizes nothing of (§3.5)
        void on_clipboard_key(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
            // the clipboard listens for serials only, which no key carries (§3.5)
        }

        // the modifiers, which the clipboard normalizes nothing of (§3.5)
        void on_clipboard_modifiers(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
            // the clipboard listens for serials only, which no modifier carries (§3.5)
        }

        // the repeat info, which the clipboard carries none of (§3.5)
        void on_clipboard_repeat(void*, wl_keyboard*, std::int32_t, std::int32_t) {
            // the clipboard listens for serials only, which no repeat belongs to (§3.5)
        }

        // the listener set of the data source, which carries the send of a copy (§3.5)
        constexpr wl_data_source_listener SOURCE_LISTENER = {&on_source_target, &on_source_send, &on_source_cancelled, &on_source_nothing, &on_source_nothing, &on_source_action};

        // the listener set of the data device, which carries the selection of a paste (§3.5)
        constexpr wl_data_device_listener DEVICE_LISTENER = {&on_device_data_offer, &on_device_enter, &on_device_nothing, &on_device_motion, &on_device_nothing, &on_device_selection};

        // the listener set of the clipboard's keyboard, which carries the serials of §3.5
        constexpr wl_keyboard_listener CLIPBOARD_KEYBOARD_LISTENER = {&on_clipboard_keymap, &on_clipboard_enter, &on_clipboard_leave, &on_clipboard_key, &on_clipboard_modifiers, &on_clipboard_repeat};

        // report whether the clipboard's state carries a usable connection (§3.5)
        [[nodiscard]] bool clipboard_ready(const WaylandClipboardState& state) {
            // the display and the manager are what a copy and a paste need (§3.5)
            return state.connection != nullptr && state.connection->data_devices() != nullptr && state.connection->seat() != nullptr;
        }

        // report whether the current offer advertises the utf8 flavour, which a paste checks (§3.5)
        [[nodiscard]] bool carries_text(const WaylandClipboardState& state) {
            // the flavours are compared one by one, which the advertisement named (§3.5)
            for (const std::string& mime : state.mimes) {
                // any of the text flavours is a paste this clipboard can read (§3.5)
                if (mime == TEXT_MIME || mime == TEXT_MIME_PLAIN || mime == TEXT_MIME_UTF8)
                    return true;
            }
            return false;
        }
    } // namespace

    // the data device clipboard, which reaches wayland and so lives behind the portable surface (§3.5, AD-05)
    class WaylandClipboard final : public Clipboard
    {
    public:
        // build the clipboard over the shared connection, which the devices come from (§3.5)
        explicit WaylandClipboard(std::shared_ptr<WaylandConnection> connection) :
            m_state(std::make_unique<WaylandClipboardState>()) {
            m_state->connection = std::move(connection);
            // a clipboard without a usable connection carries nothing, which ready reports (§3.5)
            if (!clipboard_ready(*m_state))
                return;
            // the data device is created on the seat, which the selection events ride (§3.5)
            m_state->device = wl_data_device_manager_get_data_device(m_state->connection->data_devices(), m_state->connection->seat());
            // a device that came up carries its listener and its version, which the release reads (§3.5)
            if (m_state->device != nullptr) {
                // the device listener carries the selection, installed with the state (§3.5)
                wl_data_device_add_listener(m_state->device, &DEVICE_LISTENER, m_state.get());
                // the device version caps the release request, which the teardown reads (§3.5)
                m_device_version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(m_state->device));
            }
            // the keyboard is taken where the seat announced one, which the serials ride (§3.5)
            if ((m_state->connection->seat_capabilities() & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
                m_state->keyboard = wl_seat_get_keyboard(m_state->connection->seat());
                // a keyboard that came up carries its listener and its version, which the teardown reads (§3.5)
                if (m_state->keyboard != nullptr) {
                    wl_keyboard_add_listener(m_state->keyboard, &CLIPBOARD_KEYBOARD_LISTENER, m_state.get());
                    m_keyboard_version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(m_state->keyboard));
                }
            }
        }

        // release the devices the clipboard created, which the display keeps (§3.5)
        ~WaylandClipboard() override {
            // the source is destroyed, which a cancelled copy is (§3.5)
            if (m_state->own_source != nullptr) {
                wl_data_source_destroy(m_state->own_source);
                m_state->own_source = nullptr;
            }
            // the offer is destroyed, which the compositor releases its transfer under (§3.5)
            if (m_state->offer != nullptr) {
                wl_data_offer_destroy(m_state->offer);
                m_state->offer = nullptr;
            }
            // the keyboard is released, which a version five or later carries (§3.5)
            if (m_state->keyboard != nullptr) {
                if (m_keyboard_version >= 5)
                    wl_keyboard_release(m_state->keyboard);
                else
                    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_state->keyboard));
                m_state->keyboard = nullptr;
            }
            // the device is released, which a version two or later carries (§3.5)
            if (m_state->device != nullptr) {
                if (m_device_version >= 2)
                    wl_data_device_release(m_state->device);
                else
                    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_state->device));
                m_state->device = nullptr;
            }
            // the connection is dropped with the state, which outlives no device (§3.5)
            m_state->connection = nullptr;
        }

        WaylandClipboard(const WaylandClipboard&) = delete;
        WaylandClipboard& operator=(const WaylandClipboard&) = delete;
        WaylandClipboard(WaylandClipboard&&) = delete;
        WaylandClipboard& operator=(WaylandClipboard&&) = delete;

        // override: replace what the selection carries with the text given, which is what a copy is (§3.5)
        void set_text(std::string_view text) override {
            // a clipboard without a usable connection cannot copy, which the drop reports (§3.5)
            if (!clipboard_ready(*m_state))
                return;
            // the focused window's serial is used when the clipboard keyboard has not seen one yet (§3.5)
            const std::uint32_t serial = m_state->serial != 0 ? m_state->serial : m_state->connection->last_input_serial();
            // a copy without a keyboard serial cannot be set, which the compositor refuses (§3.5)
            if (serial == 0)
                return;
            // the previous source is destroyed, because a copy replaces rather than adds (§3.5)
            if (m_state->own_source != nullptr) {
                wl_data_source_destroy(m_state->own_source);
                m_state->own_source = nullptr;
            }
            // the source carries the text, which the compositor reads on a paste (§3.5)
            wl_data_source* source = wl_data_device_manager_create_data_source(m_state->connection->data_devices());
            // a source the compositor refused leaves the old clipboard, which the guard keeps (§3.5)
            if (source == nullptr)
                return;
            // the source listener carries the send, installed with the state (§3.5)
            wl_data_source_add_listener(source, &SOURCE_LISTENER, m_state.get());
            // the standard text flavours are offered, which every desktop client reads (§3.5)
            wl_data_source_offer(source, TEXT_MIME);
            wl_data_source_offer(source, TEXT_MIME_PLAIN);
            wl_data_source_offer(source, TEXT_MIME_UTF8);
            // the text is stored, which the send event writes (§3.5)
            m_state->own_text = std::string(text);
            // the source is remembered, which a cancelled event drops (§3.5)
            m_state->own_source = source;
            // the selection is set, which the compositor announces to the focused clients (§3.5)
            wl_data_device_set_selection(m_state->device, source, serial);
        }

        // override: hand out the text the selection carries, which is empty when it carries none (§3.5)
        [[nodiscard]] std::string text() const override {
            // a clipboard without a usable connection carries nothing (§3.5)
            if (!clipboard_ready(*m_state))
                return {};
            // an offer the compositor announced carries the transfer, which the paste reads (§3.5)
            if (m_state->offer == nullptr || !carries_text(*m_state))
                return {};
            // the pipe is what the transfer flows through, which the receive event fills (§3.5)
            int pipe_ends[2] = {-1, -1};
            // a pipe that failed reads as nothing, which the paste reports (§3.5)
            if (pipe(pipe_ends) != 0)
                return {};
            // the read end is made non blocking, which the dispatch and the read alternate with (§3.5)
            (void)fcntl(pipe_ends[0], F_SETFL, O_NONBLOCK);
            // the transfer is asked for, which the compositor answers by writing the pipe (§3.5)
            wl_data_offer_receive(m_state->offer, TEXT_MIME, pipe_ends[1]);
            // the write end is closed on our side, so the read sees the end of the stream (§3.5)
            close(pipe_ends[1]);
            // the requests are sent, which the compositor answers (§3.5)
            m_state->connection->flush();
            // the bytes accumulate here, which the loop appends into (§3.5)
            std::string text;
            // the deadline bounds the wait, which a hung compositor must not outlive (§3.5)
            int remaining = PASTE_TIMEOUT_MS;
            for (;;) {
                // the events are dispatched first, which the transfer's send side needs (§3.5)
                m_state->connection->dispatch_pending();
                // the bytes available are read non blocking, which the chunk carries (§3.5)
                char buffer[4096];
                const std::ptrdiff_t got = read(pipe_ends[0], buffer, sizeof(buffer));
                // a chunk that carried bytes is appended, which the read continues after (§3.5)
                if (got > 0) {
                    text.append(buffer, static_cast<std::size_t>(got));
                    continue;
                }
                // an end of stream ends the read, which is what a finished transfer is (§3.5)
                if (got == 0)
                    break;
                // a read failure of the permanent kind ends the read, which the empty text reports (§3.5)
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    break;
                // the deadline that ran out ends the read, which the partial text reports (§3.5)
                if (remaining <= 0)
                    break;
                // one slice is waited for, which the compositor writes the pipe within (§3.5)
                pollfd slice[2];
                slice[0].fd = pipe_ends[0];
                slice[0].events = POLLIN;
                slice[1].fd = m_state->connection->fd();
                slice[1].events = POLLIN;
                slice[0].revents = 0;
                slice[1].revents = 0;
                (void)poll(slice, 2, PASTE_SLICE_MS);
                // a readable display is read and dispatched, which the transfer's send side needs (§3.5)
                if ((slice[1].revents & POLLIN) != 0)
                    m_state->connection->dispatch();
                // the deadline is spent by the slice, whatever it returned (§3.5)
                remaining -= PASTE_SLICE_MS;
            }
            // the read end is closed, because the transfer is done (§3.5)
            close(pipe_ends[0]);
            return text;
        }

        // override: report whether the clipboard currently carries text, which a paste checks first (§3.5)
        [[nodiscard]] bool has_text() const override {
            // a clipboard without a usable connection carries nothing (§3.5)
            if (!clipboard_ready(*m_state))
                return false;
            // an own source that is alive carries the text a copy set (§3.5)
            if (m_state->own_source != nullptr)
                return true;
            // the flavours of the current offer are checked, which the paste reads (§3.5)
            return carries_text(*m_state);
        }

        // override: report whether the surface can reach a clipboard, which a caller checks before copying (§3.5)
        [[nodiscard]] bool ready() const override {
            // the connection and the devices are the whole answer (§3.5)
            return clipboard_ready(*m_state);
        }

    private:
        // the data device state, which the listeners reach (§3.5)
        std::unique_ptr<WaylandClipboardState> m_state;

        // the version the data device was created at, which caps its release request (§3.5)
        std::uint32_t m_device_version = 0;

        // the version the keyboard was created at, which caps its release request (§3.5)
        std::uint32_t m_keyboard_version = 0;
    };

    void install_wayland_clipboard() {
        // the clipboard connects on first use, so loading the adapter stays free of a compositor roundtrip (§3.5)
        set_clipboard_factory([] {
            // the connection is built or adopted under the default name, which the clipboard borrows (§3.5)
            const std::shared_ptr<WaylandConnection> connection = WaylandConnection::connect(std::string_view());
            // a compositor that is unreachable leaves no clipboard, which the factory reports (§3.5)
            if (connection == nullptr)
                return std::unique_ptr<Clipboard>();
            // the factory builds the data device surface, so linking the adapter is the selection (§3.5, AD-16)
            return std::unique_ptr<Clipboard>(new WaylandClipboard(connection));
        });
    }
} // namespace albor::wayland
