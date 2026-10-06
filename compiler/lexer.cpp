#include "compiler/lexer.h"

#include <cctype>
#include <optional>
#include <string_view>

namespace albor::compiler
{
    bool Token::is(std::string_view keyword) const {
        // only an identifier can spell a keyword, so a literal never matches (§11.1)
        return kind == Kind::Identifier && text == keyword;
    }

    bool Token::is_punctuation(std::string_view symbol) const {
        // only punctuation can spell an operator, so a literal never matches (§11.1)
        return kind == Kind::Punctuation && text == symbol;
    }

    Lexer::Lexer(std::string_view source, DiagnosticList& diagnostics) :
        m_source(source), m_diagnostics(diagnostics) {}

    std::vector<Token> Lexer::tokenize() {
        // the units come out in order and the end unit closes the list (§11.1)
        std::vector<Token> tokens;
        // the scan runs until the end unit closes the list, so a parser always has a terminator (§11.1)
        while (true) {
            // the unit is appended as it was scanned, so the caller sees the spelling (§11.1)
            Token unit = scan();
            // the end unit is appended too, because a parser looks for it rather than for the list length (§11.1)
            tokens.push_back(unit);
            // the end unit closes the list, so nothing follows it (§11.1)
            if (unit.kind == Token::Kind::End)
                return tokens;
        }
    }

    SourceLocation Lexer::position() const {
        // the current line and column are the position, tracked as the scan advances (§11.1)
        return SourceLocation{.line = m_line, .column = m_column};
    }

