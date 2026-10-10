#include "compiler/translation_extractor.h"

#include <algorithm>

#include "compiler/ast.h"

namespace albor::compiler
{
    namespace
    {
        // one expression walked for the translations it declares, which the node walker needs first (§9.2)
        void walk_expression(const Expression& expression, std::vector<std::string>& out);

        // one checked tree walked for the translations it declares (§9.2)
        void walk_node(const CheckedNode& node, std::vector<std::string>& out) {
            // a property assignment may carry a translation, which is the common case (§9.2)
            for (const CheckedAssignment& assignment : node.assignments)
                walk_expression(*assignment.value, out);
            // a handler body may carry one too, because a handler may set a label (§9.2)
            for (const CheckedHandler& handler : node.handlers) {
                // each statement is walked, because a body holds several (§11.1, §9.2)
                for (const CheckedStatement& statement : handler.body) {
                    // an assignment carries a value, which may hold a translation (§11.1, §9.2)
                    if (statement.value != nullptr)
                        walk_expression(*statement.value, out);
                    // an invocation carries arguments, which may hold one too (§11.1, §9.2)
                    for (const SharedExpression& argument : statement.arguments)
                        walk_expression(*argument, out);
                }
            }
            // each child is walked, because a translation may sit anywhere in the tree (§9.2)
            for (const std::shared_ptr<CheckedNode>& child : node.children)
                walk_node(*child, out);
            // a repeat body is walked too, because its bindings are translated like any other (§9.2)
            if (node.repeat_body != nullptr)
                walk_node(*node.repeat_body, out);
        }

