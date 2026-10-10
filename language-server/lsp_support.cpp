#include "language-server/lsp_support.h"

#include <algorithm>

namespace albor::language_server
{
    namespace
    {
        // the protocol kind for a name an author types, which is what an editor shows (§12.2)
        constexpr int KIND_TEXT = 1;

        // the protocol kind for a type name, which an editor shows differently from a plain name (§12.2)
        constexpr int KIND_CLASS = 7;

        // the protocol kind for a property, which an editor shows differently from a name (§12.2)
        constexpr int KIND_PROPERTY = 10;

        // the protocol kind for a signal, which is what a handler attaches to (§12.2)
        constexpr int KIND_FUNCTION = 3;

        // the prefix that introduces a handler, which is how a signal is written (§11.1)
        constexpr std::string_view HANDLER_PREFIX = "on_";

        // whether a character may begin an identifier, which decides what a word ends at (§11.1)
        bool is_word(char c) {
            // a letter or an underscore begins a word, and anything else ends it (§11.1)
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        // the word the cursor sits in or just after, which is what hover reads (§12.2)
        std::string word_at(const std::string_view line, std::size_t character) {
            // the position is clamped, because a protocol position may name one past the last character (§12.2)
            const std::size_t at = character > line.size() ? line.size() : character;
            // the start walks left while characters are word characters (§11.1)
            std::size_t start = at;
            // the loop stops at the beginning of the line or the first non word character (§11.1)
            while (start > 0 && is_word(line[start - 1]))
                start--;
            // the end walks right, so a word typed after the cursor is included (§12.2)
            std::size_t end = at;
            // the loop stops at the end of the line or the first non word character (§11.1)
            while (end < line.size() && is_word(line[end]))
                end++;
            // the slice is what the cursor names (§12.2)
            return std::string(line.substr(start, end - start));
        }

        // the offset the cursor names, which every context decision starts from (§12.2)
        std::size_t offset_of(const Document& document, std::size_t line, std::size_t character) {
            // a line past the end names the end of the document, which is where an out of range position lands (§12.2)
            if (line >= document.line_count())
                return document.text().size();
            // the line is read, because the offset is measured within it (§12.2)
            const std::string_view text = document.line_at(line);
            // the character is clamped to the line, because a position may name one past its end (§12.2)
            const std::size_t column = character > text.size() ? text.size() : character;
            // the offset is measured by summing the lines before, which the index makes cheap enough (§12.2)
            std::size_t offset = 0;
            // each preceding line contributes its text and its newline (§12.2)
            for (std::size_t i = 0; i < line; i++)
                offset += document.line_at(i).size() + 1;
            // the column within the line completes the offset (§12.2)
            return offset + column;
        }

        // the identifier written immediately before an offset, which is what an enclosing block is named by (§11.1)
        std::string word_before(const std::string& text, std::size_t offset) {
            // the position walks left over whitespace, because a keyword is often indented away from its brace (§11.1)
            std::size_t at = offset;
            // the loop skips whitespace, which carries no meaning here (§11.1)
            while (at > 0 && (text[at - 1] == ' ' || text[at - 1] == '\t' || text[at - 1] == '\n' || text[at - 1] == '\r'))
                at--;
            // the end of the word is where the scan stopped (§11.1)
            const std::size_t end = at;
            // the start walks left while characters are word characters (§11.1)
            while (at > 0 && is_word(text[at - 1]))
                at--;
            // the slice is the name, which is what a block is called (§11.1)
            return text.substr(at, end - at);
        }

        // the property name the cursor sits after, which decides a property completion (§11.1)
        std::string property_before(const std::string_view line, std::size_t character) {
            // the position is clamped, because a protocol position may name one past the last character (§12.2)
            const std::size_t at = character > line.size() ? line.size() : character;
            // a colon before the cursor is what makes this a property position (§11.1)
            if (at == 0 || line[at - 1] != ':')
                return {};
            // the word before the colon is the property name (§11.1)
            std::size_t start = at - 1;
            // the scan walks left over the word characters before the colon (§11.1)
            while (start > 0 && is_word(line[start - 1]))
                start--;
            // the slice is the property name, which may be empty while it is being typed (§11.1)
            return std::string(line.substr(start, at - 1 - start));
        }

        // the component the file declares, which is what tells a component body from an element body (§11.1)
        std::string component_name(const std::string& text) {
            // the scan walks the text, which is the only place a component is declared (§11.1)
            std::size_t at = 0;
            // the loop runs until the keyword is found or the text ends (§11.1)
            while (at < text.size()) {
                // the keyword is looked for, which is what a component declaration opens with (§11.1)
                const std::size_t found = text.find("component", at);
                // no keyword means no component, which is what an empty file is (§11.1)
                if (found == std::string::npos)
                    return {};
                // the character before the keyword decides, because a name may end in it (§11.1)
                const bool word_before = found > 0 && is_word(text[found - 1]);
                // the character after the keyword decides too, because the keyword must stand alone (§11.1)
                std::size_t end = found + 9;
                // the whitespace is skipped, because the name follows it (§11.1)
                while (end < text.size() && (text[end] == ' ' || text[end] == '\t' || text[end] == '\n' || text[end] == '\r'))
                    end++;
                // the name ends at the first non word character (§11.1)
                std::size_t name_end = end;
                // the walk continues past the keyword, because this may be an ordinary name (§11.1)
                while (name_end < text.size() && is_word(text[name_end]))
                    name_end++;
                // a standalone keyword is the declaration, and its name is what follows (§11.1)
                if (!word_before && name_end > end)
                    return text.substr(end, name_end - end);
                // the scan resumes after the keyword, because it was not a declaration (§11.1)
                at = found + 9;
            }
            // no declaration means the file declares nothing (§11.1)
            return {};
        }

        // the element names the catalog holds, which is what an element completion offers (§11.1, §11.2)
        std::vector<CompletionItem> element_items(const compiler::ElementCatalog& catalog) {
            // the items accumulate, one per known element (§12.2)
            std::vector<CompletionItem> items;
            // every type in the catalog is offered, because any of them may be instantiated (§11.2, AD-17)
            for (const compiler::ElementType& type : catalog.types()) {
                // the detail says whether children are accepted, which is what a person cannot guess (§8)
                const std::string detail = type.accepts_children ? "container" : "widget";
                // the item is the element name, which is what the author types (§12.2)
                items.push_back(CompletionItem{.label = type.name, .detail = detail, .kind = KIND_CLASS, .documentation = ""});
            }
            // the items are returned in the catalog's order, which keeps a response stable (§12.2)
            return items;
        }

        // the properties and signals one element exposes, which is what a body completion offers (§11.1)
        std::vector<CompletionItem> element_body_items(const compiler::ElementType& type) {
            // the items accumulate, properties first (§12.2)
            std::vector<CompletionItem> items;
            // every property is offered, because any of them may be assigned (§11.1)
            for (const compiler::ElementProperty& property : type.properties) {
                // the item is the property name with its type, which is what a person needs to assign it (§11.1)
                items.push_back(CompletionItem{.label = property.name, .detail = property.type.to_string(), .kind = KIND_PROPERTY, .documentation = ""});
            }
            // every signal is offered, because a handler attaches to one by name (§11.1)
            for (const compiler::ElementSignal& signal : type.signals) {
                // the signal is offered with the handler prefix, because that is how it is written (§11.1)
                items.push_back(CompletionItem{.label = std::string(HANDLER_PREFIX) + signal.name, .detail = "signal", .kind = KIND_FUNCTION, .documentation = ""});
            }
            // the items are returned, which is what a body completion answers (§12.2)
            return items;
        }
    } // namespace

