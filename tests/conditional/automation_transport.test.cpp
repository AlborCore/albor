#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "automation/automation_transport.h"

using namespace albor::automation;

namespace
{
    // a short path under the temporary directory, because the kernel caps a unix address length (§10.3, AD-10)
    std::string endpoint_path(const char* name) { return std::string("/tmp/albor-auto-") + std::to_string(::getpid()) + "-" + name; }

    // connect to the endpoint as a client would, which is what proves the server is reachable (§10.3)
    int connect_to(const std::string& path) {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size());
        const int client = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0)
            return -1;
        if (::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            ::close(client);
            return -1;
        }
        return client;
    }

    void write_line(int client, const std::string& line) {
        const std::string framed = line + "\n";
        (void)::write(client, framed.data(), framed.size());
    }

    // a socket that refuses to block forever, because a hanging test hides the failure it is looking for (§2.2)
    void apply_timeout(int client) {
        // the timeout is set, so a server that never answers fails the read instead of hanging the suite (§2.2)
        timeval timeout{.tv_sec = 5, .tv_usec = 0};
        // the timeout alone bounds the read, because a non blocking descriptor would return empty at once (§2.2)
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }

    std::string read_line(int client) {
        std::string out;
        char byte = 0;
        while (::read(client, &byte, 1) == 1 && byte != '\n')
            out.push_back(byte);
        return out;
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
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    ASSERT_NE(create_transport(registry), nullptr);
}

TEST(AutomationTransport, ListeningPublishesAUsableToken) {
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("token");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));
    EXPECT_FALSE(transport->session_token().empty());
    transport->stop();
}

TEST(AutomationTransport, AClientAuthenticatesAndRunsAPermittedCommand) {
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("e2e");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));
    const std::string token = transport->session_token();

    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    apply_timeout(client);
    write_line(client, encode_handshake(token));
    EXPECT_EQ(read_line(client), "!ok ");
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!ok yes");
    ::close(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 1U);
    transport->stop();
}

TEST(AutomationTransport, AClientWithTheWrongTokenIsRefused) {
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("wrong");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));

    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    apply_timeout(client);
    write_line(client, encode_handshake("not-the-token"));
    EXPECT_EQ(read_line(client), "!refused authentication failed");
    ::close(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 0U);
    transport->stop();
}

TEST(AutomationTransport, ACommandBeforeTheHandshakeIsRefusedOverTheWire) {
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("early");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));

    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    apply_timeout(client);
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!refused authentication required");
    ::close(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 0U);
    transport->stop();
}

TEST(AutomationTransport, AnUnpermittedCommandIsRefusedOverTheWire) {
    std::size_t runs = 0;
    // the command is registered but never permitted, because registration is not consent (§10.3, AD-10)
    AutomationRegistry registry = registry_with_probe(&runs, false);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("blocked");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));

    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    apply_timeout(client);
    write_line(client, encode_handshake(transport->session_token()));
    (void)read_line(client);
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!refused command not allowed: probe");
    ::close(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 0U);
    transport->stop();
}

TEST(AutomationTransport, StoppingRemovesTheSocketFile) {
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("cleanup");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));
    ASSERT_NE(::access(socket.c_str(), F_OK), -1);
    transport->stop();
    EXPECT_EQ(::access(socket.c_str(), F_OK), -1);
}

TEST(AutomationTransport, ABoundSocketIsNotWorldReadable) {
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("perm");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));
    struct stat info{};
    ASSERT_EQ(::stat(socket.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 077, 0);
    transport->stop();
}

TEST(AutomationTransport, ServingWithNobodyConnectedReturnsAtOnce) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("idle");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));
    // act the call is timed, because a blocking accept would stall the frame loop that calls it (§6.1, §10.3)
    const auto before = std::chrono::steady_clock::now();
    (void)transport->serve_one();
    const auto elapsed = std::chrono::steady_clock::now() - before;
    // assert it returned without waiting, because an application frame must never wait for a client (§6.1, §10.3)
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 250);
    transport->stop();
}

TEST(AutomationTransport, AConnectedClientDoesNotStallTheServer) {
    // arrange
    std::size_t runs = 0;
    AutomationRegistry registry = registry_with_probe(&runs);
    const std::unique_ptr<AutomationTransport> transport = create_transport(registry);
    const std::string socket = endpoint_path("spans");
    ASSERT_TRUE(transport->listen(AutomationEndpoint{.socket_path = socket, .token_path = {}}));
    // act the client sends a handshake and then stays silent, which is what an idle tool looks like (§10.3)
    std::atomic<bool> done{false};
    std::thread server([&] { pump_until(done, *transport); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    apply_timeout(client);
    write_line(client, encode_handshake(transport->session_token()));
    EXPECT_EQ(read_line(client), "!ok ");
    // assert further passes keep returning while the client is quiet, because a frame loop calls this every frame (§6.1)
    for (int pass = 0; pass < 20; pass++) {
        const auto before = std::chrono::steady_clock::now();
        (void)transport->serve_one();
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count(), 250);
    }
    // act the client speaks again, which is what proves the conversation survived the quiet (§10.3)
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!ok yes");
    // exit
    ::close(client);
    done.store(true);
    server.join();
    EXPECT_EQ(runs, 1U);
    transport->stop();
}
