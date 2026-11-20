#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "albor/compiler/source_index.h"
#include "albor/compiler/value_type.h"
#include "albor/language-server/lsp_document.h"
#include "albor/language-server/lsp_support.h"

namespace albor::language_server
{
    // one notification the server sends unprompted, which is how diagnostics reach an editor (§12.2)
    struct Notification
    {
        // the method name, which is what an editor dispatches on (§12.2)
        std::string method;

        // the notification body, which is what the method's payload is (§12.2)
        std::string payload;
    };

    // what one request produced, which is a reply, a notification, or both (§12.2)
    struct Exchange
    {
        // the reply, which is absent for a notification (§12.2)
        std::optional<std::string> reply;

        // the notifications sent while handling it, which is how diagnostics are published (§12.2, §16)
        std::vector<Notification> notifications;
    };

    // one file of a root's compiled graph, which references and rename read (§12.2)
    struct IndexedSource
    {
        // the identity of the file, which is its path or the uri of an unsaved buffer (§12.2)
        std::string key;

        // the uri an editor addresses the file by, which is its open buffer or its saved file (§12.2)
        std::string uri;

        // the component the file exports, which its stem names (§11.1, §14.2)
        std::string exported_component;

        // the declarations and uses the compile resolved (§12.2)
        compiler::SourceIndex index;
    };

    // the language server, which answers an editor using the same compiler the build runs (§12.2, §16)
    class LspServer
    {
    public:
        // handle one json-rpc line, which is the whole of the protocol (§12.2)
        [[nodiscard]] Exchange handle_line(std::string_view line);

        // hand out how many diagnostics the last compile produced, which an agreement test asserts on (§16)
        [[nodiscard]] std::size_t last_diagnostic_count() const;

        // hand out how many documents are open, which a test asserts on (§12.2)
        [[nodiscard]] std::size_t document_count() const;

        // report whether an exit notification arrived, which is what ends the stdio loop (§12.2)
        [[nodiscard]] bool exit_requested() const;

    private:
        // compile an open document as a root, publishing imports and clearing stale publications (§12.2, §16)
        [[nodiscard]] Exchange compile_root(std::string_view uri);

        // forget a closed root and clear the publications no other root still owns (§12.2)
        void close_root(std::string_view uri, std::vector<Notification>& notifications);

        // recompile the open roots a change invalidates, which a save and a watched file both use (§11.1, §12.2)
        [[nodiscard]] std::vector<Notification> recompile_roots(const std::vector<std::string>& roots);

        // rebuild the catalog from the builtins and the configured manifests (§11.2, AD-17)
        void reload_catalog();

        // report whether a changed path is a configured manifest, which a refresh keys on (§11.2, §12.2)
        [[nodiscard]] bool is_manifest_path(const std::filesystem::path& path) const;

        // the manifest paths the client configured, which is the catalog source the build uses too (§11.2, AD-17)
        std::vector<std::filesystem::path> m_manifests;

        // the catalog every compile, completion and hover resolves against, which carries the manifests (§11.2, AD-17)
        compiler::ElementCatalog m_catalog = compiler::ElementCatalog::with_builtins();

        // the components each open root resolves, which completion and hover answer from (§11.1, §12.2)
        std::map<std::string, ComponentSurface, std::less<>> m_component_surfaces;

        // the documents the editor has open, which every request is answered from (§12.2)
        DocumentStore m_documents;

        // the uris each open root published, which is what a later compile or close clears (§12.2, §16)
        std::map<std::string, std::vector<std::string>, std::less<>> m_root_publications;

        // the files each open root read, which is what an imported change recompiles (§11.1, §12.2)
        std::map<std::string, std::vector<std::string>, std::less<>> m_root_dependencies;

        // the files each open root checked, root first, which references and rename read (§11.1, §12.2)
        std::map<std::string, std::vector<IndexedSource>, std::less<>> m_sources;

        // how many diagnostics the last compile produced, which an agreement test reads (§16)
        std::size_t m_last_diagnostics = 0;

        // whether an exit notification arrived, which makes the stdio loop stop (§12.2)
        bool m_exit_requested = false;
    };
} // namespace albor::language_server
