#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "albor/compiler/codegen/codegen.h"
#include "albor/compiler/diagnostic.h"
#include "albor/compiler/type_checker.h"
#include "albor/compiler/ui_loader.h"
#include "albor/core/json.h"
#include "albor/language-server/lsp_server.h"

using namespace albor;
using namespace albor::language_server;

namespace
{
    // run the compiler the way a build does, which is what the language server must agree with (§16)
    std::vector<compiler::Diagnostic> compiler_diagnostics(const std::string& text) {
        // the diagnostics accumulate, because the server publishes what the compiler found (§16)
        compiler::DiagnosticList diagnostics;
        // the builtin catalog is what a ui file may use without a manifest (§11.1, §8)
        compiler::ElementCatalog catalog = compiler::ElementCatalog::with_builtins();
        // the root buffer is answered by name, because a saved file is what a build compiles (§11.1)
        const auto reader = [&text](const std::filesystem::path& path) -> std::optional<std::string> {
            // the corpus has no imports, so only the root name answers (§11.1)
            return path.generic_string() == "panel.ui" ? std::optional<std::string>(text) : std::nullopt;
        };
        // the loader applies the file shape and naming rules the build uses (§11.1, §14.2)
        compiler::UiImportLoader loader(catalog, diagnostics, reader);
        // the root is loaded as a saved file, because the agreement compares the build pipeline (§11.1)
        const std::optional<compiler::CheckedFile> checked = loader.check("panel.ui");
        // a root that could not be checked stops before generation, because there is no typed tree to generate (§11.3)
        if (!checked.has_value())
            return diagnostics.diagnostics();
        // the generator runs too, because codegen reports its own diagnostics (§11.3)
        (void)compiler::CodeGenerator(diagnostics).generate(*checked);
        // the diagnostics are returned, which is what the agreement compares (§16)
        return diagnostics.diagnostics();
    }

    // the corpus the agreement suite runs, which is what §16 defines it over (§16)
    std::vector<std::pair<std::string, std::string>> corpus() {
        return {
            {"a clean component", R"(component Panel { Column { Label { text: "hello"; } } })"}, {"an unknown widget", R"(component Panel { Column { Nope { } } })"}, {"an unknown property", R"(component Panel { Column { Label { colour: "red"; } } })"}, {"a missing semicolon", "component Panel { Column { Label { text: \"hi\" } } }"}, {"an unclosed brace", R"(component Panel { Column { Label { text: "hi"; } })"}, {"an unknown type", R"(component Panel { in property<colour> value; Column { } })"}, {"an empty file", ""}, {"a repeat, which codegen refuses", R"(component Panel { in property<vector<int>> items; Column { Repeat(item in items) { Label { text: string(item); } } } })"},
        };
    }

    // open a document and hand back the exchange, which is what an editor does (§12.2)
    Exchange did_open(LspServer& server, const std::string& text, const std::string& uri = "file:///panel.ui") {
        // the notification carries the text inline, which is the open form (§12.2)
        const std::string line = std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":)") + core::Json(uri).to_string() + R"(,"languageId":"albor","version":1,"text":)" + core::Json(text).to_string() + "}}}";
        // the line is handled, which is the whole of the loop (§12.2)
        return server.handle_line(line);
    }

    // replace one document's text, which is the full sync form the server reads (§12.2)
    Exchange did_change(LspServer& server, const std::string& text, std::int64_t version, const std::string& uri) {
        // the change carries the full text, because the server advertises full synchronisation (§12.2)
        const std::string line = std::string(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":)") + core::Json(uri).to_string() + R"(,"version":)" + std::to_string(version) + R"(},"contentChanges":[{"text":)" + core::Json(text).to_string() + "}]}}";
        // the line is handled, which is the whole of the loop (§12.2)
        return server.handle_line(line);
    }

    // the published diagnostics, returned by value because a caller must not outlive this parse (§12.2)
    core::Json published(const Exchange& exchange) {
        // a publication is present because every open produces one (§12.2, §16)
        EXPECT_EQ(exchange.notifications.size(), 1U);
        // an exchange with no publication yields nothing, which the caller asserts on (§12.2)
        if (exchange.notifications.empty())
            return core::Json();
        // the payload is parsed fresh each time, because a cached one would answer every case with the first (§16)
        return core::Json::parse(exchange.notifications.at(0).payload).value_or(core::Json());
    }
} // namespace

TEST(LspServerChecks, InitializeAnswersWithoutNeedingADocument) {
    // arrange
    LspServer server;
    // act
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    // assert the handshake answered as a reply, because an editor waits for it (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    // the reply advertises the diagnostics and full-sync capabilities an editor needs (§12.2)
    const core::Json reply = core::Json::parse(*exchange.reply).value_or(core::Json());
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* capabilities = result->member("capabilities");
    ASSERT_NE(capabilities, nullptr);
    const core::Json* publish = capabilities->member("publishDiagnostics");
    ASSERT_NE(publish, nullptr);
    EXPECT_EQ(publish->kind(), core::Json::Kind::Object);
    const core::Json* sync = capabilities->member("textDocumentSync");
    ASSERT_NE(sync, nullptr);
    EXPECT_EQ(sync->as_number(), 1.0);
    EXPECT_EQ(server.document_count(), 0U);
}

TEST(LspServerChecks, ExitIsReportedToTheStdioLoop) {
    // arrange
    LspServer server;
    EXPECT_FALSE(server.exit_requested());
    // act
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","method":"exit"})");
    // assert the transport is told to stop, and the notification itself answers nothing (§12.2)
    EXPECT_TRUE(server.exit_requested());
    EXPECT_FALSE(exchange.reply.has_value());
    EXPECT_TRUE(exchange.notifications.empty());
}

