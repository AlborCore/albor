#include "devrt/ir.h"

namespace albor::devrt
{
    namespace
    {
        // the prefix a node line carries, which is what makes the format readable (§12.1)
        constexpr std::string_view NODE_PREFIX = "node ";

        // the prefix a property line carries, which separates it from a node (§12.1)
        constexpr std::string_view PROPERTY_PREFIX = "prop ";

        // the marker that stands in for an absent id, because an empty field would be ambiguous on a line (§12.1, AD-11)
        constexpr std::string_view NO_ID = "-";

        // the leading space count that gives a line its depth, which is what the nesting is encoded with (§12.1)
        std::size_t indentation(const std::string_view line) {
            // the depth counts spaces, which is what the writer emits (§12.1)
            std::size_t depth = 0;
            // the walk stops at the first non space, because the content starts there (§12.1)
            while (depth < line.size() && line[depth] == ' ')
                depth++;
            // the depth is returned, which is how a reader nests (§12.1)
            return depth;
        }

        // the value written after a key, which is how an id or a field is carried (§12.1)
        std::optional<std::string> field_after(const std::string_view body, std::string_view key) {
            // the key is located, because the field is written after it (§12.1)
            const std::size_t at = body.find(key);
            // an absent field is reported rather than defaulted (§12.1)
            if (at == std::string::npos)
                return std::nullopt;
            // the value starts after the key (§12.1)
            std::size_t start = at + key.size();
            // the equals sign is optional, because a reader should not require a separator (§12.1)
            if (start < body.size() && body[start] == '=')
                start++;
            // the value runs to the end of the line, because a value never spans one (§12.1)
            return std::string(body.substr(start));
        }
    } // namespace

    std::optional<IrNode> parse_ir(const std::string& text) {
        // one record per node line, which is what the text is read into before any tree exists (§12.1)
        struct Record
        {
            // the element name, which is what builds the node (§12.1)
            std::string element;

            // the author assigned id, which a reload matches on (§12.1, AD-11)
            std::string id;

            // the properties the line assigned (§12.1)
            std::vector<IrProperty> properties;

            // the record this one hangs from, which the indentation decides (§12.1)
            std::size_t parent = 0;

            // how deep the line was indented, which is what the stack closes on (§12.1)
            std::size_t depth = 0;

            // whether this record is a node rather than a property (§12.1)
            bool node = true;
        };

        // the records accumulate in the order they were read (§12.1)
        std::vector<Record> records;
        // whether a root node has been seen, because a definition describes exactly one (§12.1)
        bool has_root = false;
        // the open nodes by depth, which is how a closing brace in text closes a node (§12.1)
        std::vector<std::pair<std::size_t, std::size_t>> open;
        // the text is walked line by line, because the format is line oriented (§12.1)
        std::size_t at = 0;
        // the loop runs until the text ends (§12.1)
        while (at <= text.size()) {
            // the line ends at a newline or at the end of the text (§12.1)
            const std::size_t end = text.find('\n', at);
            // the line is taken whole, because a value never spans one (§12.1)
            const std::string_view line(text.data() + at, (end == std::string::npos ? text.size() : end) - at);
            // the position advances past the newline, which is what the loop continues from (§12.1)
            const bool last = end == std::string::npos;
            // the next position is after the line (§12.1)
            at = last ? text.size() + 1 : end + 1;
            // a blank line carries nothing (§12.1)
            if (line.empty())
                continue;
            // the depth is how far the line is nested (§12.1)
            const std::size_t depth = indentation(line);
            // the content is what remains once the indentation is taken (§12.1)
            const std::string_view content = line.substr(depth);
            // a property belongs to the innermost open node, and belongs to no node when none is open (§12.1)
            if (content.rfind(PROPERTY_PREFIX, 0) == 0) {
                // a property with no owner is malformed, because nothing could apply it (§12.1)
                if (open.empty())
                    return std::nullopt;
                // the property is split at its first space, which divides the name from the value (§12.1)
                const std::string_view body = content.substr(PROPERTY_PREFIX.size());
                // the separator is located, because a property carries both parts (§12.1)
                const std::size_t space = body.find(' ');
                // a property with no separator has no value, which is malformed (§12.1)
                if (space == std::string_view::npos)
                    return std::nullopt;
                // the property joins the node the indentation named (§12.1)
                records[open.back().second].properties.push_back(IrProperty{.name = std::string(body.substr(0, space)), .value = std::string(body.substr(space + 1))});
                // the line is consumed (§12.1)
                continue;
            }
            // anything that is neither a node nor a property is malformed (§12.1)
            if (content.rfind(NODE_PREFIX, 0) != 0)
                return std::nullopt;
            // the body follows the node prefix (§12.1)
            const std::string_view body = content.substr(NODE_PREFIX.size());
            // the element name runs to the first space, because fields follow it (§12.1)
            const std::size_t space = body.find(' ');
            // the element is the whole body when no field follows (§12.1)
            const std::string_view element = space == std::string_view::npos ? body : body.substr(0, space);
            // a node with no element name is malformed, because nothing could build it (§12.1)
            if (element.empty())
                return std::nullopt;
            // the stack closes back to this depth, because the closed nodes are siblings of the new one (§12.1)
            while (!open.empty() && open.back().first >= depth)
                open.pop_back();
            // the record is added, which is what the parent will link to by index (§12.1)
            Record record;
            // the element is stored, because that is what builds the node (§12.1)
            record.element = std::string(element);
            // the depth is stored, because the stack closes on it (§12.1)
            record.depth = depth;
            // the id field is read when present, because identity is what a reload matches on (§12.1, AD-11)
            if (space != std::string_view::npos) {
                // the field is searched for by name (§12.1, AD-11)
                if (const auto id = field_after(body, "id"); id && *id != NO_ID)
                    // the id is stored, because a marker means the author named nothing (§12.1, AD-11)
                    record.id = *id;
            }
            // a node outside any open node is a root, and a definition describes exactly one (§12.1)
            if (open.empty()) {
                // a second root is malformed, because a definition describes one component (§12.1)
                if (has_root)
                    return std::nullopt;
                // the record is adopted as the root (§12.1)
                has_root = true;
            }
            else {
                // the record hangs from the innermost open node, which is what the indentation named (§12.1)
                record.parent = open.back().second;
            }
            // the record joins the list, whose index is what a parent links to (§12.1)
            records.push_back(std::move(record));
            // the node is opened at this depth, because its own children hang from it (§12.1)
            open.emplace_back(depth, records.size() - 1);
        }
        // a definition with no root node describes nothing, which is malformed (§12.1)
        if (!has_root)
            return std::nullopt;
        // the children of each record are collected first, because a record names its parent by index (§12.1)
        std::vector<std::vector<std::size_t>> children(records.size());
        // every record but the root hangs from the parent its line recorded (§12.1)
        for (std::size_t index = 1; index < records.size(); index++)
            // the record joins its parent's list, which is what the indentation decided (§12.1)
            children[records[index].parent].push_back(index);
        // the tree is then materialized, because a record stores its parent by index rather than a pointer (§12.1)
        std::optional<IrNode> root;
        // the walk builds each node from its record and the records below it (§12.1)
        const auto build = [&records, &children](std::size_t index, auto&& self) -> IrNode {
            // the record is read, because it is the node's whole content (§12.1)
            const Record& record = records[index];
            // the node takes the record's element, which is what builds it (§12.1)
            IrNode node;
            // the element is copied (§12.1)
            node.element = record.element;
            // the id is copied, because identity is what a reload matches on (§12.1, AD-11)
            node.id = record.id;
            // the properties are moved, because they were already read from the text (§12.1)
            node.properties = std::move(records[index].properties);
            // each child record becomes a node, in the order the lines named them (§12.1)
            for (std::size_t child : children[index])
                // the child is built by the same walk, which is the whole of the recursion (§12.1)
                node.children.push_back(self(child, self));
            // the node is returned whole (§12.1)
            return node;
        };
        // the root is the first record, because it is the only one with no parent (§12.1)
        root = build(0, build);
        // the tree is returned whole, which is what the adapter reconciles (§12.1)
        return root;
    }

