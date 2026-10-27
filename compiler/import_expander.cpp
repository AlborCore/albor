#include "compiler/import_expander.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "compiler/value_type.h"

namespace albor::compiler
{
    namespace
    {
        // one property value an imported tree reads, with the file its expression was written in (§11.1)
        struct Substitution
        {
            // the value the property takes, which is null when the instance assigned none (§11.1)
            SharedExpression value;

            // the file that wrote the value, which resolves an embedded asset beside it (§6.4)
            std::filesystem::path source_path;
        };

        // the property values an imported tree reads, which an instance substitutes while its body expands (§11.1)
        using Substitutions = std::map<std::string, Substitution, std::less<>>;

        // build a path from the utf-8 bytes a ui literal carries, which a windows code page does not decode (§6.4)
        std::filesystem::path path_from_utf8(std::string_view text) {
            // the u8 spelling is what makes the conversion locale independent (§6.4)
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        // write a native path as utf-8, which a design time asset path must carry across platforms (§6.4)
        std::string path_to_utf8(const std::filesystem::path& path) {
            // the generic form keeps one separator spelling, which the reader takes on every platform (§6.4)
            const std::u8string text = path.generic_u8string();
            // the bytes are returned unchanged, because the reader decodes utf-8 itself (§6.4)
            return std::string(reinterpret_cast<const char*>(text.data()), text.size());
        }

        // resolve an embedded asset beside the file that wrote it, which is where a design time build finds it (§6.4)
        std::string resolved_asset_path(std::string_view written, const std::filesystem::path& source_path) {
            // the literal path is decoded from utf-8, because it arrived as bytes in the source (§6.4)
            const std::filesystem::path asset = path_from_utf8(written);
            // an unsaved source or an already absolute path is left as written (§6.4)
            if (source_path.empty() || asset.is_absolute())
                return std::string(written);
            // the path resolves beside the defining source, which is what the compiled path does (§6.4)
            std::filesystem::path resolved = (source_path.parent_path() / asset).lexically_normal();
            // the absolute form is what reaches a process whose working directory is not the project (§12.1)
            std::error_code error;
            // the absolute path is the one the preview and the dev runtime can open from any directory (§12.1)
            const std::filesystem::path absolute = std::filesystem::absolute(resolved, error);
            // a failed lookup keeps the normalized relative path rather than losing the asset (§6.4)
            if (!error)
                resolved = absolute;
            // the path is returned as utf-8, which every design time reader decodes (§6.4)
            return path_to_utf8(resolved);
        }

        // find the interface an element name resolves against, which is the same name the checker accepted (§11.1)
        const ImportedComponent* find_import(const std::vector<ImportedComponent>& imports, std::string_view name) {
            // the interface list is walked, because a component name is unique in one imported set (§11.1)
            for (const ImportedComponent& imported : imports) {
                // the name decides, so an instance resolves to exactly one tree (§11.1)
                if (imported.name == name)
                    return &imported;
            }
            // a name outside the set is not an imported component (§11.1)
            return nullptr;
        }

        // find the assignment an instance wrote for a declared property, which overrides the default (§11.1)
        const CheckedAssignment* assignment_named(const CheckedNode& node, std::string_view name) {
            // the assignments are walked in written order, because the checker refused a duplicate (§11.1)
            for (const CheckedAssignment& assignment : node.assignments) {
                // the property name decides, so one instance assignment replaces one value (§11.1)
                if (assignment.name == name)
                    return &assignment;
            }
            // a property the instance did not assign keeps its default (§11.1)
            return nullptr;
        }

        // find a declared property, which decides whether a name is substituted or treated as common (§11.1)
        const CheckedProperty* property_named(const ImportedComponent& imported, std::string_view name) {
            // the declarations are walked, because a component owns a handful of properties (§11.1)
            for (const CheckedProperty& property : imported.properties) {
                // the name decides, so one declaration shadows a common property of the same spelling (§11.1)
                if (property.name == name)
                    return &property;
            }
            // a name outside the declarations belongs to the common element surface (§11.1)
            return nullptr;
        }

        // report whether a name is a common element property, which an instance applies to the component root (§11.1)
        bool common_property(std::string_view name) {
            // the common set is fixed, so the lookup is a short scan (§11.1)
            for (const ElementProperty& property : common_element_properties()) {
                // the name decides, because only that property reaches the expanded root (§11.1)
                if (property.name == name)
                    return true;
            }
            // a declared property is not common, even when it shares a spelling (§11.1)
            return false;
        }

        // copy one expression with every property reference resolved, which keeps its form and written span (§11.1)
        ExpressionPtr resolve_expression(const Expression* expression, const Substitutions& substitutions, const std::filesystem::path& source_path, std::vector<std::string>& visiting) {
            // a missing expression carries nothing, so there is nothing to substitute (§11.1)
            if (expression == nullptr)
                return nullptr;
            // a literal is copied, because the imported tree is shared and must not be changed (§11.1)
            if (const auto* literal = dynamic_cast<const LiteralExpression*>(expression); literal != nullptr)
                return LiteralExpression::make(literal->kind, literal->text, literal->span);
            // a property reference is replaced by the value its instance supplied (§11.1)
            if (const auto* reference = dynamic_cast<const PropertyExpression*>(expression); reference != nullptr) {
                // a reference outside the imported declarations belongs to the importing scope (§11.1)
                const auto found = substitutions.find(reference->name);
                // a property without a value is copied, which the design time reader then drops (§12.1)
                if (found == substitutions.end() || found->second.value == nullptr)
                    return PropertyExpression::make(reference->name, reference->span);
                // a cycle leaves the reference in place, because no finite value exists for it (§11.1)
                if (std::find(visiting.begin(), visiting.end(), reference->name) != visiting.end())
                    return PropertyExpression::make(reference->name, reference->span);
                // the property enters the path before its value is resolved, which is what detects a cycle (§11.1)
                visiting.push_back(reference->name);
                // the replacement is resolved against the file that wrote it, because it may hold an asset (§11.1)
                ExpressionPtr resolved = resolve_expression(found->second.value.get(), substitutions, found->second.source_path, visiting);
                // the property leaves the path, because its substitution is complete (§11.1)
                visiting.pop_back();
                // a replacement outside the known forms is copied as the written reference (§11.1)
                return resolved != nullptr ? std::move(resolved) : PropertyExpression::make(reference->name, reference->span);
            }
            // a translation is copied, because the catalogue resolves it at display time (§9.2)
            if (const auto* translation = dynamic_cast<const TranslationExpression*>(expression); translation != nullptr)
                return TranslationExpression::make(translation->text, translation->span);
            // an image is resolved beside the file that wrote it, because a design time reader has no source (§6.4)
            if (const auto* image = dynamic_cast<const ImageExpression*>(expression); image != nullptr)
                return ImageExpression::make(resolved_asset_path(image->path, source_path), image->span);
            // a conversion is copied with its operand resolved, because the value may read a property (§11.1)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(expression); conversion != nullptr)
                return ConversionExpression::make(conversion->target_type, resolve_expression(conversion->value.get(), substitutions, source_path, visiting), conversion->span);
            // a call is copied with each argument resolved, because an argument may read a property (§11.1)
            if (const auto* call = dynamic_cast<const CallExpression*>(expression); call != nullptr) {
                // the arguments accumulate in written order, which is the order the checker typed them (§11.1)
                std::vector<ExpressionPtr> arguments;
                // each argument is resolved by the same rule, which is the whole recursion (§11.1)
                for (const ExpressionPtr& argument : call->arguments)
                    arguments.push_back(resolve_expression(argument.get(), substitutions, source_path, visiting));
                // the resolved call replaces the original (§11.1)
                return CallExpression::make(call->function, std::move(arguments), call->span);
            }
            // a binary application is copied with both operands resolved (§11.1)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(expression); binary != nullptr)
                return BinaryExpression::make(binary->op, resolve_expression(binary->left.get(), substitutions, source_path, visiting), resolve_expression(binary->right.get(), substitutions, source_path, visiting), binary->span);
            // a unary application is copied with its operand resolved (§11.1)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(expression); unary != nullptr)
                return UnaryExpression::make(unary->op, resolve_expression(unary->value.get(), substitutions, source_path, visiting), unary->span);
            // a grouping is copied with its inner expression resolved (§11.1)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(expression); grouping != nullptr)
                return GroupingExpression::make(resolve_expression(grouping->inner.get(), substitutions, source_path, visiting), grouping->span);
            // an array is copied with every element resolved, because an element may read a property (§11.1)
            if (const auto* array = dynamic_cast<const ArrayExpression*>(expression); array != nullptr) {
                // the elements accumulate in written order, which the collection preserves (§8.1)
                std::vector<ExpressionPtr> elements;
                // each element is resolved by the same rule, which is the whole recursion (§11.1)
                for (const ExpressionPtr& element : array->elements)
                    elements.push_back(resolve_expression(element.get(), substitutions, source_path, visiting));
                // the resolved list replaces the original (§11.1)
                return ArrayExpression::make(std::move(elements), array->span);
            }
            // a record construction is copied with every field value resolved (§11.1)
            if (const auto* construction = dynamic_cast<const RecordConstructionExpression*>(expression); construction != nullptr) {
                // the fields accumulate in written order, which the record preserves (§11.1)
                std::vector<ConstructionField> fields;
                // each field keeps its name and takes the resolved value (§11.1)
                for (const ConstructionField& field : construction->fields)
                    fields.push_back(ConstructionField{.name = field.name, .value = resolve_expression(field.value.get(), substitutions, source_path, visiting)});
                // the resolved construction replaces the original (§11.1)
                return RecordConstructionExpression::make(construction->type_name, std::move(fields), construction->span);
            }
            // a field read is copied with its object resolved, because the object may read a property (§11.2)
            if (const auto* access = dynamic_cast<const FieldAccessExpression*>(expression); access != nullptr)
                return FieldAccessExpression::make(resolve_expression(access->object.get(), substitutions, source_path, visiting), access->field, access->span);
            // an unknown expression has no copy, which drops the assignment rather than aliasing a shared tree (§11.1)
            return nullptr;
        }

        // copy an assignment with its property references resolved, which keeps its type and written span (§11.1)
        std::vector<CheckedAssignment> resolved_assignments(const std::vector<CheckedAssignment>& assignments, const Substitutions& substitutions, const std::filesystem::path& source_path) {
            // the copies accumulate in written order, because a later assignment for one name wins (§11.1)
            std::vector<CheckedAssignment> resolved;
            // each assignment is copied, because the imported tree is shared and must not be changed (§11.1)
            for (const CheckedAssignment& assignment : assignments) {
                // the copy keeps the property name, type, dependencies and span (§11.1)
                CheckedAssignment copy = assignment;
                // the visiting path is fresh for each value, because one substitution may reach another (§11.1)
                std::vector<std::string> visiting;
                // the value is resolved, because a reference is what an instance substitutes (§11.1)
                ExpressionPtr value = resolve_expression(assignment.value.get(), substitutions, source_path, visiting);
                // an assignment with no copy is dropped, because a shared alias would outlive this expansion (§11.1)
                if (value == nullptr)
                    continue;
                // the copy owns the resolved expression, which the design time tree keeps (§11.1)
                copy.value = SharedExpression(std::move(value));
                // the copy joins the result (§11.1)
                resolved.push_back(std::move(copy));
            }
            // the resolved copies are returned whole (§11.1)
            return resolved;
        }

        // expand one imported instance into the root its own file declares (§12.1)
        std::shared_ptr<CheckedNode> expand_node(const CheckedNode& node, const std::vector<ImportedComponent>& imports, const Substitutions& outer, const std::filesystem::path& source_path);

        // instantiate one imported component over the values its instance assigned (§12.1)
        std::shared_ptr<CheckedNode> instantiate_imported(const CheckedNode& instance, const ImportedComponent& imported, const Substitutions& outer, const std::filesystem::path& source_path) {
            // a component with no checked root has no tree to expand, which the loader already refused (§11.1)
            if (imported.root == nullptr)
                return nullptr;
            // the values the imported body reads are assembled from its instance and its defaults (§11.1)
            Substitutions substitutions;
            // every declared property is resolved once, because the expanded tree may read it many times (§11.1)
            for (const CheckedProperty& property : imported.properties) {
                // an assignment on the instance overrides the default, which is what composition means (§11.1)
                if (const CheckedAssignment* assignment = assignment_named(instance, property.name); assignment != nullptr) {
                    // the assigned value may read the importer's properties, so the outer substitution resolves it (§11.1)
                    std::vector<std::string> visiting;
                    // the value is resolved against the file that wrote the instance, which is where its asset sits (§6.4)
                    ExpressionPtr value = resolve_expression(assignment->value.get(), outer, source_path, visiting);
                    // a value with no copy carries no design time value, which the body drops (§12.1)
                    substitutions[property.name] = Substitution{.value = value != nullptr ? SharedExpression(std::move(value)) : nullptr, .source_path = source_path};
                    continue;
                }
                // a property with a default keeps it, because the instance said nothing about it (§11.1)
                if (property.has_default) {
                    // the default is stored as written, because its own references resolve when it is read (§11.1)
                    substitutions[property.name] = Substitution{.value = property.default_value, .source_path = imported.source_path};
                    continue;
                }
                // a property with neither carries no design time value, so its readers are dropped (§12.1)
                substitutions[property.name] = Substitution{.value = nullptr, .source_path = imported.source_path};
            }
            // the imported root is expanded against its own imports, which is the recursive half of composition (§11.1)
            std::shared_ptr<CheckedNode> expanded = expand_node(*imported.root, imported.imported_components, substitutions, imported.source_path);
            // a root that could not be expanded has nothing to hand back (§12.1)
            if (expanded == nullptr)
                return nullptr;
            // the instance's identity is the component identity, so it replaces the root's own when written (AD-11)
            if (!instance.explicit_id.empty())
                expanded->explicit_id = instance.explicit_id;
            // the common assignments apply to the component root, because no wrapper survives the expansion (§11.1)
            std::vector<CheckedAssignment> common = resolved_assignments(instance.assignments, outer, source_path);
            // an assignment that names a declared property belongs to the body, not to the root (§11.1)
            std::erase_if(common, [&imported](const CheckedAssignment& assignment) { return property_named(imported, assignment.name) != nullptr || !common_property(assignment.name); });
            // a common assignment replaces the root's own value, because the instance is more specific (§11.1)
            for (const CheckedAssignment& assignment : common) {
                // the root's own assignment for that name is removed first, because one node holds one value (§11.1)
                std::erase_if(expanded->assignments, [&assignment](const CheckedAssignment& existing) { return existing.name == assignment.name; });
            }
            // the instance's common assignments join the expanded root in written order (§11.1)
            expanded->assignments.insert(expanded->assignments.end(), common.begin(), common.end());
            // the expanded root is what a design time reader builds in place of the component instance (§12.1)
            return expanded;
        }

        // expand one tree, replacing every imported component element with the tree its file declares (§12.1)
        std::shared_ptr<CheckedNode> expand_node(const CheckedNode& node, const std::vector<ImportedComponent>& imports, const Substitutions& outer, const std::filesystem::path& source_path) {
            // an element that names an imported component is replaced by that component's root (§11.1)
            if (node.kind == TreeNodeKind::Element && !node.element_type.empty()) {
                // the imported interface is searched by the element name, which is what the checker accepted (§11.1)
                if (const ImportedComponent* imported = find_import(imports, node.element_type); imported != nullptr)
                    // the instance expands over its own declarations, because outer names do not reach its body (§11.1)
                    return instantiate_imported(node, *imported, outer, source_path);
            }
            // an ordinary node is copied, because the imported tree is shared and a preview may expand it again (§11.1)
            auto copy = std::make_shared<CheckedNode>(node);
            // the assignments are resolved, because an imported property reads the instance value (§11.1)
            copy->assignments = resolved_assignments(node.assignments, outer, source_path);
            // the children are expanded one by one, because any of them may instantiate an imported component (§11.1)
            copy->children.clear();
            for (const std::shared_ptr<CheckedNode>& child : node.children) {
                // a child expands by the same rule, which is the whole recursion (§11.1)
                if (child != nullptr)
                    copy->children.push_back(expand_node(*child, imports, outer, source_path));
            }
            // a repeated body expands too, because it may instantiate an imported component per item (§11.1)
            if (node.repeat_body != nullptr)
                copy->repeat_body = expand_node(*node.repeat_body, imports, outer, source_path);
            // a conditional body follows the same rule, because it is a subtree like any other (§11.1)
            if (node.conditional_body != nullptr)
                copy->conditional_body = expand_node(*node.conditional_body, imports, outer, source_path);
            // the copied tree is what the design time reader builds (§12.1)
            return copy;
        }
    } // namespace

    std::shared_ptr<CheckedNode> expand_imported_components(const std::shared_ptr<const CheckedNode>& root, const std::vector<ImportedComponent>& imports, const std::filesystem::path& source_path) {
        // a component with no root has no tree to expand (§11.1)
        if (root == nullptr)
            return nullptr;
        // the root has no outer scope, so the walk starts with no substitutions (§11.1)
        return expand_node(*root, imports, {}, source_path);
    }
} // namespace albor::compiler