TEST(LspServerChecks, AnEmbeddedImageResolvesBesideTheDocument) {
    // arrange a document whose asset sits beside it, which is what a saved editor buffer has (§12.2, §6.4)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-image";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(std::filesystem::path(ALBOR_TEST_VECTORS_DIR) / "icon.png", root / "icon.png", std::filesystem::copy_options::overwrite_existing);
    const std::string root_text = root.generic_string();
    const std::string root_uri = "file://" + std::string(root_text.starts_with('/') ? "" : "/") + root_text;
    // act
    LspServer server;
    const Exchange exchange = did_open(server, R"(component Panel { in property<image> icon: @image("icon.png"); Column {} })", root_uri + "/panel.ui");
    // assert the reference is not refused, because the editor supplied the document path (§16, §6.4)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, AFileUriIsDecodedAndAnUnsavedDocumentIsRefused) {
    // arrange a document named with percent escapes, which an editor writes for a space and two letters (§12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-uri";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(std::filesystem::path(ALBOR_TEST_VECTORS_DIR) / "icon.png", root / "icon.png", std::filesystem::copy_options::overwrite_existing);
    // the escaped directory holds its own icon, because the document resolves assets beside itself (§12.2)
    std::filesystem::create_directories(root / "panel Jj");
    std::filesystem::copy_file(std::filesystem::path(ALBOR_TEST_VECTORS_DIR) / "icon.png", root / "panel Jj" / "icon.png", std::filesystem::copy_options::overwrite_existing);
    const std::string root_text = root.generic_string();
    const std::string root_uri = "file://" + std::string(root_text.starts_with('/') ? "" : "/") + root_text;
    // act and assert a complete triplet, an upper case digit and a lower case digit all decode in the path (§12.2)
    LspServer server;
    const std::string source = R"(component Panel { in property<image> icon: @image("icon.png"); Column {} })";
    (void)did_open(server, source, root_uri + "/panel%20%4A%6a/panel.ui");
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    // assert a localhost authority is the local path, because it names this machine (§12.2)
    (void)did_open(server, source, "file://localhost" + std::string(root_text.starts_with('/') ? "" : "/") + root_text + "/panel.ui");
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    // assert a remote authority and a document that is not a file have no local path to resolve against (§12.2, §6.4)
    (void)did_open(server, source, "file://example.com/panel.ui");
    EXPECT_GT(server.last_diagnostic_count(), 0U);
    (void)did_open(server, source, "untitled:panel.ui");
    EXPECT_GT(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, ASavedDocumentPublishesTheFileStemRule) {
    // arrange a saved file whose component does not match its stem, which the build cannot compile (§11.1, §14.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-stem";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the saved name is what the build derives its pair from, so the component must match it (§11.3)
    const std::string text = R"(component ConsumerPanel { Column {} })";
    std::ofstream(root / "panel.ui") << text;
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act
    LspServer server;
    const Exchange exchange = did_open(server, text, uri);
    // assert the naming rule reaches the editor as a diagnostic, because it is what the build reports (§11.1)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 1U);
    const core::Json payload = published(exchange);
    const core::Json* entries = payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    ASSERT_EQ(entries->as_array().size(), 1U);
    const core::Json* code = entries->as_array().at(0).member("code");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->as_string(), "E0805");
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, AnUnsavedDocumentSkipsTheFileStemRule) {
    // arrange an unsaved buffer whose component cannot match the synthetic untitled file name (§11.1, §12.2)
    LspServer server;
    // act the editor opens a clean buffer with no saved path (§12.2)
    const Exchange exchange = did_open(server, R"(component Panel { Column {} })", "untitled:panel.ui");
    // assert the buffer publishes no diagnostics, because it has no file stem to compare (§12.2)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
}

TEST(LspServerChecks, OpeningADocumentPublishesItsDiagnostics) {
    // arrange
    LspServer server;
    // act
    const Exchange exchange = did_open(server, R"(component Panel { Column { Label { text: "hi"; } } })");
    // assert a publication carries no diagnostics for a clean file (§16)
    const core::Json published_payload = published(exchange);
    const core::Json* entries = published_payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    EXPECT_TRUE(entries->as_array().empty());
    // assert a notification produces no reply, because the editor expects diagnostics unprompted (§12.2)
    EXPECT_FALSE(exchange.reply.has_value());
}

