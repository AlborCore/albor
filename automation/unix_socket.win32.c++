#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "automation/automation_platform.h"
#include "automation/automation_protocol.h"
#include "automation/automation_session.h"
#include "automation/automation_session_connection.h"

namespace albor::automation::platform
{
    namespace
    {
        // the text a windows error carries, which a host logs rather than a bare number (§10.3, AD-10)
        std::string error_text(DWORD error) {
            // the system message is allocated by the api, because its length is not known before the call (§10.3)
            char* buffer = nullptr;
            // the message is asked for in the local language, which is what a developer reads (§10.3)
            const DWORD length = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
            // the message is copied before the api buffer is released (§10.3)
            std::string text = length != 0 && buffer != nullptr ? std::string(buffer, length) : std::string();
            // the api buffer is released on every path, because the message no longer needs it (§10.3)
            if (buffer != nullptr)
                ::LocalFree(buffer);
            // the trailing line break is removed, because the host adds its own (§10.3)
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
                text.pop_back();
            // a message the system did not supply is named by its number, which is still traceable (§10.3)
            return text.empty() ? "windows error " + std::to_string(error) : text;
        }

        // the sid of the process user, which is the identity a local pipe and token are restricted to (§10.3, AD-10)
        std::vector<BYTE> current_user_sid() {
            // the token is opened for query alone, because the transport never changes the process identity (§10.3)
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
                return {};
            // the size is measured first, because the user structure is variable length (§10.3)
            DWORD needed = 0;
            (void)::GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
            // the token user is read into the measured buffer, which is the whole of the identity (§10.3)
            std::vector<BYTE> buffer(needed);
            const BOOL read = needed != 0 && ::GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed);
            // the token is closed before the result is examined, because the copy is what survives (§10.3)
            ::CloseHandle(token);
            // a token that did not answer has no user, which the caller refuses rather than guessing (§10.3)
            if (!read)
                return {};
            // the user structure names the sid the access control list grants (§10.3, AD-10)
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            // the sid length is what the independent copy must hold (§10.3)
            const DWORD length = ::GetLengthSid(user->User.Sid);
            // the sid is copied, because the token structure is released with the buffer (§10.3)
            std::vector<BYTE> sid(length);
            if (!::CopySid(length, sid.data(), user->User.Sid))
                return {};
            return sid;
        }

