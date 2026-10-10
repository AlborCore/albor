#include "manifest/manifest.h"

#include <algorithm>
#include <utility>

namespace albor::manifest
{
    namespace
    {
        // the keyword that marks a declaration as describing a native object rather than a ui component (§11.2, AD-17)
        constexpr std::string_view NATIVE_KEYWORD = "native";

        // the keyword that introduces a component declaration (§11.2)
        constexpr std::string_view COMPONENT_KEYWORD = "component";

        // the keyword that introduces a data record, which is the element type a repeat walks (§11.2, AD-13)
        constexpr std::string_view RECORD_KEYWORD = "record";

        // the field name that carries a record's identity, which is the one field a row is keyed by (§18.1, AD-13)
        constexpr std::string_view KEY_FIELD = "key";

        // the keyword that introduces a property declaration, which carries the direction (§11.2)
        constexpr std::string_view PROPERTY_KEYWORD = "property";

        // the keyword that makes a property assignable from a handler (§11.2, §7)
        constexpr std::string_view INOUT_KEYWORD = "inout";

        // the keyword that introduces a signal declaration (§11.2)
        constexpr std::string_view SIGNAL_KEYWORD = "signal";

        // the keyword that introduces the semantic role declaration (§10.1, §11.2)
        constexpr std::string_view SEMANTIC_KEYWORD = "semantic";

        // the keyword that introduces the design time preview block (§11.2)
        constexpr std::string_view PREVIEW_KEYWORD = "preview";

        // the field a semantic declaration assigns, which is the role name (§10.1, §11.2)
        constexpr std::string_view ROLE_KEYWORD = "role";
    } // namespace

    ManifestParser::ManifestParser(const std::vector<Token>& tokens, DiagnosticList& diagnostics) :
        m_tokens(tokens), m_diagnostics(diagnostics) {
        // the end unit is what every out of range read is clamped to, so a lookahead is never undefined (§11.2)
        m_end.kind = Token::Kind::End;
    }

    ManifestFile ManifestParser::parse(std::string_view module) {
        // the file is assembled as the components are read, so a later mistake cannot discard an earlier one (§11.2)
        ManifestFile file;
        // the driver named the file, which is what a ui import resolves against (§11.2, AD-17)
        file.module = std::string(module);
        // the declarations run until the end unit, which every stream ends with (§11.2)
        while (peek().kind != Token::Kind::End) {
            // the offset before the declaration is kept, because the reader must always make progress (§11.2)
            const std::size_t before = m_index;
            // a record needs no native keyword, because it describes data rather than a native object (§11.2, AD-13)
            if (at_keyword(RECORD_KEYWORD)) {
                // the declaration is read into a local, because a broken one is dropped rather than kept (§11.2)
                RecordDeclaration record = parse_record();
                // a record that carries a name is kept, because a nameless one is a recovered fragment (§11.2)
                if (!record.name.empty())
                    file.records.push_back(std::move(record));
                // a declaration that consumed nothing would leave the loop on the same unit forever (§11.2)
                if (m_index == before)
                    advance();
                continue;
            }
            // the native keyword is required, because a manifest declares nothing else (§11.2, AD-17)
            if (at_keyword(NATIVE_KEYWORD)) {
                // the keyword is consumed, so the component keyword behind it is checked (§11.2)
                advance();
                // a native keyword with no component behind it is a truncated declaration (§11.2)
                if (!at_keyword(COMPONENT_KEYWORD))
                    report_unexpected(COMPONENT_KEYWORD);
            }
            // a unit that is neither the keyword nor a component is reported (§11.2)
            else if (!at_keyword(COMPONENT_KEYWORD))
                report_unexpected("native component");
            // a component is the only declaration a manifest holds (§11.2)
            if (at_keyword(COMPONENT_KEYWORD)) {
                // the declaration is read into a local, because a broken one is dropped rather than kept (§11.2)
                NativeComponent component = parse_component();
                // a component that carries a name is kept, because a nameless one is a recovered fragment (§11.2)
                if (!component.name.empty())
                    file.components.push_back(std::move(component));
            }
            // a declaration that consumed nothing would leave the loop on the same unit forever (§11.2)
            if (m_index == before)
                advance();
        }
        return file;
    }

