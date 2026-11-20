#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "albor/compiler/lexer.h"
#include "albor/compiler/parser.h"
#include "albor/compiler/source_index.h"
#include "albor/compiler/type_checker.h"
#include "albor/compiler/value_type.h"

using namespace albor::compiler;

namespace
{
    // scan, parse and check a source, then index it the way the language server does (§12.2, §16)
    SourceIndex index_of(std::string_view source) {
        // the scanner runs first, because the parser takes units (§11.1)
        DiagnosticList diagnostics;
        // the units are the parser's input, which keeps the spans the index reports (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parse tree is kept alive for the duration of the index, because checked expressions alias it (§11.1)
        auto parsed = std::make_unique<SourceFile>(Parser(tokens, diagnostics).parse());
        // the builtin catalog is what a ui file may use without a manifest (§11.1, §8)
        ElementCatalog catalog = ElementCatalog::with_builtins();
        // the structs the file declares join the catalog, which is what makes a property of one resolve (§11.1)
        register_ui_structs(*parsed, catalog, diagnostics);
        // the enums the file declares join the catalog for the same reason (§11.1)
        register_ui_enums(*parsed, catalog, diagnostics);
        // the checker resolves the file against the catalog, which is where the native types come from (§11.1)
        const CheckedFile checked = TypeChecker(catalog, diagnostics).check(*parsed);
        // the errors are collected, because a source with one is a suite defect rather than an index one (§11.1)
        const std::vector<Diagnostic> errors = diagnostics.of_severity(Diagnostic::Severity::Error);
        // a clean test source produces no error, so one here fails with the compiler's own message (§11.1)
        EXPECT_TRUE(errors.empty()) << (errors.empty() ? std::string() : errors.front().message);
        // the index is built from the same parse and checked tree the build uses (§12.2, §16)
        return index_source(source, *parsed, checked);
    }

    // find the use a suite asks about, which keeps the assertion about its target readable (§12.2)
    const SourceUse* use_named(const SourceIndex& index, std::string_view name) {
        // the uses are scanned in written order, because the name is what the suite asserts on (§12.2)
        for (const SourceUse& use : index.uses) {
            // the first use of a name is the one a suite writes its source for (§12.2)
            if (use.name == name)
                return &use;
        }
        // a name the index never wrote is a suite defect rather than an index one (§12.2)
        return nullptr;
    }

    // report whether a token kind appears, which is what an editor's colouring depends on (§12.2)
    bool has_token(const SourceIndex& index, SourceTokenKind kind) {
        // each token is compared, because one of the kind is enough to prove it is classified (§12.2)
        for (const SourceToken& token : index.tokens) {
            // the kind decides, which is the whole of what the suite asserts (§12.2)
            if (token.kind == kind)
                return true;
        }
        // a kind with no token is the defect the suite reports (§12.2)
        return false;
    }

    // find the token covering one exact span, which is how a suite checks one written name (§12.2)
    const SourceToken* token_at(const SourceIndex& index, const SourceSpan& span) {
        // each token is compared, because one span names exactly one token in a disjoint stream (§12.2)
        for (const SourceToken& token : index.tokens) {
            // the span decides, because two names never share one region (§12.2)
            if (token.span == span)
                return &token;
        }
        // a span with no token is the defect the suite reports (§12.2)
        return nullptr;
    }
} // namespace

