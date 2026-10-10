#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "compiler/value_type.h"
#include "language-server/lsp_document.h"

namespace albor::language_server
{
    // one thing an editor may offer, which is what a completion response is a list of (§12.2)
    struct CompletionItem
    {
        // the text inserted, which is the name a person types (§12.2)
        std::string label;

        // the short type shown beside it, which is what tells two same named things apart (§12.2)
        std::string detail;

        // the protocol kind, which is what an editor uses to pick an icon (§12.2)
        int kind = 1;

        // the longer explanation, which is what a person reads after accepting (§12.2)
        std::string documentation;
    };

    // what the completion request offers, which depends on where the cursor is (§12.2)
    enum class CompletionContext
    {
        // nowhere the server can say anything useful, so nothing is offered (§12.2)
        None,

        // inside an element body, where an element name may start (§12.2)
        Element,

        // after a property name and its colon, where the element's properties apply (§12.2)
        Property,

        // after the on_ prefix, where the element's signals apply (§12.2)
        Signal,

        // inside a component body, where the component's own declarations apply (§12.2)
        Declaration
    };

    // report what the cursor sits in, which decides the whole completion (§12.2)
    [[nodiscard]] CompletionContext completion_context_at(const Document& document, std::size_t line, std::size_t character);

    // the element the cursor sits inside, which is what a property completion answers from (§12.2)
    [[nodiscard]] std::string enclosing_element_at(const Document& document, std::size_t line, std::size_t character);

    // the things that may be written at this position, which is the completion response (§12.2)
    [[nodiscard]] std::vector<CompletionItem> completions_at(const Document& document, const compiler::ElementCatalog& catalog, std::size_t line, std::size_t character);

    // what the token under the cursor means, which is absent when the cursor is on nothing (§12.2)
    [[nodiscard]] std::optional<std::string> hover_at(const Document& document, const compiler::ElementCatalog& catalog, std::size_t line, std::size_t character);
} // namespace albor::language_server
