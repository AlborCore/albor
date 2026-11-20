#include "albor/language-server/lsp_server.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "albor/compiler/codegen/codegen.h"
#include "albor/compiler/diagnostic.h"
#include "albor/compiler/lexer.h"
#include "albor/compiler/source_index.h"
#include "albor/compiler/type_checker.h"
#include "albor/compiler/ui_loader.h"
#include "albor/compiler/value_type.h"
#include "albor/core/file_bytes.h"
#include "albor/core/json.h"
#include "albor/language-server/lsp_support.h"
#include "albor/manifest/manifest.h"

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

        // name the workspace root the initialize params carry, which is what a relative manifest resolves against (§11.2)
        std::filesystem::path workspace_root(const core::Json* params) {
            // a handshake without parameters names no workspace, so the process directory answers (§12.2)
            if (params == nullptr || !params->is_object())
                return {};
            // the first workspace folder is the root, because a protocol workspace may hold several (§12.2)
            const core::Json* folders = params->member("workspaceFolders");
            // a folder list names the root first, which is what a relative path resolves against (§12.2)
            if (folders != nullptr && folders->kind() == core::Json::Kind::Array && !folders->as_array().empty()) {
                // the uri is decoded because a client writes the folder as a file uri (§12.2)
                const std::optional<std::string> uri = string_member(folders->as_array().front(), "uri");
                // a file uri names the workspace directory, which is where a relative manifest sits (§12.2)
                if (uri) {
                    // the decoded path is the directory a manifest resolves beside (§11.2, §12.2)
                    const std::filesystem::path path = document_path(*uri);
                    // a workspace the uri cannot decode falls through to the older root uri (§12.2)
                    if (!path.empty())
                        return path;
                }
            }
            // an older client names its single root through rootUri, which is the same directory (§12.2)
            if (const std::optional<std::string> root = string_member(*params, "rootUri"); root)
                return document_path(*root);
            // a handshake that names no root leaves the resolution to the process directory (§12.2)
            return {};
        }

        // read the manifests the initialization options carry, which is how a client supplies its catalog (§11.2, AD-17)
        std::vector<std::filesystem::path> manifest_paths(const core::Json* params) {
            // the paths accumulate in the order the client wrote them, because that order decides a duplicate name (§11.2)
            std::vector<std::filesystem::path> paths;
            // a handshake without parameters configures no manifest, which leaves the builtins alone (§11.2, AD-17)
            if (params == nullptr || !params->is_object())
                return paths;
            // the initialization options are where a client names its workspace configuration (§12.2)
            const core::Json* options = params->member("initializationOptions");
            // options that are absent or not an object carry no manifest list (§12.2)
            if (options == nullptr || !options->is_object())
                return paths;
            // the manifest list is the catalog source the server reads at the handshake (§11.2, AD-17)
            const core::Json* manifests = options->member("manifests");
            // a list that is absent or not an array configures no manifest (§12.2)
            if (manifests == nullptr || manifests->kind() != core::Json::Kind::Array)
                return paths;
            // a relative path resolves against the workspace, because that is where the build reads it (§11.2)
            const std::filesystem::path workspace = workspace_root(params);
            // each entry is a filesystem path, because a client resolves its settings to a path (§11.2, §12.2)
            for (const core::Json& entry : manifests->as_array()) {
                // an entry that is not a string names no file, so it contributes nothing (§12.2)
                if (entry.kind() != core::Json::Kind::String)
                    continue;
                // an empty entry is a setting the user left blank, which names no manifest (§11.2)
                if (entry.as_string().empty())
                    continue;
                // the written path is decoded as utf-8, which a windows code page would otherwise corrupt (§12.2)
                const std::filesystem::path written = path_from_utf8(entry.as_string());
                // an absolute path is used as written, because the client already resolved it (§11.2)
                if (written.is_absolute()) {
                    // the normalized spelling is what a watched file change is compared against (§12.2)
                    paths.push_back(written.lexically_normal());
                    // the entry is complete, so the next one follows (§11.2)
                    continue;
                }
                // a relative path resolves against the workspace folder, which is where the build reads it (§11.2)
                const std::filesystem::path base = workspace.empty() ? std::filesystem::current_path() : workspace;
                // the joined path is normalized, because a later change and a compile spell it the same way (§12.2)
                paths.push_back((base / written).lexically_normal());
            }
            // the paths are returned in the order the client wrote them (§11.2)
            return paths;
        }

        // what one compile produced, which is the diagnostics and the files the root read (§11.1, §16)
        struct CompileResult
        {
            // the problems the front end found, which the editor is told about (§16)
            compiler::DiagnosticList diagnostics;

            // the root and every import in load order, which decides what a later change invalidates (§11.1, §12.2)
            std::vector<std::filesystem::path> dependencies;

            // the components the root sees, which completion and hover resolve an instance against (§11.1, §12.2)
            ComponentSurface components;

            // the files the compile indexed, root first, which references and rename read (§11.1, §12.2)
            std::vector<IndexedSource> sources;
        };

        // the surface a checked file exposes to an editor, which is its own components and imports (§11.1, §12.2)
        ComponentSurface component_surface(const compiler::CheckedFile& checked) {
            // the surface accumulates in declaration order, which is the order completion offers (§12.2)
            ComponentSurface surface;
            // each component the file declares is visible to the whole file, so its declared members are retained (§11.1)
            for (const compiler::CheckedComponent& component : checked.components)
                surface.local.push_back(compiler::component_element_type(component.name, component.properties, component.signals));
            // each imported interface is visible too, so an instance of it completes and hovers (§11.1)
            for (const compiler::ImportedComponent& component : checked.imported_components)
                surface.imported.push_back(compiler::component_element_type(component.name, component.properties, component.signals));
            // the bounded surface is what the editor sees, because no other file's components belong to this root (§12.2)
            return surface;
        }

        // the surface an open root carries before its first compile, which offers no component name (§12.2)
        const ComponentSurface EMPTY_COMPONENT_SURFACE;

        // run the compiler over one document's text and every open import buffer (§16)
        CompileResult compile_text(const std::string& text, const DocumentStore& documents, const std::filesystem::path& source_path, const compiler::ElementCatalog& base_catalog) {
            // the diagnostics accumulate, because the server publishes what the compiler found (§16)
            compiler::DiagnosticList diagnostics;
            // the run starts from the client catalog, because a manifest type resolves like a builtin (§11.2, AD-17)
            compiler::ElementCatalog catalog = base_catalog;
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
                const std::optional<std::vector<std::uint8_t>> bytes = core::read_regular_file(path);
                // a missing file is reported by the loader against the import that named it (§11.1)
                if (!bytes)
                    return std::nullopt;
                // the bytes are the source the parser takes (§11.1)
                return std::string(bytes->begin(), bytes->end());
            });
            // the loader checks the root and every import, and an unsaved buffer has no stem to compare (§12.2)
            const std::optional<compiler::CheckedFile> checked = loader.check(root, source_path.empty() ? compiler::RootNameCheck::Skipped : compiler::RootNameCheck::Required);
            // a root that could not be checked stops before generation, because there is no typed tree to generate (§11.3)
            if (!checked.has_value())
                return CompileResult{.diagnostics = std::move(diagnostics), .dependencies = loader.dependencies(), .components = {}, .sources = {}};
            // the indexed files start with the root, which an editor answers definitions and tokens from (§12.2)
            std::vector<IndexedSource> sources;
            // the root tree is what the loader kept alive, and its absence means no index can name a declaration (§12.2)
            if (const compiler::SourceFile* root_source = loader.root_source(); root_source != nullptr) {
                // an unsaved buffer names no path, so its key is filled from its uri once the root is stored (§12.2)
                const std::string root_key = source_path.empty() ? std::string{} : normalized_root.generic_string();
                // the root entry carries the export, which a component rename may not change (§11.1, §14.2)
                sources.push_back(IndexedSource{.key = root_key, .uri = {}, .exported_component = checked->exported_component, .index = compiler::index_source(text, *root_source, *checked)});
                // every import is indexed too, because references cross the import graph (§11.1, §12.2)
                for (const compiler::LoadedFile& loaded : loader.loaded_files()) {
                    // the root is already the first entry, so it is not indexed twice (§12.2)
                    if (loaded.path.lexically_normal() == normalized_root)
                        continue;
                    // the import is indexed without its text, because only a root answers a token request (§12.2)
                    compiler::SourceIndex imported = compiler::index_source({}, *loaded.source, *loaded.checked);
                    // the token stream is dropped, because an import is indexed for its uses alone (§12.2)
                    imported.tokens.clear();
                    // the import joins the graph, which references and rename search (§12.2)
                    sources.push_back(IndexedSource{.key = loaded.path.lexically_normal().generic_string(), .uri = {}, .exported_component = loaded.checked->exported_component, .index = std::move(imported)});
                }
            }
            // the generator runs too, because a diagnostic may come only from codegen (§11.3)
            compiler::CodeGenerator generator(diagnostics);
            // the catalog is handed over, because a native element constructs through its factory (§11.2, AD-17)
            generator.set_catalog(&catalog);
            // a document with a file path resolves its assets beside itself, which is what the build does (§12.2, §6.4)
            if (!source_path.empty())
                generator.set_source_path(source_path);
            // a tree with a type error is not generated, because only a clean one reaches the backend (§11.3)
            if (!diagnostics.has_errors()) {
                // the generated output is discarded, because the editor is told the diagnostics (§11.3)
                (void)generator.generate(*checked);
            }
            // the diagnostics and files read are returned, which the editor and a later change need (§16, §12.2)
            return CompileResult{.diagnostics = std::move(diagnostics), .dependencies = loader.dependencies(), .components = component_surface(*checked), .sources = std::move(sources)};
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

        // report whether a span covers the cursor, which is what a definition request asks (§12.2)
        bool span_contains(const compiler::SourceSpan& span, const compiler::SourceLocation& cursor) {
            // a cursor before the first character is outside the span, because the span starts later (§12.2)
            if (cursor.line < span.start.line || (cursor.line == span.start.line && cursor.column < span.start.column))
                return false;
            // a cursor at or after the exclusive end is outside the span, because the span has already ended (§12.2)
            return cursor.line < span.end.line || (cursor.line == span.end.line && cursor.column < span.end.column);
        }

        // render the declaration the cursor names as the protocol's location, which an editor opens (§12.2)
        core::Json definition_location(std::string_view document_uri, const compiler::SourceTarget& target) {
            // the location is an object, because the protocol describes it that way (§12.2)
            core::Json location;
            // a declaration in the same file is addressed by the document's own uri (§12.2)
            location.set("uri", core::Json(target.file.empty() ? std::string(document_uri) : file_uri(target.file)));
            // the range is the declared name, which is where the cursor lands (§12.2)
            core::Json range;
            // the start is the first character of the name (§12.2)
            range.set("start", position_value(target.span.start));
            // the end is one past the declared name, which is the protocol's exclusive form (§12.2)
            range.set("end", position_value(target.span.end));
            // the range joins the location (§12.2)
            location.set("range", std::move(range));
            // the location is returned whole (§12.2)
            return location;
        }

        // classify a declaration as the protocol's document symbol kind, which an outline icon shows (§12.2)
        double document_symbol_kind(compiler::SourceSymbolKind kind) {
            // the declaration kind decides, because an editor picks its icon from it (§12.2)
            switch (kind) {
            case compiler::SourceSymbolKind::Component:
                return 5.0;
            case compiler::SourceSymbolKind::Property:
                return 7.0;
            case compiler::SourceSymbolKind::Signal:
                return 24.0;
            case compiler::SourceSymbolKind::Command:
            case compiler::SourceSymbolKind::Function:
                return 12.0;
            case compiler::SourceSymbolKind::Element:
                return 19.0;
            case compiler::SourceSymbolKind::Global:
                return 13.0;
            case compiler::SourceSymbolKind::Struct:
                return 23.0;
            case compiler::SourceSymbolKind::Enum:
                return 10.0;
            }
            // a kind outside the declared set is a variable, because it names a value (§12.2)
            return 13.0;
        }

        // render one declaration as the protocol's document symbol, which an outline nests (§12.2)
        core::Json document_symbol(const compiler::SourceSymbol& symbol) {
            // the symbol is an object, because the protocol describes it that way (§12.2)
            core::Json entry;
            // the name is what the outline shows (§12.2)
            entry.set("name", core::Json(symbol.name));
            // the kind is what an editor picks an icon from (§12.2)
            entry.set("kind", core::Json(document_symbol_kind(symbol.kind)));
            // the detail is shown beside the name when the declaration says something more (§12.2)
            if (!symbol.detail.empty())
                entry.set("detail", core::Json(symbol.detail));
            // the range covers the whole declaration, which is what an outline folds (§12.2)
            core::Json range;
            // the start is where the declaration begins (§12.2)
            range.set("start", position_value(symbol.span.start));
            // the end is where the declaration ends (§12.2)
            range.set("end", position_value(symbol.span.end));
            // the range joins the entry (§12.2)
            entry.set("range", std::move(range));
            // the selection range is the declared name, which an outline highlights (§12.2)
            core::Json selection;
            // the start is the first character of the name (§12.2)
            selection.set("start", position_value(symbol.name_span.start));
            // the end is one past the name (§12.2)
            selection.set("end", position_value(symbol.name_span.end));
            // the selection joins the entry (§12.2)
            entry.set("selectionRange", std::move(selection));
            // the children are the declarations the symbol contains, which an outline nests (§12.2)
            if (!symbol.children.empty()) {
                // the rendered children accumulate in declaration order (§12.2)
                std::vector<core::Json> children;
                // each child is rendered by the same rule, because an outline nests to any depth (§12.2)
                for (const compiler::SourceSymbol& child : symbol.children)
                    children.push_back(document_symbol(child));
                // the children join the entry, which is what makes the outline a tree (§12.2)
                entry.set("children", core::Json(std::move(children)));
            }
            // the entry is returned whole (§12.2)
            return entry;
        }

        // render the indexed tokens as the protocol's delta encoded stream, which an editor paints (§12.2)
        core::Json semantic_tokens(const compiler::SourceIndex& index) {
            // the data carries five numbers per token, in the order the protocol fixes (§12.2)
            std::vector<core::Json> data;
            // the previous token's line is what a delta is measured from (§12.2)
            std::uint32_t previous_line = 0;
            // the previous token's start is what a same line delta is measured from (§12.2)
            std::uint32_t previous_start = 0;
            // each token is encoded in source order, which the index already guarantees (§12.2)
            for (const compiler::SourceToken& token : index.tokens) {
                // the protocol counts lines from zero, and a compiler line below one clamps (§12.2)
                const std::uint32_t line = token.span.start.line > 0 ? token.span.start.line - 1 : 0;
                // the protocol counts characters from zero, and a compiler column below one clamps (§12.2)
                const std::uint32_t start = token.span.start.column > 0 ? token.span.start.column - 1 : 0;
                // the end is exclusive, which is what the protocol's length counts (§12.2)
                const std::uint32_t end = token.span.end.column > 0 ? token.span.end.column - 1 : 0;
                // a token that covers no characters cannot be painted, so it carries nothing (§12.2)
                if (end <= start)
                    continue;
                // the line delta is what the protocol measures first (§12.2)
                const std::uint32_t delta_line = line - previous_line;
                // a token on the same line is measured from its predecessor, a new line starts at its column (§12.2)
                const std::uint32_t delta_start = delta_line == 0 ? start - previous_start : start;
                // the line delta is the first number (§12.2)
                data.push_back(core::Json(static_cast<double>(delta_line)));
                // the start delta is the second number (§12.2)
                data.push_back(core::Json(static_cast<double>(delta_start)));
                // the length is the third number (§12.2)
                data.push_back(core::Json(static_cast<double>(end - start)));
                // the token type is the legend index the server advertised (§12.2)
                data.push_back(core::Json(static_cast<double>(static_cast<int>(token.kind))));
                // no token carries a modifier, so the fifth number is zero (§12.2)
                data.push_back(core::Json(0.0));
                // the encoded token becomes the next token's origin (§12.2)
                previous_line = line;
                // the same holds for its start column (§12.2)
                previous_start = start;
            }
            // the reply is an object, because the protocol describes it that way (§12.2)
            core::Json result;
            // the data is the whole of a full token reply (§12.2)
            result.set("data", core::Json(std::move(data)));
            // the reply is returned whole (§12.2)
            return result;
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

        // render one source span as the protocol range, which a location and an edit both carry (§12.2)
        core::Json range_of(const compiler::SourceSpan& span) {
            // the range is an object, because the protocol describes it that way (§12.2)
            core::Json range;
            // the start is the first character of the name (§12.2)
            range.set("start", position_value(span.start));
            // the end is one past the name, which is the protocol's exclusive form (§12.2)
            range.set("end", position_value(span.end));
            // the range is returned whole (§12.2)
            return range;
        }

        // the use the cursor names, which a jump, a reference and a rename all start from (§12.2)
        const compiler::SourceUse* use_at(const compiler::SourceIndex& index, const compiler::SourceLocation& cursor) {
            // a cursor just past a name still names it, which is what a double click at the end does (§12.2)
            const compiler::SourceUse* edge = nullptr;
            // the uses are scanned in written order, because the first one covering the cursor is under it (§12.2)
            for (const compiler::SourceUse& use : index.uses) {
                // a span that covers the cursor is the exact answer (§12.2)
                if (span_contains(use.span, cursor))
                    return &use;
                // a span ending at the cursor is the fallback when the cursor sits just past the name (§12.2)
                if (use.span.end == cursor)
                    edge = &use;
            }
            // the edge fallback answers only when nothing contains the cursor (§12.2)
            return edge;
        }

        // the declaration name the cursor sits on, which a references request may start from (§12.2)
        const compiler::SourceSymbol* symbol_at(const std::vector<compiler::SourceSymbol>& symbols, const compiler::SourceLocation& cursor) {
            // every declaration is checked, because the cursor may name a top level or a nested symbol (§12.2)
            for (const compiler::SourceSymbol& symbol : symbols) {
                // the name is the only range a declaration answers from (§12.2)
                if (span_contains(symbol.name_span, cursor))
                    return &symbol;
                // the children follow, because a member and an element sit below their component (§12.2)
                if (const compiler::SourceSymbol* child = symbol_at(symbol.children, cursor); child != nullptr)
                    return child;
            }
            // nothing at the cursor means the cursor names no declaration (§12.2)
            return nullptr;
        }

        // the declaration a request names, which is its file key and the exact name span (§12.2)
        struct DeclarationId
        {
            // the file key the declaration lives in, which is a path or an unsaved buffer's uri (§12.2)
            std::string file;

            // the region the declaration name occupies, which every use targets (§12.2)
            compiler::SourceSpan span;
        };

        // the key a target names, which a same file target spells as empty (§12.2)
        std::string target_key(const std::string& indexed_key, const compiler::SourceTarget& target) {
            // a target in the indexed file is that file itself (§12.2)
            return target.file.empty() ? indexed_key : target.file.lexically_normal().generic_string();
        }

        // the declaration the cursor names, which a use target or a declaration symbol provides (§12.2)
        std::optional<DeclarationId> declaration_id_at(const compiler::SourceIndex& index, const std::string& key, const compiler::SourceLocation& cursor) {
            // a written name is resolved through its target, which is the identity a rename follows (§12.2)
            if (const compiler::SourceUse* use = use_at(index, cursor); use != nullptr && use->target.has_value())
                return DeclarationId{.file = target_key(key, *use->target), .span = use->target->span};
            // a declaration name is an identity on its own, and an element name is a use rather than a declaration (§12.2)
            if (const compiler::SourceSymbol* symbol = symbol_at(index.symbols, cursor); symbol != nullptr && symbol->kind != compiler::SourceSymbolKind::Element && !symbol->name_span.empty())
                return DeclarationId{.file = key, .span = symbol->name_span};
            // nothing at the cursor names a declaration, so no answer is possible (§12.2)
            return std::nullopt;
        }

        // find a declaration by its exact name span below one symbol list, which keeps the component it belongs to (§12.2)
        const compiler::SourceSymbol* symbol_with_span(const std::vector<compiler::SourceSymbol>& symbols, const compiler::SourceSpan& span, const compiler::SourceSymbol** parent) {
            // every declaration is checked, because the identity names one exact name span (§12.2)
            for (const compiler::SourceSymbol& symbol : symbols) {
                // the exact name span is the declaration the request names (§12.2)
                if (symbol.name_span == span)
                    return &symbol;
                // the children follow, because a member sits below its component (§12.2)
                if (const compiler::SourceSymbol* child = symbol_with_span(symbol.children, span, parent); child != nullptr) {
                    // the enclosing declaration is the parent of the symbol the search found (§12.2)
                    *parent = &symbol;
                    return child;
                }
            }
            // a span no declaration carries names nothing in this file (§12.2)
            return nullptr;
        }

        // one declaration found in the graph, which validation reads before an edit (§12.2)
        struct LocatedSymbol
        {
            // the indexed file the declaration lives in, which is the identity file (§12.2)
            const IndexedSource* source = nullptr;

            // the declaration itself, which carries its kind and name span (§12.2)
            const compiler::SourceSymbol* symbol = nullptr;

            // the component a member belongs to, which is null for a top level declaration (§12.2)
            const compiler::SourceSymbol* parent = nullptr;
        };

        // find one declaration by identity, which validation reads before any edit (§12.2)
        std::optional<LocatedSymbol> locate_symbol(const std::vector<const IndexedSource*>& sources, const DeclarationId& id) {
            // every indexed view is searched, because a file imported by several roots appears once per graph (§12.2)
            for (const IndexedSource* source : sources) {
                // only the file the identity names can hold the declaration (§12.2)
                if (source->key != id.file)
                    continue;
                // the declaration is looked up by its exact name span, with the declaration it belongs to (§12.2)
                const compiler::SourceSymbol* parent = nullptr;
                const compiler::SourceSymbol* symbol = symbol_with_span(source->index.symbols, id.span, &parent);
                // a declaration the outline does not carry has no declaration surface (§12.2)
                if (symbol == nullptr)
                    continue;
                // the located declaration carries its component, which a member's scope check reads (§12.2)
                return LocatedSymbol{.source = source, .symbol = symbol, .parent = parent};
            }
            // a declaration no indexed file carries cannot be validated (§12.2)
            return std::nullopt;
        }

        // the root entry of one open document, which its own declarations and uses are read from (§12.2)
        const IndexedSource* root_source_of(const std::map<std::string, std::vector<IndexedSource>, std::less<>>& roots, std::string_view uri) {
            // the root entry is absent when the compile produced no typed tree (§12.2)
            const auto found = roots.find(uri);
            // a document with no index answers nothing, which every caller reports as null (§12.2)
            if (found == roots.end() || found->second.empty())
                return nullptr;
            // the first entry is the root, because the compile records it before its imports (§12.2)
            return &found->second.front();
        }

        // every indexed file of every open root, which a references request searches (§12.2)
        std::vector<const IndexedSource*> indexed_sources(const std::map<std::string, std::vector<IndexedSource>, std::less<>>& roots) {
            // the files accumulate across roots, because an open importer contributes its own view (§12.2)
            std::vector<const IndexedSource*> sources;
            // each root contributes its graph, which is the root and every file it checked (§12.2)
            for (const auto& entry : roots) {
                // each file is referenced, because indexes are owned by the root's entry (§12.2)
                for (const IndexedSource& file : entry.second)
                    sources.push_back(&file);
            }
            // the files are returned in root order, which keeps a declaration lookup stable (§12.2)
            return sources;
        }

        // one place a declaration is written, which a references reply lists and a rename edits (§12.2)
        struct ReferenceSite
        {
            // the file key the site is written in, which is what dedupe compares (§12.2)
            std::string key;

            // the uri an editor addresses the file by, which the reply or the edit carries (§12.2)
            std::string uri;

            // the region the name occupies, which is what an editor highlights (§12.2)
            compiler::SourceSpan span;

            // whether the site spells a handler, which keeps the on_ prefix when renamed (§11.1)
            bool handler = false;
        };

        // collect every written name of one declaration across the indexed files (§12.2)
        std::vector<ReferenceSite> reference_sites(const std::vector<const IndexedSource*>& sources, const DeclarationId& id, bool include_declaration) {
            // the sites accumulate in graph order, because the caller sorts them for the reply (§12.2)
            std::vector<ReferenceSite> sites;
            // the declaration itself is a site when the client asked for it (§12.2)
            if (include_declaration) {
                // the declaring file is one of the indexed views, because the identity came from one of them (§12.2)
                for (const IndexedSource* source : sources) {
                    // a different file is not where the declaration is written (§12.2)
                    if (source->key != id.file)
                        continue;
                    // the declaration site carries the file's own uri, which is where the edit lands (§12.2)
                    sites.push_back(ReferenceSite{.key = source->key, .uri = source->uri, .span = id.span, .handler = false});
                    // one view of the file is enough, because the others carry the same declaration (§12.2)
                    break;
                }
            }
            // every use of every indexed file is compared against the identity (§12.2)
            for (const IndexedSource* source : sources) {
                // each written name is one candidate, because a use without a target is not this declaration (§12.2)
                for (const compiler::SourceUse& use : source->index.uses) {
                    // a use the checker left unresolved cannot name this declaration (§12.2)
                    if (!use.target.has_value())
                        continue;
                    // the target is normalized to the file key it was written in (§12.2)
                    const std::string key = target_key(source->key, *use.target);
                    // a different declaration is a different site, so the scan continues (§12.2)
                    if (key != id.file || use.target->span != id.span)
                        continue;
                    // the site carries the use's own file, because that is where an edit lands (§12.2)
                    sites.push_back(ReferenceSite{.key = source->key, .uri = source->uri, .span = use.span, .handler = use.kind == compiler::SourceUseKind::Handler});
                }
            }
            // the sites are returned whole, because dedupe and sorting are the caller's concern (§12.2)
            return sites;
        }

        // drop the repeated sites a file indexed by several roots produces, so one region is answered once (§12.2)
        std::vector<ReferenceSite> unique_sites(std::vector<ReferenceSite> sites) {
            // the unique sites accumulate in the order the graphs produced them (§12.2)
            std::vector<ReferenceSite> unique;
            // each site is compared against the ones already kept, because the graph may hold a file twice (§12.2)
            for (const ReferenceSite& site : sites) {
                // a site already kept is the same region in the same file (§12.2)
                const bool known = std::any_of(unique.begin(), unique.end(), [&site](const ReferenceSite& kept) { return kept.key == site.key && kept.span == site.span; });
                // a repeated site adds nothing, so the next one follows (§12.2)
                if (known)
                    continue;
                // a new region is kept, because it is one place the declaration is written (§12.2)
                unique.push_back(site);
            }
            // the unique sites are what the reply renders (§12.2)
            return unique;
        }

        // order the sites by file and position, which keeps a reply stable (§12.2)
        void sort_sites(std::vector<ReferenceSite>& sites) {
            // the files and positions decide in that order, because an editor reads top to bottom (§12.2)
            std::sort(sites.begin(), sites.end(), [](const ReferenceSite& lhs, const ReferenceSite& rhs) {
                // the file decides first, because a reply groups by document (§12.2)
                if (lhs.key != rhs.key)
                    return lhs.key < rhs.key;
                // the line follows, because an editor reads top to bottom (§12.2)
                if (lhs.span.start.line != rhs.span.start.line)
                    return lhs.span.start.line < rhs.span.start.line;
                // the column follows, because two sites on one line keep written order (§12.2)
                return lhs.span.start.column < rhs.span.start.column;
            });
        }

        // report whether a name may be written as an identifier, which a rename must produce (§11.1)
        bool valid_identifier(std::string_view name) {
            // an empty name writes nothing, so it is not an identifier (§11.1)
            if (name.empty())
                return false;
            // each character decides, because an identifier is a letter or underscore then name characters (§11.1)
            for (std::size_t index = 0; index < name.size(); index++) {
                // the character is read once, because both the first and the later rule read it (§11.1)
                const char c = name[index];
                // a letter or an underscore is the only first character, because a digit cannot start a name (§11.1)
                const bool start = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
                // a digit is allowed after the first character, because a name may continue with one (§11.1)
                const bool part = start || (c >= '0' && c <= '9');
                // a first character that is not a letter or underscore is refused (§11.1)
                if (index == 0 && !start)
                    return false;
                // a later character outside the name set is refused, because the parser would split the name (§11.1)
                if (index != 0 && !part)
                    return false;
            }
            // a language keyword cannot be an identifier, because the parser consumes it as syntax (§11.1)
            return !compiler::is_language_keyword(name);
        }

        // report whether a symbol kind is a component member, which shares one declared namespace (§11.1)
        bool is_member_kind(compiler::SourceSymbolKind kind) {
            // a property, a signal and a command are declared members of one component (§11.1, §18)
            return kind == compiler::SourceSymbolKind::Property || kind == compiler::SourceSymbolKind::Signal || kind == compiler::SourceSymbolKind::Command;
        }

        // count the declarations of one name in one scope, which decides whether a rename is unambiguous (§11.1)
        std::size_t declarations_named(const std::vector<compiler::SourceSymbol>& symbols, std::string_view name, bool components_only) {
            // the count accumulates over the scope, because a duplicate makes the name ambiguous (§11.1)
            std::size_t count = 0;
            // each declaration is compared, because the scope holds the whole member set (§11.1)
            for (const compiler::SourceSymbol& symbol : symbols) {
                // a component scope counts only components, because it declares no members (§11.1)
                if (components_only && symbol.kind != compiler::SourceSymbolKind::Component)
                    continue;
                // a member scope counts only members, because an element is not a declaration of this name (§11.1)
                if (!components_only && !is_member_kind(symbol.kind))
                    continue;
                // the name decides, because one spelling is one declaration unless it is duplicated (§11.1)
                if (std::string_view(symbol.name) == name)
                    count++;
            }
            // the count is what validation reads (§11.1)
            return count;
        }

        // report whether a common element property carries a name, which a property rename must not shadow (§8.2, §11.1)
        bool is_common_property(std::string_view name) {
            // every common property is compared, because the checker appends them to every type (§8.2)
            for (const compiler::ElementProperty& property : compiler::common_element_properties()) {
                // the name decides, because a declared property shadows one of the same name (§11.1)
                if (std::string_view(property.name) == name)
                    return true;
            }
            // a name outside the common set is free to declare (§11.1)
            return false;
        }

        // why a declaration may not be renamed at all, which prepareRename and rename both read (§12.2)
        std::optional<std::string> rename_target_refusal(const LocatedSymbol& located) {
            // a declaration outside the issue's symbol set has no surface this feature edits (§12.2)
            if (located.symbol->kind != compiler::SourceSymbolKind::Component && located.symbol->kind != compiler::SourceSymbolKind::Property && located.symbol->kind != compiler::SourceSymbolKind::Signal)
                return "the symbol has no renameable declaration";
            // a member needs its component, because the member scope is what a duplicate check reads (§11.1)
            if (located.symbol->kind != compiler::SourceSymbolKind::Component && located.parent == nullptr)
                return "the symbol has no renameable declaration";
            // the exported component is named after the file, so renaming it would break the build (§11.1, §14.2)
            if (located.symbol->kind == compiler::SourceSymbolKind::Component && located.source->exported_component == located.symbol->name)
                return "the component is named after its file, so renaming it needs the file renamed too";
            // a name declared twice is ambiguous, because a use resolves to the first declaration (§11.1)
            const std::size_t duplicates = located.symbol->kind == compiler::SourceSymbolKind::Component ? declarations_named(located.source->index.symbols, located.symbol->name, true) : declarations_named(located.parent->children, located.symbol->name, false);
            // a duplicate declaration has no single target, so the rename is refused (§11.1)
            if (duplicates > 1)
                return "'" + located.symbol->name + "' is declared more than once, so the rename is ambiguous";
            // the declaration is renameable, because it is one source declaration of a supported kind (§12.2)
            return std::nullopt;
        }

        // why one rename may not proceed, which validation decides before any edit is produced (§12.2)
        std::optional<std::string> rename_refusal(const compiler::ElementCatalog& catalog, const LocatedSymbol& located, std::string_view new_name) {
            // the declaration's own kind decides first, because a builtin or an identity has no surface (§12.2)
            if (const std::optional<std::string> refusal = rename_target_refusal(located); refusal.has_value())
                return refusal;
            // a rename to the same spelling changes nothing, which is a valid no-op (§12.2)
            if (new_name == located.symbol->name)
                return std::nullopt;
            // a component may not become a catalog type, because every existing spelling would resolve differently (§11.1)
            if (located.symbol->kind == compiler::SourceSymbolKind::Component && catalog.find(new_name) != nullptr)
                return "'" + std::string(new_name) + "' is a builtin or manifest type";
            // a component may not duplicate a declaration in its file, because one name resolves once (§11.1)
            if (located.symbol->kind == compiler::SourceSymbolKind::Component && declarations_named(located.source->index.symbols, new_name, true) > 0)
                return "'" + std::string(new_name) + "' is already declared in this file";
            // a member may not duplicate another member, because one name declares one slot (§11.1)
            if (located.symbol->kind != compiler::SourceSymbolKind::Component && declarations_named(located.parent->children, new_name, false) > 0)
                return "'" + std::string(new_name) + "' is already a member of this component";
            // a property may not shadow a common one, because the rename would change an existing spelling (§11.1)
            if (located.symbol->kind == compiler::SourceSymbolKind::Property && is_common_property(new_name))
                return "'" + std::string(new_name) + "' is a common element property";
            // the name is free, because nothing it could shadow or duplicate is visible here (§12.2)
            return std::nullopt;
        }

        // render one edit, which is the range a rename replaces and the text it writes there (§12.2)
        core::Json edit_of(const compiler::SourceSpan& span, std::string text) {
            // the edit is an object, because the protocol describes it that way (§12.2)
            core::Json edit;
            // the range is the name the edit replaces (§12.2)
            edit.set("range", range_of(span));
            // the text is what the editor writes there (§12.2)
            edit.set("newText", core::Json(std::move(text)));
            // the edit is returned whole (§12.2)
            return edit;
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
        CompileResult result = compile_text(document->text(), m_documents, source_path, m_catalog);
        // the count is kept, which is what an agreement test reads (§16)
        m_last_diagnostics = result.diagnostics.count();
        // the graph is addressed by the editor, so each file carries the uri it is edited by (§12.2)
        if (!result.sources.empty()) {
            // the root is the open buffer, which a pathless buffer is identified by as well (§12.2)
            result.sources.front().key = source_path.empty() ? document_uri : result.sources.front().key;
            // the root is addressed by the editor's own uri, because that is where it was opened (§12.2)
            result.sources.front().uri = document_uri;
            // each import is addressed by its open buffer when it has one, and its saved file otherwise (§12.2)
            for (std::size_t index = 1; index < result.sources.size(); index++)
                result.sources[index].uri = uri_for_path(m_documents, std::filesystem::path(result.sources[index].key));
        }
        // the indexed graph replaces the previous run, so a jump or a reference never lands in stale source (§12.2)
        m_sources[document_uri] = std::move(result.sources);
        // the surface replaces the previous run, so a removed component or import stops completing (§11.1, §12.2)
        m_component_surfaces[document_uri] = std::move(result.components);
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

    std::vector<Notification> LspServer::recompile_roots(const std::vector<std::string>& roots) {
        // the publications accumulate in root order, because one notification may invalidate several roots (§12.2)
        std::vector<Notification> publications;
        // each root is compiled once, because the caller already deduped the set (§12.2)
        for (const std::string& root : roots) {
            // a root that closed while the exchange was handled has nothing to compile (§12.2)
            if (m_documents.find(root) == nullptr)
                continue;
            // the compile publishes the root and its imports, and clears what it no longer owns (§12.2, §16)
            Exchange compiled = compile_root(root);
            // the publications join the one exchange the notification returns (§12.2)
            append_publications(publications, std::move(compiled.notifications));
        }
        // the publications are what the notification answers with, in the order the roots were named (§12.2)
        return publications;
    }

    void LspServer::reload_catalog() {
        // the builtins are the fallback, because a project without a manifest keeps the editor it has (§11.1, AD-17)
        compiler::ElementCatalog catalog = compiler::ElementCatalog::with_builtins();
        // the parsed files are kept until every record is registered, because a component may name one (§11.2, AD-17)
        std::vector<manifest::ManifestFile> parsed;
        // the manifest problems are collected here, because the catalog reads declarations rather than reports (§11.2)
        compiler::DiagnosticList diagnostics;
        // every configured manifest is read from disk, because the build reads the saved file too (§11.2, AD-17)
        for (const std::filesystem::path& path : m_manifests) {
            // a manifest the editor names is opened as a regular file, which is what the build requires as well (§11.2)
            const std::optional<std::vector<std::uint8_t>> bytes = core::read_regular_file(path);
            // a file that cannot be read contributes no declaration, which leaves the builtins in place (§11.2, AD-17)
            if (!bytes)
                continue;
            // the bytes become the source text, because the scanner takes characters rather than a file (§11.1)
            const std::string text(bytes->begin(), bytes->end());
            // the module is the file stem, which is the generated header directory a record names (§11.2, AD-17)
            const std::string module = path.stem().string();
            // the tokens live until the parse returns, because the parser borrows the stream (§11.2)
            const std::vector<compiler::Token> tokens = compiler::Lexer(text, diagnostics).tokenize();
            // the file joins the parsed set, because records register before components across manifests (§11.2, AD-17)
            parsed.push_back(manifest::ManifestParser(tokens, diagnostics).parse(module));
        }
        // every record joins first, because a component property in any manifest may name one (§11.2, AD-17, AD-13)
        for (const manifest::ManifestFile& file : parsed)
            manifest::register_records(file, catalog);
        // the components join next, which is what makes a manifest element name resolve and construct (§11.2, AD-17)
        for (const manifest::ManifestFile& file : parsed)
            manifest::register_components(file, catalog);
        // the catalog replaces the previous one, because an edit must not leave a stale type behind (§11.2, §12.2)
        m_catalog = std::move(catalog);
    }

    bool LspServer::is_manifest_path(const std::filesystem::path& path) const {
        // a path that cannot be decoded names no configured file, so nothing is refreshed (§12.2)
        if (path.empty())
            return false;
        // the path is normalized, because an editor uri and a configured path may spell one file differently (§12.2)
        const std::filesystem::path changed = path.lexically_normal();
        // each configured manifest is compared, because one match is enough to reload the catalog (§11.2, AD-17)
        for (const std::filesystem::path& manifest : m_manifests) {
            // a spelling match is the cheap answer, because most clients report the path they resolved (§12.2)
            if (manifest.lexically_normal() == changed)
                return true;
            // a case difference or a symlink is one file, which the filesystem resolves to one identity (§12.2)
            std::error_code error;
            // a deleted manifest has no identity, so the spelling comparison above is the only answer it gets (§12.2)
            if (std::filesystem::equivalent(manifest, changed, error))
                return true;
        }
        // no configured manifest names this file, so the change is not a catalog edit (§11.2)
        return false;
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
            // the client names its manifests in the initialization options, which the build shares (§11.2, AD-17)
            m_manifests = manifest_paths(params);
            // the catalog is rebuilt before the reply, because the first document may name a manifest type (§11.2, AD-17)
            reload_catalog();
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
            // the sync options are the document notifications this server reads, which the client registers for (§12.2)
            core::Json sync;
            // an open and a close notification are what the document store answers from (§12.2)
            sync.set("openClose", core::Json(true));
            // the full document text is what the parser takes, so every change replaces the whole buffer (§12.2)
            sync.set("change", core::Json(1.0));
            // a save notification refreshes the saved document and every importer it invalidates (§11.1, §12.2)
            core::Json save;
            // the buffer is already current, so a save needs no text the server would have to store (§12.2)
            save.set("includeText", core::Json(false));
            // the save options join the sync options, which is where the protocol carries them (§12.2)
            sync.set("save", std::move(save));
            // the sync options are what an editor looks for before it sends any of them (§12.2)
            capabilities.set("textDocumentSync", std::move(sync));
            // completion is offered inside an element body and after a property colon (§12.2)
            capabilities.set("completionProvider", core::Json(std::map<std::string, core::Json, std::less<>>{}));
            // hover is offered on an element or property name (§12.2)
            capabilities.set("hoverProvider", core::Json(true));
            // definition is offered on every written name the index resolved (§12.2)
            capabilities.set("definitionProvider", core::Json(true));
            // references are offered on every declaration and use the index resolved (§12.2)
            capabilities.set("referencesProvider", core::Json(true));
            // rename is offered with its prepare request, which validates the symbol before editing (§12.2)
            core::Json rename;
            // the prepare request is what an editor calls before it opens the rename box (§12.2)
            rename.set("prepareProvider", core::Json(true));
            // the rename capability is what an editor looks for before asking (§12.2)
            capabilities.set("renameProvider", std::move(rename));
            // the document outline is offered, because the compiler already knows every declaration (§12.2)
            capabilities.set("documentSymbolProvider", core::Json(true));
            // the token legend fixes the order the server encodes, which an editor maps onto colours (§12.2)
            core::Json legend;
            // the token types are the closed set the index classifies (§12.2)
            legend.set("tokenTypes", core::Json(std::vector<core::Json>{core::Json(std::string("keyword")), core::Json(std::string("type")), core::Json(std::string("property")), core::Json(std::string("event")), core::Json(std::string("function")), core::Json(std::string("variable"))}));
            // no token carries a modifier, because the index classifies by kind alone (§12.2)
            legend.set("tokenModifiers", core::Json(std::vector<core::Json>{}));
            // the semantic token capability carries the legend and the full form (§12.2)
            core::Json semantic;
            // the legend is what an editor decodes the token numbers with (§12.2)
            semantic.set("legend", std::move(legend));
            // a full reply is what this server sends, because a document is recompiled whole (§12.2)
            semantic.set("full", core::Json(true));
            // the capability is what an editor looks for before asking (§12.2)
            capabilities.set("semanticTokensProvider", std::move(semantic));
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

        // a watched file change refreshes the catalog and every root that read the changed path (§11.1, §11.2, §12.2)
        if (method->as_string() == "workspace/didChangeWatchedFiles") {
            // the change list names the watched files, where a manifest or an import edit arrives (§11.2, §12.2)
            const core::Json* changes = params == nullptr ? nullptr : params->member("changes");
            // a notification that names no change list changes nothing, so nothing is recompiled (§12.2)
            if (changes == nullptr || changes->kind() != core::Json::Kind::Array)
                return Exchange{.reply = {}, .notifications = {}};
            // the flag records whether a configured manifest changed, because that invalidates every root (§11.2)
            bool manifest_changed = false;
            // the roots accumulate across changes, because one notification may name several files (§11.1, §12.2)
            std::vector<std::string> roots;
            // each change names one file, compared against the catalog and the dependency graph (§11.1, §12.2)
            for (const core::Json& change : changes->as_array()) {
                // a change without a uri names no file, so it cannot invalidate a root (§12.2)
                const std::optional<std::string> uri = string_member(change, "uri");
                // a change the uri cannot decode names no path this server reads (§12.2)
                if (!uri)
                    continue;
                // the normalized path is what a manifest list and a dependency set are keyed by (§11.1, §11.2)
                const std::filesystem::path changed = document_path(*uri).lexically_normal();
                // a configured manifest invalidates the catalog, and with it every open root (§11.2, AD-17)
                if (is_manifest_path(changed)) {
                    manifest_changed = true;
                    continue;
                }
                // each open root that read the changed path is stale until it is recompiled (§11.1, §12.2)
                for (const auto& [root, dependencies] : m_root_dependencies) {
                    // a root whose dependency set lacks the path is not affected by it (§11.1, §12.2)
                    if (std::find(dependencies.begin(), dependencies.end(), changed.generic_string()) == dependencies.end())
                        continue;
                    // a root an earlier change already named is compiled once (§12.2)
                    if (std::find(roots.begin(), roots.end(), root) != roots.end())
                        continue;
                    // the importer joins the set that is recompiled below (§11.1, §12.2)
                    roots.push_back(root);
                }
            }
            // a change no open root read leaves every publication as it stands (§12.2)
            if (!manifest_changed && roots.empty())
                return Exchange{.reply = {}, .notifications = {}};
            // the catalog is rebuilt first, because every recompile below must resolve the new declarations (§11.2, AD-17)
            if (manifest_changed)
                reload_catalog();
            // every open root is recompiled when the catalog moved, because any may name a declared type (§11.2, §16)
            if (manifest_changed) {
                for (const std::string& root : m_documents.uris()) {
                    // a root already invalidated by an import is compiled once, in that position (§12.2)
                    if (std::find(roots.begin(), roots.end(), root) != roots.end())
                        continue;
                    // the root joins the set the reload invalidates, because the catalog is shared (§11.2, §16)
                    roots.push_back(root);
                }
            }
            // a notification produces no reply, and the publications carry every refreshed root (§12.2)
            return Exchange{.reply = {}, .notifications = recompile_roots(roots)};
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

        // a saved document is recompiled with its importers, because a save may land an import change (§11.1, §12.2)
        if (method->as_string() == "textDocument/didSave") {
            // a save names its document through the parameters, which every document notification carries (§12.2)
            if (params == nullptr || !params->is_object())
                return Exchange{.reply = {}, .notifications = {}};
            // the document is named through a text document identifier (§12.2)
            const core::Json* text_document = params->member("textDocument");
            // a save without a document names nothing, so nothing is recompiled (§12.2)
            if (text_document == nullptr || !text_document->is_object())
                return Exchange{.reply = {}, .notifications = {}};
            // the uri names the file the editor wrote to disk (§12.2)
            const std::optional<std::string> uri = string_member(*text_document, "uri");
            // a save without a uri cannot be routed, so nothing is recompiled (§12.2)
            if (!uri)
                return Exchange{.reply = {}, .notifications = {}};
            // the saved path is what its importers key on, because an import is a reverse edge (§11.1, §12.2)
            const std::filesystem::path saved_path = document_path(*uri).lexically_normal();
            // the saved document is compiled first when it is open, because it is what the editor shows (§12.2)
            std::vector<std::string> roots;
            // an open saved document is recompiled, because the save may have changed an import on disk (§11.1, §12.2)
            if (m_documents.find(*uri) != nullptr)
                roots.push_back(*uri);
            // every open root that read the saved path is recompiled, because its diagnostics depend on it (§11.1, §12.2)
            for (const auto& [root, dependencies] : m_root_dependencies) {
                // the saved document is already first, so it is not compiled twice (§12.2)
                if (root == *uri)
                    continue;
                // a root whose dependency set lacks the saved path is not affected by the save (§11.1, §12.2)
                if (std::find(dependencies.begin(), dependencies.end(), saved_path.generic_string()) == dependencies.end())
                    continue;
                // the importer joins the set that is recompiled below (§11.1, §12.2)
                roots.push_back(root);
            }
            // a save nothing reads changes no publication, which keeps an unrelated save quiet (§12.2)
            if (roots.empty())
                return Exchange{.reply = {}, .notifications = {}};
            // a notification produces no reply, and the publications carry the saved document and its importers (§12.2)
            return Exchange{.reply = {}, .notifications = recompile_roots(roots)};
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
                        // the index is dropped with the buffer, because a closed file must not answer a request (§12.2)
                        m_sources.erase(closed_uri);
                        // the surface is dropped with the buffer, because a closed file must not complete (§12.2)
                        m_component_surfaces.erase(closed_uri);
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

        // a definition request answers with the declaration the cursor names, which the compile already indexed (§12.2)
        if (method->as_string() == "textDocument/definition") {
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
            // a document that is not open has no index, because every answer comes from an editor buffer (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // the position is read, because a definition is asked at the cursor (§12.2)
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
            // the index of the open document is what every declaration was recorded in (§12.2)
            const auto found = m_sources.find(*uri);
            // a compile that produced no typed tree has no index, so the honest answer is null (§12.2)
            if (found == m_sources.end() || found->second.empty())
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the root entry is the document's own index, because the imports follow it (§12.2)
            const compiler::SourceIndex& index = found->second.front().index;
            // the protocol counts from zero, while the compiler counts from one (§12.2)
            const compiler::SourceLocation cursor{.line = static_cast<std::uint32_t>(std::max(0.0, line_value->as_number())) + 1, .column = static_cast<std::uint32_t>(std::max(0.0, character_value->as_number())) + 1};
            // the use under the cursor is the one a jump follows (§12.2)
            const compiler::SourceUse* named = nullptr;
            // a cursor just past a name still names it, which is what a double click at the end does (§12.2)
            const compiler::SourceUse* edge = nullptr;
            // the uses are scanned in written order, because the first one covering the cursor is under it (§12.2)
            for (const compiler::SourceUse& use : index.uses) {
                // a span that covers the cursor is the exact answer (§12.2)
                if (span_contains(use.span, cursor)) {
                    named = &use;
                    break;
                }
                // a span ending at the cursor is the fallback when the cursor sits just past the name (§12.2)
                if (use.span.end == cursor)
                    edge = &use;
            }
            // the exact use wins, and the edge fallback answers only when nothing contains the cursor (§12.2)
            if (named == nullptr)
                named = edge;
            // a cursor on nothing, or a name the framework declares outside source, has no location to open (§12.2)
            if (named == nullptr || !named->target.has_value())
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the declaration the name resolves to is what an editor opens (§12.2)
            return Exchange{.reply = envelope(reply_id, definition_location(*uri, *named->target)).to_string(), .notifications = {}};
        }

        // a references request answers every place one declaration is written, across the import graph (§12.2)
        if (method->as_string() == "textDocument/references") {
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
            // a document that is not open has no index, because every answer comes from an editor buffer (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // the position is read, because references are asked at the cursor (§12.2)
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
            // the root entry is the document's own index, which its declarations and uses are read from (§12.2)
            const IndexedSource* root = root_source_of(m_sources, *uri);
            // a compile that produced no typed tree has nothing to reference, so the honest answer is null (§12.2)
            if (root == nullptr)
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the protocol counts from zero, while the compiler counts from one (§12.2)
            const compiler::SourceLocation cursor{.line = static_cast<std::uint32_t>(std::max(0.0, line_value->as_number())) + 1, .column = static_cast<std::uint32_t>(std::max(0.0, character_value->as_number())) + 1};
            // the declaration the cursor names is what every use is matched against (§12.2)
            const std::optional<DeclarationId> target = declaration_id_at(root->index, root->key, cursor);
            // a cursor on nothing, or on a framework name with no source declaration, has nothing to reference (§12.2)
            if (!target.has_value())
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the client says whether the declaration itself belongs in the reply (§12.2)
            bool include_declaration = true;
            // the reference context is where the protocol carries that choice (§12.2)
            if (const core::Json* context = params->member("context"); context != nullptr && context->is_object()) {
                // the flag is read when the client wrote it, because it is a boolean (§12.2)
                if (const core::Json* include = context->member("includeDeclaration"); include != nullptr && include->kind() == core::Json::Kind::Boolean)
                    include_declaration = include->as_bool();
            }
            // every indexed file of every open root is searched, because references cross the graph (§12.2)
            const std::vector<const IndexedSource*> sources = indexed_sources(m_sources);
            // the sites are deduped, because a file imported by several roots is indexed once per graph (§12.2)
            std::vector<ReferenceSite> sites = unique_sites(reference_sites(sources, *target, include_declaration));
            // the reply order is by file and position, which keeps the answer stable (§12.2)
            sort_sites(sites);
            // the locations accumulate in reply order (§12.2)
            std::vector<core::Json> locations;
            // each site becomes one location, because that is what a references reply is a list of (§12.2)
            for (const ReferenceSite& site : sites) {
                // a location is an object with a uri and a range (§12.2)
                core::Json location;
                // the uri is the open buffer or the saved file the site sits in (§12.2)
                location.set("uri", core::Json(site.uri));
                // the range is the written name an editor highlights (§12.2)
                location.set("range", range_of(site.span));
                // the location joins the reply (§12.2)
                locations.push_back(std::move(location));
            }
            // the reply is the location list, which is what the protocol describes (§12.2)
            return Exchange{.reply = envelope(reply_id, core::Json(std::move(locations))).to_string(), .notifications = {}};
        }

        // a prepare rename request validates the cursor before the editor opens its rename box (§12.2)
        if (method->as_string() == "textDocument/prepareRename") {
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
            // a document that is not open has no index, because every answer comes from an editor buffer (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // the position is read, because prepare rename speaks about the cursor (§12.2)
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
            // the root entry is the document's own index, which its declarations and uses are read from (§12.2)
            const IndexedSource* root = root_source_of(m_sources, *uri);
            // a document with no typed tree has no rename target, which the protocol spells as null (§12.2)
            if (root == nullptr)
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the protocol counts from zero, while the compiler counts from one (§12.2)
            const compiler::SourceLocation cursor{.line = static_cast<std::uint32_t>(std::max(0.0, line_value->as_number())) + 1, .column = static_cast<std::uint32_t>(std::max(0.0, character_value->as_number())) + 1};
            // the declaration the cursor names is what the rename would edit (§12.2)
            const std::optional<DeclarationId> target = declaration_id_at(root->index, root->key, cursor);
            // a cursor that names no declaration is null, because there is nothing to rename (§12.2)
            if (!target.has_value())
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // every indexed file is searched, because the declaration may live in an import (§12.2)
            const std::vector<const IndexedSource*> sources = indexed_sources(m_sources);
            // the declaration is located before it is validated or described (§12.2)
            const std::optional<LocatedSymbol> located = locate_symbol(sources, *target);
            // a declaration the outline does not carry cannot be renamed, so null is the honest answer (§12.2)
            if (!located.has_value())
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // a builtin, an exported component or a duplicate declaration answers null before the box opens (§12.2)
            if (rename_target_refusal(*located).has_value())
                return Exchange{.reply = envelope(reply_id, core::Json()).to_string(), .notifications = {}};
            // the use under the cursor is what the editor highlights, and a declaration falls back to its name (§12.2)
            const compiler::SourceUse* use = use_at(root->index, cursor);
            // the range covers the token the rename box would replace (§12.2)
            const compiler::SourceSpan span = use != nullptr ? use->span : located->symbol->name_span;
            // the reply carries the range and the name the rename box prefills (§12.2)
            core::Json result;
            // the range is the token under the cursor (§12.2)
            result.set("range", range_of(span));
            // the placeholder is the declaration name, which is what the editor shows (§12.2)
            result.set("placeholder", core::Json(located->symbol->name));
            // the reply is what an editor opens its rename box with (§12.2)
            return Exchange{.reply = envelope(reply_id, std::move(result)).to_string(), .notifications = {}};
        }

        // a rename request validates the symbol and returns the edit every use needs (§12.2)
        if (method->as_string() == "textDocument/rename") {
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
            // a document that is not open has no index, because every answer comes from an editor buffer (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // the position is read, because the rename starts from the cursor (§12.2)
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
            // the new name is a required string, because it is what every edit writes (§12.2)
            const std::optional<std::string> new_name = string_member(*params, "newName");
            // a request with no name cannot produce an edit, so it is refused (§12.2)
            if (!new_name)
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the request carries no new name").to_string(), .notifications = {}};
            // a name the language cannot spell as an identifier is refused before anything is edited (§11.1)
            if (!valid_identifier(*new_name))
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the new name is not a valid identifier").to_string(), .notifications = {}};
            // the root entry is the document's own index, which its declarations and uses are read from (§12.2)
            const IndexedSource* root = root_source_of(m_sources, *uri);
            // a document with no typed tree has nothing to rename, which is stated rather than answered empty (§12.2)
            if (root == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document carries no index").to_string(), .notifications = {}};
            // the protocol counts from zero, while the compiler counts from one (§12.2)
            const compiler::SourceLocation cursor{.line = static_cast<std::uint32_t>(std::max(0.0, line_value->as_number())) + 1, .column = static_cast<std::uint32_t>(std::max(0.0, character_value->as_number())) + 1};
            // the declaration the cursor names is what the rename edits and validates (§12.2)
            const std::optional<DeclarationId> target = declaration_id_at(root->index, root->key, cursor);
            // a cursor that names no declaration is refused with its reason, because an empty edit would hide it (§12.2)
            if (!target.has_value())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the cursor names no renameable declaration").to_string(), .notifications = {}};
            // every indexed file is searched, because the declaration may live in an import (§12.2)
            const std::vector<const IndexedSource*> sources = indexed_sources(m_sources);
            // the declaration is located before it is validated or edited (§12.2)
            const std::optional<LocatedSymbol> located = locate_symbol(sources, *target);
            // a declaration the outline does not carry has no surface to edit (§12.2)
            if (!located.has_value())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, "the cursor names no renameable declaration").to_string(), .notifications = {}};
            // validation reads the declaration's scope before any edit is produced (§12.2)
            if (const std::optional<std::string> refusal = rename_refusal(m_catalog, *located, *new_name); refusal.has_value())
                return Exchange{.reply = failure(reply_id, INVALID_PARAMS, *refusal).to_string(), .notifications = {}};
            // the declaration and every use are the sites the edit rewrites (§12.2)
            std::vector<ReferenceSite> sites = unique_sites(reference_sites(sources, *target, true));
            // the edits are emitted in reply order, which keeps the workspace edit stable (§12.2)
            sort_sites(sites);
            // the changes group the edits by uri, because the protocol describes the edit that way (§12.2)
            std::map<std::string, std::vector<core::Json>, std::less<>> edits;
            // each site becomes one text edit in the file it is written in (§12.2)
            for (const ReferenceSite& site : sites) {
                // a handler keeps its prefix, so it is written as on_<name> (§11.1)
                const std::string text = site.handler ? "on_" + *new_name : *new_name;
                // the edit joins the file's list, which is what an editor applies as one change (§12.2)
                edits[site.uri].push_back(edit_of(site.span, text));
            }
            // each document's list becomes one array, because the protocol groups an edit by document (§12.2)
            std::map<std::string, core::Json, std::less<>> changes;
            // each document's edits join the changes, which is what an editor applies (§12.2)
            for (auto& [file, list] : edits)
                changes.emplace(file, core::Json(std::move(list)));
            // the result carries the changes, which is the whole of a workspace edit (§12.2)
            core::Json result;
            // the changes join the result, which the editor applies (§12.2)
            result.set("changes", core::Json(std::move(changes)));
            // the rename answers with the edit, which the editor applies (§12.2)
            return Exchange{.reply = envelope(reply_id, std::move(result)).to_string(), .notifications = {}};
        }

        // an outline request answers from the declarations the compile already indexed (§12.2)
        if (method->as_string() == "textDocument/documentSymbol") {
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
            // a document that is not open has no index, because every answer comes from an editor buffer (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // the rendered symbols accumulate in declaration order (§12.2)
            std::vector<core::Json> symbols;
            // a document whose compile produced no tree has an empty outline, which is the honest answer (§12.2)
            if (const auto found = m_sources.find(*uri); found != m_sources.end() && !found->second.empty()) {
                // each top level declaration is one symbol, which its members nest below (§12.2)
                for (const compiler::SourceSymbol& symbol : found->second.front().index.symbols)
                    symbols.push_back(document_symbol(symbol));
            }
            // the outline is a list, which is what the protocol describes for document symbols (§12.2)
            return Exchange{.reply = envelope(reply_id, core::Json(std::move(symbols))).to_string(), .notifications = {}};
        }

        // a semantic token request answers from the classified regions the compile already indexed (§12.2)
        if (method->as_string() == "textDocument/semanticTokens/full") {
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
            // a document that is not open has no index, because every answer comes from an editor buffer (§12.2)
            if (m_documents.find(*uri) == nullptr)
                return Exchange{.reply = failure(reply_id, INVALID_REQUEST, "the document is not open: " + *uri).to_string(), .notifications = {}};
            // a document whose compile produced no tree has no tokens, which is the honest answer (§12.2)
            if (const auto found = m_sources.find(*uri); found != m_sources.end() && !found->second.empty())
                return Exchange{.reply = envelope(reply_id, semantic_tokens(found->second.front().index)).to_string(), .notifications = {}};
            // an empty reply is what the protocol spells for no tokens (§12.2)
            return Exchange{.reply = envelope(reply_id, core::Json(std::map<std::string, core::Json, std::less<>>{{"data", core::Json(std::vector<core::Json>{})}})).to_string(), .notifications = {}};
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
            // the catalog is the client one, because a manifest type completes like a builtin (§11.2, AD-17)
            const compiler::ElementCatalog& catalog = m_catalog;
            // the root's own components and imports are what an instance completes and hovers from (§11.1, §12.2)
            const auto surface = m_component_surfaces.find(*uri);
            // an open root always carries a surface, and an empty one answers a request that arrives before it (§12.2)
            const ComponentSurface& components = surface == m_component_surfaces.end() ? EMPTY_COMPONENT_SURFACE : surface->second;
            // a completion offers whatever may be written at this position (§12.2)
            if (method->as_string() == "textDocument/completion") {
                // the items are rendered as the protocol's item objects (§12.2)
                std::vector<core::Json> rendered;
                // each item carries its label, kind and detail, which is what an editor shows (§12.2)
                for (const CompletionItem& item : completions_at(document, catalog, components, row, column)) {
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
            const std::optional<std::string> described = hover_at(document, catalog, components, row, column);
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