    std::string enclosing_element_at(const Document& document, std::size_t line, std::size_t character) {
        // the whole text is scanned, because the enclosing element may be many lines up (§12.2)
        const std::string& text = document.text();
        // the offset bounds the scan, because only the text before the cursor is context (§12.2)
        const std::size_t limit = offset_of(document, line, character);
        // the open elements stack up, because the innermost one is what a completion answers from (§11.1)
        std::vector<std::string> open;
        // the scan walks the text, tracking strings and comments so a brace inside one changes nothing (§11.1)
        bool in_string = false;
        // the comment flag is separate, because a comment may hold a brace that is not structure (§11.1)
        bool in_comment = false;
        // an index walks the text up to the cursor (§12.2)
        for (std::size_t i = 0; i < limit && i < text.size(); i++) {
            // the character decides, because each one means something different to the scanner (§11.1)
            const char c = text[i];
            // a line comment runs to the end of the line, and holds no structure (§11.1)
            if (in_comment) {
                // a newline ends the comment, because a comment is one line (§11.1)
                if (c == '\n')
                    in_comment = false;
                // the character contributes nothing while the comment runs (§11.1)
                continue;
            }
            // inside a string a brace is text, not structure (§11.1)
            if (in_string) {
                // a backslash escapes the next character, so a quote may appear inside (§11.1)
                if (c == '\\') {
                    // the next character is skipped, because it is escaped (§11.1)
                    i++;
                    // the loop continues, because the escaped character is not a delimiter (§11.1)
                    continue;
                }
                // a closing quote ends the string (§11.1)
                if (c == '"')
                    in_string = false;
                // the character contributes nothing while the string runs (§11.1)
                continue;
            }
            // a slash followed by another opens a comment, which holds no structure (§11.1)
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                // the comment flag is set, because the rest of the line is not code (§11.1)
                in_comment = true;
                // the index moves past the second slash, because it was consumed with the first (§11.1)
                i++;
                // the loop continues, because the comment opened (§11.1)
                continue;
            }
            // a quote opens a string, whose braces are text (§11.1)
            if (c == '"') {
                // the string flag is set, because the scanner is now inside a literal (§11.1)
                in_string = true;
                // the loop continues, because the string opened (§11.1)
                continue;
            }
            // an opening brace pushes the name written before it, which is how an element is tracked (§11.1)
            if (c == '{') {
                // the name is read backwards, because it precedes the brace (§11.1)
                open.push_back(word_before(text, i));
                // the loop continues, because the block opened (§11.1)
                continue;
            }
            // a closing brace pops, because the innermost block just ended (§11.1)
            if (c == '}' && !open.empty())
                open.pop_back();
        }
        // the innermost named element answers, because that is the one the cursor is inside (§12.2)
        for (auto entry = open.rbegin(); entry != open.rend(); ++entry) {
            // an unnamed block carries no element, so the scan looks outward through it (§11.1)
            if (!entry->empty())
                return *entry;
        }
        // no enclosing element means the cursor is at the top level of the file (§12.2)
        return {};
    }

