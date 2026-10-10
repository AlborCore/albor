#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace albor::core
{
    namespace
    {
        // the reader that walks the text, which holds the position every rule advances (§10.3)
        struct Reader
        {
            // the text being read, which lives as long as the parse (§10.3)
            std::string_view text;

            // the position within the text, which every rule advances (§10.3)
            std::size_t at = 0;
        };

        // skip whitespace, which json permits freely between every token (§10.3)
        void skip_space(Reader& reader) {
            // the loop stops at the first character that is not whitespace (§10.3)
            while (reader.at < reader.text.size()) {
                // the character decides, because json allows space, tab, newline and return (§10.3)
                const char c = reader.text[reader.at];
                // anything else ends the whitespace run (§10.3)
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                    break;
                // the position advances, which is the whole of skipping (§10.3)
                reader.at++;
            }
        }

        // read a json string body, which resolves the escapes the wire may carry (§10.3)
        std::optional<std::string> read_string(Reader& reader) {
            // the opening quote is required, because a string is always delimited (§10.3)
            if (reader.at >= reader.text.size() || reader.text[reader.at] != '"')
                return std::nullopt;
            // the position moves past the quote, so the body starts (§10.3)
            reader.at++;
            // the body accumulates, which is what the caller receives (§10.3)
            std::string body;
            // the loop runs until the closing quote or a malformed escape ends it (§10.3)
            while (reader.at < reader.text.size()) {
                // the character decides, because a plain one just joins the body (§10.3)
                const char c = reader.text[reader.at];
                // the closing quote ends the body (§10.3)
                if (c == '"') {
                    // the position moves past the quote, so the value is complete (§10.3)
                    reader.at++;
                    // the body is returned, which is the whole of a string (§10.3)
                    return body;
                }
                // a backslash introduces an escape, which the wire may use for any character (§10.3)
                if (c == '\\') {
                    // the position moves past the backslash, so the escape letter is next (§10.3)
                    reader.at++;
                    // the end of the text mid escape is malformed (§10.3)
                    if (reader.at >= reader.text.size())
                        return std::nullopt;
                    // the escape letter decides which character the wire meant (§10.3)
                    const char escape = reader.text[reader.at];
                    // the position advances past the letter in every case (§10.3)
                    reader.at++;
                    // the named escapes are the ones the wire may use for a control character (§10.3)
                    switch (escape) {
                    case 'n':
                        body.push_back('\n');
                        break;
                    case 't':
                        body.push_back('\t');
                        break;
                    case 'r':
                        body.push_back('\r');
                        break;
                    case 'b':
                        body.push_back('\b');
                        break;
                    case 'f':
                        body.push_back('\f');
                        break;
                    case '/':
                        body.push_back('/');
                        break;
                    case '\\':
                        body.push_back('\\');
                        break;
                    case '"':
                        body.push_back('"');
                        break;
                    case 'u': {
                        // a unicode escape needs four hex digits, so the text must hold them (§10.3)
                        if (reader.at + 4 > reader.text.size())
                            return std::nullopt;
                        // the digits are read as one value, which is what the escape means (§10.3)
                        unsigned code = 0;
                        // each digit is folded in, which is what makes it one code point (§10.3)
                        for (int i = 0; i < 4; i++) {
                            // the digit decides, because only hex digits are valid here (§10.3)
                            const char digit = reader.text[reader.at + static_cast<std::size_t>(i)];
                            // the digit's value is what the fold adds (§10.3)
                            unsigned value = 0;
                            // the ranges decide, because a hex digit is in one of three ranges (§10.3)
                            if (digit >= '0' && digit <= '9')
                                value = static_cast<unsigned>(digit - '0');
                            else if (digit >= 'a' && digit <= 'f')
                                value = static_cast<unsigned>(digit - 'a') + 10U;
                            else if (digit >= 'A' && digit <= 'F')
                                value = static_cast<unsigned>(digit - 'A') + 10U;
                            else
                                // anything else is malformed, which stops the parse (§10.3)
                                return std::nullopt;
                            // the digit joins the code point (§10.3)
                            code = code * 16U + value;
                        }
                        // the position moves past the digits, which is the whole escape (§10.3)
                        reader.at += 4;
                        // the code point is written as utf8, which is what json strings hold (§10.3)
                        if (code < 0x80U) {
                            // a single ascii byte needs one byte (§10.3)
                            body.push_back(static_cast<char>(code));
                        }
                        else if (code < 0x800U) {
                            // a two byte sequence covers the rest of the basic plane (§10.3)
                            body.push_back(static_cast<char>(0xC0U | (code >> 6U)));
                            body.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
                        }
                        else {
                            // a three byte sequence covers everything below the surrogate range (§10.3)
                            body.push_back(static_cast<char>(0xE0U | (code >> 12U)));
                            body.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
                            body.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
                        }
                        break;
                    }
                    default:
                        // an unknown escape letter is malformed (§10.3)
                        return std::nullopt;
                    }
                    continue;
                }
                // a plain character joins the body, which is the common case (§10.3)
                body.push_back(c);
                // the position advances, which is the whole of a plain character (§10.3)
                reader.at++;
            }
            // a string that never closed is malformed (§10.3)
            return std::nullopt;
        }

        // read one json value, which recurses for the nested kinds (§10.3)
        std::optional<Json> read_value(Reader& reader);

        // read a json array, which is a bracketed list of values (§10.3)
        std::optional<Json> read_array(Reader& reader) {
            // the opening bracket is required, because an array is always delimited (§10.3)
            if (reader.at >= reader.text.size() || reader.text[reader.at] != '[')
                return std::nullopt;
            // the position moves past the bracket, so the first element starts (§10.3)
            reader.at++;
            // the elements accumulate, which is what the caller receives (§10.3)
            std::vector<Json> elements;
            // the whitespace is skipped, because json permits it after the bracket (§10.3)
            skip_space(reader);
            // an immediately closing bracket is an empty array (§10.3)
            if (reader.at < reader.text.size() && reader.text[reader.at] == ']') {
                // the position moves past the bracket, so the value is complete (§10.3)
                reader.at++;
                // the empty array is returned, which is a legal value (§10.3)
                return Json(std::move(elements));
            }
            // the loop runs until the closing bracket ends the list (§10.3)
            while (true) {
                // each element is read whole, so a nested array or object parses as one value (§10.3)
                std::optional<Json> element = read_value(reader);
                // an element that does not parse makes the whole array malformed (§10.3)
                if (!element)
                    return std::nullopt;
                // the element joins the list, in the order it was written (§10.3)
                elements.push_back(std::move(*element));
                // the whitespace is skipped, because json permits it before the comma (§10.3)
                skip_space(reader);
                // the end of the text mid list is malformed (§10.3)
                if (reader.at >= reader.text.size())
                    return std::nullopt;
                // a comma continues the list (§10.3)
                if (reader.text[reader.at] == ',') {
                    // the position moves past the comma, so the next element starts (§10.3)
                    reader.at++;
                    // the loop continues, which is the whole of the list (§10.3)
                    continue;
                }
                // a closing bracket ends the list (§10.3)
                if (reader.text[reader.at] == ']') {
                    // the position moves past the bracket, so the value is complete (§10.3)
                    reader.at++;
                    // the array is returned, which is the whole of a list (§10.3)
                    return Json(std::move(elements));
                }
                // anything else between elements is malformed (§10.3)
                return std::nullopt;
            }
        }

        // read a json object, which is a braced map of values (§10.3)
        std::optional<Json> read_object(Reader& reader) {
            // the opening brace is required, because an object is always delimited (§10.3)
            if (reader.at >= reader.text.size() || reader.text[reader.at] != '{')
                return std::nullopt;
            // the position moves past the brace, so the first member starts (§10.3)
            reader.at++;
            // the members accumulate, which is what the caller receives (§10.3)
            std::map<std::string, Json, std::less<>> members;
            // the whitespace is skipped, because json permits it after the brace (§10.3)
            skip_space(reader);
            // an immediately closing brace is an empty object (§10.3)
            if (reader.at < reader.text.size() && reader.text[reader.at] == '}') {
                // the position moves past the brace, so the value is complete (§10.3)
                reader.at++;
                // the empty object is returned, which is a legal value (§10.3)
                return Json(std::move(members));
            }
            // the loop runs until the closing brace ends the map (§10.3)
            while (true) {
                // the whitespace is skipped, because json permits it before a member name (§10.3)
                skip_space(reader);
                // the member name is a string, which json requires even when the wire could omit the quotes (§10.3)
                std::optional<std::string> name = read_string(reader);
                // a member whose name is not a string makes the object malformed (§10.3)
                if (!name)
                    return std::nullopt;
                // the whitespace is skipped, because json permits it before the colon (§10.3)
                skip_space(reader);
                // the colon is required, because it separates the name from the value (§10.3)
                if (reader.at >= reader.text.size() || reader.text[reader.at] != ':')
                    return std::nullopt;
                // the position moves past the colon, so the value starts (§10.3)
                reader.at++;
                // the value is read whole, so a nested object parses as one value (§10.3)
                std::optional<Json> value = read_value(reader);
                // a member whose value does not parse makes the object malformed (§10.3)
                if (!value)
                    return std::nullopt;
                // the member joins the map, which a later duplicate replaces (§10.3)
                members.insert_or_assign(std::move(*name), std::move(*value));
                // the whitespace is skipped, because json permits it before the comma (§10.3)
                skip_space(reader);
                // the end of the text mid object is malformed (§10.3)
                if (reader.at >= reader.text.size())
                    return std::nullopt;
                // a comma continues the object (§10.3)
                if (reader.text[reader.at] == ',') {
                    // the position moves past the comma, so the next member starts (§10.3)
                    reader.at++;
                    // the loop continues, which is the whole of the object (§10.3)
                    continue;
                }
                // a closing brace ends the object (§10.3)
                if (reader.text[reader.at] == '}') {
                    // the position moves past the brace, so the value is complete (§10.3)
                    reader.at++;
                    // the object is returned, which is the whole of a map (§10.3)
                    return Json(std::move(members));
                }
                // anything else between members is malformed (§10.3)
                return std::nullopt;
            }
        }

        // read one json value, which recurses for the nested kinds (§10.3)
        std::optional<Json> read_value(Reader& reader) {
            // the whitespace is skipped, because json permits it before any value (§10.3)
            skip_space(reader);
            // the end of the text is malformed, because a value was expected (§10.3)
            if (reader.at >= reader.text.size())
                return std::nullopt;
            // the character decides which kind of value this is (§10.3)
            switch (reader.text[reader.at]) {
            case '{':
                // an object opens with a brace (§10.3)
                return read_object(reader);
            case '[':
                // an array opens with a bracket (§10.3)
                return read_array(reader);
            case '"': {
                // a string opens with a quote (§10.3)
                std::optional<std::string> body = read_string(reader);
                // a string that does not parse makes the value absent (§10.3)
                if (!body)
                    return std::nullopt;
                // the body becomes the value, which is the whole of a string (§10.3)
                return Json(std::move(*body));
            }
            case 't': {
                // the literal is matched whole, because a prefix of it is not json (§10.3)
                constexpr std::string_view word = "true";
                // a literal that is cut short by the end of the text is malformed (§10.3)
                if (reader.text.size() - reader.at < word.size())
                    return std::nullopt;
                // the literal must match exactly, which is what makes it json rather than a prefix (§10.3)
                if (reader.text.substr(reader.at, word.size()) != word)
                    return std::nullopt;
                // the position moves past the literal (§10.3)
                reader.at += word.size();
                // true is the boolean, which is the whole of the literal (§10.3)
                return Json(true);
            }
            case 'f': {
                // the literal is matched whole, because a prefix of it is not json (§10.3)
                constexpr std::string_view word = "false";
                // a literal that is cut short by the end of the text is malformed (§10.3)
                if (reader.text.size() - reader.at < word.size())
                    return std::nullopt;
                // the literal must match exactly, which is what makes it json rather than a prefix (§10.3)
                if (reader.text.substr(reader.at, word.size()) != word)
                    return std::nullopt;
                // the position moves past the literal (§10.3)
                reader.at += word.size();
                // false is the boolean, which is the whole of the literal (§10.3)
                return Json(false);
            }
            case 'n': {
                // the literal is matched whole, because a prefix of it is not json (§10.3)
                constexpr std::string_view word = "null";
                // a literal that is cut short by the end of the text is malformed (§10.3)
                if (reader.text.size() - reader.at < word.size())
                    return std::nullopt;
                // the literal must match exactly, which is what makes it json rather than a prefix (§10.3)
                if (reader.text.substr(reader.at, word.size()) != word)
                    return std::nullopt;
                // the position moves past the literal (§10.3)
                reader.at += word.size();
                // null is the absent value, which is the whole of the literal (§10.3)
                return Json();
            }
            default:
                break;
            }
            // a number begins with a digit or a sign, which is what distinguishes it from anything else (§10.3)
            const char first = reader.text[reader.at];
            // anything that is not a number start is malformed (§10.3)
            if (!((first >= '0' && first <= '9') || first == '-' || first == '+'))
                return std::nullopt;
            // the extent of the number is found, which is what strtod consumes (§10.3)
            const char* begin = reader.text.data() + reader.at;
            // the remainder is what strtod stops at, which is where the number ends (§10.3)
            char* end = nullptr;
            // the number is converted, which is both the parse and the extent (§10.3)
            const double value = std::strtod(begin, &end);
            // a conversion that consumed nothing means the text was not a number (§10.3)
            if (end == begin)
                return std::nullopt;
            // the position advances past what was consumed, which is the whole of the number (§10.3)
            reader.at += static_cast<std::size_t>(end - begin);
            // the value is returned, which is the whole of a number (§10.3)
            return Json(value);
        }

        // escape a string for the wire, which is the inverse of the parser's escapes (§10.3)
        std::string escape(const std::string& value) {
            // the output accumulates, which is what the caller receives (§10.3)
            std::string out;
            // each character is walked, because each may need escaping (§10.3)
            for (const char c : value) {
                // the character decides, because only some need escaping (§10.3)
                switch (c) {
                case '"':
                    // a quote ends a json string, so it must be escaped (§10.3)
                    out += "\\\"";
                    break;
                case '\\':
                    // a backslash introduces an escape, so it must be escaped (§10.3)
                    out += "\\\\";
                    break;
                case '\n':
                    // a newline is a control character, which json writes as an escape (§10.3)
                    out += "\\n";
                    break;
                case '\t':
                    // a tab is a control character, which json writes as an escape (§10.3)
                    out += "\\t";
                    break;
                case '\r':
                    // a return is a control character, which json writes as an escape (§10.3)
                    out += "\\r";
                    break;
                default:
                    // anything else is written as itself (§10.3)
                    out.push_back(c);
                    break;
                }
            }
            // the escaped text is returned, which is what goes on the wire (§10.3)
            return out;
        }
    } // namespace

    Json::Json() = default;
    Json::Json(bool value) :
        m_kind(Kind::Boolean), m_boolean(value) {}
    Json::Json(double value) :
        m_kind(Kind::Number), m_number(value) {}
    Json::Json(std::string value) :
        m_kind(Kind::String), m_string(std::move(value)) {}
    Json::Json(std::vector<Json> value) :
        m_kind(Kind::Array), m_array(std::move(value)) {}
    Json::Json(std::map<std::string, Json, std::less<>> value) :
        m_kind(Kind::Object), m_object(std::move(value)) {}

    Json::Kind Json::kind() const { return m_kind; }
    bool Json::is_object() const { return m_kind == Kind::Object; }
    bool Json::as_bool() const { return m_boolean; }
    double Json::as_number() const { return m_number; }
    const std::string& Json::as_string() const { return m_string; }
    const std::vector<Json>& Json::as_array() const { return m_array; }
    const std::map<std::string, Json, std::less<>>& Json::as_object() const { return m_object; }

    const Json* Json::member(std::string_view name) const {
        // a non object has no members, which is the quiet default (§10.3)
        if (m_kind != Kind::Object)
            return nullptr;
        // the member is looked up, because a request reads its members by name (§10.3)
        const auto found = m_object.find(name);
        // an absent member reads as null, which is what an optional field is (§10.3)
        return found == m_object.end() ? nullptr : &found->second;
    }

    void Json::set(std::string name, Json value) {
        // a non object cannot hold members, so the kind is corrected rather than silently dropping the write (§10.3)
        if (m_kind != Kind::Object) {
            // the kind becomes object, because that is the only value that holds members (§10.3)
            m_kind = Kind::Object;
            // the previous contents go, because they were not members (§10.3)
            m_object.clear();
        }
        // the member is added or replaced, which is how a reply is assembled (§10.3)
        m_object.insert_or_assign(std::move(name), std::move(value));
    }

    void Json::push_back(Json value) {
        // a non array cannot hold elements, so the kind is corrected rather than silently dropping the write (§10.3)
        if (m_kind != Kind::Array) {
            // the kind becomes array, because that is the only value that holds elements (§10.3)
            m_kind = Kind::Array;
            // the previous contents go, because they were not elements (§10.3)
            m_array.clear();
        }
        // the element joins the list, in the order it was added (§10.3)
        m_array.push_back(std::move(value));
    }

    std::string Json::to_string() const {
        // the output accumulates, which is what the caller receives (§10.3)
        std::string out;
        // the kind decides the spelling, because json has one spelling per kind (§10.3)
        switch (m_kind) {
        case Kind::Null:
            // null is the literal, which is how an absent value is written (§10.3)
            out += "null";
            break;
        case Kind::Boolean:
            // the two literals are the whole of a boolean (§10.3)
            out += m_boolean ? "true" : "false";
            break;
        case Kind::Number: {
            // a whole number is written without a fraction, because that is what a client expects (§10.3)
            if (m_number == std::floor(m_number) && std::abs(m_number) < 1e15)
                out += std::to_string(static_cast<long long>(m_number));
            else
                // anything else needs the general spelling (§10.3)
                out += std::to_string(m_number);
            break;
        }
        case Kind::String:
            // the quotes delimit it, and the body is escaped (§10.3)
            out += '"';
            // the escaped body goes between the quotes (§10.3)
            out += escape(m_string);
            // the closing quote ends it (§10.3)
            out += '"';
            break;
        case Kind::Array: {
            // the bracket delimits the list (§10.3)
            out += '[';
            // each element is written whole, which is what makes a nested list readable (§10.3)
            for (std::size_t i = 0; i < m_array.size(); i++) {
                // the comma separates the elements (§10.3)
                if (i != 0)
                    out += ',';
                // the element is written, which recurses for a nested value (§10.3)
                out += m_array[i].to_string();
            }
            // the bracket closes the list (§10.3)
            out += ']';
            break;
        }
        case Kind::Object: {
            // the brace delimits the map (§10.3)
            out += '{';
            // each member is written whole, which is what makes a nested object readable (§10.3)
            bool first = true;
            for (const auto& [name, value] : m_object) {
                // the comma separates the members (§10.3)
                if (!first)
                    out += ',';
                // the first member carries no comma (§10.3)
                first = false;
                // the name is quoted, which json requires (§10.3)
                out += '"';
                // the name is escaped, because a name may hold any character (§10.3)
                out += escape(name);
                // the closing quote ends the name (§10.3)
                out += '"';
                // the colon separates the name from the value (§10.3)
                out += ':';
                // the value is written, which recurses for a nested value (§10.3)
                out += value.to_string();
            }
            // the brace closes the map (§10.3)
            out += '}';
            break;
        }
        }
        // the rendered text is returned, which is what goes on the wire (§10.3)
        return out;
    }

    std::optional<Json> Json::parse(std::string_view text) {
        // the reader holds the text, which lives as long as this call (§10.3)
        Reader reader{.text = text, .at = 0};
        // the value is read whole, which is the whole of a document (§10.3)
        std::optional<Json> value = read_value(reader);
        // a document that does not parse is absent, which the caller reports as malformed (§10.3)
        if (!value)
            return std::nullopt;
        // the trailing whitespace is skipped, because json permits it after the value (§10.3)
        skip_space(reader);
        // anything after the value is malformed, because a document is one value (§10.3)
        if (reader.at != text.size())
            return std::nullopt;
        // the value is returned, which is the whole of a parse (§10.3)
        return value;
    }
} // namespace albor::core
