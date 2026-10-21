#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "automation/host/automation_host.h"
#include "automation/host/host_state.h"
#include "core/dev_hook.h"
#include "core/json.h"
#include "core/paint.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/render_context.h"
#include "render/render_executor.h"
#include "window/window_metrics.h"

TEST(AutomationHost, AHostStartsWhenBound) {
    // arrange a dummy root, which the host only checks for null before it starts the server (§10.3)
    int dummy_root = 42;
    // act bind the dummy root to the host
    albor::automation::AutomationHost::instance().bind(albor::dev::WindowBinding{.root = &dummy_root});
    // assert the host reports running and published paths
    albor::automation::AutomationHost& host = albor::automation::AutomationHost::instance();
    EXPECT_TRUE(host.running());
    EXPECT_FALSE(host.paths().socket.empty());
    EXPECT_FALSE(host.paths().token.empty());
}

TEST(AutomationHost, AHostStopsWhenUnbound) {
    // arrange a dummy root, which is released in the same test that binds it (§10.3)
    int dummy_root = 99;
    // act bind then unbind the dummy root
    albor::automation::AutomationHost::instance().bind(albor::dev::WindowBinding{.root = &dummy_root});
    albor::automation::AutomationHost& host = albor::automation::AutomationHost::instance();
    EXPECT_TRUE(host.running());
    albor::automation::AutomationHost::instance().unbind(&dummy_root);
    // assert the last close stops the endpoint, because no tree is left to observe (§10.3)
    EXPECT_FALSE(host.running());
}

TEST(HostBindingsChecks, CarriesEveryHandleIntoTheActiveBinding) {
    // arrange one tree whose five handles are distinct addresses, which is what the seam hands over (§12.1)
    albor::automation::HostBindings bindings;
    int root = 1;
    int engine = 2;
    int text = 3;
    int surface = 4;
    int executor = 5;
    // act the tree is bound, which is the only entry point the host has (§10.3)
    bindings.bind(albor::dev::WindowBinding{.root = &root, .engine = &engine, .text = &text, .surface = &surface, .executor = &executor, .has_tree = true});
    // assert every handle arrived, because a capture needs the surface and the executor too (§6.3)
    const albor::automation::HostBinding* active = bindings.active();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(bindings.count(), 1U);
    EXPECT_EQ(active->root, &root);
    EXPECT_EQ(active->engine, &engine);
    EXPECT_EQ(active->text, &text);
    EXPECT_EQ(active->surface, &surface);
    EXPECT_EQ(active->executor, &executor);
    EXPECT_TRUE(active->has_tree);
}

TEST(HostBindingsChecks, RebindingTheSameRootReplacesEveryHandle) {
    // arrange one tree bound with a first surface, which a resize or a rewire would replace (§6.3)
    albor::automation::HostBindings bindings;
    int root = 1;
    int first_surface = 2;
    int second_surface = 3;
    bindings.bind(albor::dev::WindowBinding{.root = &root, .surface = &first_surface});
    // act the same root is bound again with a second surface (§10.3)
    bindings.bind(albor::dev::WindowBinding{.root = &root, .surface = &second_surface});
    // assert the tree appears once and the new handle wins, because a stale surface would capture the old layer (§6.3)
    ASSERT_NE(bindings.active(), nullptr);
    EXPECT_EQ(bindings.count(), 1U);
    EXPECT_EQ(bindings.active()->surface, &second_surface);
}

TEST(HostBindingsChecks, RebindingTheSameRootMovesItToTheFront) {
    // arrange two trees, where the second is the active one (§10.3)
    albor::automation::HostBindings bindings;
    int first_root = 1;
    int second_root = 2;
    int first_surface = 3;
    int rebound_surface = 4;
    bindings.bind(albor::dev::WindowBinding{.root = &first_root, .surface = &first_surface});
    bindings.bind(albor::dev::WindowBinding{.root = &second_root});
    // act the first tree is bound again, which makes it the window the user just focused (§10.3)
    bindings.bind(albor::dev::WindowBinding{.root = &first_root, .surface = &rebound_surface});
    // assert both trees survive and the rebound one is active with its new handle (§10.3)
    ASSERT_NE(bindings.active(), nullptr);
    EXPECT_EQ(bindings.count(), 2U);
    EXPECT_EQ(bindings.active()->root, &first_root);
    EXPECT_EQ(bindings.active()->surface, &rebound_surface);
}

TEST(HostBindingsChecks, UnbindingTheActiveWindowFallsBackWithItsOwnHandles) {
    // arrange two trees with distinct surfaces, where the second is active (§10.3)
    albor::automation::HostBindings bindings;
    int first_root = 1;
    int second_root = 2;
    int first_surface = 3;
    int second_surface = 4;
    bindings.bind(albor::dev::WindowBinding{.root = &first_root, .surface = &first_surface});
    bindings.bind(albor::dev::WindowBinding{.root = &second_root, .surface = &second_surface});
    // act the active window closes, which leaves the first one to answer (§10.3, §5.2)
    bindings.unbind(&second_root);
    // assert the fallback carries its own surface, because the closed window's layer is gone (§5.2, §6.3)
    ASSERT_NE(bindings.active(), nullptr);
    EXPECT_EQ(bindings.count(), 1U);
    EXPECT_EQ(bindings.active()->root, &first_root);
    EXPECT_EQ(bindings.active()->surface, &first_surface);
}