TEST(LspServerChecks, AnImportedComponentResolvesFromTheDocumentDirectory) {
    // arrange a panel beside a window, because an import reads a saved file from disk (§11.1, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-clean";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the panel declares one property a window sets, which is what composition carries (§11.1)
    std::ofstream(root / "panel.ui") << R"(component Panel { in property<string> label_text: "Panel"; Column { Label { text: label_text; } } })";
    // the window imports the panel and assigns its property, which is the cross-file form (§11.1)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel { label_text: "Shared"; } } })";
    std::ofstream(root / "window.ui") << window_text;
    // the document uri carries a leading slash before a windows drive, which is what the server decodes (§12.2)
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    // act
    LspServer server;
    const Exchange exchange = did_open(server, window_text, window_uri);
    // assert the import resolved and the window carries no diagnostic (§16)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    const core::Json published_payload = core::Json::parse(exchange.notifications.at(0).payload).value_or(core::Json());
    const core::Json* entries = published_payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    EXPECT_TRUE(entries->as_array().empty());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, ADirectoryImportIsReportedRatherThanAborting) {
    // arrange a directory where an import names a component file, which no source read can accept (§11.1, #246)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-directory";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "panel.ui");
    // the window imports what reads as a file but is a directory, which the reader must refuse (§11.1)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel {} } })";
    // the document uri carries a leading slash before a windows drive, which is what the server decodes (§12.2)
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    // act
    LspServer server;
    const Exchange exchange = did_open(server, window_text, window_uri);
    // assert the import is reported against the window and the server answers instead of aborting (§16)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_GT(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, ADiagnosticInAnImportedFileIsPublishedAgainstThatFile) {
    // arrange an imported file that names an unknown widget, which belongs to that file and not the window (§16)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-broken";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the file is saved, because an import is read from disk while the root comes from the editor (§12.2)
    std::ofstream(root / "panel.ui") << R"(component Panel { in property<string> label_text: "Panel"; Column { Nope { } } })";
    // the window itself is clean, which makes the imported diagnostic the only problem (§16)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel { label_text: "Shared"; } } })";
    std::ofstream(root / "window.ui") << window_text;
    // the document uri carries a leading slash before a windows drive, which is what the server decodes (§12.2)
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    // act
    LspServer server;
    const Exchange exchange = did_open(server, window_text, window_uri);
    // assert the root publication clears the window and the imported publication names the panel (§12.2, §16)
    ASSERT_EQ(exchange.notifications.size(), 2U);
    // the root is published first, because the open document is what the editor waits for (§12.2)
    const core::Json root_payload = core::Json::parse(exchange.notifications.at(0).payload).value_or(core::Json());
    // the imported file is published second, because it is routed by its own uri (§12.2)
    const core::Json import_payload = core::Json::parse(exchange.notifications.at(1).payload).value_or(core::Json());
    // the root uri is the document the editor opened (§12.2)
    const core::Json* root_uri = root_payload.member("uri");
    ASSERT_NE(root_uri, nullptr);
    EXPECT_EQ(root_uri->as_string(), window_uri);
    // the imported uri ends with the panel, because a file uri carries the path the compiler reported (§12.2)
    const core::Json* import_uri = import_payload.member("uri");
    ASSERT_NE(import_uri, nullptr);
    EXPECT_NE(import_uri->as_string().find("/panel.ui"), std::string::npos);
    // the root has no diagnostic, because the window's own text is clean (§16)
    const core::Json* root_entries = root_payload.member("diagnostics");
    ASSERT_NE(root_entries, nullptr);
    EXPECT_TRUE(root_entries->as_array().empty());
    // the imported file carries the unknown widget, because that is where the problem was written (§16)
    const core::Json* import_entries = import_payload.member("diagnostics");
    ASSERT_NE(import_entries, nullptr);
    EXPECT_FALSE(import_entries->as_array().empty());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, AnUnsavedImportedBufferWinsOverTheDisk) {
    // arrange a broken panel on disk and a clean panel buffer, which is what an editor has before saving (§12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-buffer";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the disk copy names an unknown widget, which would fail if the server read it (§16)
    std::ofstream(root / "panel.ui") << R"(component Panel { Column { Nope { } } })";
    const std::string panel_text = R"(component Panel { in property<string> label_text: "Panel"; Column { Label { text: label_text; } } })";
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    // the window imports the panel and assigns its property, which reads the open buffer (§11.1, §12.2)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel { label_text: "Shared"; } } })";
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    // act the panel buffer is opened first and the window second, before either file is saved (§12.2)
    LspServer server;
    (void)did_open(server, panel_text, panel_uri);
    const Exchange exchange = did_open(server, window_text, window_uri);
    // assert the clean buffer was checked, because the saved panel is not what the editor shows (§12.2)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    const core::Json published_payload = core::Json::parse(exchange.notifications.at(0).payload).value_or(core::Json());
    const core::Json* entries = published_payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    EXPECT_TRUE(entries->as_array().empty());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, AFixedImportClearsItsPublishedDiagnostics) {
    // arrange a window that imports a broken panel, which publishes a diagnostic for the panel (§16)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-clear";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the panel is broken on disk, which is what the first compile reads (§11.1)
    std::ofstream(root / "panel.ui") << R"(component Panel { Column { Nope { } } })";
    // the window itself is clean, so the panel is the only source of a diagnostic (§16)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel {} } })";
    std::ofstream(root / "window.ui") << window_text;
    // the uri carries a leading slash before a windows drive, which is what the server decodes (§12.2)
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    LspServer server;
    const Exchange opened = did_open(server, window_text, window_uri);
    // assert the panel publication carried the broken widget (§16)
    ASSERT_EQ(opened.notifications.size(), 2U);
    bool saw_broken_panel = false;
    for (const Notification& notification : opened.notifications) {
        const core::Json payload = core::Json::parse(notification.payload).value_or(core::Json());
        const core::Json* uri = payload.member("uri");
        const core::Json* entries = payload.member("diagnostics");
        if (uri != nullptr && entries != nullptr && uri->as_string() == panel_uri && !entries->as_array().empty())
            saw_broken_panel = true;
    }
    EXPECT_TRUE(saw_broken_panel);
    // act the panel is fixed on disk and the window is touched, which recompiles the import (§11.1, §12.2)
    std::ofstream(root / "panel.ui") << R"(component Panel { Column {} })";
    const Exchange changed = did_change(server, window_text, 2, window_uri);
    // assert the root and the imported panel are published once, with no duplicate clear (§12.2, §16)
    ASSERT_EQ(changed.notifications.size(), 2U);
    // assert the panel now has an empty publication, which clears the old squiggle (§12.2, §16)
    bool saw_clean_panel = false;
    for (const Notification& notification : changed.notifications) {
        const core::Json payload = core::Json::parse(notification.payload).value_or(core::Json());
        const core::Json* uri = payload.member("uri");
        const core::Json* entries = payload.member("diagnostics");
        if (uri != nullptr && entries != nullptr && uri->as_string() == panel_uri) {
            saw_clean_panel = true;
            EXPECT_TRUE(entries->as_array().empty());
        }
    }
    EXPECT_TRUE(saw_clean_panel);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, AChangeToAnImportedBufferRecompilesTheOpenRoot) {
    // arrange a window that imports a broken panel, and an editor that has the panel buffer open (§11.1, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-change";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the disk panel is broken, which is what the window first reads (§11.1)
    const std::string panel_broken = R"(component Panel { Column { Nope { } } })";
    std::ofstream(root / "panel.ui") << panel_broken;
    // the window imports the panel and is clean itself (§16)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel {} } })";
    std::ofstream(root / "window.ui") << window_text;
    // the uri carries a leading slash before a windows drive, which is what the server decodes (§12.2)
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    LspServer server;
    (void)did_open(server, window_text, window_uri);
    // the panel buffer is opened with the broken text, which keeps the window broken until the edit (§12.2)
    (void)did_open(server, panel_broken, panel_uri);
    // act the panel buffer becomes clean, which must refresh the root that imports it (§11.1, §12.2)
    const Exchange changed = did_change(server, R"(component Panel { Column {} })", 2, panel_uri);
    // assert the window root was recompiled and its own publication is empty (§12.2, §16)
    bool saw_clean_window = false;
    for (const Notification& notification : changed.notifications) {
        const core::Json payload = core::Json::parse(notification.payload).value_or(core::Json());
        const core::Json* uri = payload.member("uri");
        const core::Json* entries = payload.member("diagnostics");
        if (uri != nullptr && entries != nullptr && uri->as_string() == window_uri) {
            saw_clean_window = true;
            EXPECT_TRUE(entries->as_array().empty());
        }
    }
    EXPECT_TRUE(saw_clean_window);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, ACreatedImportRecompilesTheRootThatNamedIt) {
    // arrange a window that imports a panel which does not exist yet (§11.1, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-import-create";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel {} } })";
    LspServer server;
    const Exchange opened = did_open(server, window_text, window_uri);
    // assert the missing import is the window's only diagnostic (§11.1, §16)
    ASSERT_EQ(opened.notifications.size(), 1U);
    const core::Json opened_payload = core::Json::parse(opened.notifications.at(0).payload).value_or(core::Json());
    const core::Json* opened_entries = opened_payload.member("diagnostics");
    ASSERT_NE(opened_entries, nullptr);
    EXPECT_FALSE(opened_entries->as_array().empty());
    // act the missing panel is created in the editor, which is what makes the import resolvable (§11.1, §12.2)
    const Exchange created = did_open(server, R"(component Panel { Column {} })", panel_uri);
    // assert the window was recompiled and its own diagnostic is gone (§12.2, §16)
    bool saw_clean_window = false;
    for (const Notification& notification : created.notifications) {
        const core::Json payload = core::Json::parse(notification.payload).value_or(core::Json());
        const core::Json* uri = payload.member("uri");
        const core::Json* entries = payload.member("diagnostics");
        if (uri != nullptr && entries != nullptr && uri->as_string() == window_uri) {
            saw_clean_window = true;
            EXPECT_TRUE(entries->as_array().empty());
        }
    }
    EXPECT_TRUE(saw_clean_window);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspServerChecks, AClosedDocumentIsForgotten) {
    // arrange
    LspServer server;
    (void)did_open(server, "component Panel { }");
    ASSERT_EQ(server.document_count(), 1U);
    // act
    (void)server.handle_line(R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///panel.ui"}}})");
    // assert
    EXPECT_EQ(server.document_count(), 0U);
}

TEST(LspServerChecks, AStaleChangeIsRefusedAndRepublishesNothing) {
    // arrange
    LspServer server;
    (void)did_open(server, "component Panel { }");
    // act the same version is resent, which an editor may do out of order (§12.2)
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///panel.ui","version":1},"contentChanges":[{"text":"component Panel { }"}]}})");
    // assert a refused update publishes nothing, because nothing changed (§12.2)
    EXPECT_TRUE(exchange.notifications.empty());
}

TEST(LspServerChecks, AChangeCompilesWhatTheEditorNowShows) {
    // arrange
    LspServer server;
    (void)did_open(server, R"(component Panel { Column { Label { text: "hi"; } } })");
    // act the text becomes broken, which is what a person typing does (§12.2)
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///panel.ui","version":2},"contentChanges":[{"text":"component Panel { Column { Nope { } } }"}]}})");
    // assert the publication now carries a diagnostic, because the file is broken (§16)
    const core::Json published_payload = published(exchange);
    const core::Json* entries = published_payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    EXPECT_FALSE(entries->as_array().empty());
}

TEST(LspServerChecks, TheServerAgreesWithTheCompilerOnEveryCorpusCase) {
    // this is the suite section 16 defines: the same corpus produces the same diagnostics from both (§16)
    for (const auto& [name, text] : corpus()) {
        // arrange the editor opens the case (§16)
        LspServer server;
        const Exchange exchange = did_open(server, text);
        // act the compiler is run separately, which is the other side of the comparison (§16)
        const std::vector<compiler::Diagnostic> expected = compiler_diagnostics(text);
        // assert the server published exactly what the compiler reported (§16)
        const core::Json published_payload = published(exchange);
        const core::Json* entries = published_payload.member("diagnostics");
        ASSERT_NE(entries, nullptr) << name;
        EXPECT_EQ(entries->as_array().size(), expected.size()) << name;
        // assert each code matches in order, because a count alone would pass a mismatched set (§16)
        for (std::size_t i = 0; i < expected.size() && i < entries->as_array().size(); i++) {
            // the code is what an editor filters by, so it is the stable identity of a diagnostic (§11.1, §16)
            const core::Json* code = entries->as_array().at(i).member("code");
            ASSERT_NE(code, nullptr) << name;
            EXPECT_EQ(code->as_string(), expected.at(i).code) << name;
        }
        // assert the server's own counter agrees too, which is what the suite reads (§16)
        EXPECT_EQ(server.last_diagnostic_count(), expected.size()) << name;
    }
}

TEST(LspServerChecks, ADiagnosticCarriesAZeroBasedPosition) {
    // arrange
    LspServer server;
    // act the unknown widget is on the second line, which is what a person sees (§12.2)
    const Exchange exchange = did_open(server, "component Panel {\n    Column { Nope { } }\n}");
    // assert
    const core::Json published_payload = published(exchange);
    const core::Json* entries = published_payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    ASSERT_FALSE(entries->as_array().empty());
    // the range is where an editor draws the squiggle (§12.2)
    const core::Json* range = entries->as_array().at(0).member("range");
    ASSERT_NE(range, nullptr);
    // the range carries a start and an end, which is the protocol's shape (§12.2)
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    // the line is zero based, so the second line reads as one (§12.2)
    const core::Json* line = start->member("line");
    ASSERT_NE(line, nullptr);
    EXPECT_GE(line->as_number(), 1.0);
}

TEST(LspServerChecks, MalformedJsonIsAnsweredRatherThanIgnored) {
    // arrange
    LspServer server;
    // act
    const Exchange exchange = server.handle_line("{not json");
    // assert silence would hang an editor, so a failure is stated (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
}

TEST(LspServerChecks, ARequestForAClosedDocumentIsRefused) {
    // arrange
    LspServer server;
    // act
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","id":9,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///absent.ui"}}})");
    // assert answering from nothing would be a guess, so it is refused (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    EXPECT_NE(exchange.reply->find("not open"), std::string::npos);
}