    const Token& ManifestParser::peek(std::size_t offset) const {
        // the index is clamped to the last unit, so a lookahead past the end reads the end instead (§11.2)
        const std::size_t index = m_index + offset;
        // the clamped position is returned, which is what terminates every grammar loop (§11.2)
        if (index >= m_tokens.size())
            return m_end;
        return m_tokens[index];
    }

    bool ManifestParser::at_keyword(std::string_view keyword, std::size_t offset) const {
        // the unit decides, so a literal spelled like a keyword never matches (§11.2)
        return peek(offset).is(keyword);
    }

    bool ManifestParser::at_punctuation(std::string_view symbol, std::size_t offset) const {
        // the unit at the offset decides, which is what lets a construct look one ahead (§11.2)
        return peek(offset).is_punctuation(symbol);
    }

    bool ManifestParser::accept_keyword(std::string_view keyword) {
        // a unit that is not the keyword leaves the stream untouched, which is what makes lookahead safe (§11.2)
        if (!at_keyword(keyword))
            return false;
        // the keyword is consumed, because the caller has committed to it (§11.2)
        advance();
        return true;
    }

    bool ManifestParser::accept_punctuation(std::string_view symbol) {
        // a unit that is not the symbol leaves the stream untouched, which is what makes lookahead safe (§11.2)
        if (!at_punctuation(symbol))
            return false;
        // the symbol is consumed, because the caller has committed to it (§11.2)
        advance();
        return true;
    }

    void ManifestParser::advance(std::size_t offset) {
        // the unit being consumed ends where the construct the reader was building ends, so it is recorded first (§11.2)
        m_last_consumed_end = peek(offset).span.end;
        // the offset moves past the unit the caller named, which is one past it for the common case (§11.2)
        m_index += offset + 1;
        // the cursor is clamped to the stream, so every lookahead stays inside it (§11.2)
        m_index = std::min(m_index, m_tokens.size());
    }

    void ManifestParser::expect_keyword(std::string_view keyword) {
        // a keyword that is present is consumed, which is the common case (§11.2)
        if (accept_keyword(keyword))
            return;
        // a keyword that is absent is reported rather than guessed at (§11.2)
        report_unexpected(keyword);
    }

    void ManifestParser::expect_punctuation(std::string_view symbol) {
        // a symbol that is present is consumed, which is the common case (§11.2)
        if (at_punctuation(symbol)) {
            // the symbol is consumed, because the caller has committed to it (§11.2)
            advance();
            return;
        }
        // a symbol that is absent is reported rather than guessed at (§11.2)
        report_unexpected(symbol);
    }

    std::string ManifestParser::expect_identifier() {
        // only an identifier can name a declaration, so anything else is a mistake (§11.2)
        if (peek().kind != Token::Kind::Identifier) {
            // the problem is reported rather than skipped, because a missing name has no recovery (§11.2)
            report_unexpected("an identifier");
            return {};
        }
        // the name is moved out of the unit, because the parser owns the stream (§11.2)
        std::string name = std::move(m_tokens[m_index].text);
        // the named unit is consumed, because the caller has read it (§11.2)
        advance();
        return name;
    }

    std::string ManifestParser::expect_type_name() {
        // the name is required, because the glue generator needs a type (§11.2, AD-17)
        std::string name = expect_identifier();
        // a type with no angle is scalar, which is the common case (§11.2)
        if (!at_punctuation("<"))
            return name;
        // the bracketed remainder is read whole, because a nested type carries its own angles (§11.2)
        return name + expect_bracketed_type();
    }

