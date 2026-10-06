#include "compiler/diagnostic.h"

#include <utility>

namespace albor::compiler
{
    bool SourceSpan::empty() const {
        // a span that starts where it ends covers no source, which is what an absent construct carries (§11.1)
        return start.line == end.line && start.column == end.column;
    }

    void DiagnosticList::add(Diagnostic diagnostic) {
        // the problem joins the list in the order it was found, because that is the order a reader wants (§11.1)
        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticList::add(Diagnostic::Severity severity, SourceSpan span, std::string code, std::string message) {
        // the parts are assembled into the one shape the rest of the compiler passes around (§11.1)
        Diagnostic diagnostic;
        // the severity decides whether codegen runs at all (§11.1)
        diagnostic.severity = severity;
        // the span is what a reader and an editor both point at (§11.1)
        diagnostic.span = span;
        // the code is the stable handle, so it moves before the prose that tooling never matches on (§11.1)
        diagnostic.code = std::move(code);
        // the message is what a person reads (§11.1)
        diagnostic.message = std::move(message);
        add(std::move(diagnostic));
    }

    std::size_t DiagnosticList::count() const {
        // the count is every listed problem, of either severity (§11.1)
        return m_diagnostics.size();
    }

    bool DiagnosticList::has_errors() const {
        // one error is enough to block codegen, so the scan stops at the first (§11.1)
        for (const Diagnostic& diagnostic : m_diagnostics)
            if (diagnostic.severity == Diagnostic::Severity::Error)
                return true;
        return false;
    }

    const std::vector<Diagnostic>& DiagnosticList::diagnostics() const {
        // the problems are handed out as they were reported (§11.1)
        return m_diagnostics;
    }

    std::vector<Diagnostic> DiagnosticList::of_severity(Diagnostic::Severity severity) const {
        // the filtered problems are collected, because a caller cannot hold views into the list (§11.1)
        std::vector<Diagnostic> filtered;
        // every problem is checked, because both severities may be present at once (§11.1)
        for (const Diagnostic& diagnostic : m_diagnostics)
            if (diagnostic.severity == severity)
                filtered.push_back(diagnostic);
        return filtered;
    }

    void DiagnosticList::clear() {
        // a repeated run starts from nothing, so old problems cannot be reported twice (§11.1)
        m_diagnostics.clear();
    }

    std::string format(const Diagnostic& diagnostic, std::string_view path) {
        // the prefix names the severity, because that is what a reader scans for first (§11.1)
        std::string text = diagnostic.severity == Diagnostic::Severity::Error ? "error" : "warning";
        // the code follows, because tooling and a reader both want it in the same place (§11.1)
        text += ": " + diagnostic.code + ": ";
        // the path and position make the message locatable, which a span alone cannot (§11.1)
        text += std::string(path) + ":" + std::to_string(diagnostic.span.start.line) + ":" + std::to_string(diagnostic.span.start.column) + ": ";
        // the message is what the whole line is for (§11.1)
        text += diagnostic.message;
        return text;
    }
} // namespace albor::compiler