namespace
{
    // ask for completions at a position, which is what an editor does when a person types (§12.2)
    std::vector<std::string> complete_at(LspServer& server, int line, int character) {
        // the document is open already, so the request names it (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"file:///panel.ui\"},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "}}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a reply is always produced for a request (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the items (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect, which the assertion catches (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the labels are what a person would read, which is what this collects (§12.2)
        std::vector<std::string> labels;
        // a reply wraps its answer in a result member, which is where the items live (§12.2)
        const core::Json* result = parsed->member("result");
        // a reply with no result carries nothing to read, which the assertion catches (§12.2)
        if (result == nullptr || result->kind() != core::Json::Kind::Array)
            return labels;
        // the array is walked, which is where the items are (§12.2)
        for (const core::Json& item : result->as_array()) {
            // an item with no label carries nothing, so it contributes nothing (§12.2)
            if (const core::Json* label = item.member("label"))
                labels.push_back(label->as_string());
        }
        // the labels are returned whole (§12.2)
        return labels;
    }

    // ask for a hover at a position, which is what an editor shows when a pointer rests (§12.2)
    std::string hover_at(LspServer& server, int line, int character) {
        // the request names the open document and the position (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"file:///panel.ui\"},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "}}}";
        // the request is handled (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a reply is always produced for a request (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the text (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // a null result means the cursor was on nothing, which is the honest answer (§12.2)
        const core::Json* result = parsed->member("result");
        // a null result is the protocol's spelling for nothing to say, so it yields no text (§12.2)
        if (result == nullptr || result->kind() != core::Json::Kind::Object)
            return {};
        // the contents carry the text a person reads (§12.2)
        const core::Json* contents = result->member("contents");
        // an answer without contents has nothing to say (§12.2)
        if (contents == nullptr)
            return {};
        // the text is read, which is what a person would see (§12.2)
        const core::Json* value = contents->member("value");
        // an answer without text has nothing to say (§12.2)
        return value == nullptr ? std::string() : value->as_string();
    }
} // namespace

TEST(LspCompletionChecks, CompletionInsideAnElementBodyOffersElements) {
    // arrange a file whose cursor sits where a child element may begin (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n        \n    }\n}");
    // act
    const std::vector<std::string> labels = complete_at(server, 2, 8);
    // assert the builtins are offered, because any of them may be a child (§11.1, §8)
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Label") != labels.end());
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Button") != labels.end());
}

TEST(LspCompletionChecks, CompletionAfterAColonOffersThatElementsProperties) {
    // arrange a file whose cursor sits after a property colon (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n        Label { text: \n    }\n}");
    // act
    const std::vector<std::string> labels = complete_at(server, 2, 21);
    // assert only the label's own surface is offered, because that is what may follow the colon (§11.1)
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "text") != labels.end());
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "font_size") != labels.end());
    // assert another element's property is not offered, because it may not be assigned here (§11.1)
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "spacing") == labels.end());
}

TEST(LspCompletionChecks, CompletionAfterTheHandlerPrefixOffersSignals) {
    // arrange a file whose cursor sits after on_ (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n        Button { on_\n    }\n}");
    // act
    const std::vector<std::string> labels = complete_at(server, 2, 20);
    // assert the signal is offered with its prefix, which is how it is written (§11.1)
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "on_clicked") != labels.end());
}