    std::string ManifestParser::expect_bracketed_type() {
        // the opening angle is required, so a declared type is always delimited (§11.2)
        expect_punctuation("<");
        // the name starts empty, because a bracketed type may begin with its own angle (§11.2)
        std::string name;
        // the nesting depth is tracked, because Snapshot<T> has a type inside it (§11.2)
        std::size_t depth = 1;
        // the loop runs until the matching closing angle, which is what ends a nested type (§11.2)
        while (true) {
            // the end unit means the angle was never closed, which stops the loop (§11.2)
            if (peek().kind == Token::Kind::End) {
                // the unclosed type is reported, because a declared type is never open ended (§11.2)
                report_unexpected(">");
                break;
            }
            // a nested type opens another angle, which the depth counts (§11.2)
            if (at_punctuation("<"))
                depth++;
            // an angle closes either the nesting or the type, which the depth counts (§11.2)
            if (at_punctuation(">")) {
                // the depth drops first, so the closing of the type itself exits the loop (§11.2)
                depth--;
                // a closing angle that closed the type ends it, so it is consumed and not appended (§11.2)
                if (depth == 0) {
                    // the closing angle is consumed, so the caller resumes after it (§11.2)
                    advance();
                    break;
                }
            }
            // every unit of the name is appended without its spacing, because a type carries none (§11.2)
            name += peek().text;
            // the unit is consumed, so the name is read exactly once (§11.2)
            advance();
        }
        return name;
    }

    std::string ManifestParser::expect_value_text() {
        // the value starts empty, because a manifest may declare none (§11.2)
        std::string text;
        // the nesting depth is tracked, because a value may be bracketed (§11.2)
        std::size_t depth = 0;
        // the loop runs until the terminator, which is what ends the value (§11.2)
        while (true) {
            // the end unit ends the value, because nothing can follow it, and the caller reports it (§11.2)
            if (peek().kind == Token::Kind::End) {
                // the missing terminator is reported here, because no other reader will reach this unit (§11.2)
                report_unexpected(";");
                // the value ends, because nothing can follow it (§11.2)
                break;
            }
            // a closing brace at depth zero ends the value, because it ends the declaration it belongs to (§11.2)
            if (depth == 0 && at_punctuation("}"))
                break;
            // an opening parenthesis opens a nesting, which the depth counts so its terminator is not the end (§11.2)
            if (at_punctuation("(")) {
                // the depth rises, so the closing unit is not mistaken for the terminator (§11.2)
                depth++;
            }
            // a closing parenthesis closes a nesting, which the depth counts (§11.2)
            if (at_punctuation(")"))
                depth--;
            // the terminator at depth zero ends the value, because it ends the declaration (§11.2)
            if (depth == 0 && at_punctuation(";"))
                break;
            // a colour literal keeps its hash out of its own text, so re-emission has to restore it (§11.2)
            text += peek().kind == Token::Kind::Color ? "#" + peek().text : peek().text;
            // the unit is consumed, so the value is read exactly once (§11.2)
            advance();
        }
        return text;
    }

