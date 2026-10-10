#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/diagnostic.h"

namespace albor::compiler
{
    // one lexical unit of a source file, which the parser consumes without re-reading the text (§11.1)
    struct Token
    {
        // what kind of unit this is, which is the closed set the grammar is written over (§11.1)
        enum class Kind
        {
            // the end of the file, which every scan terminates on (§11.1)
            End,

            // an identifier, which is also a keyword (§11.1)
            Identifier,

            // a macro name written with a leading at sign, such as @tr (§11.1)
            Macro,

            // a decimal or hexadecimal integer literal (§11.1)
            Integer,

            // a floating point literal (§11.1)
            Float,

            // a quoted string literal, with its escapes already applied (§11.1)
            String,

            // a length literal such as 12px, which carries its unit (§11.1, AD-27)
            Length,

            // a colour literal such as #FF0000, which carries its channels (§11.1)
            Color,

            // a punctuation or operator unit (§11.1)
            Punctuation
        };

        // what kind of unit this is (§11.1)
        Kind kind = Kind::End;

        // the text of the unit, which is the spelling as written (§11.1)
        std::string text;

        // the decoded value of a string literal, which the escapes have already been applied to (§11.1)
        std::string value;

        // where the unit starts (§11.1)
        SourceSpan span;

        // report whether the unit is the given identifier, which is how a keyword is matched (§11.1)
        [[nodiscard]] bool is(std::string_view keyword) const;

        // report whether the unit is the given punctuation, which is how an operator is matched (§11.1)
        [[nodiscard]] bool is_punctuation(std::string_view symbol) const;
    };

    // the scanner that turns source text into tokens, which reports rather than throws on bad input (§11.1)
    class Lexer
    {
    public:
        // build a scanner over a source buffer that outlives it, which is what a driver holds (§11.1)
        Lexer(std::string_view source, DiagnosticList& diagnostics);

        // scan the whole buffer, which reports every lexical problem it finds (§11.1)
        [[nodiscard]] std::vector<Token> tokenize();

        // hand out the position the next character would be read from, which a parser resumes at (§11.1)
        [[nodiscard]] SourceLocation position() const;

    private:
        // skip whitespace and comments, which carry no meaning of their own (§11.1)
        void skip_trivia();

        // scan one unit, reporting an unterminated or malformed one rather than throwing (§11.1)
        Token scan();

        // scan a number, which may be an integer, a float or a length (§11.1)
        Token scan_number();

        // scan a quoted string, applying the escapes the language defines (§11.1)
        Token scan_string();

        // scan a colour literal, which is a hash followed by six hex digits (§11.1)
        Token scan_color();

        // scan punctuation or an operator, taking the longest operator that matches (§11.1)
        [[nodiscard]] std::optional<Token> scan_punctuation();

        // report whether the scan has reached the end of the buffer, which every read is guarded by (§11.1)
        [[nodiscard]] bool at_end() const;

        // read one character and advance, which the caller only calls when the scan is not at the end (§11.1)
        char advance();

        // look at the character ahead without consuming it (§11.1)
        [[nodiscard]] char peek(std::size_t offset = 0) const;

        // the source being scanned, which outlives the scanner (§11.1)
        std::string_view m_source;

        // the problems found while scanning, which the scanner appends to (§11.1)
        DiagnosticList& m_diagnostics;

        // the byte offset the scanner has read to (§11.1)
        std::size_t m_offset = 0;

        // the line the scanner is on (§11.1)
        std::uint32_t m_line = 1;

        // the column the scanner is at (§11.1)
        std::uint32_t m_column = 1;
    };
} // namespace albor::compiler