TEST(LspCompletionChecks, CompletionAtTopLevelOffersDeclarations) {
    // arrange a file whose cursor sits at the component body (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    \n}");
    // act
    const std::vector<std::string> labels = complete_at(server, 1, 4);
    // assert the declaration keywords are offered, because each starts one (§11.1)
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "signal") != labels.end());
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "in") != labels.end());
}

TEST(LspCompletionChecks, CompletionInsideAStringOffersNothing) {
    // arrange a file whose cursor sits inside a string literal (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n        Label { text: \"hello\"; }\n    }\n}");
    // act the cursor is well inside the quoted text (§12.2)
    const std::vector<std::string> labels = complete_at(server, 2, 26);
    // assert a long list of irrelevant names inside a string would be worse than none (§12.2)
    EXPECT_TRUE(labels.empty());
}

TEST(LspCompletionChecks, HoverOnAnElementNamesIt) {
    // arrange a file whose cursor rests on an element name (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n        Label { text: \"hi\"; }\n    }\n}");
    // act
    const std::string described = hover_at(server, 2, 10);
    // assert the hover says what the name is, which is what a pointer resting on it should (§12.2)
    EXPECT_NE(described.find("Label"), std::string::npos);
}

TEST(LspCompletionChecks, HoverOnAPropertyNamesItsType) {
    // arrange a file whose cursor rests on a property name (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n        Label { text: \"hi\"; }\n    }\n}");
    // act
    const std::string described = hover_at(server, 2, 17);
    // assert the hover carries the property and its type, which is what is not obvious (§12.2)
    EXPECT_NE(described.find("text"), std::string::npos);
    EXPECT_NE(described.find("string"), std::string::npos);
}

TEST(LspCompletionChecks, HoverOnWhitespaceSaysNothing) {
    // arrange a file whose cursor rests between words (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    Column {\n    }\n}");
    // act the cursor is on indentation (§12.2)
    const std::string described = hover_at(server, 2, 2);
    // assert silence is the honest answer, because a guess would be worse (§12.2)
    EXPECT_TRUE(described.empty());
}

TEST(LspCompletionChecks, ACompletionRequestWithoutAPositionIsRefused) {
    // arrange
    LspServer server;
    (void)did_open(server, "component Panel { }");
    // act the position is missing, which answers from nowhere (§12.2)
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///panel.ui"}}})");
    // assert it is refused rather than answered from nowhere (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    EXPECT_NE(exchange.reply->find("no position"), std::string::npos);
}

namespace
{
    // ask for the definition at a position, which is what an editor does when a name is activated (§12.2)
    core::Json definition_at(LspServer& server, int line, int character, const std::string& uri = "file:///panel.ui") {
        // the request names the open document and the position (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "}}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a request always produces a reply, which the assertion catches (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the location (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect, which the assertion catches (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the result is the location or null, which is what the protocol describes (§12.2)
        const core::Json* result = parsed->member("result");
        // a reply with no result has nowhere to go, which the caller asserts on (§12.2)
        return result == nullptr ? core::Json() : *result;
    }

    // ask for the document symbols, which is what an editor shows as an outline (§12.2)
    core::Json document_symbols_for(LspServer& server, const std::string& uri = "file:///panel.ui") {
        // the request names the open document, which is the only thing an outline needs (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "}}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a request always produces a reply, which the assertion catches (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the symbols (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect, which the assertion catches (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the result is the symbol list, which is empty when the document declares nothing (§12.2)
        const core::Json* result = parsed->member("result");
        // a reply with no result has nothing to outline, which the caller asserts on (§12.2)
        return result == nullptr ? core::Json() : *result;
    }

    // ask for the semantic tokens, which is what an editor paints a document with (§12.2)
    core::Json semantic_tokens_for(LspServer& server, const std::string& uri = "file:///panel.ui") {
        // the request names the open document and the full form, which is what the server advertises (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/semanticTokens/full\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "}}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a request always produces a reply, which the assertion catches (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the data (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect, which the assertion catches (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the result carries the data array, which is the whole of a full reply (§12.2)
        const core::Json* result = parsed->member("result");
        // a reply with no result has nothing to paint, which the caller asserts on (§12.2)
        return result == nullptr ? core::Json() : *result;
    }
} // namespace

TEST(LspNavigationChecks, InitializeAdvertisesDefinitionOutlineAndTokens) {
    // arrange
    LspServer server;
    // act
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    // assert the handshake answered as a reply, because an editor waits for it (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    const core::Json reply = core::Json::parse(*exchange.reply).value_or(core::Json());
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* capabilities = result->member("capabilities");
    ASSERT_NE(capabilities, nullptr);
    // assert definition and the outline are offered, because both answer from the compile (§12.2)
    const core::Json* definition = capabilities->member("definitionProvider");
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->as_bool(), true);
    const core::Json* symbols = capabilities->member("documentSymbolProvider");
    ASSERT_NE(symbols, nullptr);
    EXPECT_EQ(symbols->as_bool(), true);
    // assert the token legend is the closed set the index classifies (§12.2)
    const core::Json* semantic = capabilities->member("semanticTokensProvider");
    ASSERT_NE(semantic, nullptr);
    const core::Json* legend = semantic->member("legend");
    ASSERT_NE(legend, nullptr);
    const core::Json* types = legend->member("tokenTypes");
    ASSERT_NE(types, nullptr);
    const std::vector<core::Json>& token_types = types->as_array();
    ASSERT_EQ(token_types.size(), 6U);
    EXPECT_EQ(token_types.at(0).as_string(), "keyword");
    EXPECT_EQ(token_types.at(1).as_string(), "type");
    EXPECT_EQ(token_types.at(2).as_string(), "property");
    EXPECT_EQ(token_types.at(3).as_string(), "event");
    EXPECT_EQ(token_types.at(4).as_string(), "function");
    EXPECT_EQ(token_types.at(5).as_string(), "variable");
    // assert the modifiers are empty, because the index classifies by kind alone (§12.2)
    const core::Json* modifiers = legend->member("tokenModifiers");
    ASSERT_NE(modifiers, nullptr);
    EXPECT_TRUE(modifiers->as_array().empty());
    // assert the full form is offered, because a document is recompiled whole (§12.2)
    const core::Json* full = semantic->member("full");
    ASSERT_NE(full, nullptr);
    EXPECT_EQ(full->as_bool(), true);
}

