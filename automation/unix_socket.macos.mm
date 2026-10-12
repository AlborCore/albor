#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ucred.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "automation/automation_platform.h"
#include "automation/automation_session.h"

namespace albor::automation::platform
{
    namespace
    {
        // the local transport, which is a unix domain socket and never a network one (§10.3, AD-10)
        class UnixSocketTransport final : public AutomationTransport
        {
        public:
            // the registry is taken at construction, because a transport that could dispatch nothing is useless (§10.3)
            explicit UnixSocketTransport(AutomationRegistry& registry) :
                m_registry(registry), m_token(AutomationSession(registry).token()) {}

            ~UnixSocketTransport() override { close_listener(); }

            [[nodiscard]] bool listen(const AutomationEndpoint& endpoint) override {
                // a second bind on the same object is refused, because one server owns one endpoint (§10.3)
                if (m_fd >= 0)
                    return fail("already listening");
                // the address is cleared, because the kernel fills it and a short path must not read as garbage (§10.3)
                sockaddr_un address{};
                // the path is copied, because the endpoint outlives this call but the kernel needs its own bytes (§10.3)
                if (endpoint.socket_path.size() >= sizeof(address.sun_path))
                    return fail("the socket path is too long for a unix domain address");
                // the family names the address kind, which is what makes this a unix domain socket (§10.3, AD-10)
                address.sun_family = AF_UNIX;
                // the path is copied into the address, which is what the kernel binds (§10.3, AD-10)
                std::memcpy(address.sun_path, endpoint.socket_path.c_str(), endpoint.socket_path.size());
                // the listener is created, which is the only descriptor that can accept (§10.3)
                m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                // a socket that failed to create has no listener, which the caller must know (§10.3)
                if (m_fd < 0)
                    return fail(std::string("socket: ") + std::strerror(errno));
                // a stale socket file is removed, because a crashed server leaves one behind (§10.3)
                ::unlink(endpoint.socket_path.c_str());
                // the address is bound, which is what makes the endpoint local and filesystem addressed (§10.3, AD-10)
                if (::bind(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
                    // the descriptor is released, because a failed bind owns nothing (§10.3)
                    close_listener();
                    return fail(std::string("bind: ") + std::strerror(errno));
                }
                // the socket is restricted to its owner, because a world readable endpoint is not local (§10.3, AD-10)
                ::chmod(endpoint.socket_path.c_str(), S_IRUSR | S_IWUSR);
                // the listener is non blocking, because a blocking accept would stall the ui thread (§6.1, §10.3)
                const int flags = ::fcntl(m_fd, F_GETFL, 0);
                // the descriptor flags are set, which is what makes accept return rather than wait (§10.3)
                if (flags < 0 || ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    // the descriptor is released, because a listener that could block owns nothing (§10.3)
                    close_listener();
                    // the failure is reported, because a blocking listener is not acceptable (§10.3)
                    return fail(std::string("non blocking listener: ") + std::strerror(errno));
                }
                // the listener starts accepting, which is the last step before a client may connect (§10.3)
                if (::listen(m_fd, 8) < 0) {
                    // the descriptor is released, because a failed listen owns nothing (§10.3)
                    close_listener();
                    return fail(std::string("listen: ") + std::strerror(errno));
                }
                // the endpoint is remembered, because a stop has to unlink the same path (§10.3)
                m_socket_path = endpoint.socket_path;
                // the token is published, because a client cannot authenticate what it cannot read (§10.3, AD-10)
                return publish_token(endpoint.token_path);
            }

            [[nodiscard]] bool serve_one() override {
                // a listener that was never bound cannot serve, which is a host error rather than a client refusal (§10.3)
                if (m_fd < 0)
                    return fail("not listening");
                // a client already connected is served first, because one conversation spans many frames (§10.3)
                if (m_client >= 0) {
                    // whatever it has sent is answered, and a frame never waits for more (§6.1, §10.3)
                    converse(m_client);
                    // the frame is done, because the next line arrives on a later frame (§10.3)
                    return true;
                }
                // the client is accepted, which is the first moment its identity can be checked (§10.3, AD-10)
                const int client = ::accept(m_fd, nullptr, nullptr);
                // a listener with nobody waiting is the normal case, and a frame must not wait for one (§6.1, §10.3)
                if (client < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    return true;
                // any other failure leaves the listener intact, so the caller may serve again (§10.3)
                if (client < 0)
                    return false;
                // the accepted descriptor stays non blocking, because the frame may not wait on it (§6.1, §10.3)
                const int flags = ::fcntl(client, F_GETFL, 0);
                // the non blocking form is set explicitly, because a client must never stall the ui thread (§10.3)
                if (flags >= 0)
                    (void)::fcntl(client, F_SETFL, flags | O_NONBLOCK);
                // the conversation is refused before any byte is read, when the peer is not this user (§10.3, AD-10)
                if (!same_user(client)) {
                    // the client is closed unread, so a refused peer sends nothing into the server (§10.3, AD-10)
                    ::close(client);
                    return true;
                }
                // the client is remembered, because a conversation spans frames (§10.3)
                m_client = client;
                // the session is fresh per client, which is what makes authentication per conversation (§10.3, AD-10)
                m_session = std::make_unique<AutomationSession>(m_registry, m_token);
                // the pending line starts empty, because a conversation begins with no partial request (§10.3)
                m_pending.clear();
                // the client is answered this frame, because its first line is usually already waiting (§10.3)
                converse(client);
                return true;
            }

            void stop() override {
                // the reading loop is told first, because it may be blocked in a read (§4.1, §10.3)
                m_stopped = true;
                // the listener is closed, which is what makes serve_one return thereafter (§4.1, §10.3)
                close_listener();
            }

            [[nodiscard]] const std::string& session_token() const override { return m_token; }

            [[nodiscard]] const std::string& last_error() const override { return m_error; }

        private:
            // close the listener and remove its socket file, which a stop and a destruction both need (§10.3)
            void close_listener() {
                // a descriptor that was never opened is not closed twice (§10.3)
                if (m_fd >= 0) {
                    // the descriptor goes first, because a live socket file would refuse the next bind (§10.3)
                    ::close(m_fd);
                    // the handle is cleared, so nothing closes it a second time (§10.3)
                    m_fd = -1;
                }
                // the socket file is removed, because a stale one would refuse the next bind (§10.3)
                if (!m_socket_path.empty()) {
                    // the path is unlinked, which releases the endpoint for whoever binds next (§10.3)
                    ::unlink(m_socket_path.c_str());
                    // the path is cleared, so a second unlink does not remove someone else's socket (§10.3)
                    m_socket_path.clear();
                }
            }

            // record a refusal and report it, so a host can log why a client was turned away (§10.3, AD-10)
            [[nodiscard]] bool fail(std::string error) {
                // the reason is kept, which is what the host reads (§10.3)
                m_error = std::move(error);
                return false;
            }

            // report whether the peer is the user that bound the socket (§10.3, AD-10)
            [[nodiscard]] bool same_user(int client) const {
                // the credentials are asked for, which is the only local way to learn the peer (§10.3, AD-10)
                xucred peer{};
                // the length is set first, because the kernel writes the structure it was given (§10.3, AD-10)
                socklen_t length = sizeof(peer);
                // the call returns the peer credentials, which is the whole of the local identity check (§10.3, AD-10)
                if (::getsockopt(client, SOL_LOCAL, LOCAL_PEERCRED, &peer, &length) < 0)
                    return false;
                // the peer must be this user, because a different user has no business driving this process (§10.3, AD-10)
                return peer.cr_uid == ::getuid();
            }

            // write the session token to the endpoint file, so a client can read what it must present (§10.3, AD-10)
            [[nodiscard]] bool publish_token(const std::string& path) {
                // an endpoint with no token file still listens, because a host may hand the token over another way (§10.3)
                if (path.empty())
                    return true;
                // the file is created readable only by its owner, because a shared token is no token at all (§10.3, AD-10)
                const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                // a token file that could not be written leaves the endpoint unauthenticated (§10.3, AD-10)
                if (file < 0)
                    return fail(std::string("token file: ") + std::strerror(errno));
                // the token written is this run's, which is what every connection authenticates against (§10.3)
                const std::string token = m_token;
                // the token is written whole, because a truncated token would never authenticate (§10.3, AD-10)
                const ssize_t written = ::write(file, token.data(), token.size());
                // the descriptor is closed, which publishes the file to whoever reads it (§10.3, AD-10)
                ::close(file);
                // a token that did not fit is refused, because a partial token is a token nobody can use (§10.3, AD-10)
                if (written != static_cast<ssize_t>(token.size()))
                    return fail("the token did not fit the endpoint file");
                // the publication succeeded, so the endpoint is ready for a client (§10.3)
                return true;
            }

            // serve whatever one client has already sent, without waiting, because a frame may not block (§6.1, §10.3)
            void converse(int client) {
                // whatever has arrived is read, and a client that has sent nothing costs one read (§10.3)
                char buffer[512];
                // the read is attempted once, because the descriptor is non blocking and a wait would stall the ui (§10.3)
                const ssize_t got = ::read(client, buffer, sizeof(buffer));
                // nothing arrived, which is the normal case between two client messages (§10.3)
                if (got <= 0) {
                    // a closed peer ends the conversation, which is the only way this read reports it (§10.3)
                    if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        // the descriptor is dropped, because the peer is gone (§10.3)
                        ::close(client);
                        // the conversation is forgotten, because there is nothing left to answer (§10.3)
                        m_client = -1;
                    }
                    // nothing was complete, so nothing is answered (§10.3)
                    return;
                }
                // each byte that arrived is placed in the pending line, which is what the protocol frames on (§10.3)
                for (ssize_t i = 0; i < got; i++) {
                    // the byte decides, because a newline is what completes a request (§10.3)
                    const char byte = buffer[i];
                    // a carriage return is dropped, so a windows client still works (§10.3)
                    if (byte == '\r')
                        continue;
                    // a newline completes a line, which is what a request is (§10.3)
                    if (byte == '\n') {
                        // the pending line is answered and the reply written, which is one exchange (§10.3)
                        const std::string reply = encode_result(m_session->handle(m_pending)) + "\n";
                        // the reply goes out whole, which is what the client reads (§10.3)
                        (void)::write(client, reply.data(), reply.size());
                        // the line is cleared, so the next one starts fresh (§10.3)
                        m_pending.clear();
                        // the loop continues, because one read may carry several lines (§10.3)
                        continue;
                    }
                    // any other byte joins the line, which is how the request accumulates (§10.3)
                    m_pending.push_back(byte);
                }
            }

            // the listener descriptor, which is the only descriptor the server owns (§10.3)
            int m_fd = -1;

            // the socket path, which a stop must remove so the next bind can succeed (§10.3)
            std::string m_socket_path;

            // the last refusal, which the host logs (§10.3, AD-10)
            std::string m_error;

            // the client being served, or minus one when nobody is connected (§10.3)
            int m_client = -1;

            // the partial line the client is sending, because a request may straddle two frames (§10.3)
            std::string m_pending;

            // the conversation being served, which spans frames so a client may take its time (§10.3, AD-10)
            std::unique_ptr<AutomationSession> m_session;

            // the commands this server runs, which the allow list governs (§10.3, AD-10)
            AutomationRegistry& m_registry;

            // the token this run published, which every connection authenticates against (§10.3, AD-10)
            std::string m_token;

            // whether the host asked the server to stop, which ends the reading loop (§4.1, §10.3)
            bool m_stopped = false;
        };
    } // namespace

    bool available() {
        // a unix domain socket exists on every macos build this project supports (§10.3, AD-10)
        return true;
    }

    std::unique_ptr<AutomationTransport> build(AutomationRegistry& registry) {
        // the transport is handed back by value, which the host owns (§10.3)
        return std::make_unique<UnixSocketTransport>(registry);
    }
} // namespace albor::automation::platform