TEST(SourceIndex, DeclarationsBecomeADocumentOutline) {
    // arrange a component that declares every member kind and one element tree (§11.1)
    const SourceIndex index = index_of(R"(
component Panel {
    in property<string> title: "hi";
    signal changed(string value);
    command save "Save";
    Column {
        Label { text: title; }
    }
}
)");
    // act the outline is the file's top level, which is the one component (§12.2)
    ASSERT_EQ(index.symbols.size(), 1U);
    const SourceSymbol& component = index.symbols.at(0);
    // assert the component carries its name and kind, which an editor shows at the top (§12.2)
    EXPECT_EQ(component.name, "Panel");
    EXPECT_EQ(component.kind, SourceSymbolKind::Component);
    // assert the name span covers the name rather than the keyword, because a jump lands on it (§12.2)
    EXPECT_EQ(component.name_span.start.line, 2U);
    EXPECT_EQ(component.name_span.end.column - component.name_span.start.column, component.name.size());
    // assert the declared members and the root element are nested below it (§12.2)
    ASSERT_EQ(component.children.size(), 4U);
    // the property is the first member, because the outline follows declaration order (§12.2)
    EXPECT_EQ(component.children.at(0).name, "title");
    EXPECT_EQ(component.children.at(0).kind, SourceSymbolKind::Property);
    // the signal follows, because a handler resolves its name (§12.2)
    EXPECT_EQ(component.children.at(1).name, "changed");
    EXPECT_EQ(component.children.at(1).kind, SourceSymbolKind::Signal);
    // the command follows, because an actionable child binds it (§12.2, §18)
    EXPECT_EQ(component.children.at(2).name, "save");
    EXPECT_EQ(component.children.at(2).kind, SourceSymbolKind::Command);
    // the element is the last child, because the declarations are written above it (§12.2)
    EXPECT_EQ(component.children.at(3).name, "Column");
    EXPECT_EQ(component.children.at(3).kind, SourceSymbolKind::Element);
    // assert the element nests the label, because an outline follows the tree shape (§12.2)
    ASSERT_EQ(component.children.at(3).children.size(), 1U);
    EXPECT_EQ(component.children.at(3).children.at(0).name, "Label");
}

TEST(SourceIndex, AnElementResolvesToItsComponentDeclaration) {
    // arrange a component used as an element beside its declaration (§11.1)
    const SourceIndex index = index_of("component Card { Column {} } component Panel { Column { Card {} } }");
    // act the use of the element name is what a definition request reads (§12.2)
    const SourceUse* use = use_named(index, "Card");
    // assert the use is an element and was indexed at all (§12.2)
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->kind, SourceUseKind::Element);
    // assert the target is the declaration in this file, which is the component symbol's name (§12.2)
    ASSERT_TRUE(use->target.has_value());
    EXPECT_TRUE(use->target->file.empty());
    ASSERT_EQ(index.symbols.size(), 2U);
    EXPECT_EQ(use->target->span, index.symbols.at(0).name_span);
}

TEST(SourceIndex, AHandlerAndASignalInvocationResolveToTheirDeclarations) {
    // arrange a handler attached to an element signal and an invocation of the component's own signal (§11.1)
    const SourceIndex index = index_of("component Card { signal pressed(); Column {} } component Panel { signal activated(); Column { Card { on_pressed: { activated(); } } } }");
    // act the handler attachment and the signal invocation are the two names a jump can land on (§12.2)
    const SourceUse* handler = use_named(index, "on_pressed");
    const SourceUse* invocation = use_named(index, "activated");
    // assert the handler is an attachment and resolved to the card's signal (§12.2)
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->kind, SourceUseKind::Handler);
    ASSERT_TRUE(handler->target.has_value());
    EXPECT_EQ(handler->target->span, index.symbols.at(0).children.at(0).name_span);
    // assert the invocation is a signal and resolved to the panel's signal (§12.2)
    ASSERT_NE(invocation, nullptr);
    EXPECT_EQ(invocation->kind, SourceUseKind::Signal);
    ASSERT_TRUE(invocation->target.has_value());
    EXPECT_EQ(invocation->target->span, index.symbols.at(1).children.at(0).name_span);
}

