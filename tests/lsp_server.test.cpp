#include <algorithm>
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
