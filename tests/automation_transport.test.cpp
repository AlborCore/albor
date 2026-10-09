#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "automation/automation_session.h"
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

    std::string read_line(int client) {
        std::string out;
        char byte = 0;
        while (::read(client, &byte, 1) == 1 && byte != '\n')
            out.push_back(byte);
        return out;
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

    std::thread server([&] { (void)transport->serve_one(); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    write_line(client, encode_handshake(token));
    EXPECT_EQ(read_line(client), "!ok ");
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!ok yes");
    ::close(client);
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

    std::thread server([&] { (void)transport->serve_one(); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    write_line(client, encode_handshake("not-the-token"));
    EXPECT_EQ(read_line(client), "!refused authentication failed");
    ::close(client);
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

    std::thread server([&] { (void)transport->serve_one(); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!refused authentication required");
    ::close(client);
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

    std::thread server([&] { (void)transport->serve_one(); });
    const int client = connect_to(socket);
    ASSERT_NE(client, -1);
    write_line(client, encode_handshake(transport->session_token()));
    (void)read_line(client);
    write_line(client, "probe");
    EXPECT_EQ(read_line(client), "!refused command not allowed: probe");
    ::close(client);
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