TEST(SourceIndex, ABindingResolvesToAPropertyAndARepeatBinding) {
    // arrange a property read by an assignment and a repeat body that reads its item (§11.1)
    const SourceIndex index = index_of("component Panel { in property<string> title: \"hi\"; in property<vector<string>> items; Column { Label { text: title; } Repeat(item in items) { Label { text: item; } } } }");
    // act the property reference and the repeat item are both bindings (§12.2)
    const SourceUse* property = use_named(index, "title");
    const SourceUse* item = use_named(index, "item");
    // assert the property reference resolved to the declared property (§12.2)
    ASSERT_NE(property, nullptr);
    EXPECT_EQ(property->kind, SourceUseKind::Binding);
    ASSERT_TRUE(property->target.has_value());
    EXPECT_EQ(property->target->span, index.symbols.at(0).children.at(0).name_span);
    // assert the repeat item resolved to the binding the repeat declared, which is not an outline symbol (§12.2)
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->kind, SourceUseKind::Binding);
    ASSERT_TRUE(item->target.has_value());
    // the target covers the item rather than the keyword, because a jump lands on the binding name (§12.2)
    EXPECT_EQ(item->target->span.end.column - item->target->span.start.column, item->name.size());
    // assert a builtin property names no source declaration, because the framework declares it outside the file (§12.2)
    const SourceUse* builtin = use_named(index, "text");
    ASSERT_NE(builtin, nullptr);
    EXPECT_EQ(builtin->kind, SourceUseKind::Property);
    EXPECT_FALSE(builtin->target.has_value());
}

TEST(SourceIndex, ASignalParameterResolvesToItsDeclaration) {
    // arrange a card signal with a parameter that its handler body reads (§11.1)
    const std::string source = "component Card { signal moved(int amount); Column {} } component Panel { signal changed(int limit); Column { Card { on_moved: { changed(amount); } } } }";
    const SourceIndex index = index_of(source);
    // act the parameter read is a binding, whose declaration is the signal parameter (§12.2)
    const SourceUse* parameter = use_named(index, "amount");
    // assert the read resolved to the parameter rather than the signal name (§12.2)
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(parameter->kind, SourceUseKind::Binding);
    ASSERT_TRUE(parameter->target.has_value());
    // the jump lands on the written parameter, which is the first amount in the file (§12.2)
    EXPECT_EQ(parameter->target->span.start.column, source.find("amount") + 1);
    EXPECT_EQ(parameter->target->span.end.column - parameter->target->span.start.column, parameter->name.size());
    // assert the parameter is painted as a variable, like every other binding (§12.2)
    const SourceToken* token = token_at(index, parameter->target->span);
    ASSERT_NE(token, nullptr);
    EXPECT_EQ(token->kind, SourceTokenKind::Variable);
}

TEST(SourceIndex, AFunctionParameterAndAGlobalResolveToTheirDeclarations) {
    // arrange a global read by a property default and a function that returns its parameter (§11.1)
    const SourceIndex index = index_of(R"(
global limit: int = 3;
fn clamp(value: int): int { return value; }
component Panel {
    in property<int> count: limit;
    Column {}
}
)");
    // act the parameter read and the global read are both bindings (§12.2)
    const SourceUse* parameter = use_named(index, "value");
    const SourceUse* global = use_named(index, "limit");
    // assert the parameter read resolved to the declared parameter (§12.2)
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(parameter->kind, SourceUseKind::Binding);
    ASSERT_TRUE(parameter->target.has_value());
    EXPECT_EQ(parameter->target->span.end.column - parameter->target->span.start.column, parameter->name.size());
    // assert the global read resolved to the module-scoped declaration (§12.2)
    ASSERT_NE(global, nullptr);
    EXPECT_EQ(global->kind, SourceUseKind::Binding);
    ASSERT_TRUE(global->target.has_value());
    // the global is the first top level symbol, because it is written first (§12.2)
    EXPECT_EQ(global->target->span, index.symbols.at(0).name_span);
}

