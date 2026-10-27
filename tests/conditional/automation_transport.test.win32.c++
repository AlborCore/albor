#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

#include "automation/automation_protocol.h"
#include "automation/automation_transport.h"

using namespace albor::automation;

namespace
{
    // the named pipe one test owns, which carries the process so parallel runs never share one (§10.3, AD-10)
    std::string pipe_name(const char* name) {
        // the process id and the test name make the pipe unique, because two hosts must never share an endpoint (§10.3)
        return "\\\\.\\pipe\\albor-auto-" + std::to_string(::GetCurrentProcessId()) + "-" + name;
    }

    // open the published pipe as a client would, which is what proves the server is reachable (§10.3)
    HANDLE connect_to(const std::string& name) {
        // the pipe name is ascii, so the wide form is a direct widening of the bytes (§10.3)
        const std::wstring wide(name.begin(), name.end());
        return ::CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    }

    // write one framed request, which is what a client sends (§10.3)
    void write_line(HANDLE client, std::string_view line) {
        // the newline is appended because the transport frames a request on it (§10.3)
        std::string bytes(line);
        bytes.push_back('\n');
        // the whole line is written once, because a local pipe accepts a small request whole (§10.3)
        DWORD written = 0;
        (void)::WriteFile(client, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }

    // read one reply, polling the pipe because a nonblocking server writes when the client is ready (§10.3)
    std::string read_line(HANDLE client) {
        // the deadline keeps a server that never answers from hanging the suite (§2.2)
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        // the reply accumulates until its framing newline arrives (§10.3)
        std::string out;
        while (std::chrono::steady_clock::now() < deadline) {
            // the available byte count is the nonblocking read, which avoids a blocking call on an idle pipe (§10.3)
            DWORD available = 0;
            if (!::PeekNamedPipe(client, nullptr, 0, nullptr, &available, nullptr)) {
                return {};
            }
            // an idle pipe yields to the server, which is where the reply is produced (§10.3)
            if (available == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            char buffer[4096];
            // the chunk is bounded by the available bytes and the local buffer, which keeps the read immediate (§10.3)
            const DWORD want = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
            DWORD got = 0;
            if (!::ReadFile(client, buffer, want, &got, nullptr) || got == 0) {
                return {};
            }
            for (DWORD index = 0; index < got; index++) {
                // a newline ends one reply, which is what the caller waits for (§10.3)
                if (buffer[index] == '\n') {
                    if (!out.empty() && out.back() == '\r')
                        out.pop_back();
                    return out;
                }
                out.push_back(buffer[index]);
            }
        }
        return {};
    }

    // pump the server until the caller is done, because one frame serves what arrived rather than a whole one (§10.3)
    void pump_until(std::atomic<bool>& done, AutomationTransport& transport) {
        // the loop runs while the caller has not finished, which is what an application frame loop does (§6.1, §10.3)
        while (!done.load()) {
            // one pass serves whatever arrived, and returns at once when nobody is waiting (§6.1, §10.3)
            (void)transport.serve_one();
            // the pass yields, because a tight loop would starve the client it is waiting for (§10.3)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    AutomationRegistry registry_with_probe(std::size_t* runs, bool permitted = true) {
        AutomationRegistry registry;
        registry.register_command("probe", [runs](const AutomationRequest&) {
            (*runs)++;
            return AutomationResult::answered("yes");
        });
        if (permitted)
            registry.allow("probe");
        return registry;
    }
} // namespace

TEST(AutomationTransport, ABuildWithATransportProvidesOne) {
    // arrange a registry with one command, which is all a transport needs to serve (§10.3)
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    // act and assert the windows build now carries a local transport rather than the old refusal (§10.3, AD-10)
    ASSERT_NE(create_transport(registry), nullptr);
}

TEST(AutomationTransport, ListeningPublishesAUsableToken) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    // act the named pipe is bound, which is what makes the endpoint reachable (§10.3)
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe_name("token"), .token_path = {}}));
    // assert the session token is nonempty, because every client must present one (§10.3, AD-10)
    EXPECT_FALSE(transport->session_token().empty());
    transport->stop();
}