TEST(HostBindingsChecks, AHandDrawnBindingOffersNoCaptureHandles) {
    // arrange a tree offered without a surface, which is what a headless host hands over (§10.3)
    albor::automation::HostBindings bindings;
    int root = 1;
    // act the tree is bound with a root alone (§10.3)
    bindings.bind(albor::dev::WindowBinding{.root = &root});
    // assert neither capture handle arrived, which is the state a capture must refuse (§6.3)
    ASSERT_NE(bindings.active(), nullptr);
    EXPECT_EQ(bindings.active()->surface, nullptr);
    EXPECT_EQ(bindings.active()->executor, nullptr);
    EXPECT_FALSE(bindings.active()->has_tree);
}

TEST(HostBindingsChecks, NoBindingSurvivesTheLastUnbind) {
    // arrange an empty stack, which is the state before any window exists (§10.3)
    albor::automation::HostBindings bindings;
    int root = 1;
    int surface = 2;
    // act and assert nothing is active before the first bind, because a capture has nothing to read (§10.3)
    EXPECT_TRUE(bindings.empty());
    EXPECT_EQ(bindings.active(), nullptr);
    bindings.bind(albor::dev::WindowBinding{.root = &root, .surface = &surface});
    bindings.unbind(&root);
    // assert the last close leaves no handle behind, which is what stops a capture after teardown (§5.2)
    EXPECT_TRUE(bindings.empty());
    EXPECT_EQ(bindings.active(), nullptr);
}

TEST(HostBindingsChecks, AHandDrawnWindowDoesNotShadowTheTree) {
    // arrange a real window under a hand drawn one, which is the order hello-world leaves behind (§6.1)
    albor::automation::HostBindings bindings;
    int tree_root = 1;
    int hand_drawn_root = 2;
    int hand_drawn_surface = 3;
    bindings.bind(albor::dev::WindowBinding{.root = &tree_root, .has_tree = true});
    bindings.bind(albor::dev::WindowBinding{.root = &hand_drawn_root, .surface = &hand_drawn_surface});
    // act and assert the capture still reads the newest window, because it carries a surface (§6.3)
    ASSERT_NE(bindings.active(), nullptr);
    EXPECT_EQ(bindings.active()->root, &hand_drawn_root);
    // assert the semantic commands keep the tree, because a hand drawn window would answer an empty one (§10.3)
    ASSERT_NE(bindings.active_tree(), nullptr);
    EXPECT_EQ(bindings.active_tree()->root, &tree_root);
}

// the client end of one conversation, which is what an out of process tool holds (§10.3)
struct HostClient
{
    // the connected descriptor, which the destructor closes
    int fd = -1;

    // the bytes read but not yet consumed, which a line protocol keeps between reads
    std::string pending;

    // connect to the published socket, which is the only endpoint the host offers (§10.3)
    static HostClient connect_to(const std::filesystem::path& socket);

    // send one line, which the transport frames on the newline (§10.3)
    void send(std::string_view line);

    // read one reply, pumping the host while the transport has nothing to hand over (§10.3)
    std::string read_line(albor::automation::AutomationHost& host);

    // close the descriptor, which ends only this conversation (§10.3)
    ~HostClient() {
        if (fd >= 0)
            ::close(fd);
    }
};

HostClient HostClient::connect_to(const std::filesystem::path& socket) {
    HostClient client;
    client.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    // a descriptor the process cannot open leaves the test to assert on the failure (§10.3)
    if (client.fd < 0)
        return client;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket.string().c_str());
    // the connection is attempted once, because a host that is not listening has nothing to answer (§10.3)
    if (::connect(client.fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(client.fd);
        client.fd = -1;
        return client;
    }
    // a read timeout keeps the test from hanging when a reply never arrives (§10.3)
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;
    ::setsockopt(client.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return client;
}

void HostClient::send(std::string_view line) {
    // the newline is what completes the request, so it is appended here (§10.3)
    std::string bytes(line);
    bytes.push_back('\n');
    // the whole line is written, which is what a client does regardless of chunking (§10.3)
    (void)::write(fd, bytes.data(), bytes.size());
}

std::string HostClient::read_line(albor::automation::AutomationHost& host) {
    // the deadline bounds the wait, because a transport that never answers must fail the test (§10.3)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        // a buffered line is handed back before the transport is pumped again (§10.3)
        const std::size_t newline = pending.find('\n');
        if (newline != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            return line;
        }
        // the host serves one pass, which is what an application frame does (§6.1, §10.3)
        host.pump();
        char chunk[4096];
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got > 0)
            pending.append(chunk, static_cast<std::size_t>(got));
    }
    return {};
}

