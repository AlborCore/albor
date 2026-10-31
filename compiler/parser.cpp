#include "compiler/parser.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace albor::compiler
{
    namespace
    {
        // the keyword that introduces an import, which the recovery point also looks for (§11.1)
        constexpr std::string_view IMPORT_KEYWORD = "import";

        // the keyword that introduces a component, which is the other top level declaration (§11.1)
        constexpr std::string_view COMPONENT_KEYWORD = "component";

        // the keyword that introduces a struct declaration, which a property or a signal may carry (§11.1)
        constexpr std::string_view STRUCT_KEYWORD = "struct";

        // the keyword that introduces an enum declaration, which a property may hold one member of (§11.1)
        constexpr std::string_view ENUM_KEYWORD = "enum";

        // the keyword that introduces a function declaration, which a binding may call (§11.1)
        constexpr std::string_view FN_KEYWORD = "fn";

        // the keyword that introduces a module-scoped value, which several components read (§11.1)
        constexpr std::string_view GLOBAL_KEYWORD = "global";

        // the keyword that introduces a property declaration, which carries the direction (§11.1)
        constexpr std::string_view PROPERTY_KEYWORD = "property";

        // the keyword that introduces a command declaration, which names one shared action (§18, AD-15)
        constexpr std::string_view COMMAND_KEYWORD = "command";

        // the keyword that introduces a signal declaration (§11.1)
        constexpr std::string_view SIGNAL_KEYWORD = "signal";

        // the keyword that makes a property assignable from a handler (§11.1, §7)
        constexpr std::string_view INOUT_KEYWORD = "inout";

        // the keyword that introduces a repeated element (§11.1)
        constexpr std::string_view REPEAT_KEYWORD = "Repeat";

        // the keyword that introduces a conditional element, whose body exists only while the condition holds (§11.1)
        constexpr std::string_view IF_KEYWORD = "if";

        // the keyword that binds an item inside a repeat body (§11.1)
        constexpr std::string_view IN_KEYWORD = "in";

        // the prefix that makes a member a handler rather than a value, which is part of the written name (§11.1)
        constexpr std::string_view ON_PREFIX = "on_";

        // the property name that carries the author assigned identity rather than a value (§11.1, AD-11)
        constexpr std::string_view ID_KEYWORD = "id";

        // the translatable string macro, which is a distinct form rather than a general call (§11.1, §9.2)
        constexpr std::string_view TR_MACRO = "tr";

        // the embedded image macro, which names an asset the compiler registers at build time (§6.4, §11.1)
        constexpr std::string_view IMAGE_MACRO = "image";

        // the conversion target names the language defines, which is a closed set (§11.1)
        [[nodiscard]] bool is_conversion_name(std::string_view name) {
            // the four builtin conversions are the only single value call forms a binding may use (§11.1)
            return name == "string" || name == "int" || name == "float" || name == "bool";
        }

        // report whether a name is a colour operation, which is what a colour expression reaches through (§6.1)
        [[nodiscard]] bool is_colour_operation(std::string_view name) {
            // the three colour operations are the closed set, because the language defines no more (§11.1)
            return name == "transparentize" || name == "alpha" || name == "alpha_set";
        }

        // report whether a name may be called at all, which keeps the grammar closed (§11.1)
        [[nodiscard]] bool is_builtin_call(std::string_view name) {
            // a conversion and a colour operation are the only two call forms (§11.1)
            return is_conversion_name(name) || is_colour_operation(name);
        }
    } // namespace

    Parser::Parser(const std::vector<Token>& tokens, DiagnosticList& diagnostics) :
        m_tokens(tokens), m_diagnostics(diagnostics) {
        // the end unit is what every out of range read is clamped to, so a lookahead is never undefined (§11.1)
        m_end.kind = Token::Kind::End;
    }

    SourceFile Parser::parse() {
        // the file is assembled as the declarations are read, so a later mistake cannot discard an earlier one (§11.1)
        SourceFile file;
        // the declarations run until the end unit, which every stream ends with (§11.1)
        while (peek().kind != Token::Kind::End)
            parse_declaration(file);
        return file;
    }

    const Token& Parser::peek(std::size_t offset) const {
        // the index is clamped to the last unit, so a lookahead past the end reads the end instead (§11.1)
        const std::size_t index = m_index + offset;
        // the clamped position is returned, which is what terminates every grammar loop (§11.1)
        if (index >= m_tokens.size())
            return m_end;
        return m_tokens[index];
    }

    bool Parser::at_keyword(std::string_view keyword) const {
        // the unit decides, so a literal spelled like a keyword never matches (§11.1)
        return peek().is(keyword);
    }

    bool Parser::at_punctuation(std::string_view symbol, std::size_t offset) const {
        // the unit at the offset decides, which is what lets a construct look one ahead (§11.1)
        return peek(offset).is_punctuation(symbol);
    }

    bool Parser::accept_keyword(std::string_view keyword) {
        // a unit that is not the keyword leaves the stream untouched, which is what makes lookahead safe (§11.1)
        if (!at_keyword(keyword))
            return false;
        // the keyword is consumed, because the caller has committed to it (§11.1)
        advance();
        return true;
    }

    bool Parser::accept_punctuation(std::string_view symbol) {
        // a unit that is not the symbol leaves the stream untouched, which is what makes lookahead safe (§11.1)
        if (!at_punctuation(symbol))
            return false;
        // the symbol is consumed, because the caller has committed to it (§11.1)
        advance();
        return true;
    }

    void Parser::advance(std::size_t offset) {
        // the unit being consumed ends where the construct the reader was building ends, so it is recorded first (§11.1)
        m_last_consumed_end = peek(offset).span.end;
        // the offset moves past the unit the caller named, which is one past it for the common case (§11.1)
        m_index += offset + 1;
        // the cursor is clamped to the stream, so every lookahead stays inside it (§11.1)
        m_index = std::min(m_index, m_tokens.size());
    }

    void Parser::expect_keyword(std::string_view keyword) {
        // a keyword that is present is consumed, which is the common case (§11.1)
        if (accept_keyword(keyword))
            return;
        // a keyword that is absent is reported rather than guessed at (§11.1)
        report_unexpected(keyword);
    }

    void Parser::expect_punctuation(std::string_view symbol) {
        // a symbol that is present is consumed, which is the common case (§11.1)
        if (accept_punctuation(symbol))
            return;
        // a symbol that is absent is reported rather than guessed at (§11.1)
        report_unexpected(symbol);
    }

    std::string Parser::expect_identifier() {
        // only an identifier can name a declaration, so anything else is a mistake (§11.1)
        if (peek().kind != Token::Kind::Identifier) {
            // the problem is reported rather than skipped, because a missing name has no recovery (§11.1)
            report_unexpected("an identifier");
            return {};
        }
        // the name is moved out of the unit, because the parser owns the stream (§11.1)
        std::string name = std::move(m_tokens[m_index].text);
        // the named unit is consumed, because the caller has read it (§11.1)
        advance();
        return name;
    }

    std::string Parser::expect_type_name(bool bracketed) {
        // the name starts empty, because a bracketed type may begin with its own angle (§11.1)
        std::string name;
        // a bracketed type opens with an angle, which the caller has to see before anything is read (§11.1)
        if (bracketed) {
            expect_punctuation("<");
            return read_type_angles();
        }
        // a bare type is required to be an identifier, because that is all the language writes (§11.1)
        if (peek().kind != Token::Kind::Identifier) {
            report_unexpected("a type name");
            return name;
        }
        // the name is moved out of the unit, because the parser owns the stream (§11.1)
        std::string bare = std::move(m_tokens[m_index].text);
        // the unit is consumed, because the type has been read (§11.1)
        advance();
        // a generic opens an angle, which the tail reads exactly as the bracketed form spells it (§11.1)
        if (at_punctuation("<")) {
            advance();
            bare += "<" + read_type_angles() + ">";
        }
        return bare;
    }

    std::string Parser::read_type_angles() {
        // the name starts empty, because the caller has already consumed the opening angle (§11.1)
        std::string name;
        // the nesting depth is tracked, because Snapshot<T> has a type inside it (§11.1)
        std::size_t depth = 1;
        // the loop runs until the matching closing angle, which is what ends a nested type (§11.1)
        while (true) {
            // the end unit means the angle was never closed, which stops the loop (§11.1)
            if (peek().kind == Token::Kind::End) {
                // the unclosed type is reported, because a type is never open ended (§11.1)
                report_unexpected(">");
                break;
            }
            // a nested type opens another angle, which the depth counts (§11.1)
            if (at_punctuation("<"))
                depth++;
            // an angle closes either the nesting or the type, which the depth counts (§11.1)
            if (at_punctuation(">")) {
                // the depth drops first, so the closing of the type itself exits the loop (§11.1)
                depth--;
                // a closing angle at depth zero has already exited, so the unit is not appended (§11.1)
                if (depth == 0) {
                    // the closing angle is consumed, so the caller resumes after it (§11.1)
                    advance();
                    break;
                }
            }
            // every unit of the name is appended without its spacing, because a type carries none (§11.1)
            name += peek().text;
            // the unit is consumed, so the name is read exactly once (§11.1)
            advance();
        }
        return name;
    }

    void Parser::parse_declaration(SourceFile& file) {
        // an import is a top level declaration in its own right (§11.1, AD-17)
        if (at_keyword(IMPORT_KEYWORD)) {
            parse_import(file);
            return;
        }
        // a struct is a top level declaration, which names a shape a property may carry (§11.1)
        if (at_keyword(STRUCT_KEYWORD)) {
            parse_struct(file);
            return;
        }
        // an enum is a top level declaration, which names a closed set a property may hold one of (§11.1)
        if (at_keyword(ENUM_KEYWORD)) {
            parse_enum(file);
            return;
        }
        // a function is a top level declaration, which a binding may call as a named computation (§11.1)
        if (at_keyword(FN_KEYWORD)) {
            parse_function(file);
            return;
        }
        // a global is a top level declaration, which several components read as a named value (§11.1)
        if (at_keyword(GLOBAL_KEYWORD)) {
            parse_global(file);
            return;
        }
        // a component is the other top level declaration, which carries the tree (§11.1)
        if (at_keyword(COMPONENT_KEYWORD)) {
            // the declaration is parsed into a local first, because a broken one is dropped rather than kept (§11.1)
            std::unique_ptr<ComponentDeclaration> component = parse_component();
            // a component that carries a name is kept, because a nameless one is a recovered fragment (§11.1)
            if (!component->name.empty())
                file.components.push_back(std::move(*component));
            return;
        }
        // anything else cannot start a declaration, so the reader is told what was expected (§11.1)
        report_unexpected("import, struct, enum, fn, global or component");
        // the units up to the next declaration are skipped, so one mistake costs one declaration (§11.1)
        recover_to_declaration();
    }

    void Parser::parse_import(SourceFile& file) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        ImportDeclaration declaration;
        // the span starts at the keyword, so a diagnostic about the import points at it (§11.1)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1, AD-17)
        advance();
        // a quoted string names a ui file, which a component declaration in that file brings into scope (§11.1)
        if (peek().kind == Token::Kind::String) {
            // the decoded path is kept as written, because the loader resolves it against the importing file (§11.1)
            declaration.path = peek().value;
            // the string is consumed, because the path is read exactly once (§11.1)
            advance();
        }
        else {
            // the module name is read as a dotted path, because a module may be qualified (§11.1, AD-17)
            declaration.module = expect_identifier();
            // a dotted module keeps reading its segments until the terminator (§11.1, AD-17)
            while (at_punctuation(".")) {
                // the dot is consumed, because the caller has committed to another segment (§11.1, AD-17)
                advance();
                // the separator joins the segments, because the module name is a single token (§11.1, AD-17)
                declaration.module += ".";
                // the next segment is required, so a trailing dot is reported (§11.1, AD-17)
                declaration.module += expect_identifier();
            }
        }
        // the declaration ends at the terminator, which is required (§11.1)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the import is kept, because a manifest module list is what the checker resolves against (§11.1, AD-17)
        file.imports.push_back(std::move(declaration));
    }

    void Parser::parse_struct(SourceFile& file) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        StructDeclaration declaration;
        // the span starts at the keyword, so a diagnostic about the struct points at it (§11.1)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the name is required, because it becomes the record type a field read resolves against (§11.1)
        declaration.name = expect_identifier();
        // the body is delimited, so the fields are read until it closes (§11.1)
        expect_punctuation("{");
        // the fields run until the closing brace or the end of the stream (§11.1)
        while (!at_punctuation("}") && peek().kind != Token::Kind::End) {
            // the field starts with its type, which the registration pass resolves against the catalog (§11.1)
            StructField field;
            // the span starts at the type, so a diagnostic about the field points at it (§11.1)
            field.span.start = peek().span.start;
            // the type is a bare name, which a parametric form then extends with its argument (§11.1)
            field.type = expect_identifier();
            // a parametric form such as vector<string> carries its argument in angles, which are read whole (§11.1, §8.1)
            if (at_punctuation("<")) {
                // the nesting depth is tracked, because the argument may itself nest (§11.1)
                std::size_t depth = 0;
                // the angles run until the matching close, which is what ends a nested argument (§11.1)
                while (peek().kind != Token::Kind::End) {
                    // the current unit is appended first, so the argument keeps its own angles (§11.1)
                    field.type += peek().text;
                    // an opening angle deepens the nesting, which the depth records (§11.1)
                    const bool opens = at_punctuation("<");
                    // a closing angle closes one level of the argument (§11.1)
                    const bool closes = at_punctuation(">");
                    // the unit is consumed, so the argument advances exactly once (§11.1)
                    advance();
                    // the depth follows the open, because a nested argument opens another angle (§11.1)
                    if (opens)
                        depth++;
                    // the depth follows the close, and the matching one ends the read (§11.1)
                    if (closes) {
                        // the depth drops, because the body it opened has closed (§11.1)
                        depth--;
                        // a close at depth zero has already been appended, so the argument is complete (§11.1)
                        if (depth == 0)
                            break;
                    }
                }
            }
            // the name is required, because a binding addresses the field by it (§11.1)
            field.name = expect_identifier();
            // the field ends at the terminator, which is required (§11.1)
            expect_punctuation(";");
            // the span ends at the terminator, which the reader has already consumed (§11.1)
            field.span.end = last_consumed_end();
            // the field joins the struct, because a struct is exactly the fields it declares (§11.1)
            declaration.fields.push_back(std::move(field));
        }
        // a body that never closed is reported, because the declaration is incomplete (§11.1)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the struct is kept, because the registration pass turns it into a record the catalog resolves (§11.1)
        file.structs.push_back(std::move(declaration));
    }

    void Parser::parse_enum(SourceFile& file) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        EnumDeclaration declaration;
        // the span starts at the keyword, so a diagnostic about the enum points at it (§11.1)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the name is required, because it becomes the set a member resolves against (§11.1)
        declaration.name = expect_identifier();
        // the body is delimited, so the members are read until it closes (§11.1)
        expect_punctuation("{");
        // the members run until the closing brace or the end of the stream (§11.1)
        while (!at_punctuation("}") && peek().kind != Token::Kind::End) {
            // each member is a bare name, which is the closed set the enum holds (§11.1)
            declaration.members.push_back(expect_identifier());
            // a comma separates the members, which is optional before the closing brace (§11.1)
            (void)accept_punctuation(",");
        }
        // a body that never closed is reported, because the declaration is incomplete (§11.1)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the enum is kept, because the registration pass turns it into a value set the catalog resolves (§11.1)
        file.enums.push_back(std::move(declaration));
    }

    void Parser::parse_function(SourceFile& file) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        FunctionDeclaration declaration;
        // the span starts at the keyword, so a diagnostic about the function points at it (§11.1)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the name is required, because a call resolves against it (§11.1)
        declaration.name = expect_identifier();
        // the parameters are delimited, so they are read until the list closes (§11.1)
        expect_punctuation("(");
        // the parameters run until the closing parenthesis or the end of the stream (§11.1)
        while (!at_punctuation(")") && peek().kind != Token::Kind::End) {
            // each parameter is a name followed by its type, which the body reads by name (§11.1)
            FunctionParameter parameter;
            // the span starts at the parameter name, so a diagnostic points at it (§11.1)
            parameter.span.start = peek().span.start;
            // the name is required, because the body reads the argument through it (§11.1)
            parameter.name = expect_identifier();
            // a colon separates the name from its type, which keeps the two readable (§11.1)
            expect_punctuation(":");
            // the type is a bare name, which the checker resolves against the catalog (§11.1)
            parameter.type = expect_type_name(false);
            // the span ends at the type, which the reader has just consumed (§11.1)
            parameter.span.end = last_consumed_end();
            // the parameter is kept, because the signature is what a call checks its arguments against (§11.1)
            declaration.parameters.push_back(std::move(parameter));
            // a comma separates the parameters, which is optional before the closing parenthesis (§11.1)
            (void)accept_punctuation(",");
        }
        // a parameter list that never closed is reported, because the signature is incomplete (§11.1)
        expect_punctuation(")");
        // a colon separates the parameter list from the return type, which the checker resolves (§11.1)
        expect_punctuation(":");
        // the return type is a bare name, which the checker resolves against the catalog (§11.1)
        declaration.return_type = expect_type_name(false);
        // the body is delimited, so the return is read from inside it (§11.1)
        expect_punctuation("{");
        // the body is a single return statement, which is the whole of the computation in this form (§11.1)
        if (at_keyword("return")) {
            // the keyword is consumed, because the expression follows it (§11.1)
            advance();
            // the returned expression is parsed with the full expression grammar, which a call reaches (§11.1)
            declaration.return_value = parse_expression();
            // the statement ends, which is what separates it from the closing brace (§11.1)
            (void)accept_punctuation(";");
        }
        // anything else cannot open the body, so the reader is told what was expected (§11.1)
        else if (!at_punctuation("}"))
            report_unexpected("a return statement");
        // a body that never closed is reported, because the declaration is incomplete (§11.1)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the function is kept, because the checker resolves its calls and the codegen emits its body (§11.1)
        file.functions.push_back(std::move(declaration));
    }

    void Parser::parse_global(SourceFile& file) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        GlobalDeclaration declaration;
        // the span starts at the keyword, so a diagnostic about the global points at it (§11.1)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the name is required, because a reference resolves against it (§11.1)
        declaration.name = expect_identifier();
        // a colon separates the name from its type, which is how a declared name binds (§11.1)
        expect_punctuation(":");
        // the type is a bare or generic name, which the checker resolves against the catalog (§11.1)
        declaration.type = expect_type_name(false);
        // an equals separates the type from the value, which is the whole of the declaration (§11.1)
        expect_punctuation("=");
        // the value is an expression, so a construction or a literal may fill it (§11.1)
        declaration.value = parse_expression();
        // the declaration ends at the terminator, which is required (§11.1)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the global is kept, because the checker types its value and the codegen emits it (§11.1)
        file.globals.push_back(std::move(declaration));
    }

    std::unique_ptr<ComponentDeclaration> Parser::parse_component() {
        // the declaration is allocated first, because the caller takes the owning pointer (§11.1)
        auto component = std::make_unique<ComponentDeclaration>();
        // the span starts at the keyword, so a diagnostic about the component points at it (§11.1)
        component->span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the name is required, because it becomes the generated class name (§11.1, AD-03a)
        component->name = expect_identifier();
        // a colon names the base component, which is what a derived component writes after its name (§11.1)
        if (accept_punctuation(":")) {
            // the base span starts at its name, because a diagnostic about the base points at it (§11.1)
            component->base_span.start = peek().span.start;
            // the base is a name, because only a component this file declares may fill the role (§11.1)
            component->base = expect_identifier();
            // the span ends at the name, which the reader has just consumed (§11.1)
            component->base_span.end = last_consumed_end();
        }
        // the body is delimited, so the members are read until it closes (§11.1)
        expect_punctuation("{");
        // the members run until the closing brace or the end of the stream (§11.1)
        while (!at_punctuation("}") && peek().kind != Token::Kind::End)
            parse_component_member(*component);
        // a body that never closed is reported, because the component is incomplete (§11.1)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.1)
        component->span.end = last_consumed_end();
        return component;
    }

    void Parser::parse_component_member(ComponentDeclaration& component) {
        // an incoming property is a declaration, which is how a binding finds its target (§11.1)
        if (at_keyword("in")) {
            parse_property_declaration(component);
            return;
        }
        // an assignable property is a declaration too, which only differs in direction (§11.1, §7)
        if (at_keyword(INOUT_KEYWORD)) {
            parse_property_declaration(component);
            return;
        }
        // a signal is a declaration, which is what a handler may invoke (§11.1)
        if (at_keyword(SIGNAL_KEYWORD)) {
            parse_signal_declaration(component);
            return;
        }
        // a command is a declaration, which every actionable child then binds by name (§18, AD-15)
        if (at_keyword(COMMAND_KEYWORD)) {
            parse_command_declaration(component);
            return;
        }
        // anything else at this level is an element of the body, which the shape below decides (§11.1)
        if (peek().kind == Token::Kind::Identifier) {
            // a derived component takes content elements, because several may fill the one slot (§11.1)
            if (!component.base.empty()) {
                component.content.push_back(parse_tree_node());
                return;
            }
            // the root is taken once, because a component that derives from none has exactly one (§11.1)
            if (component.root == nullptr) {
                component.root = parse_tree_node();
                return;
            }
        }
        // a second root or a stray unit is reported, because the grammar allows neither (§11.1)
        report_unexpected("a property, a signal or the root element");
        // the offending unit is consumed, because the member loop would otherwise stall on it (§11.1)
        advance();
    }

    void Parser::parse_property_declaration(ComponentDeclaration& component) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        PropertyDeclaration declaration;
        // the span starts at the direction keyword, so a diagnostic points at the whole declaration (§11.1)
        declaration.span.start = peek().span.start;
        // an incoming property may only be assigned from outside (§11.1, §7)
        declaration.direction = at_keyword(INOUT_KEYWORD) ? PropertyDirection::InOut : PropertyDirection::In;
        // the direction keyword is consumed, because the caller has committed to it (§11.1)
        advance();
        // the property keyword is required, because the direction alone does not declare anything (§11.1)
        expect_keyword(PROPERTY_KEYWORD);
        // the type is read between its angles, which is where the declaration states it (§11.1)
        declaration.type = expect_type_name(true);
        // the name is required, because a binding refers to it (§11.1)
        declaration.name = expect_identifier();
        // a declared default is optional, because a property may have none (§11.1)
        if (accept_punctuation(":")) {
            // the default is an expression, so a binding may compute it (§11.1)
            declaration.default_value = parse_expression();
        }
        // the declaration ends at the terminator, which is required (§11.1)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the property is kept, because it is what a binding or a handler resolves against (§11.1)
        component.properties.push_back(std::move(declaration));
    }

    void Parser::parse_signal_declaration(ComponentDeclaration& component) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§11.1)
        SignalDeclaration declaration;
        // the span starts at the keyword, so a diagnostic points at the whole declaration (§11.1)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the name is required, because a handler invokes it (§11.1)
        declaration.name = expect_identifier();
        // the parameter list is delimited, so the types are read until it closes (§11.1)
        expect_punctuation("(");
        // the list is read until the closing parenthesis or the end of the stream (§11.1)
        while (true) {
            // the closing parenthesis ends the list, which is the normal exit (§11.1)
            if (at_punctuation(")"))
                break;
            // the end unit ends the list too, because nothing can follow it (§11.1)
            if (peek().kind == Token::Kind::End)
                break;
            // each parameter is written as a type and a name, which is how a manifest declares one (§11.2)
            const std::string type = expect_type_name(false);
            // a type that could not be read ends the list, because nothing after it can be trusted (§11.1)
            if (type.empty())
                break;
            // the type is kept, because the facade and the handler agree on it (§11.1)
            declaration.parameter_types.push_back(type);
            // a parameter name is required after its type, because a handler argument needs one (§11.2)
            const std::string parameter = expect_identifier();
            // a parameter with no name is reported, because the facade cannot emit a signature without it (§11.2)
            if (parameter.empty())
                report_unexpected("a parameter name");
            // the name is kept beside the type, because a handler body reads the raised value by it (§11.1)
            declaration.parameter_names.push_back(parameter);
            // a further parameter is preceded by a comma, which is consumed here (§11.1)
            if (!accept_punctuation(","))
                break;
        }
        // a list that never closed is reported, because the declaration is incomplete (§11.1)
        expect_punctuation(")");
        // the declaration ends at the terminator, which is required (§11.1)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.1)
        declaration.span.end = last_consumed_end();
        // the signal is kept, because it is what a handler resolves against (§11.1)
        component.signals.push_back(std::move(declaration));
    }

    void Parser::parse_command_declaration(ComponentDeclaration& component) {
        // the declaration is assembled locally, because a broken one is dropped rather than kept (§18)
        CommandDeclaration declaration;
        // the span starts at the keyword, so a diagnostic points at the whole declaration (§18)
        declaration.span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§18)
        advance();
        // the name is required, because a child binds the command by it (§18)
        declaration.name = expect_identifier();
        // a quoted label follows, which is what every consumer shows (§18)
        if (peek().kind == Token::Kind::String) {
            // the label is kept as written, because the command owns the one label (§18)
            declaration.label = peek().value;
            // the label is consumed, because it is read exactly once (§18)
            advance();
        }
        // the declaration ends at the terminator, which is required (§18)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§18)
        declaration.span.end = last_consumed_end();
        // the command is kept, because it is what a child resolves against (§18, AD-15)
        component.commands.push_back(std::move(declaration));
    }

    std::unique_ptr<TreeNode> Parser::parse_tree_node() {
        // a repeat is a tree node of its own kind, so it is read by its own rule (§11.1)
        if (at_keyword(REPEAT_KEYWORD))
            return parse_repeat();
        // a conditional is a tree node of its own kind, so it is read by its own rule (§11.1)
        if (at_keyword(IF_KEYWORD))
            return parse_conditional();
        // the node is allocated first, because the caller takes the owning pointer (§11.1)
        auto node = std::make_unique<TreeNode>();
        // the node is an element, which is the default kind (§11.1)
        node->kind = TreeNodeKind::Element;
        // the span starts at the type name, so a diagnostic points at the whole node (§11.1)
        node->span.start = peek().span.start;
        // the element type is required, because the manifest or widget catalog resolves it (§11.1)
        node->element_type = expect_identifier();
        // the body is delimited, so the members are read until it closes (§11.1)
        parse_tree_body(*node);
        // the span ends at the closing brace, which the body reader has already consumed (§11.1)
        node->span.end = last_consumed_end();
        return node;
    }

    std::unique_ptr<TreeNode> Parser::parse_repeat() {
        // the node is allocated first, because the caller takes the owning pointer (§11.1)
        auto node = std::make_unique<TreeNode>();
        // the node is a repeat, which is a tree form rather than an expression (§11.1)
        node->kind = TreeNodeKind::Repeat;
        // the span starts at the keyword, so a diagnostic points at the whole node (§11.1)
        node->span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the item list is delimited, because the bindings are declared inside it (§11.1)
        expect_punctuation("(");
        // the item name is required, because the body refers to it (§11.1)
        node->repeat_item_name = expect_identifier();
        // the binding keyword is required, because the item comes from the source (§11.1)
        expect_keyword(IN_KEYWORD);
        // the source is an expression, so it may be a property or a conversion (§11.1)
        node->repeat_source = parse_expression();
        // an index name is optional, so a body that does not need it writes none (§11.1)
        if (accept_punctuation(",")) {
            // the index name is required once the comma is written, because it is the second binding (§11.1)
            node->repeat_index_name = expect_identifier();
        }
        // the list is closed, which is required (§11.1)
        expect_punctuation(")");
        // the body is delimited, so the wrapped node is read until it closes (§11.1)
        expect_punctuation("{");
        // the body must hold exactly one element, because a repeat instantiates one node per item (§11.1)
        if (!at_punctuation("}")) {
            // the wrapped element is read, because it is what the repeat instantiates (§11.1)
            node->repeat_body = parse_tree_node();
        }
        // a body that holds nothing is reported, because a repeat must instantiate something (§11.1)
        if (node->repeat_body == nullptr)
            report_unexpected("the element to repeat");
        // a body that never closed is reported, because the node is incomplete (§11.1)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.1)
        node->span.end = last_consumed_end();
        return node;
    }

    std::unique_ptr<TreeNode> Parser::parse_conditional() {
        // the node is allocated first, because the caller takes the owning pointer (§11.1)
        auto node = std::make_unique<TreeNode>();
        // the node is a conditional, which is a tree form rather than an expression (§11.1)
        node->kind = TreeNodeKind::Conditional;
        // the span starts at the keyword, so a diagnostic points at the whole node (§11.1)
        node->span.start = peek().span.start;
        // the keyword is consumed, because the caller has already matched it (§11.1)
        advance();
        // the condition is parsed with construction suspended, because the brace that follows would open one (§11.1)
        m_allow_construction = false;
        node->condition = parse_expression();
        m_allow_construction = true;
        // the body is delimited, so the wrapped node is read until it closes (§11.1)
        expect_punctuation("{");
        // the body must hold exactly one element, because a conditional instantiates one node (§11.1)
        if (!at_punctuation("}")) {
            // the wrapped element is read, because it is what the conditional instantiates (§11.1)
            node->conditional_body = parse_tree_node();
        }
        // a body that holds nothing is reported, because a conditional must instantiate something (§11.1)
        if (node->conditional_body == nullptr)
            report_unexpected("the element to show conditionally");
        // a body that never closed is reported, because the node is incomplete (§11.1)
        expect_punctuation("}");
        // the span ends at the closing brace, which the reader has already consumed (§11.1)
        node->span.end = last_consumed_end();
        return node;
    }

    void Parser::parse_tree_body(TreeNode& node) {
        // the opening brace is required, because a node always has a body (§11.1)
        expect_punctuation("{");
        // the members run until the closing brace or the end of the stream (§11.1)
        while (!at_punctuation("}") && peek().kind != Token::Kind::End)
            parse_tree_member(node);
        // a body that never closed is reported, because the node is incomplete (§11.1)
        expect_punctuation("}");
    }

    void Parser::parse_tree_member(TreeNode& node) {
        // an identifier followed by a colon is either a property assignment or a handler (§11.1)
        if (peek().kind == Token::Kind::Identifier && at_punctuation(":", 1)) {
            // a body opens with a brace, so a member whose value is a brace is a handler or a draw block (§11.1)
            if (at_punctuation("{", 2)) {
                // the draw keyword selects the drawing block, which is how a canvas declares its content (§6.4)
                if (m_tokens[m_index].text == "draw")
                    parse_draw_block(node);
                else
                    parse_handler(node);
                return;
            }
            // anything else takes a value, so it is a property assignment (§11.1)
            parse_property_assignment(node);
            return;
        }
        // anything else that starts a unit is a child element or a nested repeat (§11.1)
        if (peek().kind == Token::Kind::Identifier) {
            // the child is read and appended, because the tree is built as the body is read (§11.1)
            node.children.push_back(parse_tree_node());
            return;
        }
        // an at sign starts the children slot, which is where a derived component's content lands (§11.1)
        if (peek().kind == Token::Kind::Macro) {
            // only the children macro marks a slot, because every other macro belongs to an expression (§11.1)
            if (peek().text == "children") {
                // the node is allocated first, because the caller takes the owning pointer (§11.1)
                auto slot = std::make_unique<TreeNode>();
                // the kind marks the content point, which the checker splices a derived body into (§11.1)
                slot->kind = TreeNodeKind::Slot;
                // the span covers the marker, because a diagnostic about the slot points at it (§11.1)
                slot->span = peek().span;
                // the marker is consumed, because the node now carries it (§11.1)
                advance();
                // the slot is kept among the children, because that is the position it marks (§11.1)
                node.children.push_back(std::move(slot));
                return;
            }
            // a macro that marks nothing here is reported, because the member position starts nothing else (§11.1)
            report_unexpected("a property, a handler, a child element or @children");
            // the offending unit is consumed, because the member loop would otherwise stall on it (§11.1)
            advance();
            return;
        }
        // a unit that starts nothing is reported, because the grammar allows nothing else here (§11.1)
        report_unexpected("a property, a handler or a child element");
        // the stray unit is consumed, because the loop would otherwise stall on it (§11.1)
        advance();
    }

    void Parser::parse_property_assignment(TreeNode& node) {
        // the assignment is assembled locally, because the identifier decides which field it fills (§11.1)
        PropertyAssignment assignment;
        // the span starts at the name, so a diagnostic points at the whole assignment (§11.1)
        assignment.span.start = peek().span.start;
        // the name is required, because it names the property being assigned (§11.1)
        assignment.name = expect_identifier();
        // the colon is required, because an assignment is written with one (§11.1)
        expect_punctuation(":");
        // the author identity is a bare name rather than a binding, so it is taken as written (§11.1, AD-11)
        if (assignment.name == ID_KEYWORD) {
            // the identity is required to be an identifier, because automation selects by it (§11.1, §18.1)
            if (peek().kind != Token::Kind::Identifier) {
                // a computed identity is reported, because it would not survive live reload (§11.1, AD-11)
                m_diagnostics.add(Diagnostic::Severity::Error, peek().span, "E0200", "an id must be a bare name, not a binding");
            }
            else {
                // the identity is moved out of the unit, because the parser owns the stream (§11.1, §18.1)
                node.explicit_id = std::move(m_tokens[m_index].text);
                // the named unit is consumed, because the identity has been read (§11.1, §18.1)
                advance();
            }
            // the assignment still ends at the terminator, which is required (§11.1)
            expect_punctuation(";");
            return;
        }
        // any other property takes a value, which is a literal or a binding (§11.1)
        assignment.value = parse_expression();
        // the assignment ends at the terminator, which is required (§11.1)
        expect_punctuation(";");
        // the span ends at the terminator, which the reader has already consumed (§11.1)
        assignment.span.end = last_consumed_end();
        // the assignment is kept, because it is what the checker resolves against the element type (§11.1)
        node.assignments.push_back(std::move(assignment));
    }

    void Parser::parse_draw_block(TreeNode& node) {
        // the draw keyword is consumed first, because the colon and the body follow it (§6.4)
        advance();
        // the colon is required, because the block is written with one (§6.4)
        expect_punctuation(":");
        // the opening brace is required, because the block is a body (§6.4)
        expect_punctuation("{");
        // the calls run until the closing brace, because the block is a closed sequence (§6.4)
        while (!at_punctuation("}") && peek().kind != Token::Kind::End) {
            // the call is assembled locally, because one mistake must not hide the rest (§6.4)
            DrawCall call;
            // the span starts at the name, so a diagnostic points at the whole call (§11.1)
            call.span.start = peek().span.start;
            // a primitive is a bare name, because the vocabulary is closed (§6.4)
            if (peek().kind != Token::Kind::Identifier) {
                // anything else is reported, because no call can start with it (§6.4)
                report_unexpected("a drawing primitive");
                advance();
                continue;
            }
            // the name decides which primitive is called, so it is read as written (§6.4)
            call.primitive = expect_identifier();
            // the argument list is parenthesised, which is how every call in the language reads (§11.1)
            expect_punctuation("(");
            // the arguments are read by the shared list, because a call and an invocation are written alike (§11.1)
            call.arguments = parse_arguments();
            // the terminator is required, because statements are separated (§11.1)
            expect_punctuation(";");
            // the span ends at the terminator, which the reader has already consumed (§11.1)
            call.span.end = last_consumed_end();
            // the call is kept, because the checker matches it against the vocabulary (§6.4)
            node.draw_calls.push_back(std::move(call));
        }
        // the closing brace is required, because the block is a body (§6.4)
        expect_punctuation("}");
        // the node is marked, because a canvas is the only element that draws (§6.4)
        node.has_draw = true;
    }

    void Parser::parse_handler(TreeNode& node) {
        // the attachment is assembled locally, because the body is read into it (§11.1)
        HandlerAttachment attachment;
        // the span starts at the name, so a diagnostic points at the whole handler (§11.1)
        attachment.span.start = peek().span.start;
        // the written name is required, because a handler is written as on_signal (§11.1)
        std::string written = expect_identifier();
        // the prefix marks the member as a handler rather than a value, and it is not part of the signal name (§11.1)
        const bool prefixed = written.rfind(ON_PREFIX, 0) == 0;
        // a name without the prefix is reported, because the language has no other way to spell a handler (§11.1)
        if (!prefixed)
            m_diagnostics.add(Diagnostic::Severity::Error, attachment.span, "E0204", "a handler is written as on_<signal>");
        // the signal name is what follows the prefix, which is what the element actually raises (§11.1)
        attachment.name = prefixed ? written.substr(ON_PREFIX.size()) : std::move(written);
        // the colon is required, because an attachment is written with one (§11.1)
        expect_punctuation(":");
        // the body is delimited, so the statements are read until it closes (§11.1)
        expect_punctuation("{");
        // the body is read through the shared rule, because it has the same two statement forms everywhere (§11.1)
        parse_handler_body(attachment.body);
        // the span ends at the closing brace, which the body reader has already consumed (§11.1)
        attachment.span.end = last_consumed_end();
        // the handler is kept, because it is what the codegen turns into a callback (§11.1)
        node.handlers.push_back(std::move(attachment));
    }

    void Parser::parse_handler_body(std::vector<HandlerStatement>& body) {
        // the statements run until the closing brace or the end of the stream (§11.1)
        while (true) {
            // the closing brace ends the body, which is the normal exit (§11.1)
            if (at_punctuation("}"))
                break;
            // the end unit ends the body too, because a statement cannot follow it (§11.1)
            if (peek().kind == Token::Kind::End)
                break;
            // the offset before the statement is kept, because a malformed one must still make progress (§11.1)
            const std::size_t before = m_index;
            // the statement starts at the target name, which decides which form it is (§11.1)
            const SourceLocation start = peek().span.start;
            // the target name is required, because a statement always names something (§11.1)
            std::string target = expect_identifier();
            // the statement is assembled locally, because the punctuation decides its form (§11.1)
            HandlerStatement statement;
            // the span starts at the target, so a diagnostic points at the whole statement (§11.1)
            statement.span.start = start;
            // the target is moved into the statement, because the parser owns the stream (§11.1)
            statement.target = std::move(target);
            // a dot after the target reaches a field or a built-in method on a collection property (§11.1)
            if (at_punctuation(".")) {
                // the dot is consumed, because the name after it follows (§11.1)
                advance();
                // the name after the dot is required, because a trailing dot names nothing (§11.1)
                const std::string name = expect_identifier();
                // an opening parenthesis is a built-in method call on a collection property (§11.1)
                if (accept_punctuation("(")) {
                    // the statement is a property method, which mutates the collection in place (§11.1)
                    statement.kind = HandlerStatementKind::PropertyMethod;
                    // the method name is kept, because the checker resolves it against the closed set (§11.1)
                    statement.method = name;
                    // the arguments are read as a list, which may be empty for clear (§11.1)
                    statement.arguments = parse_arguments();
                }
                // an equals sign is an assignment to one field of a record property (§11.1)
                else if (accept_punctuation("=")) {
                    // the statement is a field write, which mutates the record in place (§11.1)
                    statement.kind = HandlerStatementKind::FieldAssignment;
                    // the field name is kept, because the checker resolves it against the record (§11.1)
                    statement.field = name;
                    // the assigned value is an expression, so it may compute anything a binding may (§11.1)
                    statement.value = parse_expression();
                }
                else {
                    // a dot followed by neither form is reported, because the body has only these (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = peek().span.start}, "E0203", "expected '(' to call a collection method or '=' to assign a field");
                }
            }
            // an assignment is written with an equals sign (§11.1)
            else if (accept_punctuation("=")) {
                // the statement is an assignment, which the checker resolves against the properties (§11.1)
                statement.kind = HandlerStatementKind::Assignment;
                // the assigned value is an expression, so it may compute anything a binding may (§11.1)
                statement.value = parse_expression();
            }
            else if (accept_punctuation("(")) {
                // the statement is a signal invocation, which carries arguments (§11.1)
                statement.kind = HandlerStatementKind::SignalInvocation;
                // the arguments are read as a list, which may be empty (§11.1)
                statement.arguments = parse_arguments();
            }
            else {
                // a statement that is neither form is reported, because the body has only these three (§11.1)
                m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = peek().span.start}, "E0201", "a handler body may only assign a property, invoke a signal or call a collection method");
            }
            // the statement ends at the terminator, which is required (§11.1)
            expect_punctuation(";");
            // the span ends at the terminator, which the reader has already consumed (§11.1)
            statement.span.end = last_consumed_end();
            // the statement is kept, because the codegen emits it as written (§11.1)
            body.push_back(std::move(statement));
            // a statement that consumed nothing would leave the loop on the same unit forever (§11.1)
            if (m_index == before)
                advance();
        }
        // a body that never closed is reported, because the handler is incomplete (§11.1)
        expect_punctuation("}");
    }

    std::vector<ExpressionPtr> Parser::parse_arguments() {
        // the arguments are collected as they are read, because the count is not known in advance (§11.1)
        std::vector<ExpressionPtr> arguments;
        // the list is read until the closing parenthesis or the end of the stream (§11.1)
        while (true) {
            // the closing parenthesis ends the list, which is the normal exit (§11.1)
            if (at_punctuation(")"))
                break;
            // the end unit ends the list too, because nothing can follow it (§11.1)
            if (peek().kind == Token::Kind::End)
                break;
            // each argument is an expression, so it follows the same precedence as a binding (§11.1)
            arguments.push_back(parse_expression());
            // a further argument is preceded by a comma, which is consumed here (§11.1)
            if (!accept_punctuation(","))
                break;
        }
        // a list that never closed is reported, because the statement is incomplete (§11.1)
        expect_punctuation(")");
        return arguments;
    }

    ExpressionPtr Parser::parse_expression() {
        // the loosest binding group is the entry point, so a binding value parses whole (§11.1)
        return parse_disjunction();
    }

    ExpressionPtr Parser::parse_disjunction() {
        // the left operand is parsed first, because the operator follows it (§11.1)
        ExpressionPtr left = parse_conjunction();
        // further operands continue while a disjunction operator follows (§11.1)
        while (at_punctuation("||")) {
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the right operand binds tighter, so the group nests to the left (§11.1)
            ExpressionPtr right = parse_conjunction();
            // the span is taken before the operands move, because a moved pointer no longer names them (§11.1)
            const SourceSpan span{.start = left->location().start, .end = right->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            left = BinaryExpression::make(BinaryOperator::Or, std::move(left), std::move(right), span);
        }
        return left;
    }

    ExpressionPtr Parser::parse_conjunction() {
        // the left operand is parsed first, because the operator follows it (§11.1)
        ExpressionPtr left = parse_equality();
        // further operands continue while a conjunction operator follows (§11.1)
        while (at_punctuation("&&")) {
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the right operand binds tighter, so the group nests to the left (§11.1)
            ExpressionPtr right = parse_equality();
            // the span is taken before the operands move, because a moved pointer no longer names them (§11.1)
            const SourceSpan span{.start = left->location().start, .end = right->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            left = BinaryExpression::make(BinaryOperator::And, std::move(left), std::move(right), span);
        }
        return left;
    }

    ExpressionPtr Parser::parse_equality() {
        // the left operand is parsed first, because the operator follows it (§11.1)
        ExpressionPtr left = parse_comparison();
        // the loop handles one operator per iteration, because the operators do not associate (§11.1)
        while (at_punctuation("==") || at_punctuation("!=")) {
            // the operator spelling decides which form this is (§11.1)
            const bool equal = at_punctuation("==");
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the right operand binds tighter, so the group nests to the left (§11.1)
            ExpressionPtr right = parse_comparison();
            // the span is taken before the operands move, because a moved pointer no longer names them (§11.1)
            const SourceSpan span{.start = left->location().start, .end = right->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            left = BinaryExpression::make(equal ? BinaryOperator::Equal : BinaryOperator::NotEqual, std::move(left), std::move(right), span);
        }
        return left;
    }

    ExpressionPtr Parser::parse_comparison() {
        // the left operand is parsed first, because the operator follows it (§11.1)
        ExpressionPtr left = parse_additive();
        // the loop handles one operator per iteration, because the operators do not associate (§11.1)
        while (at_punctuation("<=") || at_punctuation(">=") || at_punctuation("<") || at_punctuation(">")) {
            // the operator spelling decides which form this is, longest match first (§11.1)
            BinaryOperator op = BinaryOperator::Less;
            // the two character form is checked first, because it starts with the one character form (§11.1)
            if (at_punctuation("<=")) {
                op = BinaryOperator::LessEqual;
            }
            else if (at_punctuation(">=")) {
                op = BinaryOperator::GreaterEqual;
            }
            else if (at_punctuation("<")) {
                op = BinaryOperator::Less;
            }
            else {
                op = BinaryOperator::Greater;
            }
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the right operand binds tighter, so the group nests to the left (§11.1)
            ExpressionPtr right = parse_additive();
            // the span is taken before the operands move, because a moved pointer no longer names them (§11.1)
            const SourceSpan span{.start = left->location().start, .end = right->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            left = BinaryExpression::make(op, std::move(left), std::move(right), span);
        }
        return left;
    }

    ExpressionPtr Parser::parse_additive() {
        // the left operand is parsed first, because the operator follows it (§11.1)
        ExpressionPtr left = parse_multiplicative();
        // the loop continues while an additive operator follows, because both forms associate left (§11.1)
        while (at_punctuation("+") || at_punctuation("-")) {
            // the operator spelling decides which form this is (§11.1)
            const bool adding = at_punctuation("+");
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the right operand binds tighter, so the group nests to the left (§11.1)
            ExpressionPtr right = parse_multiplicative();
            // the span is taken before the operands move, because a moved pointer no longer names them (§11.1)
            const SourceSpan span{.start = left->location().start, .end = right->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            left = BinaryExpression::make(adding ? BinaryOperator::Add : BinaryOperator::Subtract, std::move(left), std::move(right), span);
        }
        return left;
    }

    ExpressionPtr Parser::parse_multiplicative() {
        // the left operand is parsed first, because the operator follows it (§11.1)
        ExpressionPtr left = parse_unary();
        // the loop continues while a multiplicative operator follows, because both forms associate left (§11.1)
        while (at_punctuation("*") || at_punctuation("/") || at_punctuation("%")) {
            // the operator spelling decides which form this is (§11.1)
            BinaryOperator op = BinaryOperator::Multiply;
            // the first two forms are multiplication and division, which the grammar spells directly (§11.1)
            if (at_punctuation("/")) {
                op = BinaryOperator::Divide;
            }
            else if (at_punctuation("%")) {
                op = BinaryOperator::Modulo;
            }
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the right operand binds tighter, so the group nests to the left (§11.1)
            ExpressionPtr right = parse_unary();
            // the span is taken before the operands move, because a moved pointer no longer names them (§11.1)
            const SourceSpan span{.start = left->location().start, .end = right->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            left = BinaryExpression::make(op, std::move(left), std::move(right), span);
        }
        return left;
    }

    ExpressionPtr Parser::parse_unary() {
        // a negation binds tighter than any binary operator, so it is read before them (§11.1)
        if (at_punctuation("!")) {
            // the span starts at the operator, so a diagnostic covers the whole application (§11.1)
            const SourceSpan start = peek().span;
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the operand is read at the same level, so a second negation nests to the right (§11.1)
            ExpressionPtr value = parse_unary();
            // the span is taken before the operand moves, because a moved pointer no longer names it (§11.1)
            const SourceSpan span{.start = start.start, .end = value->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            return UnaryExpression::make(UnaryOperator::Not, std::move(value), span);
        }
        // a numeric negation is the other prefix form (§11.1)
        if (at_punctuation("-")) {
            // the span starts at the operator, so a diagnostic covers the whole application (§11.1)
            const SourceSpan start = peek().span;
            // the operator is consumed, because the caller has committed to it (§11.1)
            advance();
            // the operand is read at the same level, so a second negation nests to the right (§11.1)
            ExpressionPtr value = parse_unary();
            // the span is taken before the operand moves, because a moved pointer no longer names it (§11.1)
            const SourceSpan span{.start = start.start, .end = value->location().end};
            // the application is assembled, so the codegen re-emits the written shape (§11.1)
            return UnaryExpression::make(UnaryOperator::Negate, std::move(value), span);
        }
        // a primary is the value, and a field read is a postfix on it, so both are read here (§11.2)
        ExpressionPtr value = parse_primary();
        // a dot after a value reads one of its fields, which is how a repeat body reaches its item (§11.2, AD-17)
        while (at_punctuation(".")) {
            // the dot is consumed, because the field name follows it (§11.2)
            advance();
            // the field name is required, because a trailing dot names nothing (§11.2)
            const std::string field = peek().kind == Token::Kind::Identifier ? std::string(peek().text) : std::string();
            // the name unit is consumed either way, because the parser owns the stream and must move on (§11.2)
            advance();
            // the span runs from the object through the name, because that is what a diagnostic covers (§11.2)
            const SourceSpan span{.start = value->location().start, .end = last_consumed_end()};
            // the access is assembled, so the codegen re-emits the written shape (§11.2)
            value = FieldAccessExpression::make(std::move(value), field, span);
        }
        return value;
    }

    ExpressionPtr Parser::parse_primary() {
        // an integer literal is a value with no operands (§11.1)
        if (peek().kind == Token::Kind::Integer) {
            // the spelling is moved out of the unit, because the parser owns the stream (§11.1)
            ExpressionPtr literal = LiteralExpression::make(LiteralExpression::Kind::Integer, std::move(m_tokens[m_index].text), m_tokens[m_index].span);
            // the unit is consumed, because the value has been read (§11.1)
            advance();
            return literal;
        }
        // a floating point literal is a value with no operands (§11.1)
        if (peek().kind == Token::Kind::Float) {
            // the spelling is moved out of the unit, because the parser owns the stream (§11.1)
            ExpressionPtr literal = LiteralExpression::make(LiteralExpression::Kind::Float, std::move(m_tokens[m_index].text), m_tokens[m_index].span);
            // the unit is consumed, because the value has been read (§11.1)
            advance();
            return literal;
        }
        // a string literal is a value with no operands (§11.1)
        if (peek().kind == Token::Kind::String) {
            // the spelling is moved out of the unit, because the parser owns the stream (§11.1)
            ExpressionPtr literal = LiteralExpression::make(LiteralExpression::Kind::String, std::move(m_tokens[m_index].text), m_tokens[m_index].span);
            // the unit is consumed, because the value has been read (§11.1)
            advance();
            return literal;
        }
        // a colour literal is a value with no operands (§11.1)
        if (peek().kind == Token::Kind::Color) {
            // the spelling is moved out of the unit, because the parser owns the stream (§11.1)
            ExpressionPtr literal = LiteralExpression::make(LiteralExpression::Kind::Color, std::move(m_tokens[m_index].text), m_tokens[m_index].span);
            // the unit is consumed, because the value has been read (§11.1)
            advance();
            return literal;
        }
        // a length literal is a value whose unit the checker resolves against the declared type (§11.1, AD-27)
        if (peek().kind == Token::Kind::Length) {
            // the spelling is moved out of the unit, because the parser owns the stream (§11.1)
            ExpressionPtr literal = LiteralExpression::make(LiteralExpression::Kind::Length, std::move(m_tokens[m_index].text), m_tokens[m_index].span);
            // the unit is consumed, because the value has been read (§11.1)
            advance();
            return literal;
        }
        // a translatable string is a macro, which has its own rule (§11.1, §9.2)
        if (peek().kind == Token::Kind::Macro && peek().text == TR_MACRO)
            return parse_translation();
        // an embedded image is a macro as well, because its path is resolved at build time (§6.4, §11.1)
        if (peek().kind == Token::Kind::Macro && peek().text == IMAGE_MACRO)
            return parse_image();
        // an opening parenthesis starts a grouping (§11.1)
        if (at_punctuation("(")) {
            // the opening parenthesis is consumed, so the inner expression starts after it (§11.1)
            advance();
            // the inner expression follows the full precedence, so parentheses group anything (§11.1)
            ExpressionPtr inner = parse_expression();
            // the span is taken before the inner expression moves, because a moved pointer no longer names it (§11.1)
            const SourceSpan span{.start = inner->location().start, .end = peek().span.start};
            // the closing parenthesis is required, because a grouping is delimited (§11.1)
            expect_punctuation(")");
            // the grouping is kept, so the codegen can reproduce the written parentheses (§11.1)
            return GroupingExpression::make(std::move(inner), span);
        }
        // an opening bracket starts a collection literal, which is the only way an author writes one (§8.1)
        if (at_punctuation("["))
            return parse_array();
        // a truth literal is spelled as an identifier, so it is recognised here (§11.1)
        if (peek().is("true") || peek().is("false")) {
            // the spelling is copied rather than moved, because the truth check needs the unit afterwards (§11.1)
            const std::string text = peek().text;
            // the span is taken from the unit, because the literal covers exactly it (§11.1)
            const SourceSpan span = peek().span;
            // the unit is consumed, because the value has been read (§11.1)
            advance();
            return LiteralExpression::make(LiteralExpression::Kind::Boolean, std::move(text), span);
        }
        // a conversion and a colour operation are the only call forms a binding may use (§11.1)
        if (peek().kind == Token::Kind::Identifier && at_punctuation("(", 1) && is_builtin_call(peek().text))
            return parse_conversion();
        // an identifier followed by a paren that is not a builtin is a call to a declared function (§11.1)
        if (peek().kind == Token::Kind::Identifier && at_punctuation("(", 1)) {
            // the span starts at the function name, so a diagnostic covers the whole call (§11.1)
            const SourceSpan span = peek().span;
            // the function name is moved out of the unit, because the parser owns the stream (§11.1)
            std::string target = std::move(m_tokens[m_index].text);
            // the name is consumed, so the argument list starts after it (§11.1)
            advance();
            // the argument list is delimited, so the values are read until it closes (§11.1)
            expect_punctuation("(");
            // the arguments travel in written order, because the call reads them left to right (§11.1)
            std::vector<ExpressionPtr> arguments;
            // an empty argument list is legal, because a function may take no parameters (§11.1)
            if (!at_punctuation(")")) {
                while (true) {
                    // each argument is a full expression, so a call may pass a computed value (§11.1)
                    arguments.push_back(parse_expression());
                    // the separator ends the argument unless the list is already closed (§11.1)
                    if (!at_punctuation(","))
                        break;
                    // the separator is consumed, so the next argument starts after it (§11.1)
                    advance();
                }
            }
            // the list is closed, which is required (§11.1)
            expect_punctuation(")");
            return CallExpression::make(std::move(target), std::move(arguments), span);
        }
        // an identifier followed by a brace is a record construction, which is how an author writes one (§11.1)
        if (peek().kind == Token::Kind::Identifier && at_punctuation("{", 1) && m_allow_construction) {
            // the span starts at the type name, so a diagnostic covers the whole construction (§11.1)
            const SourceSpan start = peek().span;
            // the type name is moved out of the unit, because the parser owns the stream (§11.1)
            std::string type_name = std::move(m_tokens[m_index].text);
            // the name is consumed, so the brace that opens the field list follows it (§11.1)
            advance();
            // the brace opens the field list, which is delimited the way an argument list is (§11.1)
            expect_punctuation("{");
            // the fields travel in written order, because the record resolves them by name (§11.1)
            std::vector<ConstructionField> fields;
            // an empty field list is legal, because every field then keeps its default (§11.1)
            if (!at_punctuation("}")) {
                while (true) {
                    // each field is a name, a colon and a value, which pairs the declared field with it (§11.1)
                    ConstructionField field;
                    // the name is required, because the value binds through it (§11.1)
                    field.name = expect_identifier();
                    // the colon separates the name from the value, which is how every value binds (§11.1)
                    expect_punctuation(":");
                    // the value follows the full precedence, so a field holds any value (§11.1)
                    field.value = parse_expression();
                    fields.push_back(std::move(field));
                    // the separator ends the field unless the list is already closed (§11.1)
                    if (!at_punctuation(","))
                        break;
                    // the separator is consumed, so the next field starts after it (§11.1)
                    advance();
                    // a separator directly before the brace ends the list, which is the trailing comma (§11.1)
                    if (at_punctuation("}"))
                        break;
                }
            }
            // the brace closes the list, which is required (§11.1)
            expect_punctuation("}");
            // the span ends at the brace, so the construction covers exactly what was written (§11.1)
            const SourceSpan span{.start = start.start, .end = last_consumed_end()};
            return RecordConstructionExpression::make(std::move(type_name), std::move(fields), span);
        }
        // an identifier is a reference to a declared property (§11.1)
        if (peek().kind == Token::Kind::Identifier) {
            // the name is moved out of the unit, because the parser owns the stream (§11.1)
            ExpressionPtr reference = PropertyExpression::make(std::move(m_tokens[m_index].text), m_tokens[m_index].span);
            // the unit is consumed, because the reference has been read (§11.1)
            advance();
            return reference;
        }
        // nothing that starts a value is left, so a placeholder carries the span and the report (§11.1)
        report_unexpected("a value");
        // the span is taken before the consume, because the placeholder covers the offending unit (§11.1)
        const SourceSpan span = peek().span;
        // the unit is consumed, so the enclosing loop terminates rather than reporting the same unit forever (§11.1)
        advance();
        // the placeholder is a literal, because the caller must receive something it can free (§11.1)
        return LiteralExpression::make(LiteralExpression::Kind::Integer, "", span);
    }

    ExpressionPtr Parser::parse_array() {
        // the span starts at the bracket, so a diagnostic about the list covers it (§8.1)
        const SourceSpan start = peek().span;
        // the opening bracket is consumed, so the first element starts after it (§8.1)
        advance();
        // the elements are collected in written order, which is the order the collection preserves (§8.1)
        std::vector<ExpressionPtr> elements;
        // an empty bracket pair is an empty collection, which is legal and needs no element (§8.1)
        if (!at_punctuation("]")) {
            // a trailing separator after the last element is allowed, because an author writes lists that way (§8.1)
            while (true) {
                // each element follows the full precedence, so a list holds any value (§8.1)
                elements.push_back(parse_expression());
                // the separator ends the element unless the list is already closed (§8.1)
                if (!at_punctuation(","))
                    break;
                // the separator is consumed, so the next element starts after it (§8.1)
                advance();
                // a separator directly before the bracket ends the list, which is the trailing comma (§8.1)
                if (at_punctuation("]"))
                    break;
            }
        }
        // the span ends at the bracket, so the list covers exactly what was written (§8.1)
        const SourceSpan span{.start = start.start, .end = peek().span.end};
        // the closing bracket is required, because a list is delimited (§8.1)
        expect_punctuation("]");
        return ArrayExpression::make(std::move(elements), span);
    }

    ExpressionPtr Parser::parse_translation() {
        // the span starts at the macro unit, so the catalog records the call site (§11.1, §9.2)
        const SourceSpan span = peek().span;
        // the macro unit is consumed, so the argument starts after it (§11.1, §9.2)
        advance();
        // the argument list is delimited, so the literal is read until it closes (§11.1, §9.2)
        expect_punctuation("(");
        // a translatable string takes one literal, because a key with holes is written with concatenation (§11.1, §9.2)
        ExpressionPtr argument = parse_expression();
        // the list is closed, which is required (§11.1, §9.2)
        expect_punctuation(")");
        // only a literal may be translated, because a computed value has no source text (§11.1, §9.2)
        const LiteralExpression* literal = dynamic_cast<const LiteralExpression*>(argument.get());
        // a macro with an empty name never reaches here, because the scanner reported it (§11.1, §9.2)
        if (literal == nullptr) {
            // a computed argument is reported, because there is no string to put in a catalog (§11.1, §9.2)
            m_diagnostics.add(Diagnostic::Severity::Error, span, "E0202", "@tr takes a string literal");
            // the computed argument is still returned, so the enclosing expression stays well formed (§11.1)
            return argument;
        }
        return TranslationExpression::make(literal->text, span);
    }

    ExpressionPtr Parser::parse_image() {
        // the span starts at the macro unit, so a diagnostic covers the whole call (§11.1)
        const SourceSpan span = peek().span;
        // the macro unit is consumed, so the argument starts after it (§11.1)
        advance();
        // the argument list is delimited, so the path is read until it closes (§11.1)
        expect_punctuation("(");
        // the path is a full expression, which the literal check below then narrows (§11.1)
        ExpressionPtr argument = parse_expression();
        // the list is closed, which is required (§11.1)
        expect_punctuation(")");
        // only a literal names an asset at build time, because a computed path cannot be embedded (§6.4)
        const LiteralExpression* literal = dynamic_cast<const LiteralExpression*>(argument.get());
        // a non-literal path is reported, because the generator cannot read an expression (§6.4)
        if (literal == nullptr || literal->kind != LiteralExpression::Kind::String) {
            // the problem names the rule, because that is what the author must change (§6.4)
            m_diagnostics.add(Diagnostic::Severity::Error, span, "E0205", "@image takes a string literal path");
            // the argument is still returned, so the enclosing expression stays well formed (§11.1)
            return argument;
        }
        return ImageExpression::make(literal->text, span);
    }

    ExpressionPtr Parser::parse_conversion() {
        // the span starts at the builtin name, so a diagnostic covers the whole call (§11.1)
        const SourceSpan span = peek().span;
        // the builtin name is moved out of the unit, because the parser owns the stream (§11.1)
        std::string target = std::move(m_tokens[m_index].text);
        // a colour operation is the only builtin that takes a second argument (§11.1)
        const bool colour = is_colour_operation(target);
        // the name is consumed, so the argument list starts after it (§11.1)
        advance();
        // the argument list is delimited, so the values are read until it closes (§11.1)
        expect_punctuation("(");
        // the first value is an expression, so a builtin may wrap a computed value (§11.1)
        ExpressionPtr value = parse_expression();
        // a conversion and a colour operation that both take one value keep the older node (§11.1)
        if (!colour) {
            // the list is closed, which is required (§11.1)
            expect_punctuation(")");
            return ConversionExpression::make(std::move(target), std::move(value), span);
        }
        // a colour operation takes the fraction or alpha it works by as its second argument (§6.1)
        expect_punctuation(",");
        // the second value is an expression, so a bound fraction may drive the operation (§6.1)
        ExpressionPtr amount = parse_expression();
        // the list is closed, which is required (§11.1)
        expect_punctuation(")");
        // the arguments travel in written order, because the operation reads them left to right (§11.1)
        std::vector<ExpressionPtr> arguments;
        // the colour is the first operand of every operation (§6.1)
        arguments.push_back(std::move(value));
        // the amount is the second, because it is what the operation works by (§6.1)
        arguments.push_back(std::move(amount));
        return CallExpression::make(std::move(target), std::move(arguments), span);
    }

    void Parser::report_unexpected(std::string_view expected) {
        // the problem points at the current unit, so a reader sees exactly what was found (§11.1)
        const std::string found = peek().kind == Token::Kind::End ? std::string("the end of the file") : peek().text;
        // the report names what was expected and what was found, because both are needed to fix it (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, peek().span, "E0203", "expected " + std::string(expected) + " but found '" + found + "'");
    }

    void Parser::recover_to_declaration() {
        // the nesting depth is tracked, because a declaration keyword inside braces is not a boundary (§11.1)
        std::size_t depth = 0;
        // the scan runs until a boundary or the end of the stream (§11.1)
        while (peek().kind != Token::Kind::End) {
            // a closing brace closes the innermost body, which the depth records (§11.1)
            if (at_punctuation("}")) {
                // the brace is consumed at any depth, because the scan has to keep making progress (§11.1)
                advance();
                // a brace inside a body only changes the depth, so the scan continues (§11.1)
                if (depth == 0)
                    continue;
                // the depth drops, because the body it opened has closed (§11.1)
                depth--;
                continue;
            }
            // an opening brace opens a body, which the depth records (§11.1)
            if (at_punctuation("{")) {
                // the brace is consumed, because the depth has to follow it (§11.1)
                advance();
                // the depth rises, so a declaration keyword inside the body is not a boundary (§11.1)
                depth++;
                continue;
            }
            // a declaration keyword at the top level is the boundary the scan is looking for (§11.1)
            if (depth != 0) {
                // the unit belongs to the broken body, so it is skipped rather than treated as a boundary (§11.1)
                advance();
                continue;
            }
            // the keywords are checked at the top level only, because a nested one is part of the broken body (§11.1)
            if (at_keyword(IMPORT_KEYWORD) || at_keyword(STRUCT_KEYWORD) || at_keyword(ENUM_KEYWORD) || at_keyword(FN_KEYWORD) || at_keyword(COMPONENT_KEYWORD)) {
                // the scan stops before the keyword, so the next declaration is parsed from its start (§11.1)
                return;
            }
            // any other unit is part of the broken construct, so it is skipped (§11.1)
            advance();
        }
    }
} // namespace albor::compiler