    namespace
    {
        // report whether a position sits inside a string literal, which is text rather than code (§11.1)
        bool inside_string(const std::string_view text, std::size_t at) {
            // the quotes are counted up to the position, because an odd count means one is open (§11.1)
            bool open = false;
            // the walk covers the line, because a literal never spans one in this language (§11.1)
            for (std::size_t i = 0; i < at && i < text.size(); i++) {
                // a backslash escapes the next character, so a quote after it is not a delimiter (§11.1)
                if (open && text[i] == '\\') {
                    // the escaped character is skipped, because it is not a delimiter (§11.1)
                    i++;
                    // the walk continues past the escape (§11.1)
                    continue;
                }
                // a quote toggles the state, which is what makes the parity meaningful (§11.1)
                if (text[i] == '"')
                    open = !open;
            }
            // an open quote at the position means the cursor is inside a literal (§11.1)
            return open;
        }
    } // namespace

    CompletionContext completion_context_at(const Document& document, std::size_t line, std::size_t character) {
        // a position past the last line names nothing, so nothing is offered (§12.2)
        if (line >= document.line_count())
            return CompletionContext::None;
        // the line is read, because the shape of the request is decided from it (§12.2)
        const std::string_view text = document.line_at(line);
        // the character is clamped, because a position may name one past the last character (§12.2)
        const std::size_t at = character > text.size() ? text.size() : character;
        // inside a string the cursor is on text, where a list of names would be noise rather than help (§11.1)
        if (inside_string(text, at))
            return CompletionContext::None;
        // the element the cursor sits inside, which decides what may be written (§12.2)
        const std::string element = enclosing_element_at(document, line, character);
        // the text before the cursor is what decides between a property, a signal and an element (§11.1)
        const std::string_view before = text.substr(0, at);
        // a handler prefix before the cursor means a signal may follow (§11.1)
        const std::size_t handler_start = before.rfind(HANDLER_PREFIX);
        // the prefix is only a handler when nothing but a name follows it (§11.1)
        if (handler_start != std::string_view::npos) {
            // the characters after the prefix are walked, because only name characters keep it a handler (§11.1)
            bool only_name = true;
            // the walk covers whatever was typed after the prefix (§11.1)
            for (std::size_t i = handler_start + HANDLER_PREFIX.size(); i < before.size(); i++) {
                // a character that is not a name character ends the handler and starts something else (§11.1)
                if (!is_word(before[i])) {
                    // the flag clears, because the prefix is not what introduces a signal here (§11.1)
                    only_name = false;
                    // the walk stops, because one non name character settles it (§11.1)
                    break;
                }
            }
            // a bare prefix with a name after it is a signal being typed (§12.2)
            if (only_name && !element.empty())
                return CompletionContext::Signal;
        }
        // a colon before the cursor means a property may follow (§11.1)
        if (!property_before(text, at).empty())
            return CompletionContext::Property;
        // the component's own body holds declarations as well as children, so it is its own context (§11.1)
        if (!element.empty() && element == component_name(document.text()))
            return CompletionContext::Declaration;
        // inside any other named element, an element name may follow (§11.1)
        if (!element.empty())
            return CompletionContext::Element;
        // a colon at the very start of a body is still a property position (§11.1)
        if (!at || text[at - 1] == ':')
            return CompletionContext::Property;
        // nowhere else is the top level, where a component declaration may begin (§11.1)
        return CompletionContext::Declaration;
    }

