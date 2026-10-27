#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

#include "automation/host/automation_host.h"
#include "core/json.h"

using namespace albor;

namespace
{
    // open the named pipe the host published, which is the one endpoint a client connects to (§10.3)
    HANDLE connect_to(const std::filesystem::path& socket) {
        // the path the host published is the named pipe namespace, which CreateFile opens directly (§10.3)
        return ::CreateFileW(socket.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    }

    // write one framed request, which the transport reads by newline (§10.3)
    void write_line(HANDLE client, std::string_view line) {
        // the newline is what makes a request a line, which is the only framing the transport uses (§10.3)
        std::string bytes(line);
        bytes.push_back('\n');
        // the whole request is small enough for one local write (§10.3)
        DWORD written = 0;
        (void)::WriteFile(client, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }

    // read one reply, pumping the host while the pipe has nothing to hand over (§10.3)
    std::string read_line(automation::AutomationHost& host, HANDLE client) {
        // the deadline keeps a host that never answers from hanging the suite (§2.2)
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::string out;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD available = 0;
            if (!::PeekNamedPipe(client, nullptr, 0, nullptr, &available, nullptr))
                return {};
            // an idle pipe is served, because the host only answers on a pump (§10.3)
            if (available == 0) {
                host.pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            char buffer[4096];
            const DWORD want = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
            DWORD got = 0;
            if (!::ReadFile(client, buffer, want, &got, nullptr) || got == 0)
                return {};
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
} // namespace

TEST(AutomationHostWindows, AHostServesMcpOverANamedPipe) {
    // arrange a root the host observes, which is enough to start the endpoint (§10.3)
    int root = 0;
    automation::AutomationHost& host = automation::AutomationHost::instance();
    host.bind(dev::WindowBinding{.root = &root});
    ASSERT_TRUE(host.running());
    // assert the published endpoint is a named pipe rather than a filesystem socket (§10.3, AD-10)
    EXPECT_EQ(host.paths().socket.string().rfind("\\\\.\\pipe\\", 0), 0);
    // the token the client presents is the one the transport published (§10.3, AD-10)
    std::ifstream token_file(host.paths().token);
    std::string token;
    std::getline(token_file, token);
    ASSERT_FALSE(token.empty());
    const HANDLE client = connect_to(host.paths().socket);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    // act the client initializes MCP, which is the only protocol the transparent host speaks (§10.3, AD-20)
    const std::string handshake = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"_meta\":{\"albor/token\":" + core::Json(token).to_string() + "}}}";
    write_line(client, handshake);
    const std::string initialized = read_line(host, client);
    // assert the handshake was accepted, because a refused one explains every later failure (§10.3, AD-10)
    const std::optional<core::Json> init_reply = core::Json::parse(initialized);
    ASSERT_TRUE(init_reply.has_value());
    EXPECT_EQ(init_reply->member("error"), nullptr);
    // act the client lists the tools, which is the first request after a successful handshake (§10.3)
    write_line(client, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
    const std::string listing = read_line(host, client);
    const std::optional<core::Json> list_reply = core::Json::parse(listing);
    // assert the listed result exists, which proves the pipe carried a second request on the same conversation (§10.3)
    ASSERT_TRUE(list_reply.has_value());
    EXPECT_NE(list_reply->member("result"), nullptr);
    // exit the client and endpoint are released, which leaves the next test a clean host (§10.3)
    ::CloseHandle(client);
    host.unbind(&root);
    EXPECT_FALSE(host.running());
}
