#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace albor::compiler
{
    // one position inside a source file, where a column counts bytes from the start of its line (§11.1)
    struct SourceLocation
    {
        // the one based line the position is on
        std::uint32_t line = 0;

        // the one based column the position is at
        std::uint32_t column = 0;

        // compare two positions field by field, which is what makes a span comparable (§11.1)
        friend bool operator==(const SourceLocation& lhs, const SourceLocation& rhs) = default;
    };

    // the region of a source file a construct occupies, which is what a diagnostic points at (§11.1)
    struct SourceSpan
    {
        // where the construct starts (§11.1)
        SourceLocation start;

        // where the construct ends, which is exclusive (§11.1)
        SourceLocation end;

        // report whether the span covers no source at all, which is what an absent construct carries (§11.1)
        [[nodiscard]] bool empty() const;

        // compare two spans field by field
        friend bool operator==(const SourceSpan& lhs, const SourceSpan& rhs) = default;
    };

    // one reported problem in a source file, which is how the compiler returns everything wrong at once (§11.1)
    struct Diagnostic
    {
        // whether the problem blocks code generation (§11.1)
        enum class Severity
        {
            // the problem blocks code generation, because the construct cannot be translated (§11.1)
            Error,

            // the problem is reported but code generation continues (§11.1)
            Warning
        };

        // how bad the problem is, which decides whether codegen runs (§11.1)
        Severity severity = Severity::Error;

        // where the problem is, which is what an editor squiggles (§11.1)
        SourceSpan span;

        // what is wrong, stated for a person reading it (§11.1)
        std::string message;

        // the stable identifier tooling matches on, which is never prose (§11.1)
        std::string code;
    };

    // every problem found in one compilation, which is returned rather than thrown (§2.1, AD-01)
    class DiagnosticList
    {
    public:
        // build an empty list, which is what a file without problems produces (§11.1)
        DiagnosticList() = default;

        // report a problem, which the caller describes with its own span (§11.1)
        void add(Diagnostic diagnostic);

        // report a problem at a span, which is the common case (§11.1)
        void add(Diagnostic::Severity severity, SourceSpan span, std::string code, std::string message);

        // report how many problems are listed, which is what a driver checks before generating (§11.1)
        [[nodiscard]] std::size_t count() const;

        // report whether any listed problem blocks code generation (§11.1)
        [[nodiscard]] bool has_errors() const;

        // hand out the problems in the order they were found (§11.1)
        [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;

        // hand out the problems of one severity, which is how a driver separates blocking from advisory (§11.1)
        [[nodiscard]] std::vector<Diagnostic> of_severity(Diagnostic::Severity severity) const;

        // drop every problem, which a driver does between runs (§11.1)
        void clear();

    private:
        // the reported problems, in the order they were found (§11.1)
        std::vector<Diagnostic> m_diagnostics;
    };

    // render a diagnostic the way a command line driver prints it, which a test asserts on (§11.1)
    [[nodiscard]] std::string format(const Diagnostic& diagnostic, std::string_view path);
} // namespace albor::compiler