        // build the security descriptor that grants the current user alone, which a local endpoint requires (§10.3, AD-10)
        PSECURITY_DESCRIPTOR security_descriptor_for(const std::vector<BYTE>& sid) {
            // a user without a sid has no identity to grant, which the caller refuses (§10.3)
            if (sid.empty())
                return nullptr;
            // the sid is rendered into the sddl text the descriptor parser reads (§10.3)
            LPWSTR sid_text = nullptr;
            if (!::ConvertSidToStringSidW(const_cast<PSID>(static_cast<const void*>(sid.data())), &sid_text))
                return nullptr;
            // the descriptor grants generic all to that user alone, which is the local boundary (§10.3, AD-10)
            const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid_text) + L")";
            // the rendered sid is released, because the sddl text now carries it (§10.3)
            ::LocalFree(sid_text);
            // the parser builds the descriptor the pipe and token are created under (§10.3)
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
                return nullptr;
            return descriptor;
        }

        // the local transport, which is a named pipe and never a network endpoint (§10.3, AD-10)
        class WindowsPipeTransport final : public AutomationTransport
        {
        public:
            // the registry is taken at construction, because a transport that could dispatch nothing is useless (§10.3)
            explicit WindowsPipeTransport(AutomationRegistry& registry) :
                m_factory(default_factory()), m_registry(registry), m_token(AutomationSession(registry).token()) {}

            ~WindowsPipeTransport() override { stop(); }

            [[nodiscard]] bool listen(const AutomationEndpoint& endpoint) override {
                // a second bind on the same object is refused, because one server owns one endpoint (§10.3)
                if (m_pipe != INVALID_HANDLE_VALUE)
                    return fail("already listening");
                // the previous refusal is cleared, because a new endpoint starts without one (§10.3)
                m_error.clear();
                // the user identity is read once, because both the pipe and its token are restricted to it (§10.3, AD-10)
                m_user_sid = current_user_sid();
                if (m_user_sid.empty())
                    return fail("the process user has no security identifier");
                // the descriptor is built once, because the pipe and token share one owner (§10.3, AD-10)
                PSECURITY_DESCRIPTOR descriptor = security_descriptor_for(m_user_sid);
                if (descriptor == nullptr)
                    return fail("the endpoint security descriptor could not be built");
                // the attributes carry the owner only descriptor into the pipe creation (§10.3, AD-10)
                SECURITY_ATTRIBUTES attributes{};
                attributes.nLength = sizeof(attributes);
                attributes.lpSecurityDescriptor = descriptor;
                attributes.bInheritHandle = FALSE;
                // the endpoint path is the named pipe a client opens, which the host publishes in its descriptor (§10.3)
                const std::wstring pipe_name = std::filesystem::path(endpoint.socket_path).wstring();
                // the pipe is nonblocking and refuses remote clients, because a frame must not wait (§6.1, §10.3)
                m_pipe = ::CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64u * 1024u, 64u * 1024u, 0, &attributes);
                // a pipe that could not be created leaves no listener, which the caller must know (§10.3)
                if (m_pipe == INVALID_HANDLE_VALUE) {
                    const DWORD error = ::GetLastError();
                    ::LocalFree(descriptor);
                    return fail("CreateNamedPipe: " + error_text(error));
                }
                // the token is published before the listener is ready, because a client needs it (§10.3)
                const bool published = publish_token(endpoint.token_path, attributes);
                // the descriptor is released, because the pipe and token now own their security (§10.3)
                ::LocalFree(descriptor);
                // a token that could not be written leaves no endpoint (§10.3)
                if (!published) {
                    ::CloseHandle(m_pipe);
                    m_pipe = INVALID_HANDLE_VALUE;
                    return false;
                }
                // the endpoint is ready, which is what the host reports to a client (§10.3)
                return true;
            }

            [[nodiscard]] bool serve_one() override {
                // a listener that was never bound cannot serve, which is a host error rather than a client refusal (§10.3)
                if (m_pipe == INVALID_HANDLE_VALUE)
                    return fail("not listening");
                // a client already connected is served first, because one conversation spans many frames (§10.3)
                if (!m_connected) {
                    // a new client is accepted without waiting, which keeps the ui thread responsive (§6.1, §10.3)
                    if (!connect_client())
                        return false;
                    // no client was waiting, which is the normal case between two frames (§6.1)
                    if (!m_connected)
                        return true;
                }
                // a reply that could not be written last frame is retried before anything is read (§10.3)
                if (!flush_output())
                    return true;
                // whatever the client has sent is answered, and a frame never waits for more (§6.1, §10.3)
                converse();
                return true;
            }

            void set_connection_factory(ConnectionFactory factory) override {
                // an empty factory restores the raw protocol, which is what a manual host expects from the default (§10.3)
                m_factory = factory ? std::move(factory) : default_factory();
            }

            void stop() override {
                // the client is released first, because its connection may still refer to the registry (§10.3, AD-20)
                drop_client();
                // the listener is closed, which is what makes serve_one report no listener thereafter (§4.1, §10.3)
                if (m_pipe != INVALID_HANDLE_VALUE) {
                    ::CloseHandle(m_pipe);
                    m_pipe = INVALID_HANDLE_VALUE;
                }
                // the identity is cleared with the endpoint, because no later connection is checked against it (§10.3)
                m_user_sid.clear();
            }

            [[nodiscard]] const std::string& session_token() const override { return m_token; }

            [[nodiscard]] const std::string& last_error() const override { return m_error; }

        private:
            // build the raw connection the manual host protocol has always used (§10.3)
            [[nodiscard]] ConnectionFactory default_factory() {
                // the registry is borrowed, because the transport outlives every connection it builds (§10.3)
                return [this](std::string_view token) { return std::make_unique<SessionConnection>(m_registry, std::string(token)); };
            }

            // record a refusal and report it, so a host can log why a client was turned away (§10.3, AD-10)
            [[nodiscard]] bool fail(std::string error) {
                // the reason is kept, which is what the host reads (§10.3)
                m_error = std::move(error);
                return false;
            }

            // write the session token to the endpoint file, so a client can read what it must present (§10.3, AD-10)
            [[nodiscard]] bool publish_token(const std::string& path, const SECURITY_ATTRIBUTES& attributes) {
                // an endpoint with no token file still listens, because a host may hand the token over another way (§10.3)
                if (path.empty())
                    return true;
                // the token path is opened owner only, because a shared token is no token at all (§10.3, AD-10)
                const std::wstring wide = std::filesystem::path(path).wstring();
                HANDLE file = ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, const_cast<SECURITY_ATTRIBUTES*>(&attributes), CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                // a token file that could not be opened leaves the endpoint unauthenticated (§10.3, AD-10)
                if (file == INVALID_HANDLE_VALUE)
                    return fail("token file: " + error_text(::GetLastError()));
                // the token is written whole, because a truncated token would never authenticate (§10.3, AD-10)
                DWORD written = 0;
                const BOOL wrote = ::WriteFile(file, m_token.data(), static_cast<DWORD>(m_token.size()), &written, nullptr);
                // the descriptor is closed before the write is judged, because a close publishes the file (§10.3, AD-10)
                ::CloseHandle(file);
                // a token that did not fit is refused, because a partial token is a token nobody can use (§10.3, AD-10)
                if (!wrote || written != m_token.size()) {
                    // the partial file is removed, because it must not be read as a complete token (§10.3, AD-10)
                    ::DeleteFileW(wide.c_str());
                    return fail("the token did not fit the endpoint file");
                }
                // the publication succeeded, so the endpoint is ready for a client (§10.3)
                return true;
            }

            // accept the client that is waiting, without waiting for one that is not (§10.3)
            [[nodiscard]] bool connect_client() {
                // the pipe is connected, where the client arrived before this call (§10.3)
                if (!::ConnectNamedPipe(m_pipe, nullptr)) {
                    const DWORD error = ::GetLastError();
                    // an already connected pipe is the successful race, which the call reports as an error (§10.3)
                    if (error != ERROR_PIPE_CONNECTED) {
                        // no client is the normal case between frames, which is not a listener failure (§6.1, §10.3)
                        if (error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING)
                            return true;
                        // any other failure leaves the listener intact, so the caller may serve again (§10.3)
                        return fail("ConnectNamedPipe: " + error_text(error));
                    }
                }
                // a peer that is not this user is refused before any byte is read (§10.3, AD-10)
                if (!same_user()) {
                    // the client is disconnected unread, because a refused peer sends nothing (§10.3, AD-10)
                    ::DisconnectNamedPipe(m_pipe);
                    return true;
                }
                // the conversation is marked live, because every later read and write belongs to it (§10.3)
                m_connected = true;
                // protocol per client, host can choose MCP (§10.3, AD-20)
                m_connection = m_factory(m_token);
                // empty factory, drop client (§10.3)
                if (!m_connection) {
                    // the client is disconnected, because no protocol can answer it (§10.3)
                    drop_client();
                    // the refusal is recorded, which is what the host logs (§10.3)
                    return fail("the connection factory built no connection");
                }
                // the pending line starts empty, because a conversation begins with no partial request (§10.3)
                m_pending.clear();
                // the output starts empty, because a new client has not been sent a reply yet (§10.3)
                m_output.clear();
                m_output_offset = 0;
                // the client is answered this frame, because its first line is usually already waiting (§10.3)
                converse();
                return true;
            }

            // report whether the peer runs as the user that created the pipe (§10.3, AD-10)
            [[nodiscard]] bool same_user() const {
                // a transport without an identity refuses every peer, because it cannot check one (§10.3)
                if (m_user_sid.empty())
                    return false;
                // the client process is asked for, because its token names the user that owns it (§10.3, AD-10)
                ULONG pid = 0;
                if (!::GetNamedPipeClientProcessId(m_pipe, &pid))
                    return false;
                // the client process is opened for identity alone, because the server never reads its memory (§10.3)
                HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (process == nullptr)
                    return false;
                // the token is read from the client process, which is the macos same user check (§10.3, AD-10)
                HANDLE token = nullptr;
                bool same = false;
                if (::OpenProcessToken(process, TOKEN_QUERY, &token)) {
                    // the user structure is measured first, because its sid is variable length (§10.3)
                    DWORD needed = 0;
                    (void)::GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
                    // the client user is read into the measured buffer, which is the identity compared below (§10.3)
                    std::vector<BYTE> buffer(needed);
                    if (needed != 0 && ::GetTokenInformation(token, TokenUser, buffer.data(), needed, &needed)) {
                        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
                        // the two sids decide, because a different user may not drive this process (§10.3, AD-10)
                        same = ::EqualSid(const_cast<PSID>(static_cast<const void*>(m_user_sid.data())), user->User.Sid) != FALSE;
                    }
                    // the token is released before the process, because it was opened through it (§10.3)
                    ::CloseHandle(token);
                }
                // the process handle is released on every path, because the check owns it only here (§10.3)
                ::CloseHandle(process);
                return same;
            }

            // serve whatever one client has already sent, without waiting, because a frame may not block (§6.1, §10.3)
            void converse() {
                // whatever has arrived is read, and a client that has sent nothing costs one read (§10.3)
                char buffer[4096];
                // the loop drains the pipe, because one non blocking read may leave a line for the next frame (§10.3)
                for (;;) {
                    // the read is attempted, because the pipe is non blocking and a wait would stall the ui (§6.1, §10.3)
                    DWORD got = 0;
                    if (!::ReadFile(m_pipe, buffer, sizeof(buffer), &got, nullptr)) {
                        const DWORD error = ::GetLastError();
                        // the empty pipe is the normal case between two client messages (§10.3)
                        if (error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING)
                            return;
                        // other errors mean peer went away, which ends only this conversation (§10.3)
                        drop_client();
                        return;
                    }
                    // a closed peer ends the conversation, which is the only way this read reports it (§10.3)
                    if (got == 0) {
                        drop_client();
                        return;
                    }
                    // each byte that arrived is placed in the pending line, which is what the protocol frames on (§10.3)
                    for (DWORD index = 0; index < got; index++) {
                        const char byte = buffer[index];
                        // a carriage return is dropped, so a windows client still works (§10.3)
                        if (byte == '\r')
                            continue;
                        // a newline completes a line, which is what a request is (§10.3)
                        if (byte == '\n') {
                            // the line is handled by the connection, which is the only protocol seam (§10.3, AD-20)
                            const std::optional<std::string> reply = m_connection->handle(m_pending);
                            // the line is cleared, so the next one starts fresh (§10.3)
                            m_pending.clear();
                            // a notification takes no reply, so nothing joins the output (§10.3, §12.2)
                            if (reply) {
                                // the reply joins the output, which is flushed when the read is done (§10.3)
                                m_output += *reply;
                                // the newline frames the reply for the next read on the client side (§10.3)
                                m_output.push_back('\n');
                            }
                            // a reply that cannot be written ends the conversation (§10.3)
                            if (!flush_output())
                                return;
                            // the loop continues, because one read may carry several lines (§10.3)
                            continue;
                        }
                        // any other byte joins the line, which is how the request accumulates (§10.3)
                        m_pending.push_back(byte);
                    }
                }
            }

            // write whatever the output buffer holds, retaining the remainder when the client cannot take it now (§10.3)
            [[nodiscard]] bool flush_output() {
                // a client that is gone owns no output, which a prior read already released (§10.3)
                if (!m_connected)
                    return false;
                // the buffer is drained in order, because a reply is a sequence of bytes on the wire (§10.3)
                while (m_output_offset < m_output.size()) {
                    // the write starts at the first undelivered byte, which keeps a large reply linear (§10.3)
                    DWORD written = 0;
                    // the request is chunked, because a nonblocking pipe refuses a write larger than its buffer (§10.3)
                    const std::size_t remaining = m_output.size() - m_output_offset;
                    const DWORD request = static_cast<DWORD>(remaining < 16u * 1024u ? remaining : 16u * 1024u);
                    if (!::WriteFile(m_pipe, m_output.data() + m_output_offset, request, &written, nullptr)) {
                        const DWORD error = ::GetLastError();
                        // a full pipe buffer keeps the remainder for the next frame rather than dropping it (§10.3)
                        if (error == ERROR_NO_DATA) {
                            // a closed peer also reports no data, which a peek tells apart from a full buffer (§10.3)
                            DWORD available = 0;
                            if (!::PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &available, nullptr)) {
                                drop_client();
                                return false;
                            }
                            return true;
                        }
                        // a write error is the peer going away, which ends only this conversation (§10.3)
                        drop_client();
                        return false;
                    }
                    // a write that moved nothing is retried next frame, because a spin here would stall the ui (§10.3)
                    if (written == 0)
                        return true;
                    // the delivered bytes are counted, because erasing them would rewrite the tail on every send (§10.3)
                    m_output_offset += written;
                }
                // the whole reply is delivered, so the buffer is released and the next reply starts fresh (§10.3)
                m_output.clear();
                m_output_offset = 0;
                return true;
            }

            // release the connected client and everything the conversation buffered (§10.3)
            void drop_client() {
                // destroy connection before borrowed registry (§10.3, AD-20)
                m_connection.reset();
                // a partial request belongs to a client that is gone (§10.3)
                m_pending.clear();
                // a partial reply belongs to a client that is gone (§10.3)
                m_output.clear();
                m_output_offset = 0;
                // the pipe is disconnected, which is what tells the peer the conversation ended (§10.3)
                if (m_connected) {
                    ::DisconnectNamedPipe(m_pipe);
                    m_connected = false;
                }
            }

            // the pipe the server listens on, which is invalid until the endpoint is bound (§10.3)
            HANDLE m_pipe = INVALID_HANDLE_VALUE;

            // the user the pipe and its token are restricted to, which every peer is checked against (§10.3, AD-10)
            std::vector<BYTE> m_user_sid;

            // whether a client is connected, which decides whether a read or an accept runs (§10.3)
            bool m_connected = false;

            // the last refusal, which the host logs (§10.3, AD-10)
            std::string m_error;

            // the partial line the client is sending, because a request may straddle two frames (§10.3)
            std::string m_pending;

            // the reply the client has not taken yet, because a non blocking write may be partial (§10.3)
            std::string m_output;

            // the delivered prefix the next write skips, because erasing it per write would copy the whole tail (§10.3)
            std::size_t m_output_offset = 0;

            // protocol: raw default, MCP with host (§10.3, AD-20)
            std::unique_ptr<AutomationConnection> m_connection;

            // how a client connection is built, which is the seam the host uses to install MCP (§10.3, AD-20)
            ConnectionFactory m_factory;

            // the commands this server runs, which the allow list governs (§10.3, AD-10)
            AutomationRegistry& m_registry;

            // the token this run published, which every connection authenticates against (§10.3, AD-10)
            std::string m_token;
        };
    } // namespace

    bool available() {
        // a named pipe exists on every windows build this project supports (§10.3, AD-10)
        return true;
    }

    std::unique_ptr<AutomationTransport> build(AutomationRegistry& registry) {
        // the transport is handed back by value, which the host owns (§10.3)
        return std::make_unique<WindowsPipeTransport>(registry);
    }
} // namespace albor::automation::platform
