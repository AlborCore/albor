#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/diagnostic.h"
#include "compiler/lexer.h"
#include "compiler/value_type.h"

namespace albor::manifest
{
    using compiler::Diagnostic;
    using compiler::DiagnosticList;
    using compiler::ElementCatalog;
    using compiler::SourceLocation;
    using compiler::SourceSpan;
    using compiler::Token;

    // the direction a native property may move in, which decides whether a handler may assign it (§11.2, §7)
    enum class PropertyDirection
    {
        // assigned only from outside, which is the default for a bound property (§11.2)
        In,

        // assigned from a handler as well as from outside (§11.2)
        InOut
    };

    // one property a native component declares, which is what a binding resolves against (§11.2, AD-17)
    struct NativeProperty
    {
        // the declared name, which the facade and the checker agree on (§11.2)
        std::string name;

        // the type as written between the angle brackets, which may nest as Snapshot<T> does (§11.2)
        std::string type;

        // the direction, which decides whether a handler may assign it (§11.2, §7)
        PropertyDirection direction = PropertyDirection::In;

        // whether the property declared a default, because a default is optional (§11.2)
        bool has_default = false;

        // the default as written, which the glue generator re-emits rather than reinterprets (§11.2)
        std::string default_text;

        // where the declaration was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // one parameter of a native signal, which a handler invocation must match (§11.2)
    struct SignalParameter
    {
        // the parameter name, which the generated facade uses (§11.2)
        std::string name;

        // the type as written, which the facade signature uses (§11.2)
        std::string type;
    };

    // one signal a native component declares, which is what a handler may invoke (§11.2)
    struct NativeSignal
    {
        // the declared name, which the facade and the checker agree on (§11.2)
        std::string name;

        // the parameters in declaration order, which the facade signature follows (§11.2)
        std::vector<SignalParameter> parameters;

        // where the declaration was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // the semantic role a native component publishes, which assistive technology reads (§10.1, §11.2)
    struct SemanticDeclaration
    {
        // the role name as written, which the runtime maps onto a known role (§10.1, §11.2)
        std::string role;

        // where the declaration was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // one property the preview host overrides, so the design time stub differs from the real default (§11.2)
    struct PreviewOverride
    {
        // the property being overridden, which must be declared on the component (§11.2)
        std::string property;

        // the value as written, which the stub re-emits rather than reinterprets (§11.2)
        std::string value_text;

        // where the override was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // one native component the manifest declares, which the compiler resolves element types against (§11.2, AD-17)
    struct NativeComponent
    {
        // the declared name, which is the element type a .ui file writes (§11.2, AD-17)
        std::string name;

        // the properties the component exposes to bindings (§11.2)
        std::vector<NativeProperty> properties;

        // the signals the component raises, which a handler may attach to (§11.2)
        std::vector<NativeSignal> signals;

        // the semantic role, which is absent when the component publishes none (§10.1, §11.2)
        std::optional<SemanticDeclaration> semantic;

        // the preview overrides, which the design time stub applies (§11.2)
        std::vector<PreviewOverride> preview_overrides;

        // where the declaration was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // one field a record declares, which a .ui file reads by name (§11.2, AD-17)
    struct RecordField
    {
        // the field name, which is what a binding writes as item.name (§11.2, AD-17)
        std::string name;

        // the field type as written, which the glue generator maps to a c++ type (§11.2, AD-17)
        std::string type;

        // the default as written, which the glue generator re-emits rather than reinterprets (§11.2)
        std::string default_value;

        // whether the field carries the record's identity, which is the one field a row is keyed by (§18.1, AD-13)
        bool is_key = false;

        // where the field was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // one data record the manifest declares, which is the element type a .ui repeat walks (§11.2, AD-17, AD-13)
    struct RecordDeclaration
    {
        // the declared name, which is the element type a .ui file writes after vector (§11.2, AD-17)
        std::string name;

        // the fields the record carries, where exactly one carries its identity (§11.2, AD-18)
        std::vector<RecordField> fields;

        // where the declaration was written, so a diagnostic points at it (§11.2)
        SourceSpan span;
    };

    // one parsed manifest, which is the single source of truth for the native types it declares (§11.2, AD-17)
    struct ManifestFile
    {
        // the module the driver named the file after, which is what a .ui import resolves against (§11.1, §11.2, AD-17)
        std::string module;

        // the components the file declares, in declaration order (§11.2, AD-17)
        std::vector<NativeComponent> components;

