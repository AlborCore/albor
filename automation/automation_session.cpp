#include "automation/automation_session.h"

#include <random>
#include <utility>

#include "automation/automation_protocol.h"

namespace albor::automation
{
    namespace
    {
        // the handshake prefix, which the session matches before it treats a line as a command (§10.3, AD-10)
        constexpr std::string_view HANDSHAKE = "!auth ";

        // mint a token for one session, which is why the source is per call and not shared (§10.3, AD-10)
        std::string mint_token() {
            // the engine is seeded from the system, because two sessions must not share a token (§10.3, AD-10)
            std::random_device seed;
            // the generator is seeded per session, which is what makes a token unpredictable (§10.3, AD-10)
            std::mt19937 engine(seed());
            // the range is the printable ascii without the separator, so a token never splits a field (§10.3)
            std::uniform_int_distribution<int> character(33, 126);
            // the token is built a character at a time, which is the whole of it (§10.3, AD-10)
            std::string token;
            // the length is fixed, because a short token is guessable (§10.3, AD-10)
            for (int i = 0; i < 32; i++) {
                // one draw per character, because drawing twice would silently drop half the entropy (§10.3, AD-10)
                const int drawn = character(engine);
                // the separator is replaced rather than skipped, so a token never contains the field break (§10.3)
                const int value = drawn == ' ' ? 'a' : drawn;
                // the character joins the token, which is what the client presents (§10.3, AD-10)
                token.push_back(static_cast<char>(value));
            }
            // the token is returned whole, which is the only copy this session keeps (§10.3, AD-10)
            return token;
        }
    } // namespace

    AutomationSession::AutomationSession(AutomationRegistry& registry) :
        m_registry(registry), m_token(mint_token()) {}

    AutomationSession::AutomationSession(AutomationRegistry& registry, std::string token) :
        m_registry(registry), m_token(std::move(token)) {}

    HandshakeResult AutomationSession::authenticate(std::string_view presented) {
        // an authenticated conversation does not re authenticate, which keeps a token from being replayed (§10.3)
        if (m_authenticated)
            return HandshakeResult{.authenticated = true, .error = {}};
        // the comparison walks both strings fully, so a wrong token leaks nothing by how long it took (§10.3, AD-10)
        unsigned char difference = presented.size() == m_token.size() ? 0 : 1;
        // the shorter of the two is walked, so a length mismatch costs the same time as a wrong byte (§10.3)
        const std::size_t shared = presented.size() < m_token.size() ? presented.size() : m_token.size();
        // each byte is folded in, so no early exit discloses a matching prefix (§10.3, AD-10)
        for (std::size_t i = 0; i < shared; i++)
            difference |= static_cast<unsigned char>(presented[i] ^ m_token[i]);
        // a token of the wrong length is refused without saying so (§10.3, AD-10)
        if (presented.size() != m_token.size())
            difference = 1;
        // the conversation is authenticated only when every byte agreed (§10.3, AD-10)
        m_authenticated = difference == 0;
        // the refusal never states the expected token, which would hand it to whoever asked (§10.3, AD-10)
        return HandshakeResult{.authenticated = m_authenticated, .error = m_authenticated ? std::string() : std::string("authentication failed")};
    }

    bool AutomationSession::authenticated() const { return m_authenticated; }

    AutomationResult AutomationSession::handle(std::string_view line) {
        // a handshake is answered by the handshake rule, which is the only line that runs before a command (§10.3, AD-10)
        if (line.starts_with(HANDSHAKE)) {
            // the token is the rest of the line, which the handshake encoded as one field (§10.3, AD-10)
            const HandshakeResult handshake = authenticate(line.substr(HANDSHAKE.size()));
            // a successful handshake answers empty, because there is nothing to report (§10.3)
            if (handshake.authenticated)
                return AutomationResult::answered("");
            // a failed handshake is refused with the reason, which never carries the token (§10.3, AD-10)
            return AutomationResult::refused(handshake.error);
        }
        // a command before the handshake is refused, because the conversation has proved nothing (§10.3, AD-10)
        if (!m_authenticated)
            return AutomationResult::refused("authentication required");
        // the line is decoded and dispatched, which is the only path into the application (§10.3, AD-20)
        return m_registry.dispatch(decode_request(line));
    }

    const std::string& AutomationSession::token() const { return m_token; }
} // namespace albor::automation