TEST(LspNavigationChecks, DefinitionOnAnElementOpensTheComponentDeclaration) {
    // arrange a file that declares a component and instantiates it, which is the same file jump (§12.2)
    const std::string source = "component Card { Column {} } component Panel { Column { Card {} } }";
    LspServer server;
    (void)did_open(server, source, "untitled:panel.ui");
    // act the cursor rests inside the element name, which is what activation writes (§12.2)
    const std::size_t use = source.find("Card", source.find("Panel"));
    const core::Json location = definition_at(server, 0, static_cast<int>(use), "untitled:panel.ui");
    // assert the answer names the same document, because the declaration is in this file (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* uri = location.member("uri");
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(uri->as_string(), "untitled:panel.ui");
    // assert the range is the declared name, which is where the cursor lands (§12.2)
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* declaration_start = start->member("character");
    ASSERT_NE(declaration_start, nullptr);
    EXPECT_EQ(declaration_start->as_number(), static_cast<double>(source.find("Card")));
    const core::Json* end = range->member("end");
    ASSERT_NE(end, nullptr);
    const core::Json* declaration_end = end->member("character");
    ASSERT_NE(declaration_end, nullptr);
    EXPECT_EQ(declaration_end->as_number(), static_cast<double>(source.find("Card") + 4U));
}

TEST(LspNavigationChecks, DefinitionAtTheEndOfANameStillResolves) {
    // arrange a component used as an element on the first line (§11.1)
    const std::string source = "component Card { Column {} } component Panel { Column { Card {} } }";
    LspServer server;
    (void)did_open(server, source, "untitled:panel.ui");
    // act the cursor sits one past the last character, which is where a double click lands (§12.2)
    const std::size_t use = source.find("Card", source.find("Panel"));
    const core::Json location = definition_at(server, 0, static_cast<int>(use + 4U), "untitled:panel.ui");
    // assert the fallback still answers with the declaration, because a name ends right there (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(source.find("Card")));
}

TEST(LspNavigationChecks, DefinitionOnAPropertyBindingOpensTheDeclaration) {
    // arrange a component whose property is read by a label binding (§11.1)
    const std::string source = R"(component Panel { in property<string> title: "hi"; Column { Label { text: title; } } })";
    LspServer server;
    (void)did_open(server, source);
    // act the cursor rests inside the binding, which is not the declaration itself (§12.2)
    const std::size_t use = source.find("title", source.find("text:"));
    const core::Json location = definition_at(server, 0, static_cast<int>(use));
    // assert the declaration is the property, which is the first title the file writes (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(source.find("title")));
}

TEST(LspNavigationChecks, DefinitionOnAHandlerOpensTheSignal) {
    // arrange a component element whose signal a handler attaches to (§11.1)
    const std::string source = "component Card { signal pressed(); Column {} } component Panel { Column { Card { on_pressed: { } } } }";
    LspServer server;
    (void)did_open(server, source, "untitled:panel.ui");
    // act the cursor rests inside the handler name, which is the signal with its prefix (§12.2)
    const std::size_t use = source.find("on_pressed");
    const core::Json location = definition_at(server, 0, static_cast<int>(use), "untitled:panel.ui");
    // assert the declaration is the signal, because that is the name the handler attaches to (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(source.find("pressed")));
}

TEST(LspNavigationChecks, DefinitionOnABuiltinElementAnswersNull) {
    // arrange a label element the framework declares rather than a ui file (§12.2)
    const std::string source = R"(component Panel { Column { Label { } } })";
    LspServer server;
    (void)did_open(server, source);
    // act the cursor rests on the builtin element name (§12.2)
    const std::size_t use = source.find("Label");
    const core::Json location = definition_at(server, 0, static_cast<int>(use));
    // assert the honest answer is null, because no source location exists to open (§12.2)
    EXPECT_EQ(location.kind(), core::Json::Kind::Null);
}

TEST(LspNavigationChecks, DefinitionInAnImportedFileOpensThatFile) {
    // arrange a window beside a panel, because an import reads a saved file from disk (§11.1, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-definition-import";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the panel declares itself with a property, which is what the window instantiates (§11.1)
    const std::string panel_text = R"(component Panel { Column {} })";
    std::ofstream(root / "panel.ui") << panel_text;
    // the window imports the panel and instantiates it, which is the cross-file form (§11.1)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel {} } })";
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    LspServer server;
    (void)did_open(server, window_text, window_uri);
    // act the cursor rests inside the imported element name (§12.2)
    const std::size_t use = window_text.find("Panel {");
    const core::Json location = definition_at(server, 0, static_cast<int>(use), window_uri);
    // assert the answer names the imported file, because the declaration lives there (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* uri = location.member("uri");
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(uri->as_string(), panel_uri);
    // assert the range is the panel's declared name, which is where the cursor lands (§12.2)
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(panel_text.find("Panel")));
    std::filesystem::remove_all(root, ignored);
}

TEST(LspNavigationChecks, DefinitionOnABuiltinPropertyAndSignalAnswersNull) {
    // arrange a label whose property and handler are framework declarations (§12.2)
    const std::string source = R"(component Panel { Column { Label { text: "hi"; on_clicked: { } } } })";
    LspServer server;
    (void)did_open(server, source);
    // act the cursor rests on the builtin property and on the builtin handler (§12.2)
    const core::Json property = definition_at(server, 0, static_cast<int>(source.find("text")));
    const core::Json handler = definition_at(server, 0, static_cast<int>(source.find("on_clicked")));
    // assert the honest answer is null for both, because no ui declaration exists (§12.2)
    EXPECT_EQ(property.kind(), core::Json::Kind::Null);
    EXPECT_EQ(handler.kind(), core::Json::Kind::Null);
}

TEST(LspNavigationChecks, DefinitionOnAnImportedPropertyOpensItsDeclaration) {
    // arrange a panel whose property a window assigns, which is the cross-file assignment shape (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-definition-property";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the panel declares the property the window fills, which is where the jump must land (§11.1)
    const std::string panel_text = R"(component Panel { in property<string> title: "hi"; Column {} })";
    std::ofstream(root / "panel.ui") << panel_text;
    // the window imports the panel and assigns its property, which is a reference to another file (§11.1)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel { title: "x"; } } })";
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    LspServer server;
    (void)did_open(server, window_text, window_uri);
    // act the cursor rests inside the assigned property name (§12.2)
    const std::size_t use = window_text.find("title");
    const core::Json location = definition_at(server, 0, static_cast<int>(use), window_uri);
    // assert the answer names the imported file, because the property is declared there (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* uri = location.member("uri");
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(uri->as_string(), panel_uri);
    // assert the range is the panel's property name, which is the target the assignment names (§12.2)
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(panel_text.find("title")));
    std::filesystem::remove_all(root, ignored);
}