    void Lexer::skip_trivia() {
        // the trivia loop runs until a unit character appears or the buffer ends (§11.1)
        while (!at_end()) {
            // the character decides what kind of trivia it is (§11.1)
            const char ahead = peek();
            // a space, a tab or a newline separates units and carries no meaning (§11.1)
            if (ahead == ' ' || ahead == '\t' || ahead == '\r' || ahead == '\n') {
                // the separator is consumed so the position moves past it (§11.1)
                advance();
                continue;
            }
            // a line comment runs to the end of its line (§11.1)
            if (ahead == '/' && peek(1) == '/') {
                // the comment is consumed up to but not including the newline (§11.1)
                while (!at_end() && peek() != '\n')
                    advance();
                continue;
            }
            // a block comment runs to its terminator and may span lines (§11.1)
            if (ahead == '/' && peek(1) == '*') {
                // the position of the opening is kept, because an unterminated one is reported against it (§11.1)
                const SourceLocation opening = position();
                // the opening delimiter is consumed (§11.1)
                advance();
                advance();
                // the scan continues until the terminator or the end of the buffer (§11.1)
                while (!at_end()) {
                    // the closing pair ends the comment (§11.1)
                    if (peek() == '*' && peek(1) == '/') {
                        advance();
                        advance();
                        break;
                    }
                    advance();
                }
                // a comment that reached the end was never closed, which the source cannot recover from (§11.1)
                if (at_end())
                    m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = opening, .end = position()}, "E0100", "unterminated block comment");
                continue;
            }
            // anything else starts a unit, so the trivia is over (§11.1)
            return;
        }
    }

    Token Lexer::scan() {
        // the scan keeps going until a unit is produced, because a bad character is skipped rather than returned (§11.1)
        while (true) {
            // the trivia before each unit is skipped, so a skipped character does not stall the scan (§11.1)
            skip_trivia();
            // the end of the buffer leaves nothing to scan, so the caller closes the list (§11.1)
            if (at_end())
                return Token{.kind = Token::Kind::End, .text = {}, .value = {}, .span = SourceSpan{.start = position(), .end = position()}};
            // the position of the first character is kept, because a span covers the whole unit (§11.1)
            const SourceLocation start = position();
            // the character decides which unit is scanned (§11.1)
            const char ahead = peek();
            // a digit starts a number, which may carry a unit or be hexadecimal (§11.1)
            if (std::isdigit(static_cast<unsigned char>(ahead)) != 0)
                return scan_number();
            // a quote starts a string (§11.1)
            if (ahead == '"')
                return scan_string();
            // a hash starts a colour (§11.1)
            if (ahead == '#')
                return scan_color();
            // an at sign starts a macro name (§11.1)
            if (ahead == '@') {
                // the at sign is consumed, because the macro kind already carries it (§11.1)
                advance();
                // the name accumulates while the identifier characters are read (§11.1)
                std::string name;
                // the run continues over the identifier characters, because a macro name is an identifier (§11.1)
                while (!at_end()) {
                    // the character decides whether the name continues (§11.1)
                    const char letter = peek();
                    // anything else ends the name, because a macro name is not qualified (§11.1)
                    if (std::isalpha(static_cast<unsigned char>(letter)) == 0 && std::isdigit(static_cast<unsigned char>(letter)) == 0 && letter != '_')
                        break;
                    name += advance();
                }
                // a macro with no name is reported, because the parser has nothing to route (§11.1)
                if (name.empty()) {
                    // the problem is reported against the at sign alone (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = position()}, "E0106", "expected a macro name after @");
                    // the scan continues, so one bad macro does not hide the units after it (§11.1)
                    continue;
                }
                // the macro carries its name without the at sign, because the kind already says it was one (§11.1)
                return Token{.kind = Token::Kind::Macro, .text = name, .value = name, .span = SourceSpan{.start = start, .end = position()}};
            }
            // a letter or an underscore starts an identifier, which is also a keyword (§11.1)
            if (std::isalpha(static_cast<unsigned char>(ahead)) != 0 || ahead == '_') {
                // the spelling accumulates while the identifier characters are read (§11.1)
                std::string text;
                // the run continues over letters, digits and underscores, because all three may appear (§11.1)
                while (!at_end()) {
                    // the character decides whether the identifier continues (§11.1)
                    const char letter = peek();
                    // the first character is already a letter, so a digit may only follow it (§11.1)
                    const bool part = std::isalpha(static_cast<unsigned char>(letter)) != 0 || std::isdigit(static_cast<unsigned char>(letter)) != 0 || letter == '_';
                    // anything else ends the identifier (§11.1)
                    if (!part)
                        break;
                    text += advance();
                }
                return Token{.kind = Token::Kind::Identifier, .text = text, .value = text, .span = SourceSpan{.start = start, .end = position()}};
            }
            // every other symbol is punctuation or an operator (§11.1)
            const std::optional<Token> punctuation = scan_punctuation();
            // a symbol that is in no punctuation set is reported and skipped, so the scan reaches the units after it (§11.1)
            if (!punctuation.has_value())
                continue;
            return *punctuation;
        }
    }

    Token Lexer::scan_number() {
        // the position of the first digit is kept, because a span covers the whole literal (§11.1)
        const SourceLocation start = position();
        // the spelling accumulates as the digits are read (§11.1)
        std::string text;
        // a hexadecimal literal carries a prefix and its digits use letters, so it is read as a run (§11.1)
        bool hexadecimal = false;
        // the prefix is checked before the digits, because it changes how they are read (§11.1)
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            // the prefix joins the spelling and the digits follow it (§11.1)
            text += advance();
            text += advance();
            // the flag tells the loop that letters are digits rather than an identifier (§11.1)
            hexadecimal = true;
        }
        // a hexadecimal prefix with no digits behind it is not a number at all, so it is reported (§11.1)
        if (hexadecimal && !at_end() && std::isxdigit(static_cast<unsigned char>(peek())) == 0) {
            // the problem is reported against the whole malformed literal (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = position()}, "E0107", "a hexadecimal literal needs at least one digit");
            // the literal is returned as an integer, so the scan reaches the units after it (§11.1)
            return Token{.kind = Token::Kind::Integer, .text = text, .value = text, .span = SourceSpan{.start = start, .end = position()}};
        }
        // a float carries a decimal point and an exponent, which decide its kind (§11.1)
        bool fractional = false;
        // the digit run continues while the characters belong to the literal (§11.1)
        while (!at_end()) {
            // the character decides whether the literal continues (§11.1)
            const char digit = peek();
            // a decimal digit continues any literal (§11.1)
            const bool decimal_digit = std::isdigit(static_cast<unsigned char>(digit)) != 0;
            // a hexadecimal literal also takes letters, so 0xFF is one unit (§11.1)
            const bool hex_digit = hexadecimal && (std::isxdigit(static_cast<unsigned char>(digit)) != 0);
            // a dot makes the literal a float only where a digit may follow it (§11.1)
            const bool dot = digit == '.' && !fractional && std::isdigit(static_cast<unsigned char>(peek(1))) != 0;
            // the literal stops at anything else (§11.1)
            if (!decimal_digit && !hex_digit && !dot)
                break;
            // the flag is set before the dot is consumed, so the loop stops after it (§11.1)
            if (dot)
                fractional = true;
            text += advance();
        }
        // an exponent continues a float, so it is read after the digits (§11.1)
        if ((peek() == 'e' || peek() == 'E') && (std::isdigit(static_cast<unsigned char>(peek(1))) != 0 || ((peek(1) == '+' || peek(1) == '-') && std::isdigit(static_cast<unsigned char>(peek(2))) != 0))) {
            // the exponent marker joins the spelling (§11.1)
            text += advance();
            // a signed exponent consumes its sign, because the sign belongs to the exponent (§11.1)
            if (peek() == '+' || peek() == '-')
                text += advance();
            // the exponent digits continue the run (§11.1)
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0)
                text += advance();
            // an exponent always makes the literal a float (§11.1)
            fractional = true;
        }
        // a unit suffix makes the literal a length, which is the only non numeric suffix the language has (§11.1, AD-27)
        if (std::isalpha(static_cast<unsigned char>(peek())) != 0) {
            // the suffix is read as letters, so px and phx are both accepted (§11.1, AD-27)
            std::string suffix;
            while (!at_end() && std::isalpha(static_cast<unsigned char>(peek())) != 0)
                suffix += advance();
            // only the pixel units are defined, so any other suffix is reported rather than guessed (§11.1, AD-27)
            if (suffix != "px" && suffix != "phx")
                m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = position()}, "E0101", "unknown unit '" + suffix + "', expected px or phx");
            // the suffix joins the spelling, so the value carries its unit (§11.1)
            text += suffix;
            return Token{.kind = Token::Kind::Length, .text = text, .value = text, .span = SourceSpan{.start = start, .end = position()}};
        }
        // a fractional or exponent literal is a float and everything else is an integer (§11.1)
        const Token::Kind kind = fractional || hexadecimal ? Token::Kind::Float : Token::Kind::Integer;
        return Token{.kind = kind, .text = text, .value = text, .span = SourceSpan{.start = start, .end = position()}};
    }

    Token Lexer::scan_string() {
        // the position of the opening quote is kept, because a span covers the literal (§11.1)
        const SourceLocation start = position();
        // the opening quote is consumed, so the body starts after it (§11.1)
        advance();
        // the decoded value accumulates while the escapes are applied (§11.1)
        std::string value;
        // the body is read until the closing quote or the end of the buffer (§11.1)
        while (!at_end()) {
            // the closing quote ends the literal (§11.1)
            if (peek() == '"') {
                // the closing quote is consumed so the span covers it (§11.1)
                advance();
                return Token{.kind = Token::Kind::String, .text = value, .value = value, .span = SourceSpan{.start = start, .end = position()}};
            }
            // a newline cannot appear inside a literal, because the language has no line continuation (§11.1)
            if (peek() == '\n')
                break;
            // a backslash introduces an escape (§11.1)
            if (peek() == '\\') {
                // the escape marker is consumed before the escaped character is read (§11.1)
                advance();
                // the end of the buffer after a backslash leaves nothing to escape (§11.1)
                if (at_end())
                    break;
                // the escaped character is read as whatever follows the marker (§11.1)
                const char escaped = advance();
                // the escapes the language defines are translated, and anything else stands for itself (§11.1)
                switch (escaped) {
                // a newline is written escaped so a literal may span lines (§11.1)
                case 'n':
                    value += '\n';
                    break;
                // a quote is written escaped so it may appear inside a literal (§11.1)
                case '"':
                    value += '"';
                    break;
                // a backslash is written escaped so it may appear inside a literal (§11.1)
                case '\\':
                    value += '\\';
                    break;
                // a tab is written escaped so it need not be a literal character in the source (§11.1)
                case 't':
                    value += '\t';
                    break;
                default:
                    // an undefined escape is reported rather than silently dropped (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Warning, SourceSpan{.start = position(), .end = position()}, "W0102", std::string("unknown escape '\\") + escaped + "'");
                    value += escaped;
                    break;
                }
                continue;
            }
            // anything else is taken as written, which is what a utf8 literal needs (§11.1)
            value += advance();
        }
        // the buffer ended before the closing quote, so the literal is unterminated (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = position()}, "E0103", "unterminated string literal");
        return Token{.kind = Token::Kind::String, .text = value, .value = value, .span = SourceSpan{.start = start, .end = position()}};
    }

    Token Lexer::scan_color() {
        // the position of the hash is kept, because a span covers the literal (§11.1)
        const SourceLocation start = position();
        // the hash is consumed, because it marks the colour (§11.1)
        advance();
        // the channels are read as hex digits, six of them for the opaque form (§11.1)
        std::string channels;
        // the run continues while the characters are hex digits, and the count is bounded (§11.1)
        while (channels.size() < 8 && std::isxdigit(static_cast<unsigned char>(peek())) != 0)
            channels += advance();
        // a colour needs six or eight digits, and anything else is not a colour at all (§11.1)
        if (channels.size() != 6 && channels.size() != 8) {
            // the malformed literal is reported rather than guessed at (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = start, .end = position()}, "E0104", "a colour needs six or eight hex digits");
            // the unit is still returned, so the parser reports a second problem rather than looping (§11.1)
            return Token{.kind = Token::Kind::Color, .text = channels, .value = channels, .span = SourceSpan{.start = start, .end = position()}};
        }
        return Token{.kind = Token::Kind::Color, .text = channels, .value = channels, .span = SourceSpan{.start = start, .end = position()}};
    }

    std::optional<Token> Lexer::scan_punctuation() {
        // the position of the first character is kept, because a span covers the whole operator (§11.1)
        const SourceLocation start = position();
        // the character ahead is kept, because a symbol outside the set has to be reported by value (§11.1)
        const char ahead = peek();
        // the longer operators are tried first, because the shorter ones are their prefixes (§11.1)
        static const char* const LONG_OPERATORS[] = {"==", "!=", ">=", "<=", "&&", "||"};
        // the list is scanned until one matches, which is what makes the longest match (§11.1)
        for (const char* candidate : LONG_OPERATORS) {
            // a candidate matches only when every character of it is ahead (§11.1)
            bool matches = true;
            // the characters of the candidate are checked in order (§11.1)
            for (std::size_t index = 0; candidate[index] != '\0'; index++) {
                // the first mismatch ends the check, because the operator is not this one (§11.1)
                if (peek(index) != candidate[index]) {
                    matches = false;
                    break;
                }
            }
            // a matched operator is consumed whole, because its meaning differs from its prefixes (§11.1)
            if (matches) {
                // the spelling accumulates so the span covers every character (§11.1)
                std::string text;
                // the characters of the operator are consumed in order (§11.1)
                for (std::size_t index = 0; candidate[index] != '\0'; index++)
                    text += advance();
                return Token{.kind = Token::Kind::Punctuation, .text = text, .value = text, .span = SourceSpan{.start = start, .end = position()}};
            }
        }
        // the single character symbols are listed, because the language has no other punctuation (§11.1)
        static const std::string_view SYMBOLS = "+-*/%<>!?:=.,;()[]{}$&|^~@\\";
        // a symbol that is not in the list starts nothing, so the scan cannot continue from it (§11.1)
        if (SYMBOLS.find(ahead) == std::string_view::npos) {
            // the position of the bad character is kept, because the report points at it (§11.1)
            const SourceLocation where = position();
            // the character is consumed, so the scan terminates rather than reporting it forever (§11.1)
            advance();
            // the problem is reported after the consume, so the span covers the character (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, SourceSpan{.start = where, .end = position()}, "E0105", std::string("unexpected character '") + ahead + "'");
            return std::nullopt;
        }
        // a listed symbol is consumed whole, which covers every remaining single character operator (§11.1)
        const std::string text(1, advance());
        return Token{.kind = Token::Kind::Punctuation, .text = text, .value = text, .span = SourceSpan{.start = start, .end = position()}};
    }

    bool Lexer::at_end() const {
        // the offset is compared against the buffer, which is what makes the end observable (§11.1)
        return m_offset >= m_source.size();
    }

    char Lexer::advance() {
        // the character is taken before the offset moves, so the position follows the read (§11.1)
        const char current = m_source[m_offset++];
        // a newline starts the next line, so the column restarts from it (§11.1)
        if (current == '\n') {
            // the line counter moves on before the column resets (§11.1)
            m_line++;
            m_column = 1;
        }
        else {
            // every other character advances the column, which counts bytes (§11.1)
            m_column++;
        }
        return current;
    }

    char Lexer::peek(std::size_t offset) const {
        // a position past the end reads nothing, which is how the scan terminates (§11.1)
        const std::size_t index = m_offset + offset;
        // the end of the buffer reports a null character rather than reading out of range (§11.1)
        if (index >= m_source.size())
            return '\0';
        return m_source[index];
    }
} // namespace albor::compiler