    NativeComponent ManifestParser::parse_component() {
        // the declaration is assembled locally, because the caller takes it by value (§11.2)
        NativeComponent component;
        // the span starts at the keyword, so a diagnostic points at the whole declaration (§11.2)
        component.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.2)
        advance();
        // the name is required, because it is the element type a ui file writes (§11.2, AD-17)
        component.name = expect_identifier();
        // the opening brace is required, because a component always has a body (§11.2)
        expect_punctuation("{");
        // the members run until the closing brace or the end of the stream (§11.2)
        while (true) {
            // the closing brace ends the body, which is the normal exit (§11.2)
            if (at_punctuation("}"))
                break;
            // the end unit ends the body too, because no member can follow it (§11.2)
            if (peek().kind == Token::Kind::End)
                break;
            // the member is read into the declaration the caller is building (§11.2)
            parse_component_member(component);
        }
        // a body that never closed is reported, because the declaration is incomplete (§11.2)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.2)
        component.span.end = m_last_consumed_end;
        return component;
    }

    RecordDeclaration ManifestParser::parse_record() {
        // the declaration is assembled locally, because the caller takes it by value (§11.2)
        RecordDeclaration record;
        // the span starts at the keyword, so a diagnostic points at the whole declaration (§11.2)
        record.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.2)
        advance();
        // the name is required, because it is the element type a ui file writes after vector (§11.2, AD-17)
        record.name = expect_identifier();
        // the opening brace is required, because a record always has a body (§11.2)
        expect_punctuation("{");
        // the fields run until the closing brace or the end of the stream (§11.2)
        while (true) {
            // the closing brace ends the body, which is the normal exit (§11.2)
            if (at_punctuation("}"))
                break;
            // the end unit ends the body too, because no field can follow it (§11.2)
            if (peek().kind == Token::Kind::End)
                break;
            // the field is read into the declaration the caller is building (§11.2)
            parse_record_field(record);
        }
        // a body that never closed is reported, because the declaration is incomplete (§11.2)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.2)
        record.span.end = m_last_consumed_end;
        // a record without a key field has no identity, which is what a row is keyed by (§18.1, AD-11)
        if (std::none_of(record.fields.begin(), record.fields.end(), [](const RecordField& field) { return field.is_key; }))
            m_diagnostics.add(Diagnostic::Severity::Error, record.span, "E0308", "a record must declare a 'key' field, because its identity is explicit");
        return record;
    }

    void ManifestParser::parse_record_field(RecordDeclaration& record) {
        // the span starts at the name, so a diagnostic points at the whole field (§11.2)
        RecordField field;
        field.span.start = peek().span.start;
        // the field name is required, because a binding addresses the field by it (§11.2)
        field.name = expect_identifier();
        // the colon separates the name from its type, which a binding checks against (§11.2)
        expect_punctuation(":");
        // the type is required, because the glue generator needs a c++ type (§11.2, AD-17)
        field.type = expect_type_name();
        // the field named key carries the record's identity, which a row is keyed by (§18.1, AD-11)
        field.is_key = field.name == KEY_FIELD;
        // an equals sign introduces a default, which the glue generator re-emits (§11.2)
        if (accept_punctuation("=")) {
            // the value is read as written, because the generator re-emits rather than reinterprets (§11.2)
            field.default_value = expect_value_text();
        }
        // the separator is optional, so a field that omits it is still read (§11.2)
        const bool has_separator = accept_punctuation(";");
        // a field that carries no separator and no default is still complete, because the type ended it (§11.2)
        (void)has_separator;
        // the span ends at the last unit the field consumed (§11.2)
        field.span.end = m_last_consumed_end;
        // the field joins the record, because a record is exactly the fields it declares (§11.2)
        record.fields.push_back(std::move(field));
    }

    void ManifestParser::parse_component_member(NativeComponent& component) {
        // the offset before the member is kept, because a member that consumes nothing must still advance (§11.2)
        const std::size_t before = m_index;
        // an incoming property is a declaration, which is how a binding finds its target (§11.2)
        if (at_keyword("in")) {
            parse_property_declaration(component);
            return;
        }
        // an assignable property is a declaration too, which only differs in direction (§11.2, §7)
        if (at_keyword(INOUT_KEYWORD)) {
            parse_property_declaration(component);
            return;
        }
        // a signal is a declaration, which is what a handler may invoke (§11.2)
        if (at_keyword(SIGNAL_KEYWORD)) {
            parse_signal_declaration(component);
            return;
        }
        // the semantic role describes what assistive technology announces (§10.1, §11.2)
        if (at_keyword(SEMANTIC_KEYWORD)) {
            parse_semantic_declaration(component);
            return;
        }
        // the preview block holds the design time overrides (§11.2)
        if (at_keyword(PREVIEW_KEYWORD)) {
            parse_preview_block(component);
            return;
        }
        // anything else cannot start a member, so the reader is told what was expected (§11.2)
        report_unexpected("a property, a signal, a semantic role or a preview block");
        // the units up to the next member are skipped, so one mistake costs one member (§11.2)
        recover_to_component();
        // the recovery stopped at a boundary it consumed or at one the member loop handles, so nothing more is needed (§11.2)
        (void)before;
    }

    void ManifestParser::parse_property_declaration(NativeComponent& component) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.2)
        NativeProperty property;
        // the span starts at the direction keyword, so a diagnostic points at the whole declaration (§11.2)
        property.span.start = peek().span.start;
        // an incoming property may only be assigned from outside (§11.2, §7)
        property.direction = at_keyword(INOUT_KEYWORD) ? PropertyDirection::InOut : PropertyDirection::In;
        // the direction keyword is consumed, because the caller has committed to it (§11.2)
        advance();
        // the property keyword is required, because the direction alone does not declare anything (§11.2)
        expect_keyword(PROPERTY_KEYWORD);
        // the type is read between its angles, which is where the declaration states it (§11.2)
        property.type = expect_bracketed_type();
        // the name is required, because a binding refers to it (§11.2)
        property.name = expect_identifier();
        // a declared default is optional, because a property may have none (§11.2)
        if (accept_punctuation(":")) {
            // the colon promises a value, so the declaration has one (§11.2)
            property.has_default = true;
            // the default is read as written, because the glue generator re-emits rather than reinterprets it (§11.2)
            property.default_text = expect_value_text();
        }
        // the terminator is required, because the declaration ends there (§11.2)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.2)
        property.span.end = m_last_consumed_end;
        // the property is kept, because it is what a binding resolves against (§11.2)
        component.properties.push_back(std::move(property));
    }

    void ManifestParser::parse_signal_declaration(NativeComponent& component) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.2)
        NativeSignal signal;
        // the span starts at the keyword, so a diagnostic points at the whole declaration (§11.2)
        signal.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.2)
        advance();
        // the name is required, because a handler invokes it (§11.2)
        signal.name = expect_identifier();
        // the parameter list is delimited, so the parameters are read until it closes (§11.2)
        expect_punctuation("(");
        // the list is read until the closing parenthesis or the end of the stream (§11.2)
        while (true) {
            // the closing parenthesis ends the list, which is the normal exit (§11.2)
            if (at_punctuation(")"))
                break;
            // the end unit ends the list too, because no parameter can follow it (§11.2)
            if (peek().kind == Token::Kind::End)
                break;
            // each parameter is written as a type and a name, which is how a manifest declares one (§11.2)
            SignalParameter parameter;
            // the type is a bare name, because a signal declares it between no brackets (§11.2)
            parameter.type = expect_identifier();
            // the name is required after the type, because the facade signature needs it (§11.2)
            parameter.name = expect_identifier();
            // a parameter with no name is reported, because the facade could not emit a signature (§11.2)
            if (parameter.name.empty())
                report_unexpected("a parameter name");
            // a parameter with no type is reported for the same reason, because the signature names it (§11.2)
            if (parameter.type.empty())
                report_unexpected("a parameter type");
            // the parameter is kept in declaration order, because the facade signature follows that order (§11.2)
            signal.parameters.push_back(std::move(parameter));
            // a further parameter is preceded by a comma, which is consumed here (§11.2)
            if (!at_punctuation(","))
                break;
            // the comma is consumed, so the next parameter starts after it (§11.2)
            advance();
            // a comma at the end of the list is reported, because it promises a parameter that is not there (§11.2)
            if (at_punctuation(")")) {
                report_unexpected("a parameter type");
                break;
            }
        }
        // a list that never closed is reported, because the declaration is incomplete (§11.2)
        expect_punctuation(")");
        // the terminator is required, because the declaration ends there (§11.2)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.2)
        signal.span.end = m_last_consumed_end;
        // the signal is kept, because it is what a handler resolves against (§11.2)
        component.signals.push_back(std::move(signal));
    }

    void ManifestParser::parse_semantic_declaration(NativeComponent& component) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.2)
        SemanticDeclaration declaration;
        // the span starts at the keyword, so a diagnostic points at the whole declaration (§11.2)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.2)
        advance();
        // the role field is required, because the semantic role is what assistive technology announces (§10.1, §11.2)
        const std::string field = expect_identifier();
        // the only field a semantic declaration carries is the role, because that is all the facade publishes (§10.1, §11.2)
        if (field != ROLE_KEYWORD)
            m_diagnostics.add(Diagnostic::Severity::Error, declaration.span, "E0304", "a semantic declaration carries only a role");
        // the colon is required, because a declaration assigns a value (§11.2)
        expect_punctuation(":");
        // the role is a string, because it names a role the runtime maps onto a known one (§10.1, §11.2)
        if (peek().kind == Token::Kind::String) {
            // the role is moved out of the unit, because the parser owns the stream (§10.1, §11.2)
            declaration.role = std::move(m_tokens[m_index].text);
            // the literal is consumed, because the role has been read (§10.1, §11.2)
            advance();
        }
        else {
            // anything but a string is reported, because the runtime matches a role by name (§10.1, §11.2)
            report_unexpected("a role name");
        }
        // the terminator is required, because the declaration ends there (§11.2)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.2)
        declaration.span.end = m_last_consumed_end;
        // the declaration is kept, because the facade publishes what it announces (§10.1, §11.2)
        component.semantic = declaration;
    }

    void ManifestParser::parse_preview_block(NativeComponent& component) {
        // the keyword is consumed, because the caller has already matched it (§11.2)
        advance();
        // the opening brace is required, because a preview block always has a body (§11.2)
        expect_punctuation("{");
        // the overrides run until the closing brace or the end of the stream (§11.2)
        while (true) {
            // the closing brace ends the block, which is the normal exit (§11.2)
            if (at_punctuation("}"))
                break;
            // the end unit ends the block too, because no override can follow it (§11.2)
            if (peek().kind == Token::Kind::End)
                break;
            // the override is assembled locally, because the name decides which field it fills (§11.2)
            PreviewOverride override_entry;
            // the span starts at the name, so a diagnostic points at the whole override (§11.2)
            override_entry.span.start = peek().span.start;
            // the property name is required, because an override replaces a declared default (§11.2)
            override_entry.property = expect_identifier();
            // the colon is required, because an override assigns a value (§11.2)
            expect_punctuation(":");
            // the value is read as written, because the stub re-emits rather than reinterprets it (§11.2)
            override_entry.value_text = expect_value_text();
            // the terminator is required, because the override ends there (§11.2)
            expect_punctuation(";");
            // the span ends at the terminator, which the reader has already consumed (§11.2)
            override_entry.span.end = m_last_consumed_end;
            // the override is kept, because the preview host applies it at design time (§11.2)
            component.preview_overrides.push_back(std::move(override_entry));
        }
        // a block that never closed is reported, because the declaration is incomplete (§11.2)
        expect_punctuation("}");
    }

    void ManifestParser::recover_to_component() {
        // the nesting depth is tracked, because a nested body is not where the next member starts (§11.2)
        std::size_t depth = 0;
        // the scan runs until the next member starts or the component ends (§11.2)
        while (peek().kind != Token::Kind::End) {
            // a closing brace at the top level ends the component, so the member loop is the one that stops (§11.2)
            if (depth == 0 && at_punctuation("}"))
                return;
            // a keyword at the top level starts a member, so the scan stops before it (§11.2)
            if (depth == 0 && starts_member())
                return;
            // a terminator at the top level ends the broken member, so the scan stops after it (§11.2)
            if (depth == 0 && at_punctuation(";")) {
                // the terminator is consumed, so the member loop resumes after it (§11.2)
                advance();
                return;
            }
            // a closing brace closes a nested body, so the depth drops (§11.2)
            if (at_punctuation("}")) {
                // the brace is consumed, because the depth has to follow it (§11.2)
                depth--;
                // the unit is consumed, because the scan moves past it (§11.2)
                advance();
                continue;
            }
            // an opening brace opens a nested body, which the depth records (§11.2)
            if (at_punctuation("{")) {
                // the brace is consumed, because the depth has to follow it (§11.2)
                advance();
                // the depth rises, so a member keyword inside the body is not treated as a boundary (§11.2)
                depth++;
                continue;
            }
            // any other unit is part of the broken member, so it is skipped (§11.2)
            advance();
        }
    }

    bool ManifestParser::starts_member() const {
        // the keywords are checked one at a time, so each reports its own result rather than a short circuit (§11.2)
        bool starts = at_keyword("in");
        // a keyword already matched needs no further check (§11.2)
        if (!starts)
            starts = at_keyword(INOUT_KEYWORD);
        // a keyword already matched needs no further check (§11.2)
        if (!starts)
            starts = at_keyword(SIGNAL_KEYWORD);
        // a keyword already matched needs no further check (§11.2)
        if (!starts)
            starts = at_keyword(SEMANTIC_KEYWORD);
        // the last keyword completes the set of member forms (§11.2)
        if (!starts)
            starts = at_keyword(PREVIEW_KEYWORD);
        return starts;
    }

    void ManifestParser::report_unexpected(std::string_view expected) {
        // the problem points at the current unit, so a reader sees exactly what was found (§11.2)
        const std::string found = peek().kind == Token::Kind::End ? std::string("the end of the file") : peek().text;
        // the report names what was expected and what was found, because both are needed to fix it (§11.2)
        m_diagnostics.add(Diagnostic::Severity::Error, peek().span, "E0303", "expected " + std::string(expected) + " but found '" + found + "'");
    }
    namespace
    {
        // the compiler type a written field type names, which is the same spelling the checker resolves (§11.2, AD-17)
        compiler::ValueType field_value_type(std::string_view written) {
            // a boolean field carries a bool, which a binding stores directly (§11.2)
            if (written == "bool")
                return compiler::scalar(compiler::TypeKind::Bool);
            // an integer field carries an int, which is what counts rows and indices (§11.2)
            if (written == "int")
                return compiler::scalar(compiler::TypeKind::Int);
            // a floating point field carries a float, which is what a measurement arrives as (§11.2)
            if (written == "float")
                return compiler::scalar(compiler::TypeKind::Float);
            // a text field carries a string, which is what a label reads (§11.2)
            if (written == "string")
                return compiler::scalar(compiler::TypeKind::String);
            // a colour field carries the framework colour, which names no platform type (§11.2, AD-05)
            if (written == "color")
                return compiler::scalar(compiler::TypeKind::Color);
            // a snapshot field carries an immutable shared value, which is how data crosses a thread (§11.2, §13.2)
            if (written.starts_with("Snapshot<") && written.ends_with(">")) {
                // the element name sits between the brackets, which is the type argument (§11.2, §13.2)
                const std::string_view element = written.substr(9, written.size() - 10);
                // an empty argument names no type, so the field is untyped rather than guessed (§11.2)
                if (!element.empty())
                    return compiler::ValueType::parameterized(compiler::TypeKind::Snapshot, compiler::ValueType::native(std::string(element)));
            }
            // a type outside the set the glue generator spells is unknown, which the checker reports (§11.2)
            return compiler::error_type();
        }
    } // namespace

    void register_records(const ManifestFile& file, ElementCatalog& catalog) {
        // the records are walked in declaration order, which is the order a ui file reads their fields (§11.2, AD-17)
        for (const RecordDeclaration& declaration : file.records) {
            // the record starts with its name and collects the fields as they are translated (§11.2, AD-17)
            compiler::RecordType record{.name = declaration.name, .fields = {}};
            // each field is walked, because a field read resolves by name against the declaration (§11.2, AD-17)
            for (const RecordField& field : declaration.fields) {
                // the written type becomes the type a field read produces, which is the whole of the bridge (§11.2)
                record.fields.push_back(compiler::RecordFieldType{.name = field.name, .type = field_value_type(field.type), .is_key = field.is_key});
            }
            // the record joins the catalog, which is what makes vector<Task> resolve (§11.2, AD-17, AD-13)
            catalog.add_record(record);
        }
    }
} // namespace albor::manifest
