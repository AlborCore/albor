#include <cstddef>
#include <string>
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
    Exchange did_open(LspServer& server, const std::string& text) {
        // the notification carries the text inline, which is the open form (§12.2)
        const std::string line = std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///panel.ui","languageId":"albor","version":1,"text":)") + core::Json(text).to_string() + "}}}";
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
    EXPECT_EQ(server.document_count(), 0U);
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