TEST(SourceIndex, EveryLegendKindIsClassifiedAndTheStreamIsOrdered) {
    // arrange a file that declares a component, a property, a signal, a function, a global and an element (§11.1)
    const SourceIndex index = index_of(R"(
global limit: int = 3;
fn helper(value: string): string { return value; }
component Panel {
    in property<string> title: "hi";
    signal changed();
    Column {}
}
)");
    // act the tokens are what an editor paints, so the legend must cover all six kinds (§12.2)
    EXPECT_TRUE(has_token(index, SourceTokenKind::Keyword));
    EXPECT_TRUE(has_token(index, SourceTokenKind::Type));
    EXPECT_TRUE(has_token(index, SourceTokenKind::Property));
    EXPECT_TRUE(has_token(index, SourceTokenKind::Event));
    EXPECT_TRUE(has_token(index, SourceTokenKind::Function));
    EXPECT_TRUE(has_token(index, SourceTokenKind::Variable));
    // assert every token covers one line, because the protocol counts a length within one line (§12.2)
    for (const SourceToken& token : index.tokens) {
        // the span must end on the line it starts, because a multi line token cannot be encoded (§12.2)
        EXPECT_EQ(token.span.start.line, token.span.end.line);
        // the span must cover at least one character, because a zero length token paints nothing (§12.2)
        EXPECT_LT(token.span.start.column, token.span.end.column);
    }
    // assert the stream is ordered and disjoint, because the protocol encodes a delta from its predecessor (§12.2)
    for (std::size_t position = 1; position < index.tokens.size(); position++) {
        // the previous token is what the protocol measures against (§12.2)
        const SourceToken& previous = index.tokens.at(position - 1);
        // the current token is the one that must follow it (§12.2)
        const SourceToken& current = index.tokens.at(position);
        // the line never decreases, because an editor paints top to bottom (§12.2)
        const bool later_line = previous.span.start.line < current.span.start.line;
        // a token on the same line starts at or after its predecessor, which is what the delta assumes (§12.2)
        const bool same_line_after = previous.span.start.line == current.span.start.line && previous.span.start.column <= current.span.start.column;
        // one of the two orders must hold, because the stream is sorted (§12.2)
        EXPECT_TRUE(later_line || same_line_after);
        // a token on the same line starts after its predecessor ends, because a region is painted once (§12.2)
        if (previous.span.start.line == current.span.start.line)
            EXPECT_LE(previous.span.end.column, current.span.start.column);
    }
}

TEST(SourceIndex, TheFiveCategoriesCoverTheWrittenNames) {
    // arrange a file whose element, property, signal, handler and binding names are all written once (§12.2)
    const SourceIndex index = index_of(R"(component Panel {
    in property<string> title: "hi";
    signal activated();
    Column {
        Label { text: title; }
        Button { on_clicked: { activated(); } }
    }
})");
    // act each use is looked up and the token covering it is read from the stream (§12.2)
    const SourceUse* element = use_named(index, "Column");
    const SourceUse* property = use_named(index, "text");
    const SourceUse* signal = use_named(index, "activated");
    const SourceUse* handler = use_named(index, "on_clicked");
    const SourceUse* binding = use_named(index, "title");
    // assert every use was indexed, because one missing kind would pass the rest silently (§12.2)
    ASSERT_NE(element, nullptr);
    ASSERT_NE(property, nullptr);
    ASSERT_NE(signal, nullptr);
    ASSERT_NE(handler, nullptr);
    ASSERT_NE(binding, nullptr);
    // assert an element name is a type, which is what an editor colours it as (§12.2)
    const SourceToken* element_token = token_at(index, element->span);
    ASSERT_NE(element_token, nullptr);
    EXPECT_EQ(element_token->kind, SourceTokenKind::Type);
    // assert a property name is a property, which is the attribute it fills (§12.2)
    const SourceToken* property_token = token_at(index, property->span);
    ASSERT_NE(property_token, nullptr);
    EXPECT_EQ(property_token->kind, SourceTokenKind::Property);
    // assert a signal name is an event, which is what a handler reacts to (§12.2)
    const SourceToken* signal_token = token_at(index, signal->span);
    ASSERT_NE(signal_token, nullptr);
    EXPECT_EQ(signal_token->kind, SourceTokenKind::Event);
    // assert a handler name is a function, which is what an editor marks callbacks as (§12.2)
    const SourceToken* handler_token = token_at(index, handler->span);
    ASSERT_NE(handler_token, nullptr);
    EXPECT_EQ(handler_token->kind, SourceTokenKind::Function);
    // assert a binding name is a variable, which is what it reads (§12.2)
    const SourceToken* binding_token = token_at(index, binding->span);
    ASSERT_NE(binding_token, nullptr);
    EXPECT_EQ(binding_token->kind, SourceTokenKind::Variable);
}