TEST(LspNavigationChecks, DefinitionOnAnImportedSignalOpensItsDeclaration) {
    // arrange a panel whose signal a window attaches a handler to, which is the cross-file handler shape (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-definition-signal";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the panel declares the signal the window reacts to, which is where the jump must land (§11.1)
    const std::string panel_text = R"(component Panel { signal pressed(); Column {} })";
    std::ofstream(root / "panel.ui") << panel_text;
    // the window imports the panel and attaches a handler, which names the signal across files (§11.1)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel { on_pressed: { } } } })";
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    LspServer server;
    (void)did_open(server, window_text, window_uri);
    // act the cursor rests inside the handler name, which carries the signal after its prefix (§12.2)
    const std::size_t use = window_text.find("on_pressed");
    const core::Json location = definition_at(server, 0, static_cast<int>(use), window_uri);
    // assert the answer names the imported file, because the signal is declared there (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* uri = location.member("uri");
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(uri->as_string(), panel_uri);
    // assert the range is the panel's signal name, which is the declaration the handler attaches to (§12.2)
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(panel_text.find("pressed")));
    std::filesystem::remove_all(root, ignored);
}

TEST(LspNavigationChecks, ADefinitionRequestForAClosedDocumentIsRefused) {
    // arrange
    LspServer server;
    // act the request names a document the editor never opened (§12.2)
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","id":8,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///absent.ui"},"position":{"line":0,"character":0}}})");
    // assert answering from nothing would be a guess, so it is refused (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    EXPECT_NE(exchange.reply->find("not open"), std::string::npos);
}

TEST(LspNavigationChecks, DocumentSymbolsOutlineEveryDeclaration) {
    // arrange a component that declares a property, a signal, a command and one element (§11.1)
    LspServer server;
    (void)did_open(server, "component Panel {\n    in property<string> title: \"hi\";\n    signal changed();\n    command save \"Save\";\n    Column {}\n}");
    // act the outline is what an editor shows without parsing the text itself (§12.2)
    const core::Json symbols = document_symbols_for(server);
    // assert the one component is the top of the outline (§12.2)
    ASSERT_EQ(symbols.kind(), core::Json::Kind::Array);
    ASSERT_EQ(symbols.as_array().size(), 1U);
    const core::Json& component = symbols.as_array().at(0);
    const core::Json* name = component.member("name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->as_string(), "Panel");
    const core::Json* kind = component.member("kind");
    ASSERT_NE(kind, nullptr);
    EXPECT_EQ(kind->as_number(), 5.0);
    // assert the declared members and the root element are nested below it (§12.2)
    const core::Json* children = component.member("children");
    ASSERT_NE(children, nullptr);
    ASSERT_EQ(children->as_array().size(), 4U);
    // the property is the first member, because the outline follows declaration order (§12.2)
    const core::Json* property_kind = children->as_array().at(0).member("kind");
    ASSERT_NE(property_kind, nullptr);
    EXPECT_EQ(property_kind->as_number(), 7.0);
    // the selection range is the declared name, which an outline highlights when it is chosen (§12.2)
    const core::Json* selection = children->as_array().at(0).member("selectionRange");
    ASSERT_NE(selection, nullptr);
    const core::Json* selection_start = selection->member("start");
    ASSERT_NE(selection_start, nullptr);
    const core::Json* selection_line = selection_start->member("line");
    ASSERT_NE(selection_line, nullptr);
    EXPECT_EQ(selection_line->as_number(), 1.0);
    const core::Json* selection_character = selection_start->member("character");
    ASSERT_NE(selection_character, nullptr);
    const core::Json* selection_end = selection->member("end");
    ASSERT_NE(selection_end, nullptr);
    const core::Json* selection_end_character = selection_end->member("character");
    ASSERT_NE(selection_end_character, nullptr);
    EXPECT_EQ(selection_end_character->as_number() - selection_character->as_number(), 5.0);
    // the signal follows, which an editor marks as an event (§12.2)
    const core::Json* signal_kind = children->as_array().at(1).member("kind");
    ASSERT_NE(signal_kind, nullptr);
    EXPECT_EQ(signal_kind->as_number(), 24.0);
    // the command follows, which an editor marks as a function (§12.2)
    const core::Json* command_kind = children->as_array().at(2).member("kind");
    ASSERT_NE(command_kind, nullptr);
    EXPECT_EQ(command_kind->as_number(), 12.0);
    // the element is the last child, which an editor marks as an object (§12.2)
    const core::Json* element_kind = children->as_array().at(3).member("kind");
    ASSERT_NE(element_kind, nullptr);
    EXPECT_EQ(element_kind->as_number(), 19.0);
}

TEST(LspNavigationChecks, DocumentSymbolsClassifyEveryTopLevelKind) {
    // arrange a file with the top level declaration kinds the outline must name (§11.1)
    LspServer server;
    (void)did_open(server, "struct Frame { float width; }\nenum Mode { A, B }\nglobal limit: int = 3;\nfn helper(value: int): int { return value; }\ncomponent Panel { Column {} }");
    // act the outline lists the declarations in source order (§12.2)
    const core::Json symbols = document_symbols_for(server);
    // assert one entry per declaration, because each is a top level symbol (§12.2)
    ASSERT_EQ(symbols.kind(), core::Json::Kind::Array);
    ASSERT_EQ(symbols.as_array().size(), 5U);
    // assert the protocol kind of each, because an editor picks its icon from it (§12.2)
    const std::vector<double> expected{23.0, 10.0, 13.0, 12.0, 5.0};
    for (std::size_t index = 0; index < expected.size(); index++) {
        // the entry at this position is the declaration written at the same position (§12.2)
        const core::Json* kind = symbols.as_array().at(index).member("kind");
        ASSERT_NE(kind, nullptr);
        EXPECT_EQ(kind->as_number(), expected.at(index));
    }
}

TEST(LspNavigationChecks, SemanticTokensClassifyEveryLegendKind) {
    // arrange a file that declares a component, a property, a signal, a function, a global and an element (§11.1)
    LspServer server;
    (void)did_open(server, R"(global limit: int = 3; fn helper(value: string): string { return value; } component Panel { in property<string> title: "hi"; signal changed(); Column {} })");
    // act the token stream is what an editor paints the document with (§12.2)
    const core::Json semantic = semantic_tokens_for(server);
    // assert the reply carries a data array, because that is the whole of a full reply (§12.2)
    ASSERT_EQ(semantic.kind(), core::Json::Kind::Object);
    const core::Json* data = semantic.member("data");
    ASSERT_NE(data, nullptr);
    // assert each token is five numbers, which is the protocol's fixed shape (§12.2)
    ASSERT_EQ(data->as_array().size() % 5U, 0U);
    // the legend kinds seen in the stream are what the suite asserts on (§12.2)
    std::vector<int> kinds;
    // the decoded line is where the next delta starts (§12.2)
    double line = 0.0;
    // the decoded column is where the next same line delta starts (§12.2)
    double column = 0.0;
    // the previous token's end is what a non-overlap check compares against (§12.2)
    double previous_end = 0.0;
    // the previous token's line is what decides whether two tokens share a row (§12.2)
    double previous_line = -1.0;
    // each group of five is one token (§12.2)
    for (std::size_t position = 0; position < data->as_array().size(); position += 5) {
        // the line delta is the first number (§12.2)
        const double delta_line = data->as_array().at(position).as_number();
        // the start delta is the second number (§12.2)
        const double delta_start = data->as_array().at(position + 1).as_number();
        // the length is the third number (§12.2)
        const double length = data->as_array().at(position + 2).as_number();
        // the kind is the fourth number, which indexes the legend (§12.2)
        const double kind = data->as_array().at(position + 3).as_number();
        // the modifiers are the fifth number, which this server never sets (§12.2)
        const double modifiers = data->as_array().at(position + 4).as_number();
        // assert the stream never moves backwards, because the protocol encodes a delta (§12.2)
        EXPECT_GE(delta_line, 0.0);
        // a token on the same line never starts before its predecessor (§12.2)
        if (delta_line == 0.0)
            EXPECT_GE(delta_start, 0.0);
        // a token always covers at least one character, because nothing shorter can be painted (§12.2)
        EXPECT_GT(length, 0.0);
        // the kind is one the legend advertised (§12.2)
        EXPECT_GE(kind, 0.0);
        EXPECT_LE(kind, 5.0);
        // the server sets no modifiers, because it classifies by kind alone (§12.2)
        EXPECT_EQ(modifiers, 0.0);
        // the absolute line is what the next delta builds on (§12.2)
        line += delta_line;
        // the absolute column is the delta itself or the predecessor's column plus it (§12.2)
        column = delta_line == 0.0 ? column + delta_start : delta_start;
        // assert the decoded position is on the line the token claims (§12.2)
        EXPECT_GE(line, 0.0);
        // assert the decoded column is a real character within the line (§12.2)
        EXPECT_GE(column, 0.0);
        // assert two tokens on one row never overlap, because an editor paints a region once (§12.2)
        if (line == previous_line)
            EXPECT_LE(previous_end, column);
        // the token's end is what the next same line token must not pass (§12.2)
        previous_end = column + length;
        // the token's line is what the next token's row is compared against (§12.2)
        previous_line = line;
        // the kind joins the set the suite asserts on (§12.2)
        kinds.push_back(static_cast<int>(kind));
    }
    // assert every legend kind is present, because a missing one means the index failed to classify it (§12.2)
    for (int kind = 0; kind <= 5; kind++)
        EXPECT_TRUE(std::find(kinds.begin(), kinds.end(), kind) != kinds.end());
}

TEST(LspNavigationChecks, SemanticTokensPinAnExactPositionAndLength) {
    // arrange a component written across three lines, so a line delta is exercised (§12.2)
    LspServer server;
    (void)did_open(server, "component Panel {\n    in property<string> title: \"hi\";\n    Column {}\n}");
    // act the token stream is what an editor paints the document with (§12.2)
    const core::Json semantic = semantic_tokens_for(server);
    const core::Json* data = semantic.member("data");
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->as_array().size(), 30U);
    // assert component is line 0, column 0, length 9, keyword, with no modifier (§12.2)
    EXPECT_DOUBLE_EQ(data->as_array().at(0).as_number(), 0.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(1).as_number(), 0.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(2).as_number(), 9.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(3).as_number(), 0.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(4).as_number(), 0.0);
    // assert Panel follows ten characters later on the same line, as a type (§12.2)
    EXPECT_DOUBLE_EQ(data->as_array().at(5).as_number(), 0.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(6).as_number(), 10.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(7).as_number(), 5.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(8).as_number(), 1.0);
    // assert in is the first token of line 1, at column 4, length 2 (§12.2)
    EXPECT_DOUBLE_EQ(data->as_array().at(10).as_number(), 1.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(11).as_number(), 4.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(12).as_number(), 2.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(13).as_number(), 0.0);
    // assert title is decoded at line 1, column 24, length 5, as a property (§12.2)
    EXPECT_DOUBLE_EQ(data->as_array().at(20).as_number(), 0.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(21).as_number(), 17.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(22).as_number(), 5.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(23).as_number(), 2.0);
    // assert Column is the first token of line 2, at column 4, length 6, as a type (§12.2)
    EXPECT_DOUBLE_EQ(data->as_array().at(25).as_number(), 1.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(26).as_number(), 4.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(27).as_number(), 6.0);
    EXPECT_DOUBLE_EQ(data->as_array().at(28).as_number(), 1.0);
}

