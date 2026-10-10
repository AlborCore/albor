#include "language-server/lsp_server.h"

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/type_checker.h"
#include "compiler/value_type.h"
#include "core/json.h"
#include "language-server/lsp_support.h"

namespace albor::language_server
{
    namespace
    {
        // the error code for a request this server does not answer, which the protocol numbers (§12.2)
        constexpr int METHOD_NOT_FOUND = -32601;

        // the error code for a request whose parameters do not make sense, which the protocol numbers (§12.2)
        constexpr int INVALID_PARAMS = -32602;

        // the error code for a request naming a document that is not open, which the protocol numbers (§12.2)
        constexpr int INVALID_REQUEST = -32600;

        // build the reply envelope for an identifier, which every response carries (§12.2)
        core::Json envelope(const core::Json& id, core::Json result) {
            // the reply is an object, because protocol replies are (§12.2)
            core::Json reply;
            // the version identifies the protocol (§12.2)
            reply.set("jsonrpc", core::Json(std::string("2.0")));
            // the identifier echoes the request, which is how an editor matches the pair (§12.2)
            reply.set("id", id);
            // the result is what the request produced (§12.2)
            reply.set("result", std::move(result));
            // the reply is returned whole (§12.2)
            return reply;
        }

        // build the reply envelope for a failure, which carries no result (§12.2)
        core::Json failure(const core::Json& id, int code, std::string message) {
            // the reply is an object, because protocol replies are (§12.2)
            core::Json reply;
            // the version identifies the protocol (§12.2)
            reply.set("jsonrpc", core::Json(std::string("2.0")));
            // the identifier echoes the request (§12.2)
            reply.set("id", id);
            // the error carries the code an editor branches on (§12.2)
            core::Json error;
            // the code is the protocol number (§12.2)
            error.set("code", core::Json(static_cast<double>(code)));
            // the message is the stated reason, which is what a person reads (§12.2)
            error.set("message", core::Json(std::move(message)));
            // the error joins the reply, which is where the protocol puts it (§12.2)
            reply.set("error", std::move(error));
            // the reply is returned whole (§12.2)
            return reply;
        }

        // read one string member, returning nothing when it is absent or not a string (§12.2)
        std::optional<std::string> string_member(const core::Json& parent, std::string_view name) {
            // the member is read, because a request names its document through one (§12.2)
            const core::Json* value = parent.member(name);
            // an absent or mistyped member yields nothing, which the caller reports as invalid (§12.2)
            if (value == nullptr || value->kind() != core::Json::Kind::String)
                return std::nullopt;
            // the string is returned whole (§12.2)
            return value->as_string();
        }

        // run the compiler over one document's text, which is what produces the diagnostics (§16)
        compiler::DiagnosticList compile_text(const std::string& text) {
            // the diagnostics accumulate, because the server publishes what the compiler found (§16)
            compiler::DiagnosticList diagnostics;
            // the builtin catalog is what a ui file may use without a manifest (§11.1, §8)
            compiler::ElementCatalog catalog = compiler::ElementCatalog::with_builtins();
            // the scanner runs first, because the parser takes units (§11.1)
            const std::vector<compiler::Token> tokens = compiler::Lexer(text, diagnostics).tokenize();
            // the parser builds the untyped tree (§11.1)
            const compiler::SourceFile file = compiler::Parser(tokens, diagnostics).parse();
            // the checker resolves it against the catalog, which is where the types come from (§11.1, AD-17)
            const compiler::CheckedFile checked = compiler::TypeChecker(catalog, diagnostics).check(file);
            // the generator runs too, because a diagnostic may come only from codegen (§11.3)
            (void)compiler::CodeGenerator(diagnostics).generate(checked);
            // the diagnostics are returned, which is what the editor is told (§16)
            return diagnostics;
        }

        // render one compiler location as the protocol spells it, which counts from zero rather than one (§16)
        core::Json position_value(const compiler::SourceLocation& location) {
            // the position is an object, because the protocol describes it that way (§12.2)
            core::Json position;
            // a compiler line below one cannot be shifted, so it clamps rather than wrapping (§12.2)
            position.set("line", core::Json(static_cast<double>(location.line > 0 ? location.line - 1 : 0)));
            // a compiler column below one cannot be shifted, so it clamps rather than wrapping (§12.2)
            position.set("character", core::Json(static_cast<double>(location.column > 0 ? location.column - 1 : 0)));
            // the position is returned whole (§12.2)
            return position;
        }