    std::vector<CompletionItem> completions_at(const Document& document, const compiler::ElementCatalog& catalog, std::size_t line, std::size_t character) {
        // the context decides the response, because one cursor means different things in different places (§12.2)
        const CompletionContext context = completion_context_at(document, line, character);
        // nowhere useful offers nothing, because a long list of irrelevant names is worse than none (§12.2)
        if (context == CompletionContext::None)
            return {};
        // a signal completion offers the signals of the element the cursor is inside (§11.1)
        if (context == CompletionContext::Signal) {
            // the enclosing element is read, because its signals are what may follow (§12.2)
            const std::string element = enclosing_element_at(document, line, character);
            // an unknown element has no signals to offer, so nothing is offered rather than a guess (§12.2)
            const compiler::ElementType* type = catalog.find(element);
            // an unknown element offers nothing (§12.2)
            if (type == nullptr)
                return {};
            // the signals only, which is what the handler prefix asked for (§11.1)
            std::vector<CompletionItem> items;
            // each signal is offered with the prefix, which is how it is written (§11.1)
            for (const compiler::ElementSignal& signal : type->signals) {
                // the item names the handler a person types (§12.2)
                items.push_back(CompletionItem{.label = std::string(HANDLER_PREFIX) + signal.name, .detail = "signal", .kind = KIND_FUNCTION, .documentation = ""});
            }
            // the signals are returned, which is the whole of a signal completion (§12.2)
            return items;
        }
        // a property completion offers the enclosing element's own properties and signals (§11.1)
        if (context == CompletionContext::Property) {
            // the enclosing element is read, because its properties are what may be assigned (§12.2)
            const std::string element = enclosing_element_at(document, line, character);
            // a body with no named element is a component body, which has no element properties (§11.1)
            if (element.empty())
                return {};
            // the type is read from the catalog, which is the single source of truth (§11.2, AD-17)
            const compiler::ElementType* type = catalog.find(element);
            // an element the catalog does not hold offers nothing rather than another element's properties (§12.2, AD-17)
            if (type == nullptr)
                return {};
            // the element's own surface is what may follow the colon (§11.1)
            return element_body_items(*type);
        }
        // inside an element body, any known element may be the child being written (§11.1)
        if (context == CompletionContext::Element)
            return element_items(catalog);
        // a component body holds both its declarations and its child elements, so both are offered (§11.1)
        std::vector<CompletionItem> items;
        // the keywords are what may start a declaration there (§11.1)
        for (const char* keyword : {"in", "inout", "signal", "on_"}) {
            // each keyword is offered, because each starts a declaration (§11.1)
            items.push_back(CompletionItem{.label = keyword, .detail = "keyword", .kind = KIND_TEXT, .documentation = ""});
        }
        // the child elements are what may start an element there, which is the other half of that body (§11.1)
        const std::vector<CompletionItem> children = element_items(catalog);
        // the element names join the keywords, because both are valid at that position (§11.1)
        items.insert(items.end(), children.begin(), children.end());
        // both are returned, which is what a component body answers (§12.2)
        return items;
    }

