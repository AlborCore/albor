#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace albor::core
{
    // one json value, which is what both the automation mcp adapter and the language server speak (§10.3, §12.2)
    class Json
    {
    public:
        // the kinds a value can take, which is the whole of what the wire carries (§10.3)
        enum class Kind
        {
            // the absence of a value, which json spells null
            Null,

            // a boolean, which is true or false and nothing else
            Boolean,

            // a number, which the wire may spell with or without a fraction (§10.3)
            Number,

            // a string, which carries escapes the parser resolves (§10.3)
            String,

            // an ordered list, which is what a json array is (§10.3)
            Array,

            // a keyed map, which is what a json object is and what every request is (§10.3)
            Object
        };

        // build an absent value, which is what a member that was never set reads as (§10.3)
        Json();

        // build a boolean, which is one of the kinds a value takes (§10.3)
        explicit Json(bool value);

        // build a number, which is one of the kinds a value takes (§10.3)
        explicit Json(double value);

        // build a string, which is one of the kinds a value takes (§10.3)
        explicit Json(std::string value);

        // build an array, which is one of the kinds a value takes (§10.3)
        explicit Json(std::vector<Json> value);

        // build an object, which is one of the kinds a value takes (§10.3)
        explicit Json(std::map<std::string, Json, std::less<>> value);

        // report which kind this value is, which a reader dispatches on (§10.3)
        [[nodiscard]] Kind kind() const;

        // report whether this value is an object, which a request always is (§10.3)
        [[nodiscard]] bool is_object() const;

        // read the boolean, which is only meaningful when this value is a boolean (§10.3)
        [[nodiscard]] bool as_bool() const;

        // read the number, which is only meaningful when this value is a number (§10.3)
        [[nodiscard]] double as_number() const;

        // read the string, which is only meaningful when this value is a string (§10.3)
        [[nodiscard]] const std::string& as_string() const;

        // read the array, which is only meaningful when this value is an array (§10.3)
        [[nodiscard]] const std::vector<Json>& as_array() const;

        // read the object, which is only meaningful when this value is an object (§10.3)
        [[nodiscard]] const std::map<std::string, Json, std::less<>>& as_object() const;

        // read the member of an object, returning null when it is absent or this is not an object (§10.3)
        [[nodiscard]] const Json* member(std::string_view name) const;

        // add or replace an object member, which a reply is assembled from (§10.3)
        void set(std::string name, Json value);

        // add to an array, which a reply is assembled from (§10.3)
        void push_back(Json value);

        // render this value as json, which is what a reply is written as (§10.3)
        [[nodiscard]] std::string to_string() const;

        // parse one json document, returning nothing when the text is not valid json (§10.3)
        [[nodiscard]] static std::optional<Json> parse(std::string_view text);

    private:
        // which kind this value is, which every reader dispatches on (§10.3)
        Kind m_kind = Kind::Null;

        // the boolean, which is only read when the kind is boolean (§10.3)
        bool m_boolean = false;

        // the number, which is only read when the kind is number (§10.3)
        double m_number = 0.0;

        // the string, which is only read when the kind is string (§10.3)
        std::string m_string;

        // the array, which is only read when the kind is array (§10.3)
        std::vector<Json> m_array;

        // the object, which is only read when the kind is object (§10.3)
        std::map<std::string, Json, std::less<>> m_object;
    };
} // namespace albor::core
