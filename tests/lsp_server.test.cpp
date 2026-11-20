#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "albor/compiler/codegen/codegen.h"
#include "albor/compiler/diagnostic.h"
#include "albor/compiler/lexer.h"
#include "albor/compiler/type_checker.h"
#include "albor/compiler/ui_loader.h"
#include "albor/core/file_bytes.h"
#include "albor/core/json.h"
#include "albor/language-server/lsp_server.h"
#include "albor/manifest/manifest.h"

using namespace albor;
using namespace albor::language_server;

namespace
{
    // run the compiler the way a build does, which is what the language server must agree with (§16)
    std::vector<compiler::Diagnostic> compiler_diagnostics(const std::string& text, const std::vector<std::filesystem::path>& manifests = {}) {
        // the diagnostics accumulate, because the server publishes what the compiler found (§16)
        compiler::DiagnosticList diagnostics;
        // the catalog starts with the builtin widgets, which every ui file may instantiate (§11.1, §8)
        compiler::ElementCatalog catalog = compiler::ElementCatalog::with_builtins();
        // every manifest is parsed first, because a declaration in one may name a record another declares (§11.2, AD-17)
        std::vector<manifest::ManifestFile> parsed_manifests;
        // each manifest is read, because its records and native components are what the catalog resolves (§11.2, AD-17)
        for (const std::filesystem::path& manifest : manifests) {
            // the manifest text is read whole, because its declarations are known before the ui file is checked (§11.2)
            const std::optional<std::vector<std::uint8_t>> manifest_bytes = core::read_regular_file(manifest);
            // a manifest that cannot be read contributes no declaration, which is what the server does as well (§11.2)
            if (!manifest_bytes)
                continue;
            // the module name is the file stem, which is what the glue generator names the directory after (§11.2)
            const std::string module = manifest.stem().string();
            // the bytes become the source text, because the scanner takes characters rather than a file (§11.1)
            const std::string manifest_text(manifest_bytes->begin(), manifest_bytes->end());
            // the manifest's own problems join the run, because a bad declaration is the author's problem too (§11.2)
            const std::vector<compiler::Token> manifest_tokens = compiler::Lexer(manifest_text, diagnostics).tokenize();
            // the manifest is parsed into declarations, which is where the records and the components live (§11.2, AD-17)
            parsed_manifests.push_back(manifest::ManifestParser(manifest_tokens, diagnostics).parse(module));
        }
        // every record joins first, because a component property in any manifest may name one (§11.2, AD-17, AD-13)
        for (const manifest::ManifestFile& parsed : parsed_manifests)
            manifest::register_records(parsed, catalog);
        // the components join next, which is what makes a native element constructible (§11.2, AD-17)
        for (const manifest::ManifestFile& parsed : parsed_manifests)
            manifest::register_components(parsed, catalog);
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
        compiler::CodeGenerator generator(diagnostics);
        // the catalog is handed over, because a native element constructs through its factory (§11.2, AD-17)
        generator.set_catalog(&catalog);
        // the generated output is discarded, because the agreement compares the diagnostics (§11.3)
        (void)generator.generate(*checked);
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

    // perform the handshake with a manifest list, which is how a client supplies its catalog (§11.2, §12.2)
    Exchange did_initialize(LspServer& server, const std::vector<std::filesystem::path>& manifests, const std::string& workspace_uri = {}) {
        // the paths become json strings, because the protocol carries them as text (§12.2)
        std::vector<core::Json> entries;
        // each configured path is written the way a client writes a setting (§11.2)
        for (const std::filesystem::path& manifest : manifests)
            entries.push_back(core::Json(manifest.generic_string()));
        // the initialization options carry the manifest list, which is the channel the server reads (§11.2, §12.2)
        core::Json options;
        // the list is an array of strings, which is what the server expects (§12.2)
        options.set("manifests", core::Json(std::move(entries)));
        // the parameters carry the options and, when given, the workspace a relative manifest resolves against (§12.2)
        core::Json params;
        // the options are the catalog source the handshake names (§11.2, AD-17)
        params.set("initializationOptions", std::move(options));
        // a workspace uri is what a relative manifest resolves against, which the documentation promises (§11.2, §12.2)
        if (!workspace_uri.empty()) {
            // the first folder is the root a relative path joins to (§12.2)
            core::Json folder;
            // the uri names the directory the manifest sits in (§12.2)
            folder.set("uri", core::Json(workspace_uri));
            // the folder list carries the one workspace this test names (§12.2)
            params.set("workspaceFolders", core::Json(std::vector<core::Json>{std::move(folder)}));
        }
        // the request is an object, because every protocol request is (§12.2)
        core::Json request;
        // the version identifies the protocol (§12.2)
        request.set("jsonrpc", core::Json(std::string("2.0")));
        // the identifier is what the reply echoes (§12.2)
        request.set("id", core::Json(1.0));
        // the method is the handshake, which is what configures the catalog (§12.2)
        request.set("method", core::Json(std::string("initialize")));
        // the parameters join the request, which is where the options live (§12.2)
        request.set("params", std::move(params));
        // the handshake is handled, which is the whole of the exchange (§12.2)
        return server.handle_line(request.to_string());
    }

    // report a watched manifest edit, which is how a client refreshes the catalog (§11.2, §12.2)
    Exchange did_change_watched_manifest(LspServer& server, const std::filesystem::path& manifest) {
        // the path becomes the file uri the protocol carries (§12.2)
        const std::string path = manifest.generic_string();
        // an absolute path yields a uri with a third slash after the scheme (§12.2)
        const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
        // the change is a file event, which is what the watched notification carries (§12.2)
        core::Json change;
        // the uri names the manifest the editor saw change (§11.2, §12.2)
        change.set("uri", core::Json(uri));
        // the type is changed, because the file exists before and after the edit (§12.2)
        change.set("type", core::Json(2.0));
        // the change list carries the one event this helper raises (§12.2)
        core::Json params;
        // the list is an array, because the protocol groups changes that way (§12.2)
        params.set("changes", core::Json(std::vector<core::Json>{std::move(change)}));
        // the message is an object, because every protocol message is (§12.2)
        core::Json message;
        // the version identifies the protocol (§12.2)
        message.set("jsonrpc", core::Json(std::string("2.0")));
        // the method is the watched file notification, which is what a client sends (§12.2)
        message.set("method", core::Json(std::string("workspace/didChangeWatchedFiles")));
        // the parameters join the notification, which is where the change lives (§12.2)
        message.set("params", std::move(params));
        // the notification is handled, which is the whole of the exchange (§12.2)
        return server.handle_line(message.to_string());
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
    std::vector<std::string> complete_at(LspServer& server, int line, int character, const std::string& uri = "file:///panel.ui") {
        // the document is open already, so the request names it (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "}}}";
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
    std::string hover_at(LspServer& server, int line, int character, const std::string& uri = "file:///panel.ui") {
        // the request names the open document and the position (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "}}}";
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

TEST(LspComponentCompletionChecks, CompletionOffersOwnComponentsThenImportsThenCatalog) {
    // arrange a panel that declares a base and imports a side panel, which are two component kinds (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-order";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file declares the component the panel instantiates from another file (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the hidden file sits on disk unimported, so it is outside the panel's visible scope (§12.2)
    std::ofstream(root / "hidden.ui") << R"(component Hidden { Column { } })";
    // the empty line inside the column is where a child element may begin (§11.1)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Holder {\n    in property<string> caption;\n    signal tapped();\n    Column { @children }\n}\n\ncomponent Panel : Holder {\n    Column {\n        Holder {\n            caption: \"x\";\n            on_tapped: { }\n        }\n\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the cursor rests on the empty line inside the column (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> labels = complete_at(server, 14, 8, uri);
    // assert the file's own components come first, in declaration order (§11.1)
    ASSERT_GE(labels.size(), 4U);
    EXPECT_EQ(labels.at(0), "Holder");
    EXPECT_EQ(labels.at(1), "Panel");
    // assert the imported component follows the file's own (§11.1)
    EXPECT_EQ(labels.at(2), "SidePanel");
    // assert the catalog follows the imports, because it resolves last (§11.1, §11.2)
    const auto label = std::find(labels.begin(), labels.end(), "Label");
    ASSERT_NE(label, labels.end());
    EXPECT_GT(std::distance(labels.begin(), label), 2);
    // assert a file that is on disk but not imported is never offered (§12.2)
    EXPECT_EQ(std::find(labels.begin(), labels.end(), "Hidden"), labels.end());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, CompletionOnALocalInstanceOffersItsMembers) {
    // arrange a panel whose base is instantiated inside its own content (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-local-members";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file is present because the panel imports it, so its members are in scope elsewhere (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the property and the handler sit on the lines the requests below address (§12.2)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Holder {\n    in property<string> caption;\n    signal tapped();\n    Column { @children }\n}\n\ncomponent Panel : Holder {\n    Column {\n        Holder {\n            caption: \"x\";\n            on_tapped: { }\n        }\n\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the requests sit after the property colon and after the handler prefix (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> properties = complete_at(server, 11, 20, uri);
    const std::vector<std::string> signals = complete_at(server, 12, 15, uri);
    // assert the local instance offers the property it declares (§11.1)
    EXPECT_NE(std::find(properties.begin(), properties.end(), "caption"), properties.end());
    // assert a common property is offered too, because every node arranges (§11.1, §8.2)
    EXPECT_NE(std::find(properties.begin(), properties.end(), "tooltip"), properties.end());
    // assert the handler prefix offers the local signal with its prefix (§11.1)
    EXPECT_NE(std::find(signals.begin(), signals.end(), "on_tapped"), signals.end());
    // assert the imported component's members are not offered on a local instance (§12.2)
    EXPECT_EQ(std::find(properties.begin(), properties.end(), "title"), properties.end());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, CompletionOnAnImportedInstanceOffersItsMembers) {
    // arrange a panel that instantiates the component its import declares (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-imported-members";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file declares the property and the signal the instance writes (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the local component declares a member of its own, which must not leak onto the imported instance (§12.2)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Holder {\n    in property<string> caption;\n    signal tapped();\n    Column { @children }\n}\n\ncomponent Panel : Holder {\n    Column {\n        SidePanel {\n            title: \"x\";\n            on_closed: { }\n        }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the requests sit after the property colon and after the handler prefix (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> properties = complete_at(server, 11, 18, uri);
    const std::vector<std::string> signals = complete_at(server, 12, 15, uri);
    // assert the imported instance offers the property its interface declares (§11.1)
    EXPECT_NE(std::find(properties.begin(), properties.end(), "title"), properties.end());
    // assert a common property is offered too, because every node arranges (§11.1, §8.2)
    EXPECT_NE(std::find(properties.begin(), properties.end(), "tooltip"), properties.end());
    // assert the handler prefix offers the imported signal with its prefix (§11.1)
    EXPECT_NE(std::find(signals.begin(), signals.end(), "on_closed"), signals.end());
    // assert the local component's members are not offered on an imported instance (§12.2)
    EXPECT_EQ(std::find(properties.begin(), properties.end(), "caption"), properties.end());
    EXPECT_EQ(std::find(signals.begin(), signals.end(), "on_tapped"), signals.end());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, HoverNamesALocalComponentAndItsMembers) {
    // arrange a panel whose base is instantiated inside its own content (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-local-hover";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file is present because the panel imports it, so the scope is the real one (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the instance, its property and its handler sit on the lines the hovers below address (§12.2)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Holder {\n    in property<string> caption;\n    signal tapped();\n    Column { @children }\n}\n\ncomponent Panel : Holder {\n    Column {\n        Holder {\n            caption: \"x\";\n            on_tapped: { }\n        }\n\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the pointer rests on the instance, its property and its handler (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::string component = hover_at(server, 10, 10, uri);
    const std::string property = hover_at(server, 11, 14, uri);
    const std::string signal = hover_at(server, 12, 16, uri);
    // assert the instance is named as the component it builds (§11.1, §12.2)
    EXPECT_EQ(component, "Holder component");
    // assert the property names the component it belongs to and its type (§11.1)
    EXPECT_NE(property.find("Holder.caption"), std::string::npos);
    EXPECT_NE(property.find("string"), std::string::npos);
    // assert the handler names the signal it attaches to (§11.1)
    EXPECT_EQ(signal, "Holder.tapped signal");
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, HoverNamesAnImportedComponentAndItsMembers) {
    // arrange a panel that instantiates the component its import declares (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-imported-hover";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file declares the property and the signal the instance writes (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the instance, its property and its handler sit on the lines the hovers below address (§12.2)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel {\n            title: \"x\";\n            on_closed: { }\n        }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the pointer rests on the instance, its property and its handler (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::string component = hover_at(server, 4, 10, uri);
    const std::string property = hover_at(server, 5, 14, uri);
    const std::string signal = hover_at(server, 6, 16, uri);
    // assert the instance is named as the imported component it builds (§11.1, §12.2)
    EXPECT_EQ(component, "SidePanel component");
    // assert the property names the imported component it belongs to and its type (§11.1)
    EXPECT_NE(property.find("SidePanel.title"), std::string::npos);
    EXPECT_NE(property.find("string"), std::string::npos);
    // assert the handler names the signal it attaches to (§11.1)
    EXPECT_EQ(signal, "SidePanel.closed signal");
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, ALocalComponentShadowsTheImportedName) {
    // arrange a local component named like the imported one, which the resolver prefers (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-shadow";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file declares the same name, which the local declaration shadows (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the local interface redeclares the shared property with another type, which is how shadowing is observable (§11.1)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent SidePanel {\n    in property<string> tint;\n    in property<float> title;\n    signal pressed();\n    Column { @children }\n}\n\ncomponent Panel : SidePanel {\n    Column {\n        SidePanel {\n            tint: \"red\";\n            title: 1.0;\n            on_pressed: { }\n        }\n\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the requests ask for elements, the property, the signal and two hovers (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> elements = complete_at(server, 17, 8, uri);
    const std::vector<std::string> properties = complete_at(server, 12, 17, uri);
    const std::vector<std::string> signals = complete_at(server, 14, 15, uri);
    const std::string tint_hover = hover_at(server, 12, 14, uri);
    const std::string title_hover = hover_at(server, 13, 14, uri);
    // assert the shared name is offered once, because the local declaration wins (§11.1)
    EXPECT_EQ(std::count(elements.begin(), elements.end(), "SidePanel"), 1);
    // assert the instance resolves the local property, because the local interface shadows the import (§11.1)
    EXPECT_NE(std::find(properties.begin(), properties.end(), "tint"), properties.end());
    // assert the local signal wins for the same reason (§11.1)
    EXPECT_NE(std::find(signals.begin(), signals.end(), "on_pressed"), signals.end());
    EXPECT_EQ(std::find(signals.begin(), signals.end(), "on_closed"), signals.end());
    // assert the hover reads the local member, because the local declaration wins (§11.1)
    EXPECT_EQ(tint_hover, "SidePanel.tint : string");
    // assert a shared name reads the local type rather than the hidden import's (§11.1)
    EXPECT_EQ(title_hover, "SidePanel.title : float");
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, AChangedImportStopsCompletingAndACloseRefusesTheDocument) {
    // arrange a panel that declares a base and imports a side panel (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-replaced";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file declares the component the first text names (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the first text names a local base and the import, which both must complete (§11.1)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Holder {\n    in property<string> caption;\n    Column { @children }\n}\n\ncomponent Panel : Holder {\n    Column {\n\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the first compile is asked for the two component names (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> before = complete_at(server, 9, 8, uri);
    // assert both kinds are offered before the edit (§11.1)
    EXPECT_NE(std::find(before.begin(), before.end(), "Holder"), before.end());
    EXPECT_NE(std::find(before.begin(), before.end(), "SidePanel"), before.end());
    // act the editor drops the local base and the import, which replaces the surface (§12.2)
    const std::string changed = "component Panel {\n    Column {\n\n    }\n}\n";
    (void)did_change(server, changed, 2, uri);
    const std::vector<std::string> after = complete_at(server, 2, 8, uri);
    // assert a removed component and a removed import stop completing (§11.1, §12.2)
    EXPECT_EQ(std::find(after.begin(), after.end(), "Holder"), after.end());
    EXPECT_EQ(std::find(after.begin(), after.end(), "SidePanel"), after.end());
    // assert the catalog still answers, because only the file's own surface was replaced (§11.2, AD-17)
    EXPECT_NE(std::find(after.begin(), after.end(), "Label"), after.end());
    // act the editor closes the document, which drops the retained surface (§12.2)
    const std::string close = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "}}}";
    (void)server.handle_line(close);
    const Exchange exchange = server.handle_line("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":2,\"character\":8}}}");
    // assert a closed document is refused rather than answered from a stale surface (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    EXPECT_NE(exchange.reply->find("not open"), std::string::npos);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, AComponentThatShadowsACatalogNameIsOfferedOnce) {
    // arrange a local component named like a builtin widget, which §11.1 resolves before the catalog (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-catalog-shadow";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the base declares its own property, which the builtin Label does not have (§11.1)
    const std::string text = "component Label {\n    in property<string> tint;\n    Column { @children }\n}\n\ncomponent Panel : Label {\n    Column {\n        Label {\n            tint: \"red\";\n        }\n\n    }\n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because a saved root follows the file stem rule (§11.1, §14.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the completion asks inside the column and the hover rests on the instance (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> elements = complete_at(server, 11, 8, uri);
    const std::string described = hover_at(server, 7, 10, uri);
    // assert the name is offered once, because the local declaration shadows the builtin (§11.1)
    EXPECT_EQ(std::count(elements.begin(), elements.end(), "Label"), 1);
    // assert the hover names a component rather than the builtin widget it hides (§11.1, §12.2)
    EXPECT_EQ(described, "Label component");
    std::filesystem::remove_all(root, ignored);
}

TEST(LspComponentCompletionChecks, AComponentBodyOffersTheVisibleComponents) {
    // arrange a panel that imports a component, which its own body may instantiate (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-component-declaration";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    // the imported file declares the component the body may name (§11.1)
    std::ofstream(root / "side_panel.ui") << R"(component SidePanel { in property<string> title; signal closed(); Column { } })";
    // the indented empty line is the top level of the component body, where a child may begin (§11.1)
    const std::string text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    \n}\n";
    std::ofstream(root / "panel.ui") << text;
    // the uri names the saved file, because an import resolves relative to it (§11.1, §12.2)
    const std::string path = (root / "panel.ui").generic_string();
    const std::string uri = "file://" + std::string(path.starts_with('/') ? "" : "/") + path;
    // act the cursor sits at the top level of the component body (§12.2)
    LspServer server;
    (void)did_open(server, text, uri);
    const std::vector<std::string> labels = complete_at(server, 3, 4, uri);
    // assert a declaration keyword is offered, because the body holds declarations (§11.1)
    EXPECT_NE(std::find(labels.begin(), labels.end(), "signal"), labels.end());
    // assert the imported component is offered, because the body may instantiate it (§11.1)
    EXPECT_NE(std::find(labels.begin(), labels.end(), "SidePanel"), labels.end());
    std::filesystem::remove_all(root, ignored);
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

TEST(LspNavigationChecks, InitializeAdvertisesReferencesAndRename) {
    // arrange
    LspServer server;
    // act
    const Exchange exchange = server.handle_line(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{}})");
    // assert the handshake answered as a reply, because an editor waits for it (§12.2)
    ASSERT_TRUE(exchange.reply.has_value());
    const core::Json reply = core::Json::parse(*exchange.reply).value_or(core::Json());
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* capabilities = result->member("capabilities");
    ASSERT_NE(capabilities, nullptr);
    // assert references are offered, because the index resolves every written name (§12.2)
    const core::Json* references = capabilities->member("referencesProvider");
    ASSERT_NE(references, nullptr);
    EXPECT_EQ(references->as_bool(), true);
    // assert rename is offered with its prepare request, because validation precedes the edit (§12.2)
    const core::Json* rename = capabilities->member("renameProvider");
    ASSERT_NE(rename, nullptr);
    const core::Json* prepare = rename->member("prepareProvider");
    ASSERT_NE(prepare, nullptr);
    EXPECT_EQ(prepare->as_bool(), true);
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

namespace
{
    // the manifest fixture, which declares a native component, its data type and a record (§11.2, AD-17)
    constexpr std::string_view MANIFEST_FIXTURE = R"(
native component WaveformView {
    in property<Snapshot<WaveformData>> data;
    in property<color> line_color: #00FF00;
    signal selection_changed(float time_pos);
}

record Task {
    key: string;
    title: string;
    done: bool = false;
}
)";

    // the ui fixture, which uses the manifest component and record the way an application does (§11.2, AD-17)
    constexpr std::string_view MANIFEST_UI = R"(component Panel {
    in property<Snapshot<WaveformData>> data;
    inout property<vector<Task>> tasks;

    Column {
        WaveformView {
            data: data;
        }

        Repeat(task in tasks) {
            Label { text: task.title; }
        }
    }
})";
} // namespace

TEST(LspManifestChecks, AManifestComponentAndRecordResolveInTheEditor) {
    // arrange a manifest that declares the component, its data type and the record the file names (§11.2, AD-17)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-clean";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    // act the editor supplies the manifest at initialize and opens a file that uses it (§11.2, §12.2)
    LspServer server;
    (void)did_initialize(server, {manifest});
    const Exchange exchange = did_open(server, std::string(MANIFEST_UI));
    // assert the compile resolves the manifest declarations, because that is the build catalog (§11.2, AD-17, §16)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    const core::Json payload = published(exchange);
    const core::Json* entries = payload.member("diagnostics");
    ASSERT_NE(entries, nullptr);
    EXPECT_TRUE(entries->as_array().empty());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, CompletionAndHoverOfferManifestNames) {
    // arrange a file whose cursor positions name the manifest component, property and signal (§11.2, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-completion";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    // the text keeps the manifest names on lines the requests below address (§12.2)
    const std::string text = "component Panel {\n    in property<Snapshot<WaveformData>> data;\n    Column {\n        WaveformView {\n            line_color: #00FF00;\n            on_selection_changed: { }\n        }\n    }\n}";
    LspServer server;
    (void)did_initialize(server, {manifest});
    (void)did_open(server, text);
    // act the request asks inside the column, after the colon and after the handler prefix (§12.2)
    const std::vector<std::string> elements = complete_at(server, 3, 8);
    const std::vector<std::string> properties = complete_at(server, 4, 23);
    const std::vector<std::string> signals = complete_at(server, 5, 15);
    // assert the component, its property and its signal are offered by name (§11.2, AD-17)
    EXPECT_TRUE(std::find(elements.begin(), elements.end(), "WaveformView") != elements.end());
    EXPECT_TRUE(std::find(properties.begin(), properties.end(), "line_color") != properties.end());
    EXPECT_TRUE(std::find(signals.begin(), signals.end(), "on_selection_changed") != signals.end());
    // act the hover requests rest on the component, the property and the signal (§12.2)
    const std::string component = hover_at(server, 3, 10);
    const std::string property = hover_at(server, 4, 20);
    const std::string signal = hover_at(server, 5, 20);
    // assert each hover carries the manifest name, which is what the editor shows (§11.2, §12.2)
    EXPECT_NE(component.find("WaveformView"), std::string::npos);
    EXPECT_NE(property.find("line_color"), std::string::npos);
    EXPECT_NE(property.find("color"), std::string::npos);
    EXPECT_NE(signal.find("selection_changed"), std::string::npos);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, AManifestEditRefreshesOpenRootsAndClearsStaleDiagnostics) {
    // arrange a manifest that declares nothing, so the open file cannot resolve its names yet (§11.2, AD-17)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-reload";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << "";
    LspServer server;
    (void)did_initialize(server, {manifest});
    // act the editor opens a file that needs the declarations the empty manifest does not carry (§12.2)
    const Exchange opened = did_open(server, std::string(MANIFEST_UI));
    // assert the unresolved names are reported, because the catalog holds no such declaration (§11.2, AD-17)
    ASSERT_EQ(opened.notifications.size(), 1U);
    ASSERT_GT(server.last_diagnostic_count(), 0U);
    // act the manifest gains the declarations and the client reports the saved edit (§11.2, §12.2)
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    const Exchange refreshed = did_change_watched_manifest(server, manifest);
    // assert the open root is republished clean, which clears the stale unknown-type diagnostics (§12.2, §16)
    ASSERT_FALSE(refreshed.notifications.empty());
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    bool cleared = false;
    for (const Notification& notification : refreshed.notifications) {
        // a publication names one file, which is what this test routes against the root (§12.2)
        if (notification.method != "textDocument/publishDiagnostics")
            continue;
        // the payload is parsed, because the diagnostics are the answer under test (§16)
        const core::Json payload = core::Json::parse(notification.payload).value_or(core::Json());
        // a publication for another file carries no evidence about the root (§12.2)
        const core::Json* uri = payload.member("uri");
        // a payload without a uri names no routed file, so it is skipped (§12.2)
        if (uri == nullptr || uri->kind() != core::Json::Kind::String || uri->as_string() != "file:///panel.ui")
            continue;
        // the root publication is the one this assertion reads (§12.2)
        cleared = true;
        // the entries are the squiggles an editor shows, which must now be gone (§16)
        const core::Json* entries = payload.member("diagnostics");
        ASSERT_NE(entries, nullptr);
        EXPECT_TRUE(entries->as_array().empty());
    }
    EXPECT_TRUE(cleared);
    // act the declarations are removed again, which is a manifest edit in the other direction (§11.2, §12.2)
    std::ofstream(manifest) << "";
    const Exchange broken = did_change_watched_manifest(server, manifest);
    // assert the root is recompiled against the empty catalog and the unknown names return (§11.2, §16)
    ASSERT_FALSE(broken.notifications.empty());
    EXPECT_GT(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, ARelativeManifestResolvesAgainstTheFirstWorkspaceFolder) {
    // arrange a workspace folder holding the manifest, which a client may name relative to it (§11.2, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-relative";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    std::ofstream(root / "waveform.ui-manifest") << MANIFEST_FIXTURE;
    // the workspace uri names the folder the relative path resolves against (§12.2)
    const std::string folder = root.generic_string();
    const std::string workspace_uri = "file://" + std::string(folder.starts_with('/') ? "" : "/") + folder;
    // act the client names only the file name, which joins the first workspace folder (§11.2, §12.2)
    LspServer server;
    (void)did_initialize(server, {std::filesystem::path("waveform.ui-manifest")}, workspace_uri);
    const Exchange exchange = did_open(server, std::string(MANIFEST_UI));
    // assert the resolved manifest supplied the catalog, because the file compiles cleanly (§11.2, AD-17)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, TheServerAgreesWithTheCompilerOnAManifestBackedCorpus) {
    // this is the suite section 16 defines, run over a corpus that names manifest declarations (§16)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-agreement";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    // the corpus covers the names the manifest supplies and the mistakes a reader makes with them (§16)
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"a clean manifest component and record", std::string(MANIFEST_UI)},
        {"an unknown manifest property", R"(component Panel { Column { WaveformView { nope: 1; } } })"},
        {"an unknown record field", R"(component Panel { inout property<vector<Task>> tasks; Column { Repeat(task in tasks) { Label { text: task.nope; } } } })"},
    };
    for (const auto& [name, text] : cases) {
        // arrange the editor opens the case with the manifest the build uses (§11.2, §16)
        LspServer server;
        (void)did_initialize(server, {manifest});
        const Exchange exchange = did_open(server, text);
        // act the compiler is run separately, which is the other side of the comparison (§16)
        const std::vector<compiler::Diagnostic> expected = compiler_diagnostics(text, {manifest});
        // assert the server published exactly what the compiler reported (§16)
        const core::Json published_payload = published(exchange);
        const core::Json* entries = published_payload.member("diagnostics");
        ASSERT_NE(entries, nullptr) << name;
        EXPECT_EQ(entries->as_array().size(), expected.size()) << name;
        // assert each code matches in order, because a count alone would pass a mismatched set (§16)
        for (std::size_t index = 0; index < expected.size() && index < entries->as_array().size(); index++) {
            // the code is what an editor filters by, so it is the stable identity of a diagnostic (§11.1, §16)
            const core::Json* code = entries->as_array().at(index).member("code");
            ASSERT_NE(code, nullptr) << name;
            EXPECT_EQ(code->as_string(), expected.at(index).code) << name;
        }
        // assert the server's own counter agrees too, which is what the suite reads (§16)
        EXPECT_EQ(server.last_diagnostic_count(), expected.size()) << name;
    }
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, AManifestEditRefreshesEveryOpenRoot) {
    // arrange a manifest and two open roots that both name its component (§11.2, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-every-root";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    LspServer server;
    (void)did_initialize(server, {manifest});
    const std::string first = R"(component First { Column { WaveformView { line_color: #00FF00; } } })";
    const std::string second = R"(component Second { Column { WaveformView { line_color: #00FF00; } } })";
    (void)did_open(server, first, "file:///first.ui");
    (void)did_open(server, second, "file:///second.ui");
    // act the manifest loses the component, which leaves both roots unresolved (§11.2, §12.2)
    std::ofstream(manifest) << "";
    const Exchange refreshed = did_change_watched_manifest(server, manifest);
    // assert every open root is republished, not only the last one the store hands out (§12.2, §16)
    bool first_published = false;
    bool second_published = false;
    for (const Notification& notification : refreshed.notifications) {
        // a publication for another file carries no evidence about either root (§12.2)
        if (notification.method != "textDocument/publishDiagnostics")
            continue;
        // the payload is parsed, because the uri and the diagnostics are what the assertion reads (§16)
        const core::Json payload = core::Json::parse(notification.payload).value_or(core::Json());
        // a payload without a string uri names no routed file, so it is skipped (§12.2)
        const core::Json* uri = payload.member("uri");
        // a payload that names no file carries no evidence about a root (§12.2)
        if (uri == nullptr || uri->kind() != core::Json::Kind::String)
            continue;
        // the entries are what the editor paints for that file (§16)
        const core::Json* entries = payload.member("diagnostics");
        ASSERT_NE(entries, nullptr);
        // each root is republished with the unknown name, which is what a refresh must show (§16)
        if (uri->as_string() == "file:///first.ui") {
            first_published = true;
            EXPECT_FALSE(entries->as_array().empty());
        }
        // the second root is the one a first-root-only loop would leave stale (§12.2)
        if (uri->as_string() == "file:///second.ui") {
            second_published = true;
            EXPECT_FALSE(entries->as_array().empty());
        }
    }
    EXPECT_TRUE(first_published);
    EXPECT_TRUE(second_published);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, AWatchedFileOutsideTheManifestListChangesNothing) {
    // arrange a configured manifest and an open root that resolves through it (§11.2, AD-17)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-unrelated";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    // the second manifest is on disk but not configured, so it is not a catalog source (§11.2, AD-17)
    const std::filesystem::path other = root / "other.ui-manifest";
    std::ofstream(other) << "native component OtherView {}\n";
    LspServer server;
    (void)did_initialize(server, {manifest});
    (void)did_open(server, std::string(MANIFEST_UI));
    ASSERT_EQ(server.last_diagnostic_count(), 0U);
    // act the client reports an edit to a manifest that was never configured (§12.2)
    const Exchange watched = did_change_watched_manifest(server, other);
    // assert nothing is reloaded or republished, because the catalog did not change (§11.2, §12.2)
    EXPECT_TRUE(watched.notifications.empty());
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, ARelativeManifestRefreshesFromAnAbsoluteUri) {
    // arrange a workspace folder and a relative manifest, which resolve at the handshake (§11.2, §12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-relative-refresh";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path manifest = root / "waveform.ui-manifest";
    std::ofstream(manifest) << MANIFEST_FIXTURE;
    // the workspace uri names the folder the relative path resolves against (§12.2)
    const std::string folder = root.generic_string();
    const std::string workspace_uri = "file://" + std::string(folder.starts_with('/') ? "" : "/") + folder;
    LspServer server;
    (void)did_initialize(server, {std::filesystem::path("waveform.ui-manifest")}, workspace_uri);
    (void)did_open(server, std::string(MANIFEST_UI));
    ASSERT_EQ(server.last_diagnostic_count(), 0U);
    // act the manifest is emptied and the client reports its absolute file uri (§12.2)
    std::ofstream(manifest) << "";
    const Exchange refreshed = did_change_watched_manifest(server, root / "waveform.ui-manifest");
    // assert the resolved relative path still matches the event, so the root is recompiled (§12.2, §16)
    ASSERT_FALSE(refreshed.notifications.empty());
    EXPECT_GT(server.last_diagnostic_count(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspManifestChecks, ARecordAndAComponentResolveFromSeparateManifests) {
    // arrange a component in the first manifest and the record it names in the second (§11.2, AD-17)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-manifest-separate";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path component_manifest = root / "component.ui-manifest";
    std::ofstream(component_manifest) << "native component TaskView {\n    in property<vector<Task>> tasks;\n}\n";
    const std::filesystem::path record_manifest = root / "record.ui-manifest";
    std::ofstream(record_manifest) << "record Task {\n    key: string;\n    title: string;\n}\n";
    // the ui file names the record as its own property and as the component's property (§11.2, AD-17)
    const std::string text = R"(component Panel { inout property<vector<Task>> tasks; Column { TaskView { tasks: tasks; } } })";
    // act the editor supplies both manifests, with the record declared in the one listed second (§11.2, AD-17)
    LspServer server;
    (void)did_initialize(server, {component_manifest, record_manifest});
    const Exchange exchange = did_open(server, text);
    // assert the record resolves before the component's surface is registered, because records join first (§11.2, AD-17)
    ASSERT_EQ(exchange.notifications.size(), 1U);
    EXPECT_EQ(server.last_diagnostic_count(), 0U);
    // assert the compiler resolves the same pair, because the editor and the build agree (§16)
    EXPECT_TRUE(compiler_diagnostics(text, {component_manifest, record_manifest}).empty());
    std::filesystem::remove_all(root, ignored);
}

namespace
{
    // ask for references at a position, which is what an editor does when a person asks where a name is used (§12.2)
    core::Json references_at(LspServer& server, int line, int character, const std::string& uri, bool include_declaration = true) {
        // the request names the open document, the cursor and whether the declaration is included (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/references\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "},\"context\":{\"includeDeclaration\":" + std::string(include_declaration ? "true" : "false") + "}}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a reply is always produced for a request (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the locations (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the result is a location list or null, which the protocol allows (§12.2)
        const core::Json* result = parsed->member("result");
        // a reply with no result reads as null, which the caller asserts on (§12.2)
        return result == nullptr ? core::Json() : *result;
    }

    // ask what a rename would replace, which an editor calls before it opens the rename box (§12.2)
    core::Json prepare_rename_at(LspServer& server, int line, int character, const std::string& uri) {
        // the request names the open document and the cursor (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/prepareRename\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "}}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a reply is always produced for a request (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test asserts on the range (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the result is a range and a placeholder or null, which the protocol allows (§12.2)
        const core::Json* result = parsed->member("result");
        // a reply with no result reads as null, which the caller asserts on (§12.2)
        return result == nullptr ? core::Json() : *result;
    }

    // ask for a rename, which returns the whole reply because a refusal carries an error (§12.2)
    core::Json rename_reply(LspServer& server, int line, int character, const std::string& new_name, const std::string& uri) {
        // the request names the open document, the cursor and the name to write (§12.2)
        const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/rename\",\"params\":{\"textDocument\":{\"uri\":" + core::Json(uri).to_string() + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(character) + "},\"newName\":" + core::Json(new_name).to_string() + "}}";
        // the request is handled, which is the whole of the exchange (§12.2)
        const Exchange exchange = server.handle_line(request);
        // a reply is always produced for a request (§12.2)
        EXPECT_TRUE(exchange.reply.has_value());
        // the reply is parsed, because a test reads either the edit or the refusal (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(*exchange.reply);
        // a reply that does not parse is a defect (§12.2)
        EXPECT_TRUE(parsed.has_value());
        // the whole reply is returned, because a refusal names its reason in an error (§12.2)
        return parsed.value_or(core::Json());
    }

    // the zero based line an offset sits on, which the protocol counts from (§12.2)
    int line_at(const std::string& text, std::size_t offset) {
        // the newlines before the offset are what the line number counts (§12.2)
        return static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
    }

    // the zero based column an offset sits on, which the protocol counts from (§12.2)
    int column_at(const std::string& text, std::size_t offset) {
        // the last newline before the offset starts the line (§12.2)
        const std::size_t line_start = text.rfind('\n', offset);
        // an offset on the first line starts at the document start (§12.2)
        const std::size_t start = line_start == std::string::npos ? 0 : line_start + 1;
        // the column is the distance from the line start (§12.2)
        return static_cast<int>(offset - start);
    }
} // namespace

TEST(LspReferenceChecks, ReferencesFindEveryUseInTheFile) {
    // arrange a property used twice, which is the local shape a references request answers (§12.2)
    const std::string text = "component Panel {\n    in property<string> title: \"a\";\n    Column {\n        Label { text: title; }\n        Label { text: title; }\n    }\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the request starts on the declaration, which is where the name is defined (§12.2)
    const std::size_t declaration = text.find("title");
    const core::Json with_declaration = references_at(server, line_at(text, declaration), column_at(text, declaration), uri);
    // assert the declaration and both reads are answered (§12.2)
    ASSERT_EQ(with_declaration.kind(), core::Json::Kind::Array);
    EXPECT_EQ(with_declaration.as_array().size(), 3U);
    // act the client asks for the uses alone, which is the protocol's other form (§12.2)
    const core::Json uses_only = references_at(server, line_at(text, declaration), column_at(text, declaration), uri, false);
    // assert the declaration is left out, because the client said so (§12.2)
    ASSERT_EQ(uses_only.kind(), core::Json::Kind::Array);
    EXPECT_EQ(uses_only.as_array().size(), 2U);
}

TEST(LspReferenceChecks, ReferencesCrossIntoAnImportedFile) {
    // arrange a side panel that uses its own property, which an importer alone cannot see (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-references-import";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string side_text = "component SidePanel {\n    in property<string> title;\n    Column {\n        Label { text: title; }\n    }\n}\n";
    std::ofstream(root / "side_panel.ui") << side_text;
    const std::string panel_text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel { title: \"x\"; }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << panel_text;
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string side_path = (root / "side_panel.ui").generic_string();
    const std::string side_uri = "file://" + std::string(side_path.starts_with('/') ? "" : "/") + side_path;
    LspServer server;
    (void)did_open(server, panel_text, panel_uri);
    // act the request starts on the assignment, whose declaration lives in the imported file (§12.2)
    const std::size_t assignment = panel_text.find("title");
    const core::Json result = references_at(server, line_at(panel_text, assignment), column_at(panel_text, assignment), panel_uri);
    // assert the declaration, the imported read and the assignment are all answered (§12.2)
    ASSERT_EQ(result.kind(), core::Json::Kind::Array);
    ASSERT_EQ(result.as_array().size(), 3U);
    std::size_t panel_sites = 0;
    std::size_t side_sites = 0;
    // each location is counted by the file it sits in, which is what crosses the import graph (§12.2)
    for (const core::Json& location : result.as_array()) {
        // the uri names the file the site is written in (§12.2)
        const core::Json* uri = location.member("uri");
        ASSERT_NE(uri, nullptr);
        // the open importer and the imported file are the two files this fixture uses (§12.2)
        if (uri->as_string() == panel_uri)
            panel_sites++;
        // the imported file carries the declaration and its own read (§12.2)
        if (uri->as_string() == side_uri)
            side_sites++;
    }
    // assert one site is in the open importer and two are in the imported file (§12.2)
    EXPECT_EQ(panel_sites, 1U);
    EXPECT_EQ(side_sites, 2U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspReferenceChecks, ReferencesIncludeAnOpenImporter) {
    // arrange the same pair, both open, which is what a person sees while editing the imported file (§12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-references-importer";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string side_text = "component SidePanel {\n    in property<string> title;\n    Column {\n        Label { text: title; }\n    }\n}\n";
    std::ofstream(root / "side_panel.ui") << side_text;
    const std::string panel_text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel { title: \"x\"; }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << panel_text;
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string side_path = (root / "side_panel.ui").generic_string();
    const std::string side_uri = "file://" + std::string(side_path.starts_with('/') ? "" : "/") + side_path;
    LspServer server;
    (void)did_open(server, side_text, side_uri);
    (void)did_open(server, panel_text, panel_uri);
    // act the request starts on the imported declaration, which only the importer's graph can see (§12.2)
    const std::size_t declaration = side_text.find("title");
    const core::Json result = references_at(server, line_at(side_text, declaration), column_at(side_text, declaration), side_uri);
    // assert the declaration, the imported read and the importer's assignment are each answered once (§12.2)
    ASSERT_EQ(result.kind(), core::Json::Kind::Array);
    EXPECT_EQ(result.as_array().size(), 3U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspReferenceChecks, ReferencesOnABuiltinNameAnswerNothing) {
    // arrange a builtin property, which the framework declares outside the file (§12.2)
    const std::string text = "component Panel { Column { Label { text: \"hi\"; } } }";
    LspServer server;
    (void)did_open(server, text);
    // act the request starts on the builtin name (§12.2)
    const std::size_t builtin = text.find("text");
    const core::Json result = references_at(server, 0, column_at(text, builtin), "file:///panel.ui");
    // assert a name with no source declaration has nothing to reference (§12.2)
    EXPECT_EQ(result.kind(), core::Json::Kind::Null);
}

TEST(LspRenameChecks, PrepareRenameReturnsTheRangeAndName) {
    // arrange a property with one read, which are the two places a rename may start from (§12.2)
    const std::string text = "component Panel {\n    in property<string> title: \"a\";\n    Column { Label { text: title; } }\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the request starts on the declaration and then on the read (§12.2)
    const std::size_t declaration = text.find("title");
    const std::size_t use = text.find("title", declaration + 1);
    const core::Json on_declaration = prepare_rename_at(server, line_at(text, declaration), column_at(text, declaration), uri);
    const core::Json on_use = prepare_rename_at(server, line_at(text, use), column_at(text, use), uri);
    // assert both name the property, which is what the rename box prefills (§12.2)
    ASSERT_EQ(on_declaration.kind(), core::Json::Kind::Object);
    ASSERT_EQ(on_use.kind(), core::Json::Kind::Object);
    const core::Json* declaration_name = on_declaration.member("placeholder");
    const core::Json* use_name = on_use.member("placeholder");
    ASSERT_NE(declaration_name, nullptr);
    ASSERT_NE(use_name, nullptr);
    EXPECT_EQ(declaration_name->as_string(), "title");
    EXPECT_EQ(use_name->as_string(), "title");
    // assert each range starts where the cursor names the token (§12.2)
    const core::Json* declaration_range = on_declaration.member("range");
    const core::Json* use_range = on_use.member("range");
    ASSERT_NE(declaration_range, nullptr);
    ASSERT_NE(use_range, nullptr);
    const core::Json* declaration_start = declaration_range->member("start");
    const core::Json* use_start = use_range->member("start");
    ASSERT_NE(declaration_start, nullptr);
    ASSERT_NE(use_start, nullptr);
    EXPECT_EQ(static_cast<int>(declaration_start->member("line")->as_number()), line_at(text, declaration));
    EXPECT_EQ(static_cast<int>(use_start->member("line")->as_number()), line_at(text, use));
}

TEST(LspRenameChecks, RenameALocalPropertyRewritesDeclarationAndUses) {
    // arrange a property with one read, which the rename must move together (§12.2)
    const std::string text = "component Panel {\n    in property<string> title: \"a\";\n    Column { Label { text: title; } }\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the request starts on the read and writes the new name (§12.2)
    const std::size_t declaration = text.find("title");
    const std::size_t use = text.find("title", declaration + 1);
    const core::Json reply = rename_reply(server, line_at(text, use), column_at(text, use), "caption", uri);
    // assert the edit was produced and names one document (§12.2)
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* changes = result->member("changes");
    ASSERT_NE(changes, nullptr);
    ASSERT_EQ(changes->as_object().size(), 1U);
    const auto found = changes->as_object().find(uri);
    ASSERT_NE(found, changes->as_object().end());
    // assert the declaration and the read are both rewritten (§12.2)
    ASSERT_EQ(found->second.as_array().size(), 2U);
    // each edit writes the same name, because one declaration is renamed (§12.2)
    for (const core::Json& edit : found->second.as_array()) {
        // the text is what the editor writes in the range (§12.2)
        const core::Json* new_text = edit.member("newText");
        ASSERT_NE(new_text, nullptr);
        EXPECT_EQ(new_text->as_string(), "caption");
    }
    // assert the first edit is the declaration, because the reply is sorted by position (§12.2)
    const core::Json* first_range = found->second.as_array().at(0).member("range");
    ASSERT_NE(first_range, nullptr);
    EXPECT_EQ(static_cast<int>(first_range->member("start")->member("line")->as_number()), line_at(text, declaration));
}

TEST(LspRenameChecks, RenameAnImportedPropertyRewritesBothFiles) {
    // arrange a side panel whose property the importer assigns, which the rename must move together (§12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-rename-import";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string side_text = "component SidePanel {\n    in property<string> title;\n    Column {\n        Label { text: title; }\n    }\n}\n";
    std::ofstream(root / "side_panel.ui") << side_text;
    const std::string panel_text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel { title: \"x\"; }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << panel_text;
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string side_path = (root / "side_panel.ui").generic_string();
    const std::string side_uri = "file://" + std::string(side_path.starts_with('/') ? "" : "/") + side_path;
    LspServer server;
    (void)did_open(server, panel_text, panel_uri);
    // act the request starts on the importer's assignment (§12.2)
    const std::size_t assignment = panel_text.find("title");
    const core::Json reply = rename_reply(server, line_at(panel_text, assignment), column_at(panel_text, assignment), "caption", panel_uri);
    // assert one workspace edit names both files, because the declaration lives in the import (§12.2)
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* changes = result->member("changes");
    ASSERT_NE(changes, nullptr);
    ASSERT_EQ(changes->as_object().size(), 2U);
    const auto panel_changes = changes->as_object().find(panel_uri);
    const auto side_changes = changes->as_object().find(side_uri);
    ASSERT_NE(panel_changes, changes->as_object().end());
    ASSERT_NE(side_changes, changes->as_object().end());
    // assert the closed import is edited at its declaration and its own read (§12.2)
    ASSERT_EQ(side_changes->second.as_array().size(), 2U);
    // assert the importer is edited at its assignment (§12.2)
    ASSERT_EQ(panel_changes->second.as_array().size(), 1U);
    // each imported edit writes the same new name (§12.2)
    for (const core::Json& edit : side_changes->second.as_array()) {
        // the text is what the editor writes in the imported file (§12.2)
        const core::Json* new_text = edit.member("newText");
        ASSERT_NE(new_text, nullptr);
        EXPECT_EQ(new_text->as_string(), "caption");
    }
    std::filesystem::remove_all(root, ignored);
}

TEST(LspRenameChecks, RenameASignalKeepsTheHandlerPrefix) {
    // arrange a side panel that raises a signal the importer attaches to and the component invokes (§11.1)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-rename-signal";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string side_text = "component SidePanel {\n    signal closed();\n    Column {\n        Button { on_clicked: { closed(); } }\n    }\n}\n";
    std::ofstream(root / "side_panel.ui") << side_text;
    const std::string panel_text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel { on_closed: { } }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << panel_text;
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string side_path = (root / "side_panel.ui").generic_string();
    const std::string side_uri = "file://" + std::string(side_path.starts_with('/') ? "" : "/") + side_path;
    LspServer server;
    (void)did_open(server, panel_text, panel_uri);
    // act the request starts on the handler, which spells the signal with its prefix (§11.1)
    const std::size_t handler = panel_text.find("on_closed");
    const core::Json prepared = prepare_rename_at(server, line_at(panel_text, handler), column_at(panel_text, handler), panel_uri);
    // assert the rename box prefills the bare signal name, because the prefix is not part of it (§11.1)
    ASSERT_EQ(prepared.kind(), core::Json::Kind::Object);
    ASSERT_NE(prepared.member("placeholder"), nullptr);
    EXPECT_EQ(prepared.member("placeholder")->as_string(), "closed");
    // act the rename writes the bare signal name, which every handler spells with its prefix (§11.1)
    const core::Json reply = rename_reply(server, line_at(panel_text, handler), column_at(panel_text, handler), "dismissed", panel_uri);
    // assert the handler keeps its prefix and the declaration and invocation take the bare name (§11.1)
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* changes = result->member("changes");
    ASSERT_NE(changes, nullptr);
    const auto panel_changes = changes->as_object().find(panel_uri);
    const auto side_changes = changes->as_object().find(side_uri);
    ASSERT_NE(panel_changes, changes->as_object().end());
    ASSERT_NE(side_changes, changes->as_object().end());
    ASSERT_EQ(panel_changes->second.as_array().size(), 1U);
    EXPECT_EQ(panel_changes->second.as_array().at(0).member("newText")->as_string(), "on_dismissed");
    ASSERT_EQ(side_changes->second.as_array().size(), 2U);
    // every imported edit writes the bare signal name, which the prefix is added to at the handler (§11.1)
    for (const core::Json& edit : side_changes->second.as_array()) {
        // the text is what the editor writes in the imported file (§12.2)
        const core::Json* new_text = edit.member("newText");
        ASSERT_NE(new_text, nullptr);
        EXPECT_EQ(new_text->as_string(), "dismissed");
    }
    std::filesystem::remove_all(root, ignored);
}

TEST(LspRenameChecks, RenameABaseComponentRewritesTheBaseAndInstances) {
    // arrange a base used by a base clause and an instance, which are both written names (§11.1)
    const std::string text = "component Holder {\n    in property<string> caption;\n    Column { @children }\n}\n\ncomponent Panel : Holder {\n    Column {\n        Holder { caption: \"x\"; }\n    }\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the request starts on the base clause, which is the added form (§11.1)
    const std::size_t declaration = text.find("Holder");
    const std::size_t base = text.find("Holder", declaration + 1);
    const core::Json references = references_at(server, line_at(text, base), column_at(text, base), uri);
    // assert the declaration, the base clause and the instance are all answered (§12.2)
    ASSERT_EQ(references.kind(), core::Json::Kind::Array);
    EXPECT_EQ(references.as_array().size(), 3U);
    // act the rename moves the base and its instance together (§11.1)
    const core::Json reply = rename_reply(server, line_at(text, base), column_at(text, base), "Section", uri);
    // assert every site is rewritten, because a component name is one declaration (§11.1)
    const core::Json* result = reply.member("result");
    ASSERT_NE(result, nullptr);
    const core::Json* changes = result->member("changes");
    ASSERT_NE(changes, nullptr);
    const auto found = changes->as_object().find(uri);
    ASSERT_NE(found, changes->as_object().end());
    ASSERT_EQ(found->second.as_array().size(), 3U);
    // each edit writes the new component name (§12.2)
    for (const core::Json& edit : found->second.as_array()) {
        // the text is what the editor writes in the range (§12.2)
        const core::Json* new_text = edit.member("newText");
        ASSERT_NE(new_text, nullptr);
        EXPECT_EQ(new_text->as_string(), "Section");
    }
}

TEST(LspRenameChecks, RenameRefusesTheExportedComponent) {
    // arrange a saved root whose component is named after its file, which the build depends on (§11.1, §14.2)
    const std::string text = "component Panel { Column {} }";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the editor asks to prepare a rename on the exported name (§12.2)
    const std::size_t name = text.find("Panel");
    const core::Json prepared = prepare_rename_at(server, 0, column_at(text, name), uri);
    // assert the prepare request answers nothing, because renaming it would need the file renamed too (§11.1, §14.2)
    EXPECT_EQ(prepared.kind(), core::Json::Kind::Null);
    // act the client asks anyway, which the server refuses with its reason (§12.2)
    const core::Json reply = rename_reply(server, 0, column_at(text, name), "Dialog", uri);
    // assert the refusal names the file-stem rule (§11.1, §14.2)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    const core::Json* message = error->member("message");
    ASSERT_NE(message, nullptr);
    EXPECT_NE(message->as_string().find("named after its file"), std::string::npos);
}

TEST(LspRenameChecks, RenameRefusesABuiltinOrAnIdentity) {
    // arrange a builtin property and an author id, which are the two names with no source declaration (§12.2, AD-11)
    const std::string text = "component Panel {\n    Column {\n        Label { id: action; text: \"hi\"; }\n    }\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the editor asks to prepare a rename on the builtin and on the identity (§12.2)
    const std::size_t builtin = text.find("text");
    const std::size_t identity = text.find("action");
    const core::Json builtin_prepared = prepare_rename_at(server, line_at(text, builtin), column_at(text, builtin), uri);
    const core::Json identity_prepared = prepare_rename_at(server, line_at(text, identity), column_at(text, identity), uri);
    // assert neither name is a rename target, because neither is a source declaration (§12.2, AD-11)
    EXPECT_EQ(builtin_prepared.kind(), core::Json::Kind::Null);
    EXPECT_EQ(identity_prepared.kind(), core::Json::Kind::Null);
    // act the client asks for the identity anyway, which must not become an implicit-key rename (§12.2, AD-11)
    const core::Json reply = rename_reply(server, line_at(text, identity), column_at(text, identity), "other", uri);
    // assert the refusal states that no declaration was named (§12.2)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    const core::Json* message = error->member("message");
    ASSERT_NE(message, nullptr);
    EXPECT_NE(message->as_string().find("no renameable declaration"), std::string::npos);
}

TEST(LspRenameChecks, RenameRefusesAnAmbiguousDeclaration) {
    // arrange a property declared twice, which the compiler reports and no use can disambiguate (§11.1)
    const std::string text = "component Panel {\n    in property<string> title: \"a\";\n    in property<string> title: \"b\";\n    Column {}\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the editor asks to prepare a rename on the first declaration (§12.2)
    const std::size_t first = text.find("title");
    const core::Json prepared = prepare_rename_at(server, line_at(text, first), column_at(text, first), uri);
    // assert an ambiguous name prepares nothing, because one spelling resolves once (§11.1)
    EXPECT_EQ(prepared.kind(), core::Json::Kind::Null);
    // act the client asks anyway, which the server refuses rather than editing the first (§12.2)
    const core::Json reply = rename_reply(server, line_at(text, first), column_at(text, first), "caption", uri);
    // assert the refusal says the name is ambiguous (§11.1)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    const core::Json* message = error->member("message");
    ASSERT_NE(message, nullptr);
    EXPECT_NE(message->as_string().find("ambiguous"), std::string::npos);
}

TEST(LspRenameChecks, RenameRefusesACollidingName) {
    // arrange a component with a second property and the common property set (§11.1, §8.2)
    const std::string text = "component Panel {\n    in property<string> title;\n    in property<string> subtitle;\n    Column {}\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the rename targets another declared member, which would duplicate a slot (§11.1)
    const std::size_t title = text.find("title");
    const core::Json member = rename_reply(server, line_at(text, title), column_at(text, title), "subtitle", uri);
    // assert the collision is refused rather than silently declaring two members (§11.1)
    const core::Json* member_error = member.member("error");
    ASSERT_NE(member_error, nullptr);
    EXPECT_NE(member_error->member("message")->as_string().find("already a member"), std::string::npos);
    // act the rename targets a common property, which the declaration would shadow (§11.1, §8.2)
    const core::Json common = rename_reply(server, line_at(text, title), column_at(text, title), "tooltip", uri);
    // assert the shadowing collision is refused too, because an existing spelling would resolve differently (§11.1)
    const core::Json* common_error = common.member("error");
    ASSERT_NE(common_error, nullptr);
    EXPECT_NE(common_error->member("message")->as_string().find("common element property"), std::string::npos);
}

TEST(LspReferenceChecks, ASharedNameInAnotherDocumentIsNotAReference) {
    // arrange two open documents that declare the same property name, which is not the same declaration (§12.2)
    const std::string left_text = "component Left { in property<string> title; Column { Label { text: title; } } }";
    const std::string right_text = "component Right { in property<string> title; Column { Label { text: title; } } }";
    const std::string left_uri = "file:///left.ui";
    const std::string right_uri = "file:///right.ui";
    LspServer server;
    (void)did_open(server, left_text, left_uri);
    (void)did_open(server, right_text, right_uri);
    // act the request starts on the left declaration, whose name is spelled the same in the right document (§12.2)
    const std::size_t declaration = left_text.find("title");
    const core::Json result = references_at(server, line_at(left_text, declaration), column_at(left_text, declaration), left_uri);
    // assert the declaration and the left read are answered, and the right document is untouched (§12.2)
    ASSERT_EQ(result.kind(), core::Json::Kind::Array);
    ASSERT_EQ(result.as_array().size(), 2U);
    // every location is in the document the request named, because a name alone is not identity (§12.2)
    for (const core::Json& location : result.as_array()) {
        // the uri names the file the site is written in (§12.2)
        const core::Json* uri = location.member("uri");
        ASSERT_NE(uri, nullptr);
        EXPECT_EQ(uri->as_string(), left_uri);
    }
    // act the rename writes the left declaration only (§12.2)
    const core::Json reply = rename_reply(server, line_at(left_text, declaration), column_at(left_text, declaration), "caption", left_uri);
    // assert one document is edited, because the right one declares a different symbol (§12.2)
    const core::Json* reply_result = reply.member("result");
    ASSERT_NE(reply_result, nullptr);
    const core::Json* changes = reply_result->member("changes");
    ASSERT_NE(changes, nullptr);
    ASSERT_EQ(changes->as_object().size(), 1U);
    EXPECT_NE(changes->as_object().find(left_uri), changes->as_object().end());
    EXPECT_EQ(changes->as_object().find(right_uri), changes->as_object().end());
}

TEST(LspReferenceChecks, AnUntitledBufferIsAnsweredUnderItsOwnUri) {
    // arrange a side panel shared by a saved root and an untitled buffer, which both assign the same property (§12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-reference-untitled";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string side_text = "component SidePanel {\n    in property<string> title;\n    Column {\n        Label { text: title; }\n    }\n}\n";
    std::ofstream(root / "side_panel.ui") << side_text;
    const std::string panel_text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel { title: \"x\"; }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << panel_text;
    // the untitled buffer imports the saved file by its absolute path, which is the only way it can share it (§11.1)
    const std::string side_import = (root / "side_panel.ui").generic_string();
    const std::string untitled_text = "import \"" + side_import + "\";\n\ncomponent Notes {\n    Column {\n        SidePanel { title: \"n\"; }\n    }\n}\n";
    // the saved files are addressed by their file uris, which an editor routes (§12.2)
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string side_path = (root / "side_panel.ui").generic_string();
    const std::string side_uri = "file://" + std::string(side_path.starts_with('/') ? "" : "/") + side_path;
    // the untitled buffer has no path, so its editor uri is its whole identity (§12.2)
    const std::string untitled_uri = "untitled:notes.ui";
    LspServer server;
    (void)did_open(server, panel_text, panel_uri);
    (void)did_open(server, untitled_text, untitled_uri);
    // act the request starts on the saved root's assignment, which is one of three uses of the property (§12.2)
    const std::size_t assignment = panel_text.find("title");
    const core::Json result = references_at(server, line_at(panel_text, assignment), column_at(panel_text, assignment), panel_uri);
    // assert the declaration, the imported read and both assignments are each answered once (§12.2)
    ASSERT_EQ(result.kind(), core::Json::Kind::Array);
    ASSERT_EQ(result.as_array().size(), 4U);
    std::size_t panel_sites = 0;
    std::size_t side_sites = 0;
    std::size_t untitled_sites = 0;
    // each location belongs to the file it was written in, because a range is meaningless in another buffer (§12.2)
    for (const core::Json& location : result.as_array()) {
        // the uri names the file the site is written in (§12.2)
        const core::Json* uri = location.member("uri");
        ASSERT_NE(uri, nullptr);
        // the three files are counted separately, because mixing them would put a range in the wrong document (§12.2)
        if (uri->as_string() == panel_uri)
            panel_sites++;
        // the imported file carries the declaration and its own read (§12.2)
        if (uri->as_string() == side_uri)
            side_sites++;
        // the untitled buffer carries its own assignment, which must keep its own uri (§12.2)
        if (uri->as_string() == untitled_uri)
            untitled_sites++;
    }
    // assert the untitled site is never reported under the asking document's uri (§12.2)
    EXPECT_EQ(panel_sites, 1U);
    EXPECT_EQ(side_sites, 2U);
    EXPECT_EQ(untitled_sites, 1U);
    // act the rename writes each file under its own uri, including the untitled buffer (§12.2)
    const core::Json reply = rename_reply(server, line_at(panel_text, assignment), column_at(panel_text, assignment), "caption", panel_uri);
    // assert three documents are edited, because three files write the property (§12.2)
    const core::Json* reply_result = reply.member("result");
    ASSERT_NE(reply_result, nullptr);
    const core::Json* changes = reply_result->member("changes");
    ASSERT_NE(changes, nullptr);
    ASSERT_EQ(changes->as_object().size(), 3U);
    const auto untitled_changes = changes->as_object().find(untitled_uri);
    ASSERT_NE(untitled_changes, changes->as_object().end());
    // assert the untitled buffer is edited once, in its own coordinates (§12.2)
    EXPECT_EQ(untitled_changes->second.as_array().size(), 1U);
    std::filesystem::remove_all(root, ignored);
}

TEST(LspRenameChecks, RenameAnOpenImportedPropertyKeysItsOpenUri) {
    // arrange the imported file open beside its importer, which is what a person sees while editing both (§12.2)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-lsp-rename-open-import";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::string side_text = "component SidePanel {\n    in property<string> title;\n    Column {\n        Label { text: title; }\n    }\n}\n";
    std::ofstream(root / "side_panel.ui") << side_text;
    const std::string panel_text = "import \"side_panel.ui\";\n\ncomponent Panel {\n    Column {\n        SidePanel { title: \"x\"; }\n    }\n}\n";
    std::ofstream(root / "panel.ui") << panel_text;
    const std::string panel_path = (root / "panel.ui").generic_string();
    const std::string panel_uri = "file://" + std::string(panel_path.starts_with('/') ? "" : "/") + panel_path;
    const std::string side_path = (root / "side_panel.ui").generic_string();
    const std::string side_uri = "file://" + std::string(side_path.starts_with('/') ? "" : "/") + side_path;
    LspServer server;
    (void)did_open(server, side_text, side_uri);
    (void)did_open(server, panel_text, panel_uri);
    // act the request starts on the importer's assignment (§12.2)
    const std::size_t assignment = panel_text.find("title");
    const core::Json reply = rename_reply(server, line_at(panel_text, assignment), column_at(panel_text, assignment), "caption", panel_uri);
    // assert the imported file is keyed by the uri the editor opened, not a file uri beside it (§12.2)
    const core::Json* reply_result = reply.member("result");
    ASSERT_NE(reply_result, nullptr);
    const core::Json* changes = reply_result->member("changes");
    ASSERT_NE(changes, nullptr);
    ASSERT_EQ(changes->as_object().size(), 2U);
    EXPECT_NE(changes->as_object().find(side_uri), changes->as_object().end());
    EXPECT_NE(changes->as_object().find(panel_uri), changes->as_object().end());
    std::filesystem::remove_all(root, ignored);
}

TEST(LspRenameChecks, RenameRefusesADeclarationOutsideTheSymbolSet) {
    // arrange a global, a function parameter and a repeat item, which name values rather than component members (§11.1)
    const std::string text = "global limit: int = 3;\nfn clamp(value: int): int { return value; }\ncomponent Panel {\n    in property<vector<string>> rows: [\"a\"];\n    Column {\n        Repeat(row in rows) {\n            Label { text: row; }\n        }\n    }\n}\n";
    const std::string uri = "file:///panel.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the editor asks to prepare a rename on each name, which none of them declares a renameable surface (§12.2)
    const std::size_t global = text.find("limit");
    const std::size_t parameter = text.find("value");
    const std::size_t parameter_read = text.find("value", parameter + 1);
    const std::size_t repeat = text.find("Repeat(");
    const std::size_t item = text.find("row", repeat);
    const std::size_t item_read = text.find("row", text.find("Label", item));
    // assert a value declaration and a binding never open a rename box, because only component symbols do (§12.2)
    EXPECT_EQ(prepare_rename_at(server, line_at(text, global), column_at(text, global), uri).kind(), core::Json::Kind::Null);
    EXPECT_EQ(prepare_rename_at(server, line_at(text, parameter), column_at(text, parameter), uri).kind(), core::Json::Kind::Null);
    EXPECT_EQ(prepare_rename_at(server, line_at(text, parameter_read), column_at(text, parameter_read), uri).kind(), core::Json::Kind::Null);
    EXPECT_EQ(prepare_rename_at(server, line_at(text, item), column_at(text, item), uri).kind(), core::Json::Kind::Null);
    EXPECT_EQ(prepare_rename_at(server, line_at(text, item_read), column_at(text, item_read), uri).kind(), core::Json::Kind::Null);
    // act the client asks to rename the repeat read, which has no declaration the feature edits (§12.2)
    const core::Json reply = rename_reply(server, line_at(text, item_read), column_at(text, item_read), "entry", uri);
    // assert the refusal is stated rather than producing an edit (§12.2)
    const core::Json* error = reply.member("error");
    ASSERT_NE(error, nullptr);
    EXPECT_NE(error->member("message")->as_string().find("no renameable declaration"), std::string::npos);
}

TEST(LspRenameChecks, RenameRefusesACatalogTypeOrADeclaredComponent) {
    // arrange a base beside its export, which are the two names a component rename could collide with (§11.1)
    const std::string text = "component Holder {\n    Column { @children }\n}\n\ncomponent Dashboard : Holder {\n    Column {}\n}\n";
    const std::string uri = "file:///dashboard.ui";
    LspServer server;
    (void)did_open(server, text, uri);
    // act the rename targets a catalog type, which a declared name would shadow (§11.1)
    const std::size_t holder = text.find("Holder");
    const core::Json catalog = rename_reply(server, line_at(text, holder), column_at(text, holder), "Column", uri);
    // assert the shadowing collision is refused before an edit is produced (§11.1)
    const core::Json* catalog_error = catalog.member("error");
    ASSERT_NE(catalog_error, nullptr);
    EXPECT_NE(catalog_error->member("message")->as_string().find("builtin or manifest type"), std::string::npos);
    // act the rename targets the file's other component, which would declare one name twice (§11.1)
    const core::Json declared = rename_reply(server, line_at(text, holder), column_at(text, holder), "Dashboard", uri);
    // assert the duplicate declaration is refused too (§11.1)
    const core::Json* declared_error = declared.member("error");
    ASSERT_NE(declared_error, nullptr);
    EXPECT_NE(declared_error->member("message")->as_string().find("already declared in this file"), std::string::npos);
}