    std::optional<std::string> hover_at(const Document& document, const compiler::ElementCatalog& catalog, std::size_t line, std::size_t character) {
        // a position past the last line names nothing (§12.2)
        if (line >= document.line_count())
            return std::nullopt;
        // the line is read, because the token under the cursor is on it (§12.2)
        const std::string_view text = document.line_at(line);
        // the word the cursor sits in is read, which is what hover describes (§12.2)
        const std::string word = word_at(text, character);
        // an empty word means the cursor is on whitespace, which has nothing to say (§12.2)
        if (word.empty())
            return std::nullopt;
        // a signal is described through its handler prefix, which is how it is written (§11.1)
        const bool handler = word.rfind(HANDLER_PREFIX, 0) == 0;
        // the signal name is what follows the prefix (§11.1)
        const std::string signal_name = handler ? word.substr(HANDLER_PREFIX.size()) : word;
        // the enclosing element is read, because a property and a signal belong to one (§12.2)
        const std::string element = enclosing_element_at(document, line, character);
        // a named element answers from the catalog, which is the single source of truth (§11.2, AD-17)
        const compiler::ElementType* type = element.empty() ? nullptr : catalog.find(element);
        // a handler is described as the signal it attaches to (§11.1)
        if (handler && type != nullptr) {
            // the signal is looked up by its bare name (§11.1)
            const compiler::ElementSignal* signal = type->find_signal(signal_name);
            // a signal the element does not raise has nothing to say (§12.2)
            if (signal == nullptr)
                return std::nullopt;
            // the description names the element and the signal, which is what a person needs (§12.2)
            return element + "." + signal->name + " signal";
        }
        // a word may be a property of the enclosing element (§11.1)
        if (type != nullptr) {
            // the property is looked up by name (§11.1)
            const compiler::ElementProperty* property = type->find_property(word);
            // a property the element does not expose has nothing to say (§12.2)
            if (property != nullptr)
                return element + "." + property->name + " : " + property->type.to_string();
        }
        // the word may be the element itself, which is what hovering its own name means (§12.2)
        const compiler::ElementType* hovered = catalog.find(word);
        // a name the catalog holds is described as an element (§11.1, AD-17)
        if (hovered != nullptr)
            return hovered->name + (hovered->accepts_children ? " container" : " widget");
        // nothing else is described, because a guess would be worse than silence (§12.2)
        return std::nullopt;
    }
} // namespace albor::language_server