    std::string write_ir(const IrNode& root) {
        // the output accumulates, which is what the caller receives (§12.1)
        std::string out;
        // the walk visits a node and everything under it, because a definition is a tree (§12.1)
        const auto emit = [&out](const IrNode& node, std::size_t depth, auto&& self) -> void {
            // the indentation is the depth, which is what the reader nests by (§12.1)
            out.append(depth, ' ');
            // the node prefix opens the line (§12.1)
            out += NODE_PREFIX;
            // the element name is written, because that is what builds the node (§12.1)
            out += node.element;
            // the id separator is written either way, so every node line carries the field (§12.1, AD-11)
            out += " id=";
            // the id is written when the author named one, because identity is what a reload matches on (§12.1, AD-11)
            if (!node.id.empty())
                // the id follows (§12.1, AD-11)
                out += node.id;
            else
                // the marker stands in for "the author named nothing" (§12.1, AD-11)
                out += NO_ID;
            // the newline ends the line, because the format is line oriented (§12.1)
            out.push_back('\n');
            // each property is written on its own line, inside its node (§12.1)
            for (const IrProperty& property : node.properties) {
                // the property is written one level deeper, because it belongs to the node above (§12.1)
                out.append(depth + 1, ' ');
                // the property prefix opens the line (§12.1)
                out += PROPERTY_PREFIX;
                // the name is written, which is what a setter is looked up by (§12.1)
                out += property.name;
                // the separator divides the name from the value (§12.1)
                out.push_back(' ');
                // the value follows as written (§12.1)
                out += property.value;
                // the newline ends the property line (§12.1)
                out.push_back('\n');
            }
            // each child is written one level deeper, which is what encodes the nesting (§12.1)
            for (const IrNode& child : node.children)
                // the child is emitted by the same walk, which is the whole of the recursion (§12.1)
                self(child, depth + 1, self);
        };
        // the walk starts at the root, which is what the reader expects first (§12.1)
        emit(root, 0, emit);
        // the text is returned whole, which is what a compiler driver writes (§12.1)
        return out;
    }
} // namespace albor::devrt
