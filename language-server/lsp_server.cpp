#include "language-server/lsp_server.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "compiler/type_checker.h"
#include "compiler/ui_loader.h"
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

        // build a path from the utf-8 bytes a file uri carries, which a windows code page does not decode (§12.2)
        std::filesystem::path path_from_utf8(const std::string& text) {
            // the u8 spelling is what makes the conversion locale independent (§12.2)
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        // read one hexadecimal digit, which a percent escape needs (§12.2)
        int hex_digit(char digit) {
            // a decimal digit is its own value, because the escape writes one byte in base sixteen (§12.2)
            if (digit >= '0' && digit <= '9')
                return digit - '0';
            // an upper case letter follows the nine digits (§12.2)
            if (digit >= 'A' && digit <= 'F')
                return digit - 'A' + 10;
            // a lower case letter is the same value as the upper one (§12.2)
            return digit - 'a' + 10;
        }

        // resolve a file uri to the source path, which is what lets an embedded asset resolve beside it (§12.2, §6.4)
        std::filesystem::path document_path(std::string_view uri) {
            // a uri that is not a file has no path the build could resolve against (§12.2)
            if (!uri.starts_with("file://"))
                return {};
            // the scheme is removed, which leaves the authority and the path (§12.2)
            std::string_view encoded = uri.substr(7);
            // a host before the first slash is not a local path, except for localhost (§12.2)
            if (const std::size_t slash = encoded.find('/'); slash != std::string_view::npos && slash != 0) {
                // a remote host has no local document path the compiler can read (§12.2)
                if (encoded.substr(0, slash) != "localhost")
                    return {};
                // the local host is dropped, because its path is the local file path (§12.2)
                encoded.remove_prefix(slash);
            }
            // the escaped bytes accumulate into the path the filesystem takes (§12.2)
            std::string decoded;
            // each escape is decoded, because an editor writes a space as a percent triplet (§12.2)
            for (std::size_t index = 0; index < encoded.size(); ++index) {
                // a triplet is decoded only when it is complete and hexadecimal (§12.2)
                if (encoded[index] == '%' && index + 2 < encoded.size() && std::isxdigit(static_cast<unsigned char>(encoded[index + 1])) != 0 && std::isxdigit(static_cast<unsigned char>(encoded[index + 2])) != 0) {
                    // the high nibble and the low nibble form the byte the editor meant (§12.2)
                    decoded.push_back(static_cast<char>(hex_digit(encoded[index + 1]) * 16 + hex_digit(encoded[index + 2])));
                    // the two digits are consumed with the percent sign (§12.2)
                    index += 2;
                    continue;
                }
                // a byte that is not an escape is already the path (§12.2)
                decoded.push_back(encoded[index]);
            }
            // a windows file uri carries a slash before the drive letter, which is not part of the path (§12.2)
            if (std::filesystem::path::preferred_separator == '\\' && decoded.size() >= 3 && decoded.front() == '/' && std::isalpha(static_cast<unsigned char>(decoded[1])) != 0 && decoded[2] == ':')
                decoded.erase(decoded.begin());
            // the utf-8 bytes become the native path the generator opens (§12.2)
            return path_from_utf8(decoded);
        }

        // name the root path a compile resolves against, which is synthetic for an unsaved buffer (§11.1)
        std::filesystem::path root_path(const std::filesystem::path& source_path) {
            // a saved document names itself, because its imports resolve beside it (§11.1)
            if (!source_path.empty())
                return source_path;
            // an unsaved buffer resolves imports from the process directory, which an absolute path keeps routable (§11.1)
            return std::filesystem::current_path() / "untitled.ui";
        }

        // what one compile produced, which is the diagnostics and the files the root read (§11.1, §16)
        struct CompileResult
        {
            // the problems the front end found, which the editor is told about (§16)
            compiler::DiagnosticList diagnostics;

            // the root and every import in load order, which decides what a later change invalidates (§11.1, §12.2)
            std::vector<std::filesystem::path> dependencies;
        };

        // run the compiler over one document's text and every open import buffer (§16)
        CompileResult compile_text(const std::string& text, const DocumentStore& documents, const std::filesystem::path& source_path) {
            // the diagnostics accumulate, because the server publishes what the compiler found (§16)
            compiler::DiagnosticList diagnostics;
            // the builtin catalog is what a ui file may use without a manifest (§11.1, §8)
            compiler::ElementCatalog catalog = compiler::ElementCatalog::with_builtins();
            // the root is named even for an unsaved buffer, because an import resolves against a directory (§11.1)
            const std::filesystem::path root = root_path(source_path);
            // the normalized root is what the reader compares against, because the loader normalizes its keys (§11.1)
            const std::filesystem::path normalized_root = root.lexically_normal();
            // the reader returns the editor buffers first and reads a closed import from beside the root (§12.2)
            compiler::UiImportLoader loader(catalog, diagnostics, [&text, &documents, &normalized_root](const std::filesystem::path& path) -> std::optional<std::string> {
                // the root buffer is what the editor shows, which may not be saved yet (§12.2)
                if (path.lexically_normal() == normalized_root)
                    return text;
                // an open imported file is read from its unsaved buffer, because that is what the editor shows (§12.2)
                for (const std::string& uri : documents.uris()) {
                    // a document whose path is the import is the buffer the checker must use (§12.2)
                    if (document_path(uri).lexically_normal() == path) {
                        // the stored text is the editor's current copy, which wins over the disk (§12.2)
                        const Document* imported = documents.find(uri);
                        // a document that vanished during the walk falls through to the disk (§12.2)
                        if (imported != nullptr)
                            return imported->text();
                    }
                }
                // an imported file that is not open is read from disk, because it is a saved source (§11.1)
                std::ifstream file(path, std::ios::binary);
                // a missing file is reported by the loader against the import that named it (§11.1)
                if (!file)
                    return std::nullopt;
                // the bytes are the source the parser takes (§11.1)
                return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            });
            // the loader checks the root and every file it imports, which is what resolves a cross-file component (§11.1)
            const std::optional<compiler::CheckedFile> checked = loader.check(root);
            // a root that could not be checked stops before generation, because there is no typed tree to generate (§11.3)
            if (!checked.has_value())
                return CompileResult{.diagnostics = std::move(diagnostics), .dependencies = loader.dependencies()};
            // the generator runs too, because a diagnostic may come only from codegen (§11.3)
            compiler::CodeGenerator generator(diagnostics);
            // a document with a file path resolves its assets beside itself, which is what the build does (§12.2, §6.4)
            if (!source_path.empty())
                generator.set_source_path(source_path);
            // a tree with a type error is not generated, because only a clean one reaches the backend (§11.3)
            if (!diagnostics.has_errors()) {
                // the generated output is discarded, because the editor is told the diagnostics (§11.3)
                (void)generator.generate(*checked);
            }
            // the diagnostics and files read are returned, which the editor and a later change need (§16, §12.2)
            return CompileResult{.diagnostics = std::move(diagnostics), .dependencies = loader.dependencies()};
        }

        // encode a path as a file uri, which is how an editor routes a diagnostic for an imported file (§12.2)
        std::string file_uri(const std::filesystem::path& source) {
            // a relative diagnostic path is made absolute, because an editor cannot route a relative uri (§12.2)
            std::error_code error;
            // the absolute form is the one the editor opened beside the root (§12.2)
            const std::filesystem::path absolute = source.is_absolute() ? source : std::filesystem::absolute(source, error);
            // a failed absolute lookup keeps the written path, because a publication is better than none (§12.2)
            const std::string text = (error ? source : absolute).generic_string();
            // the uri starts with the file scheme, which is what the server decodes on the way in (§12.2)
            std::string uri = "file://";
            // a windows drive path needs the leading slash the uri form carries (§12.2)
            if (!text.starts_with('/'))
                uri.push_back('/');
            // each byte is copied or escaped, because a path may carry a space or a percent (§12.2)
            for (const char byte : text) {
                // an unreserved byte is the path itself, which an editor reads unchanged (§12.2)
                const unsigned char value = static_cast<unsigned char>(byte);
                if (std::isalnum(value) != 0 || byte == '-' || byte == '_' || byte == '.' || byte == '~' || byte == '/' || byte == ':') {
                    uri.push_back(byte);
                    continue;
                }
                // an escaped byte is written as a percent triplet in upper case (§12.2)
                constexpr char digits[] = "0123456789ABCDEF";
                uri.push_back('%');
                uri.push_back(digits[value >> 4]);
                uri.push_back(digits[value & 0x0F]);
            }
            // the uri is returned whole (§12.2)
            return uri;
        }

        // name the uri an editor knows a path by, which is the open buffer when one exists (§12.2)
        std::string uri_for_path(const DocumentStore& documents, const std::filesystem::path& path) {
            // the path is normalized first, because the loader reports it that way (§11.1)
            const std::filesystem::path normalized = path.lexically_normal();
            // an open buffer wins over the disk path, because that is what the editor is showing (§12.2)
            for (const std::string& uri : documents.uris()) {
                // a file document whose path is the dependency names the buffer to publish against (§12.2)
                if (document_path(uri).lexically_normal() == normalized)
                    return uri;
            }
            // a closed file is published against its own file uri, which is where the editor will route it (§12.2)
            return file_uri(normalized);
        }

        // report whether one source path produced a diagnostic, which decides whether it is published (§16)
        bool has_diagnostics(const compiler::DiagnosticList& diagnostics, std::string_view path) {
            // the list is walked in report order, because one matching path is enough (§16)
            return std::any_of(diagnostics.diagnostics().begin(), diagnostics.diagnostics().end(), [path](const compiler::Diagnostic& diagnostic) { return diagnostic.path == path; });
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
        core::Json diagnostics_payload(std::string_view uri, const compiler::DiagnosticList& diagnostics, std::string_view path, bool root) {
            // the payload is an object, because the protocol describes it that way (§12.2)
            core::Json payload;
            // the uri names the file, which is how an editor routes the publication (§12.2)
            payload.set("uri", core::Json(std::string(uri)));
            // the entries accumulate, one per diagnostic (§16)
            std::vector<core::Json> entries;
            // each diagnostic becomes one entry, in the order the compiler reported them (§16)
            for (const compiler::Diagnostic& diagnostic : diagnostics.diagnostics()) {
                // a diagnostic belongs to one file, so a publication carries only that file's entries (§16)
                if (root ? (!diagnostic.path.empty() && diagnostic.path != path) : diagnostic.path != path)
                    continue;
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

        // render the empty publication that clears a fixed file or a removed import (§12.2, §16)
        std::string empty_diagnostics(std::string_view uri) {
            // the payload is an object, because the protocol describes it that way (§12.2)
            core::Json payload;
            // the uri names the file whose squiggles are cleared (§12.2)
            payload.set("uri", core::Json(std::string(uri)));
            // an empty list is what clears every diagnostic for the file (§12.2)
            payload.set("diagnostics", core::Json(std::vector<core::Json>{}));
            // the payload is returned whole (§12.2)
            return payload.to_string();
        }

        // append the publications from one exchange, which is how several recompiles answer one notification (§12.2)
        void append_publications(std::vector<Notification>& target, std::vector<Notification> source) {
            // each notification keeps its order, because the root is published before its imports (§12.2)
            target.insert(target.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
        }
    } // namespace

    Exchange LspServer::compile_root(std::string_view uri) {
        // a root that is not open has nothing to compile, because every answer comes from an editor buffer (§12.2)
        const Document* document = m_documents.find(uri);
        if (document == nullptr)
            return {};
        // the uri the editor knows the root by is what a publication is routed against (§12.2)
        const std::string document_uri = document->uri();
        // the saved path is what imports resolve against, and an unsaved buffer gets a synthetic one (§11.1)
        const std::filesystem::path source_path = document_path(document_uri);
        // the root key is normalized, because the loader reports every path that way (§11.1)
        const std::string root_key = root_path(source_path).lexically_normal().generic_string();
        // the front end checks the root and every imported file, which is what carries cross-file diagnostics (§11.1)
        const CompileResult result = compile_text(document->text(), m_documents, source_path);
        // the count is kept, which is what an agreement test reads (§16)
        m_last_diagnostics = result.diagnostics.count();
        // the previous publications are what a clean import has to clear (§12.2, §16)
        std::vector<std::string> previous;
        if (const auto found = m_root_publications.find(document_uri); found != m_root_publications.end())
            previous = found->second;
        // the files this run read are the reverse edges an imported change invalidates (§11.1, §12.2)
        std::vector<std::string> dependency_paths;
        for (const std::filesystem::path& dependency : result.dependencies) {
            // the root itself is published separately, because its payload carries unannotated diagnostics (§16)
            const std::string key = dependency.lexically_normal().generic_string();
            if (key == root_key)
                continue;
            // an imported path is a dependency even when clean, because a later change must find it (§12.2)
            if (std::find(dependency_paths.begin(), dependency_paths.end(), key) == dependency_paths.end())
                dependency_paths.push_back(key);
        }
        // a diagnostic path the loader did not list is still published, because the compiler is authoritative (§16)
        for (const compiler::Diagnostic& diagnostic : result.diagnostics.diagnostics()) {
            // the root and an unannotated codegen problem already reached the root publication (§16)
            if (diagnostic.path.empty() || diagnostic.path == root_key)
                continue;
            // a path already tracked is not repeated, because one publication clears the whole file (§12.2)
            if (std::find(dependency_paths.begin(), dependency_paths.end(), diagnostic.path) == dependency_paths.end())
                dependency_paths.push_back(diagnostic.path);
        }
        // the import uris are resolved once, because an open imported buffer is published against its editor uri (§12.2)
        std::vector<std::string> dependency_uris;
        for (const std::string& dependency : dependency_paths)
            dependency_uris.push_back(uri_for_path(m_documents, dependency));
        // the root publication comes first, because an editor expects the open document to settle (§12.2, §16)
        std::vector<Notification> publications;
        publications.push_back(Notification{.method = "textDocument/publishDiagnostics", .payload = diagnostics_payload(document_uri, result.diagnostics, root_key, true).to_string()});
        // the uris this root currently owns are what a later compile compares against (§12.2)
        std::vector<std::string> current{document_uri};
        // each imported file is published when it has a problem or when its previous problem must be cleared (§12.2, §16)
        for (std::size_t index = 0; index < dependency_paths.size(); index++) {
            // a diagnostic on the imported path is what the editor shows there (§16)
            const bool diagnosed = has_diagnostics(result.diagnostics, dependency_paths.at(index));
            // a previous publication is cleared even when the file is clean now (§12.2)
            const bool was_published = std::find(previous.begin(), previous.end(), dependency_uris.at(index)) != previous.end();
            // a clean import that was never published needs no notification, which keeps an open quiet (§12.2)
            if (!diagnosed && !was_published)
                continue;
            // another root may still have a problem in this import, so this root must not clear it (§12.2)
            bool owned_elsewhere = false;
            for (const auto& [other_root, uris] : m_root_publications) {
                // the root being recompiled is replaced at the end, not consulted for ownership (§12.2)
                if (other_root == document_uri)
                    continue;
                // a uri another root still publishes is not cleared by this clean compile (§12.2)
                if (std::find(uris.begin(), uris.end(), dependency_uris.at(index)) != uris.end()) {
                    owned_elsewhere = true;
                    break;
                }
            }
            // a clean import owned by another root stays as that root published it (§12.2)
            if (!diagnosed && owned_elsewhere)
                continue;
            // the payload carries only that file's diagnostics, because its offsets address its own text (§16)
            publications.push_back(Notification{.method = "textDocument/publishDiagnostics", .payload = diagnostics_payload(dependency_uris.at(index), result.diagnostics, dependency_paths.at(index), false).to_string()});
            // a diagnosed import stays owned, because its next clean compile has to clear it (§12.2)
            if (diagnosed)
                current.push_back(dependency_uris.at(index));
        }
        // an imported file that left the graph is cleared once no other root publishes it (§12.2)
        for (const std::string& old_uri : previous) {
            // a uri this root still owns is already published above (§12.2)
            if (std::find(current.begin(), current.end(), old_uri) != current.end())
                continue;
            // a uri that is still a dependency was handled above, whether it was cleared or left to another root (§12.2)
            if (std::find(dependency_uris.begin(), dependency_uris.end(), old_uri) != dependency_uris.end())
                continue;
            // another root may still own the same imported file, so its publication must stay (§12.2)
            bool owned_elsewhere = false;
            for (const auto& [other_root, uris] : m_root_publications) {
                // the root being recompiled is replaced at the end, not consulted for ownership (§12.2)
                if (other_root == document_uri)
                    continue;
                // a uri any other root still publishes is not cleared here (§12.2)
                if (std::find(uris.begin(), uris.end(), old_uri) != uris.end()) {
                    owned_elsewhere = true;
                    break;
                }
            }
            // the stale squiggle is cleared, because no root owns it any more (§12.2, §16)
            if (!owned_elsewhere)
                publications.push_back(Notification{.method = "textDocument/publishDiagnostics", .payload = empty_diagnostics(old_uri)});
        }
        // the root's publication and dependency set replace the previous run (§12.2, §11.1)
        m_root_publications[document_uri] = std::move(current);
        m_root_dependencies[document_uri] = std::move(dependency_paths);
        // a notification produces no reply, and the publications carry every file this run touched (§12.2)
        return Exchange{.reply = {}, .notifications = std::move(publications)};
    }

    void LspServer::close_root(std::string_view uri, std::vector<Notification>& notifications) {
        // a uri that was never a root owns no publication, so there is nothing to clear (§12.2)
        const auto found = m_root_publications.find(uri);
        if (found == m_root_publications.end()) {
            // the dependency entry is dropped even when the publication entry was never stored (§12.2)
            m_root_dependencies.erase(std::string(uri));
            return;
        }
        // the published uris are copied before the entry is erased, because ownership is checked against the rest (§12.2)
        std::vector<std::string> published = std::move(found->second);
        // the closed root leaves both maps, so a later change does not recompile it (§12.2)
        m_root_publications.erase(found);
        // the dependency entry goes with the publication entry (§11.1, §12.2)
        m_root_dependencies.erase(std::string(uri));
        // each uri the closed root owned is cleared when no other root still publishes it (§12.2)
        for (const std::string& old_uri : published) {
            // a remaining root that owns the uri keeps its diagnostics (§12.2)
            bool owned_elsewhere = false;
            for (const auto& [other_root, uris] : m_root_publications) {
                // a remaining root that publishes the uri keeps it alive (§12.2)
                if (std::find(uris.begin(), uris.end(), old_uri) != uris.end()) {
                    owned_elsewhere = true;
                    break;
                }
            }
            // a uri no root owns any more has its squiggles cleared (§12.2, §16)
            if (!owned_elsewhere)
                notifications.push_back(Notification{.method = "textDocument/publishDiagnostics", .payload = empty_diagnostics(old_uri)});
        }
    }

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
            capabilities.set("publishDiagnostics", std::move(publish));
            // the full document text is what the parser takes, so every change replaces the whole buffer (§12.2)
            capabilities.set("textDocumentSync", core::Json(1.0));
            // completion is offered inside an element body and after a property colon (§12.2)
            capabilities.set("completionProvider", core::Json(std::map<std::string, core::Json, std::less<>>{}));
            // hover is offered on an element or property name (§12.2)
            capabilities.set("hoverProvider", core::Json(true));
            // the capabilities join the result (§12.2)
            result.set("capabilities", std::move(capabilities));
            // the handshake answers as a reply, because an editor waits for it (§12.2)
            return Exchange{.reply = envelope(reply_id, std::move(result)).to_string(), .notifications = {}};
        }

        // a graceful shutdown answers with null, because an editor waits before closing the channel (§12.2)
        if (method->as_string() == "shutdown")
            return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};

        // an exit notification ends the conversation, which the stdio loop reads after handling it (§12.2)
        if (method->as_string() == "exit") {
            // the flag tells the transport to stop after this exchange (§12.2)
            m_exit_requested = true;
            // exit produces no reply, because the protocol answers nothing to a notification (§12.2)
            return Exchange{.reply = {}, .notifications = {}};
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
            // the changed uri and path are what its importers key on, because an import is a reverse edge (§11.1, §12.2)
            const std::string changed_uri = *uri;
            const std::filesystem::path changed_path = document_path(changed_uri).lexically_normal();
            // the changed document is compiled first, because it is what the editor is showing (§12.2)
            std::vector<std::string> roots{changed_uri};
            // every open root that read this file is recompiled, because its diagnostics depend on it (§12.2, §16)
            for (const auto& [root, dependencies] : m_root_dependencies) {
                // the changed document is already first, so it is not compiled twice (§12.2)
                if (root == changed_uri)
                    continue;
                // a root that read this path is stale until it is recompiled (§11.1, §12.2)
                if (std::find(dependencies.begin(), dependencies.end(), changed_path.generic_string()) != dependencies.end())
                    roots.push_back(root);
            }
            // the publications accumulate across the changed root and every importer (§12.2)
            std::vector<Notification> publications;
            // the changed root's count is what the agreement test reads, even when an importer publishes later (§16)
            std::size_t changed_diagnostics = 0;
            // each root is compiled once, in root order, because the first publication is the edited document (§12.2)
            for (const std::string& root : roots) {
                // a root that closed while the exchange was handled has nothing to compile (§12.2)
                if (m_documents.find(root) == nullptr)
                    continue;
                // the compile publishes the root and its imports, and clears what it no longer owns (§12.2, §16)
                Exchange compiled = compile_root(root);
                // the edited document's count is kept, because that is the compile the editor asked for (§16)
                if (root == changed_uri)
                    changed_diagnostics = m_last_diagnostics;
                // the publications join the one exchange the notification returns (§12.2)
                append_publications(publications, std::move(compiled.notifications));
            }
            // the edited document's count wins over any importer compiled after it (§16)
            m_last_diagnostics = changed_diagnostics;
            // a notification produces no reply, and the publications carry every file that changed (§12.2)
            return Exchange{.reply = {}, .notifications = std::move(publications)};
        }

        // a closed document is forgotten, which is what didClose means (§12.2)
        if (method->as_string() == "textDocument/didClose") {
            // a document request carries parameters (§12.2)
            if (params != nullptr && params->is_object()) {
                // the document is named through a text document identifier (§12.2)
                if (const core::Json* text_document = params->member("textDocument"); text_document != nullptr && text_document->is_object()) {
                    // the uri names what to forget (§12.2)
                    if (const std::optional<std::string> uri = string_member(*text_document, "uri")) {
                        // the closed path is what its importers key on, before the buffer is forgotten (§11.1, §12.2)
                        const std::string closed_uri = *uri;
                        const std::filesystem::path closed_path = document_path(closed_uri).lexically_normal();
                        // an open root that read this buffer must fall back to the saved file (§11.1, §12.2)
                        std::vector<std::string> importers;
                        for (const auto& [root, dependencies] : m_root_dependencies) {
                            // the closed document is not an importer of itself (§12.2)
                            if (root == closed_uri)
                                continue;
                            // a root that read this path has a stale view until it is recompiled (§11.1, §12.2)
                            if (std::find(dependencies.begin(), dependencies.end(), closed_path.generic_string()) != dependencies.end())
                                importers.push_back(root);
                        }
                        // the buffer is forgotten, which is what a close means (§12.2)
                        m_documents.close(closed_uri);
                        // the publications of the closed root are cleared unless another root still owns them (§12.2)
                        std::vector<Notification> publications;
                        close_root(closed_uri, publications);
                        // each importer is recompiled against the disk copy, which the editor now sees (§11.1, §12.2)
                        for (const std::string& root : importers) {
                            // an importer that closed in the same exchange has nothing to compile (§12.2)
                            if (m_documents.find(root) == nullptr)
                                continue;
                            // the recompile publishes the importer and its imports against the saved file (§12.2, §16)
                            Exchange compiled = compile_root(root);
                            // the publications join the one exchange the close returns (§12.2)
                            append_publications(publications, std::move(compiled.notifications));
                        }
                        // a close produces no reply, and publications clear or refresh its dependents (§12.2)
                        return Exchange{.reply = {}, .notifications = std::move(publications)};
                    }
                }
            }
            // a close with no document names nothing, which produces no reply and no publication (§12.2)
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

    bool LspServer::exit_requested() const { return m_exit_requested; }
} // namespace albor::language_server