        // one expression walked for the translations it declares (§9.2)
        void walk_expression(const Expression& expression, std::vector<std::string>& out) {
            // a translation is collected, because that is what the catalogue is keyed by (§9.2)
            if (const auto* translation = dynamic_cast<const TranslationExpression*>(&expression); translation != nullptr) {
                // an empty source names nothing a reader would see (§9.2)
                if (!translation->text.empty()) {
                    // the source joins the set, because a catalogue holds one entry per string (§9.2)
                    out.push_back(translation->text);
                }
            }
            // a conversion wraps a value, which is walked behind it (§9.2)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression))
                walk_expression(*conversion->value, out);
            // a binary application has two operands, which are both walked (§9.2)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
                // the left operand comes first, which is the written order (§9.2)
                walk_expression(*binary->left, out);
                // the right operand follows (§9.2)
                walk_expression(*binary->right, out);
            }
            // a unary application has one operand, which is walked behind it (§9.2)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                walk_expression(*unary->value, out);
            // a grouping has an inner expression, which is walked behind it (§9.2)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
                walk_expression(*grouping->inner, out);
            // a bracketed list has elements, each of which may translate, and they are walked in order (§9.2)
            if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression)) {
                // every element is walked, because a translation may sit anywhere in the list (§9.2)
                for (const ExpressionPtr& element : array->elements)
                    walk_expression(*element, out);
            }
            // a property reference is a leaf, so nothing further is walked behind it (§9.2)
        }

        // escape a source string for the template, which is line oriented and cannot hold a newline (§9.2)
        std::string escape(const std::string& value) {
            // the output accumulates, because a source may hold a quote or a backslash (§9.2)
            std::string out;
            // each character is walked, because each may need escaping (§9.2)
            for (const char c : value) {
                // a newline or a quote would break the template's own quoting (§9.2)
                if (c == '\\' || c == '"')
                    // the character is escaped, which is what a quoted form needs (§9.2)
                    out.push_back('\\');
                // a newline cannot appear inside a quoted template entry at all, so it becomes a space (§9.2)
                if (c == '\n') {
                    // a space keeps the entry on one line without changing what a reader sees (§9.2)
                    out.push_back(' ');
                }
                else {
                    // any other character is written as itself (§9.2)
                    out.push_back(c);
                }
            }
            // the escaped text is returned (§9.2)
            return out;
        }
    } // namespace

    std::vector<std::string> extract_translations(const CheckedFile& file) {
        // the sources accumulate as the tree is walked (§9.2)
        std::vector<std::string> sources;
        // each component is walked, because a file declares several (§9.2)
        for (const CheckedComponent& component : file.components) {
            // the component root is walked, which is where every binding hangs (§9.2)
            if (component.root != nullptr)
                walk_node(*component.root, sources);
        }
        // the same source written twice is one entry, because a catalogue is keyed by the string (§9.2)
        std::sort(sources.begin(), sources.end());
        // the duplicates are dropped, which is what makes the catalogue one entry per distinct string (§9.2)
        sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
        // the sources are returned whole, which is what the build writes out (§9.2)
        return sources;
    }

    std::string write_catalog_template(const std::vector<std::string>& sources) {
        // the output accumulates, which is what a translator edits (§9.2)
        std::string out;
        // the header states what the file is, because a translator opens it without the build (§9.2)
        out += "# albor translation template: fill in the text beside each source string\n";
        // the runtime falls back to the source, so an empty entry is a valid state and not an error (§9.2)
        out += "# an empty translation is shown as the source string, so leaving one is safe (§9.2)\n";
        // each source is written as one entry, in the sorted order extraction produced (§9.2)
        for (const std::string& source : sources) {
            // the entry opens with the source, which is the key a translation is filed under (§9.2)
            out += "\nmsgid \"";
            // the escaped source follows (§9.2)
            out += escape(source);
            // the entry closes the key and opens the translation, which a translator fills (§9.2)
            out += "\"\nmsgstr \"\"\n";
        }
        // the template is returned whole (§9.2)
        return out;
    }

    std::string write_catalog_table(const std::vector<std::string>& sources, const std::vector<std::pair<std::string, std::string>>& entries) {
        // the output accumulates, which is the generated unit (§9.2)
        std::string out;
        // the provenance opens the unit, which identifies what produced it (§14, §9.2)
        out += "// generated from the albor catalogue, do not edit (§9.2, §14.2)\n";
        // the include guard is the pragma, which is the only guard the project uses (§14.1)
        out += "#pragma once\n";
        // the blank opens the include section (§14.1)
        out += "\n";
        // the catalogue is what the generated table fills (§9.2)
        out += "#include \"i18n/catalog.h\"\n";
        // the blank opens the namespace (§14.1)
        out += "\n";
        // the generated code lives beside the runtime it installs into (§9.2)
        out += "namespace albor::i18n\n";
        out += "{\n";
        // the function is the entry point an application calls at startup (§9.2)
        out += "    // install this locale's catalogue, which an application calls once before it runs (§9.2)\n";
        out += "    void install_generated_catalog()\n";
        out += "    {\n";
        // the catalogue is built fresh, because a locale is one table rather than a mixture (§9.2)
        out += "        Catalog catalog;\n";
        // each translated entry contributes, and each untranslated one is skipped (§9.2)
        for (const std::string& source : sources) {
            // the translation is looked up by the source, which is the key extraction produced (§9.2)
            const auto entry = std::find_if(entries.begin(), entries.end(), [&source](const std::pair<std::string, std::string>& candidate) { return candidate.first == source; });
            // an entry with no translation is skipped, because the runtime falls back to the source (§9.2)
            if (entry == entries.end() || entry->second.empty())
                continue;
            // the entry is added, which is what the generated table contributes (§9.2)
            out += "        catalog.add(\"" + escape(source) + "\", \"" + escape(entry->second) + "\");\n";
        }
        // the catalogue is installed, which is what makes every translation resolve through it (§9.2)
        out += "        set_current(std::move(catalog));\n";
        // the function closes (§14.1)
        out += "    }\n";
        // the namespace closes (§14.1)
        out += "} // namespace albor::i18n\n";
        // the unit is returned whole, which is what the build writes (§14.2)
        return out;
    }
} // namespace albor::compiler
