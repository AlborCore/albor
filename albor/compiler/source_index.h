#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "albor/compiler/diagnostic.h"
#include "albor/compiler/parser.h"
#include "albor/compiler/type_checker.h"

namespace albor::compiler
{
    // what a declaration in a source file is, which an editor maps onto a document symbol kind (§12.2)
    enum class SourceSymbolKind
    {
        // a component declaration, which is the unit the build compiles (§11.1)
        Component,

        // a property declaration, which a binding or a handler resolves (§11.1)
        Property,

        // a signal declaration, which a handler attaches to or invokes (§11.1)
        Signal,

        // a command declaration, which an actionable child binds by name (§18)
        Command,

        // one element of a component tree, which a document outline shows (§11.1)
        Element,

        // a function declaration, which a binding may call (§11.1)
        Function,

        // a module-scoped value, which several components read (§11.1)
        Global,

        // a struct declaration, which a property or a signal may carry (§11.1)
        Struct,

        // an enum declaration, which a property may hold one member of (§11.1)
        Enum
    };

    // one declaration an editor shows in a document outline and jumps to (§12.2)
    struct SourceSymbol
    {
        // the declared name as written, which an editor shows (§12.2)
        std::string name;

        // what the declaration is, which decides the document symbol kind (§12.2)
        SourceSymbolKind kind = SourceSymbolKind::Component;

        // the short description an editor shows beside the name, such as the declared type (§12.2)
        std::string detail;

        // the region the whole declaration occupies, which is the outline range (§12.2)
        SourceSpan span;

        // the region the name occupies, which is where a jump lands and a token starts (§12.2)
        SourceSpan name_span;

        // the declarations the symbol contains, such as a component's members (§12.2)
        std::vector<SourceSymbol> children;
    };

    // what a written name refers to, which decides how a definition and a token are classified (§12.2)
    enum class SourceUseKind
    {
        // an element type name, which resolves to a component declaration (§11.1)
        Element,

        // a property name, which resolves to a property declaration (§11.1)
        Property,

        // a signal name, which resolves to a signal declaration (§11.1)
        Signal,

        // a handler attachment, which resolves to the element's signal (§11.1)
        Handler,

        // a binding reference, which resolves to a property, command, global, parameter or repeat binding (§11.1)
        Binding
    };

    // where a written name resolves, which is what a jump to the definition points at (§12.2)
    struct SourceTarget
    {
        // the file the declaration lives in, which is empty when it is the indexed file itself (§12.2)
        std::filesystem::path file;

        // the region the declaration name occupies, which is where the cursor lands (§12.2)
        SourceSpan span;
    };

    // one written name and the declaration it resolves to, which definition and token classification both read (§12.2)
    struct SourceUse
    {
        // the name as written, which a test and a hover may read (§12.2)
        std::string name;

        // what the name refers to, which decides the token type (§12.2)
        SourceUseKind kind = SourceUseKind::Binding;

        // the region the name occupies, which is where the cursor sits (§12.2)
        SourceSpan span;

        // the declaration the name resolves to, which is absent when the framework declares nothing in source (§12.2)
        std::optional<SourceTarget> target;
    };

    // what a semantic token classifies as, which is the legend the server advertises (§12.2)
    enum class SourceTokenKind
    {
        // a language keyword such as component or property (§11.1)
        Keyword,

        // an element, component, struct or enum name, which names a type (§11.1)
        Type,

        // a property declaration or reference (§11.1)
        Property,

        // a signal declaration or invocation (§11.1)
        Event,

        // a handler or a function (§11.1)
        Function,

        // a binding, a repeat item or a module-scoped value (§11.1)
        Variable
    };

    // one semantic token, which is a classified region of the source (§12.2)
    struct SourceToken
    {
        // what the region is, which is the legend index an editor receives (§12.2)
        SourceTokenKind kind = SourceTokenKind::Keyword;

        // the region the token covers, which is one name on one line (§12.2)
        SourceSpan span;
    };

    // everything an editor needs to answer definitions, document symbols and semantic tokens from one compile (§12.2)
    struct SourceIndex
    {
        // the declarations in source order, which the document outline shows (§12.2)
        std::vector<SourceSymbol> symbols;

        // every written name and its resolved declaration, which definition and tokens read (§12.2)
        std::vector<SourceUse> uses;

        // every classified region in source order, which the semantic token reply carries (§12.2)
        std::vector<SourceToken> tokens;
    };

    // build an editor index from the same parse and checked file the build uses, which is what §16 requires (§12.2, §16)
    [[nodiscard]] SourceIndex index_source(std::string_view text, const SourceFile& file, const CheckedFile& checked);
} // namespace albor::compiler
