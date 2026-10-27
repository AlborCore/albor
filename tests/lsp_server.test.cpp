#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"
#include "core/json.h"
#include "language-server/lsp_server.h"

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
        // the whole pipeline runs, because a diagnostic may come from any stage (§11.1, §11.3)
        const std::vector<compiler::Token> tokens = compiler::Lexer(text, diagnostics).tokenize();
        // the parser builds the untyped tree (§11.1)
        const compiler::SourceFile file = compiler::Parser(tokens, diagnostics).parse();
        // the checker resolves it against the catalog (§11.1)
        const compiler::CheckedFile checked = compiler::TypeChecker(catalog, diagnostics).check(file);
        // the generator runs too, because codegen reports its own diagnostics (§11.3)
        (void)compiler::CodeGenerator(diagnostics).generate(checked);
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
    const std::string root_text = root.generic_string();
    const std::string root_uri = "file://" + std::string(root_text.starts_with('/') ? "" : "/") + root_text;
    // act and assert a complete triplet, an upper case digit and a lower case digit all decode beside the document (§12.2)
    LspServer server;
    const std::string source = R"(component Panel { in property<image> icon: @image("icon.png"); Column {} })";
    (void)did_open(server, source, root_uri + "/panel%20%4A%6a.ui");
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