        // render diagnostics as the protocol's publish payload, which is what an editor consumes (§12.2, §16)
        core::Json diagnostics_payload(const Document& document, const compiler::DiagnosticList& diagnostics) {
            // the payload is an object, because the protocol describes it that way (§12.2)
            core::Json payload;
            // the uri names the document, which is how an editor routes the publication (§12.2)
            payload.set("uri", core::Json(document.uri()));
            // the entries accumulate, one per diagnostic (§16)
            std::vector<core::Json> entries;
            // each diagnostic becomes one entry, in the order the compiler reported them (§16)
            for (const compiler::Diagnostic& diagnostic : diagnostics.diagnostics()) {
                // an entry is an object, because the protocol describes it that way (§12.2)
                core::Json entry;
                // the range is where the squiggle goes, which the document turns offsets into (§12.2, §16)
                core::Json range;
                // the start comes from the compiler's own start offset (§11.1, §16)
                range.set("start", position_value(diagnostic.span.start));
                // the end comes from the compiler's own end offset (§11.1, §16)
                range.set("end", position_value(diagnostic.span.end));
                // the range joins the entry (§12.2)
                entry.set("range", std::move(range));
                // the severity is an error for anything the compiler reported as one, which is every diagnostic (§11.1)
                entry.set("severity", core::Json(1.0));
                // the code is what an editor filters by, and the compiler codes are stable (§11.1)
                entry.set("code", core::Json(diagnostic.code));
                // the source is the compiler, which is what the agreement suite compares against (§16)
                entry.set("source", core::Json(std::string("albor")));
                // the message is what a person reads (§12.2)
                entry.set("message", core::Json(diagnostic.message));
                // the entry joins the publication (§16)
                entries.push_back(std::move(entry));
            }
            // the entries join the payload, which is the whole of a publication (§12.2)
            payload.set("diagnostics", core::Json(std::move(entries)));
            // the payload is returned whole (§12.2)
            return payload;
        }
    } // namespace