TEST(AutomationHost, AScreenshotCrossesTheSocketAsAnImage) {
    // arrange a real surface holding one presented frame, which is what a capture reads (§6.3)
    albor::software::SoftwareRendererBackend backend;
    const albor::WindowMetrics metrics = albor::WindowMetrics::from_logical(albor::Size{8.0f, 8.0f}, 1.0f);
    const std::unique_ptr<albor::RenderSurface> surface = backend.create_surface(albor::NativeWindowHandle{}, metrics);
    ASSERT_NE(surface, nullptr);
    albor::RenderContext& frame = surface->begin_frame();
    frame.draw_rect(albor::Rect::from_origin_size(albor::Point{0.0f, 0.0f}, albor::Size{8.0f, 8.0f}), albor::Paint{.fill = albor::Color::rgb(0.1f, 0.2f, 0.8f)});
    surface->end_frame();
    albor::RenderExecutor executor(&backend);
    // act the host observes the surface, which starts the endpoint and permits the capture (§10.3)
    int root = 0;
    albor::automation::AutomationHost& host = albor::automation::AutomationHost::instance();
    host.bind(albor::dev::WindowBinding{.root = &root, .surface = surface.get(), .executor = &executor, .has_tree = false});
    ASSERT_TRUE(host.running());
    // the token the client presents is the one the transport published (§10.3, AD-10)
    std::ifstream token_file(host.paths().token);
    std::string token;
    std::getline(token_file, token);
    ASSERT_FALSE(token.empty());
    HostClient client = HostClient::connect_to(host.paths().socket);
    ASSERT_GE(client.fd, 0);
    // act the client handshakes, lists the surface and asks for a screenshot (§10.3, AD-20)
    const std::string handshake = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"_meta\":{\"albor/token\":" + albor::core::Json(token).to_string() + "}}}";
    client.send(handshake);
    const std::string initialized = client.read_line(host);
    client.send("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
    const std::string listing = client.read_line(host);
    client.send("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"capture.screenshot\",\"arguments\":{}}}");
    const std::string answered = client.read_line(host);
    // assert the handshake was accepted, because a refused one explains every later failure (§10.3, AD-10)
    const std::optional<albor::core::Json> init_reply = albor::core::Json::parse(initialized);
    ASSERT_TRUE(init_reply.has_value());
    EXPECT_EQ(init_reply->member("error"), nullptr);
    // assert the default surface lists twelve tools, which is what the host permits (§10.3, AD-10)
    const std::optional<albor::core::Json> list_reply = albor::core::Json::parse(listing);
    ASSERT_TRUE(list_reply.has_value());
    const albor::core::Json* tools = list_reply->member("result")->member("tools");
    ASSERT_NE(tools, nullptr);
    EXPECT_EQ(tools->as_array().size(), 12U);
    // assert the capture answered an image block, which is what a png is on this protocol (§10.3)
    const std::optional<albor::core::Json> call_reply = albor::core::Json::parse(answered);
    ASSERT_TRUE(call_reply.has_value());
    const albor::core::Json* content = call_reply->member("result")->member("content");
    ASSERT_NE(content, nullptr);
    ASSERT_EQ(content->as_array().size(), 1U);
    const albor::core::Json& block = content->as_array().at(0);
    EXPECT_EQ(block.member("type")->as_string(), "image");
    EXPECT_EQ(block.member("mimeType")->as_string(), "image/png");
    // act the base64 is decoded, which is the only form an image block carries (§10.3)
    const std::string encoded = block.member("data")->as_string();
    ASSERT_GT(encoded.size(), 8U);
    std::string png;
    for (std::size_t at = 0; at + 3 < encoded.size(); at += 4) {
        // every four characters carry three bytes, which the alphabet fixes (§10.3)
        std::uint32_t group = 0;
        for (std::size_t index = 0; index < 4; index++) {
            const char symbol = encoded[at + index];
            const std::uint32_t value = symbol >= 'A' && symbol <= 'Z' ? static_cast<std::uint32_t>(symbol - 'A') : symbol >= 'a' && symbol <= 'z' ? static_cast<std::uint32_t>(symbol - 'a' + 26) : symbol >= '0' && symbol <= '9' ? static_cast<std::uint32_t>(symbol - '0' + 52) : symbol == '+' ? 62U : 63U;
            group = (group << 6) | value;
        }
        png.push_back(static_cast<char>((group >> 16) & 0xFFU));
        png.push_back(static_cast<char>((group >> 8) & 0xFFU));
        png.push_back(static_cast<char>(group & 0xFFU));
    }
    // assert the decoded bytes open with the png signature, which no other answer would carry (ISO 15948)
    ASSERT_GE(png.size(), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[0]), 0x89U);
    EXPECT_EQ(png.substr(1, 3), "PNG");
    // cleanup the binding releases the endpoint, which the next test starts again (§10.3)
    host.unbind(&root);
}