        // the records the file declares, which are the element types a repeat walks (§11.2, AD-17, AD-13)
        std::vector<RecordDeclaration> records;
    };

    // register the records a manifest declares into a catalog, which is what makes vector<Task> resolve (§11.2, AD-13)
    void register_records(const ManifestFile& file, compiler::ElementCatalog& catalog);

    // the parser that turns a scanned manifest into a declaration list, which reports rather than throws (§11.2)
    class ManifestParser
    {
    public:
        // build a parser over a scanned token stream, which the caller keeps alive for the parse (§11.2)
        ManifestParser(const std::vector<Token>& tokens, DiagnosticList& diagnostics);

        // parse the stream under the given module name, recovering at every component boundary (§11.2)
        [[nodiscard]] ManifestFile parse(std::string_view module);

    private:
        // hand out the unit at the given offset from the current one, which is how lookahead stays cheap (§11.2)
        [[nodiscard]] const Token& peek(std::size_t offset = 0) const;

        // report whether the unit at the given offset is the given keyword (§11.2)
        [[nodiscard]] bool at_keyword(std::string_view keyword, std::size_t offset = 0) const;

        // report whether the unit at the given offset is the given punctuation (§11.2)
        [[nodiscard]] bool at_punctuation(std::string_view symbol, std::size_t offset = 0) const;

        // report whether the unit at the given offset is the given keyword, consuming it when it is (§11.2)
        [[nodiscard]] bool accept_keyword(std::string_view keyword);

        // report whether the unit at the given offset is the given punctuation, consuming it when it is (§11.2)
        [[nodiscard]] bool accept_punctuation(std::string_view symbol);

        // consume the unit at the given offset, recording where it ended so a span can close on it (§11.2)
        void advance(std::size_t offset = 0);

        // consume a keyword the grammar requires, reporting a problem when it is absent (§11.2)
        void expect_keyword(std::string_view keyword);

        // consume a punctuation the grammar requires, reporting a problem when it is absent (§11.2)
        void expect_punctuation(std::string_view symbol);

        // read the identifier the grammar requires, reporting a problem when it is absent (§11.2)
        [[nodiscard]] std::string expect_identifier();

        // read a bracketed type name, which may nest as Snapshot<T> does (§11.2)
        [[nodiscard]] std::string expect_bracketed_type();

        // read a type name written bare, which may still nest as vector<string> does (§11.2)
        [[nodiscard]] std::string expect_type_name();

        // read a value as written, which the glue generator re-emits rather than reinterprets (§11.2)
        [[nodiscard]] std::string expect_value_text();

        // read one component declaration, which is what a manifest declares (§11.2)
        [[nodiscard]] NativeComponent parse_component();

        // read one record declaration, which is the element type a repeat walks (§11.2, AD-13)
        [[nodiscard]] RecordDeclaration parse_record();

        // read one field of a record, which is what a binding addresses by name (§11.2)
        void parse_record_field(RecordDeclaration& record);

        // read one member of a component, which is a property, a signal, a semantic role or a preview block (§11.2)
        void parse_component_member(NativeComponent& component);

        // read a property declaration with its direction, type, name and optional default (§11.2)
        void parse_property_declaration(NativeComponent& component);

        // read a signal declaration with its parameters (§11.2)
        void parse_signal_declaration(NativeComponent& component);

        // read the semantic role declaration (§11.2)
        void parse_semantic_declaration(NativeComponent& component);

        // read the preview block, whose members override declared defaults (§11.2)
        void parse_preview_block(NativeComponent& component);

        // skip units to the next member, which is how one mistake costs one member (§11.2)
        void recover_to_component();

        // report whether the current unit starts a component member, which is where recovery stops (§11.2)
        [[nodiscard]] bool starts_member() const;

        // report that the current unit cannot appear here, which points a reader at the mistake (§11.2)
        void report_unexpected(std::string_view expected);

        // the units being parsed, which the caller keeps alive for the whole parse (§11.2)
        const std::vector<Token>& m_tokens;

        // the problems found while parsing, which the parser appends to (§11.2)
        DiagnosticList& m_diagnostics;

        // the offset of the unit the parser is on (§11.2)
        std::size_t m_index = 0;

        // where the most recently consumed unit ended, which is what a closing span takes (§11.2)
        SourceLocation m_last_consumed_end;

        // the end unit, which every out of range read is clamped to so a lookahead is safe (§11.2)
        Token m_end;
    };
} // namespace albor::manifest