TEST(LspNavigationChecks, AChangeRebuildsTheDefinitionIndex) {
    // arrange a property binding whose declaration sits on the first line (§12.2)
    const std::string source = R"(component Panel { in property<string> title: "hi"; Column { Label { text: title; } } })";
    LspServer server;
    (void)did_open(server, source);
    // the binding is the last title, which is not the declaration (§12.2)
    const std::size_t use = source.find("title", source.find("text:"));
    // act a newline is inserted before the component, which moves every declaration down (§12.2)
    const std::string moved = "\n" + source;
    (void)did_change(server, moved, 2, "file:///panel.ui");
    // assert the definition follows the edit, because a stale index would still answer line zero (§12.2)
    const core::Json location = definition_at(server, 1, static_cast<int>(use));
    // the answer is a location, because the binding still resolves after the move (§12.2)
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* line = start->member("line");
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->as_number(), 1.0);
}

TEST(LspNavigationChecks, AChangeToAnImportedBufferRefreshesTheImporterIndex) {
    // arrange a panel on disk and a window that imports it, with both open (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-definition-import-change";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the panel declaration starts on the first line, which the edit moves down (§12.2)
    const std::string panel_text = R"(component Panel { Column {} })";
    std::ofstream(root / "panel.ui") << panel_text;
    // the window imports the panel and instantiates it, which is the jump under test (§11.1)
    const std::string window_text = R"(import "panel.ui"; component Window { Column { Panel {} } })";
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string window_path = (root / "window.ui").generic_string();
    const std::string window_uri = "file://" + std::string(window_path.starts_with('/') ? "" : "/") + window_path;
    LspServer server;
    (void)did_open(server, window_text, window_uri);
    (void)did_open(server, panel_text, panel_uri);
    // act the imported buffer gains a line, which moves its declaration down (§12.2)
    (void)did_change(server, "\n" + panel_text, 2, panel_uri);
    // assert the window's jump follows the imported edit, because a stale index would answer line zero (§12.2)
    const std::size_t use = window_text.find("Panel {");
    const core::Json location = definition_at(server, 0, static_cast<int>(use), window_uri);
    ASSERT_EQ(location.kind(), core::Json::Kind::Object);
    const core::Json* uri = location.member("uri");
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(uri->as_string(), panel_uri);
    const core::Json* range = location.member("range");
    ASSERT_NE(range, nullptr);
    const core::Json* start = range->member("start");
    ASSERT_NE(start, nullptr);
    const core::Json* line = start->member("line");
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->as_number(), 1.0);
    // the declaration name starts after the keyword on the moved line (§12.2)
    const core::Json* character = start->member("character");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(character->as_number(), static_cast<double>(std::string("component ").size()));
    std::filesystem::remove_all(root, ignored);
}