    Exchange LspServer::handle_line(std::string_view line) {
        // the line is parsed, because a request is a json document (§12.2)
        const std::optional<core::Json> parsed = core::Json::parse(line);
        // a line that is not json is answered, because silence would hang an editor (§12.2)
        if (!parsed)
            return Exchange{.reply = failure(core::Json(), INVALID_PARAMS, "the request is not json").to_string(), .notifications = {}};
        // the request is an object, because protocol requests are (§12.2)
        if (!parsed->is_object())
            return Exchange{.reply = failure(core::Json(), INVALID_PARAMS, "the request is not an object").to_string(), .notifications = {}};
        // the method is read, because it is what this server routes (§12.2)
        const core::Json* method = parsed->member("method");
        // a request with no method cannot be routed (§12.2)
        if (method == nullptr || method->kind() != core::Json::Kind::String)
            return Exchange{.reply = failure(core::Json(), INVALID_PARAMS, "the request carries no method").to_string(), .notifications = {}};
        // the parameters are read when present, because every document request carries them (§12.2)
        const core::Json* params = parsed->member("params");
        // the identifier is read, because a reply echoes it and a notification has none (§12.2)
        const core::Json* id = parsed->member("id");
        // a request with no identifier is a notification, which the editor expects no reply to (§12.2)
        const bool notification = id == nullptr || id->kind() == core::Json::Kind::Null;
        // the identifier a reply would echo, which an absent one stands in as null (§12.2)
        const core::Json reply_id = id != nullptr ? *id : core::Json();

        // the initialize handshake advertises what this server can do, and reaches no document (§12.2)
        if (method->as_string() == "initialize") {
            // a handshake carries no identifier in practice, but it is answered either way (§12.2)
            core::Json result;
            // the version is the one this server speaks (§12.2)
            result.set("serverInfo", core::Json(std::map<std::string, core::Json, std::less<>>{{"name", core::Json(std::string("albor"))}, {"version", core::Json(std::string("1"))}}));
            // the capabilities advertise what this server answers (§12.2)
            core::Json capabilities;
            // only diagnostics are published from the server, which is what this server sends (§12.2)
            core::Json publish;
            // the server does not ask the client to publish its own diagnostics (§12.2)
            publish.set("relatedInformation", core::Json(false));
            // the diagnostics capability is what an editor looks for (§12.2)
            capabilities.set("textDocumentSync", core::Json(1.0));
            // the capabilities join the result (§12.2)
            result.set("capabilities", std::move(capabilities));
            // the handshake answers as a reply, because an editor waits for it (§12.2)
            return Exchange{.reply = envelope(reply_id, std::move(result)).to_string(), .notifications = {}};
        }

        // an opened or changed document is stored and compiled, which is the whole of the loop (§12.2, §16)
        if (method->as_string() == "textDocument/didOpen" || method->as_string() == "textDocument/didChange") {
            // a document request carries parameters, without which nothing can be routed (§12.2)
            if (params == nullptr || !params->is_object())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the request carries no document").to_string(), .notifications = {}};
            // the document is named through a text document identifier (§12.2)
            const core::Json* text_document = params->member("textDocument");
            // a request with no document names nothing (§12.2)
            if (text_document == nullptr || !text_document->is_object())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the request names no document").to_string(), .notifications = {}};
            // the uri is required, because it is how the editor is told what the diagnostics are for (§12.2)
            const std::optional<std::string> uri = string_member(*text_document, "uri");
            // an absent uri names nothing (§12.2)
            if (!uri)
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the document has no uri").to_string(), .notifications = {}};
            // the text is read from the document itself on open (§12.2)
            std::string text;
            // the version is read when present, because an editor may omit it (§12.2)
            std::int64_t version = 0;
            // the text document identifier carries the text on an open (§12.2)
            if (const auto given = string_member(*text_document, "text"); given)
                text = *given;
            // the version member is read, which is what rejects a stale notification (§12.2)
            if (const core::Json* given = text_document->member("version"); given != nullptr && given->kind() == core::Json::Kind::Number)
                version = static_cast<std::int64_t>(given->as_number());
            // a change carries its text in a list of edits, which is the full form this server reads (§12.2)
            if (const core::Json* changes = params->member("contentChanges"); changes != nullptr && changes->kind() == core::Json::Kind::Array && !changes->as_array().empty()) {
                // the last edit wins, because a full replacement is the only form this server applies (§12.2)
                const core::Json& change = changes->as_array().back();
                // a change whose text is not a string cannot be applied (§12.2)
                if (const auto given = string_member(change, "text"); given)
                    text = *given;
                // a change may carry its own version, which supersedes the document's (§12.2)
                if (const core::Json* given = change.member("version"); given != nullptr && given->kind() == core::Json::Kind::Number)
                    version = static_cast<std::int64_t>(given->as_number());
            }
            // the document is stored, and a stale notification is refused (§12.2)
            if (!m_documents.open(*uri, text, version))
                return Exchange{.reply = {}, .notifications = {}};
            // the compile runs over what was stored, which is the same text an editor is showing (§16)
            const Document& document = *m_documents.find(*uri);
            const compiler::DiagnosticList diagnostics = compile_text(document.text());
            // the count is kept, which is what an agreement test reads (§16)
            m_last_diagnostics = diagnostics.count();
            // the publication is a notification, because an editor expects diagnostics unprompted (§12.2, §16)
            Notification published{.method = "textDocument/publishDiagnostics", .payload = diagnostics_payload(document, diagnostics).to_string()};
            // a notification produces no reply, and one notification carrying the diagnostics (§12.2)
            return Exchange{.reply = {}, .notifications = {std::move(published)}};
        }

        // a closed document is forgotten, which is what didClose means (§12.2)
        if (method->as_string() == "textDocument/didClose") {
            // a document request carries parameters (§12.2)
            if (params != nullptr && params->is_object()) {
                // the document is named through a text document identifier (§12.2)
                if (const core::Json* text_document = params->member("textDocument"); text_document != nullptr && text_document->is_object()) {
                    // the uri names what to forget (§12.2)
                    if (const std::optional<std::string> uri = string_member(*text_document, "uri"))
                        m_documents.close(*uri);
                }
            }
            // a close produces no reply and no publication, because nothing changed in the document (§12.2)
            return Exchange{.reply = {}, .notifications = {}};
        }

        // a request naming a document that is not open is refused, rather than answered from nothing (§12.2)
        if (method->as_string() == "textDocument/completion" || method->as_string() == "textDocument/hover") {
            // a document request carries parameters (§12.2)
            if (params == nullptr || !params->is_object())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the request carries no document").to_string(), .notifications = {}};
            // the document is named through a text document identifier (§12.2)
            const core::Json* text_document = params->member("textDocument");
            // a request with no document names nothing (§12.2)
            if (text_document == nullptr || !text_document->is_object())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the request names no document").to_string(), .notifications = {}};
            // the uri names what to read (§12.2)
            const std::optional<std::string> uri = string_member(*text_document, "uri");
            // an absent uri names nothing (§12.2)
            if (!uri)
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the document has no uri").to_string(), .notifications = {}};
            // a document that is not open is refused, because an editor should have opened it first (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // the position is read, because both requests answer from where the cursor is (§12.2)
            const core::Json* position = params->member("position");
            // a request with no position answers from nowhere, which is refused rather than guessed (§12.2)
            if (position == nullptr || !position->is_object())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the request carries no position").to_string(), .notifications = {}};
            // the line is required, because it is the row the cursor is on (§12.2)
            const core::Json* line_value = position->member("line");
            // the character is required, because it is the column within that row (§12.2)
            const core::Json* character_value = position->member("character");
            // a request missing either answers from nowhere, which is refused (§12.2)
            if (line_value == nullptr || character_value == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the position is incomplete").to_string(), .notifications = {}};
            // the document is read, because both requests answer from its text (§12.2)
            const Document& document = *m_documents.find(*uri);
            // the row is read as the protocol's zero based number, and a negative one clamps rather than wraps (§12.2)
            const auto row = static_cast<std::size_t>(std::max(0.0, line_value->as_number()));
            // the column is read the same way, because a negative position is a client bug (§12.2)
            const auto column = static_cast<std::size_t>(std::max(0.0, character_value->as_number()));
            // the catalog is the builtin one a ui file may use without a manifest (§11.1, §8)
            const compiler::ElementCatalog catalog = compiler::ElementCatalog::with_builtins();
            // a completion offers whatever may be written at this position (§12.2)
            if (method->as_string() == "textDocument/completion") {
                // the items are rendered as the protocol's item objects (§12.2)
                std::vector<core::Json> rendered;
                // each item carries its label, kind and detail, which is what an editor shows (§12.2)
                for (const CompletionItem& item : completions_at(document, catalog, row, column)) {
                    // an item is an object, because the protocol describes it that way (§12.2)
                    core::Json entry;
                    // the label is what a person accepts and inserts (§12.2)
                    entry.set("label", core::Json(item.label));
                    // the kind is what an editor picks an icon from (§12.2)
                    entry.set("kind", core::Json(static_cast<double>(item.kind)));
                    // the detail is what tells two same named things apart (§12.2)
                    entry.set("detail", core::Json(item.detail));
                    // the item joins the response (§12.2)
                    rendered.push_back(std::move(entry));
                }
                // the response is a list, which is what the protocol describes for completions (§12.2)
                return Exchange{.reply = envelope(reply_id, core::Json(std::move(rendered))).to_string(), .notifications = {}};
            }
            // a hover describes what the cursor is on, and says nothing when it is on nothing (§12.2)
            const std::optional<std::string> described = hover_at(document, catalog, row, column);
            // an empty hover is a null result, which is what the protocol spells for nothing to say (§12.2)
            if (!described)
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the contents carry the text a person reads beside the cursor (§12.2)
            core::Json contents;
            // the plain text form is one every editor renders (§12.2)
            contents.set("kind", core::Json(std::string("plaintext")));
            // the description is the text itself (§12.2)
            contents.set("value", core::Json(*described));
            // the result carries the contents, which is the protocol's shape (§12.2)
            core::Json result;
            // the contents join the result (§12.2)
            result.set("contents", std::move(contents));
            // the hover answers with the description (§12.2)
            return Exchange{.reply = envelope(reply_id, std::move(result)).to_string(), .notifications = {}};
        }

        // a method this server does not answer is stated, because silence would hang an editor (§12.2)
        if (notification)
            return Exchange{.reply = {}, .notifications = {}};
        // an unknown method on a request is a stated failure (§12.2)
        return Exchange{.reply = failure(reply_id, METHOD_NOT_FOUND, "no such method: " + method->as_string()).to_string(), .notifications = {}};
    }

    std::size_t LspServer::last_diagnostic_count() const { return m_last_diagnostics; }

    std::size_t LspServer::document_count() const { return m_documents.size(); }
} // namespace albor::language_server