TEST(AutomationTransport, TheTokenFileIsPublished) {
    // arrange a token path under the temporary directory, which is where the host publishes it (§10.3, AD-10)
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::filesystem::path token_path = std::filesystem::temp_directory_path() / ("albor-auto-" + std::to_string(::GetCurrentProcessId()) + "-token");
    // act
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe_name("file"), .token_path = token_path.string()}));
    // assert the file carries the session token, which is what a client reads before it connects (§10.3, AD-10)
    std::ifstream file(token_path);
    std::string written;
    std::getline(file, written);
    EXPECT_EQ(written, transport->session_token());
    transport->stop();
    // exit the token file is removed, because the test owns it rather than a host directory (§10.3)
    std::error_code ignored;
    std::filesystem::remove(token_path, ignored);
}

TEST(AutomationTransport, AClientAuthenticatesAndRunsAPermittedCommand) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("e2e");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    // the server runs on its own thread, because a named pipe server must serve while the client waits (§10.3)
    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const HANDLE client = connect_to(pipe);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    // act the client authenticates and runs a permitted command (§10.3, AD-10)
    write_line(client, encode_handshake(transport->session_token()));
    EXPECT_EQ(read_line(client), "!ok text ");
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!ok text yes");
    ::CloseHandle(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 1U);
    transport->stop();
}

TEST(AutomationTransport, AClientWithTheWrongTokenIsRefused) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("wrong");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const HANDLE client = connect_to(pipe);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    // act the client presents a token the session did not mint (§10.3, AD-10)
    write_line(client, encode_handshake("not-the-token"));
    EXPECT_EQ(read_line(client), "!refused authentication failed");
    ::CloseHandle(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 0U);
    transport->stop();
}

TEST(AutomationTransport, ACommandBeforeTheHandshakeIsRefusedOverTheWire) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("early");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const HANDLE client = connect_to(pipe);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    // act a command arrives before any token was proved (§10.3, AD-10)
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!refused authentication required");
    ::CloseHandle(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 0U);
    transport->stop();
}

TEST(AutomationTransport, AnUnpermittedCommandIsRefusedOverTheWire) {
    // arrange a command that is registered but never permitted (§10.3, AD-10)
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs, false);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("blocked");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const HANDLE client = connect_to(pipe);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    write_line(client, encode_handshake(transport->session_token()));
    (void)read_line(client);
    // act
    write_line(client, "probe");
    // assert the allow list refused the command, because registration is not consent (§10.3, AD-10)
    EXPECT_EQ(read_line(client), "!refused command not allowed: probe");
    ::CloseHandle(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 0U);
    transport->stop();
}

TEST(AutomationTransport, ServingWithNobodyConnectedReturnsAtOnce) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("idle");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    // act the call is timed, because a blocking accept would stall the frame loop that calls it (§6.1, §10.3)
    const auto before = std::chrono::steady_clock::now();
    (void)transport->serve_one();
    const auto elapsed = std::chrono::steady_clock::now() - before;
    // assert it returned without waiting, because an application frame must never wait for a client (§6.1, §10.3)
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 250);
    transport->stop();
}

TEST(AutomationTransport, ALargeReplyArrivesWholeAndTheNextOneFollowsIt) {
    // arrange a reply far larger than the pipe buffer, which is what a frame becomes (§10.3)
    const std::string large(256u * 1024u, 'a');
    AutomationRegistry registry;
    registry.register_command("large", [large](const AutomationRequest&) { return AutomationResult::answered(large); });
    registry.allow("large");
    registry.register_command("probe", [](const AutomationRequest&) { return AutomationResult::answered("yes"); });
    registry.allow("probe");
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("large");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const HANDLE client = connect_to(pipe);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    write_line(client, encode_handshake(transport->session_token()));
    (void)read_line(client);
    // act the large reply is requested, then a small one behind it (§10.3)
    write_line(client, "large");
    const std::string received = read_line(client);
    write_line(client, "probe");
    const std::string next = read_line(client);
    ::CloseHandle(client);
    done.store(true);
    server.join();
    // assert the whole reply arrived and the stream stayed aligned, which a prefix left behind would break (§10.3)
    EXPECT_EQ(received, "!ok text " + large);
    EXPECT_EQ(next, "!ok text yes");
    transport->stop();
}

TEST(AutomationTransport, StoppingMakesTheEndpointUnavailable) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    ASSERT_NE(transport, nullptr);
    const std::string pipe = pipe_name("cleanup");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = pipe, .token_path = {}}));
    // act
    transport->stop();
    // assert a client can no longer open the endpoint, because a stopped server owns no pipe (§10.3)
    const HANDLE client = connect_to(pipe);
    EXPECT_EQ(client, INVALID_HANDLE_VALUE);
    if (client != INVALID_HANDLE_VALUE)
        ::CloseHandle(client);
}
