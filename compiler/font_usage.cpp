#include "compiler/font_usage.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace albor::compiler
{
    namespace
    {
        // one fallback family written as a literal, which a warning points at (§11.1)
        struct FallbackLiteral
        {
            // the family the author wrote (§9.1)
            std::string family;

            // where the family was written, which a diagnostic points at (§11.1)
            SourceSpan span;
        };

        // read a string literal, which is the only family value the compiler can check (§11.1)
        const std::string* literal_string(const Expression& expression) {
            // the expression must be a literal, because a computed family is unknown at compile time (§11.1)
            const auto* literal = dynamic_cast<const LiteralExpression*>(&expression);
            // a non literal or a literal of another kind names no family (§11.1)
            if (literal == nullptr || literal->kind != LiteralExpression::Kind::String)
                return nullptr;
            // the literal text is the family the author wrote (§9.1)
            return &literal->text;
        }

        // map a written weight member to the enumeration, which the catalog stores (§9.1)
        std::optional<FontWeight> written_weight(std::string_view member) {
            // each written member is the lower case spelling the named set declares (§9.1)
            if (member == "thin")
                return FontWeight::Thin;
            // the light member follows thin (§9.1)
            if (member == "light")
                return FontWeight::Light;
            // the regular member is the default (§9.1)
            if (member == "regular")
                return FontWeight::Regular;
            // the medium member follows regular (§9.1)
            if (member == "medium")
                return FontWeight::Medium;
            // the semibold member follows medium (§9.1)
            if (member == "semibold")
                return FontWeight::Semibold;
            // the bold member is the emphatic stroke (§9.1)
            if (member == "bold")
                return FontWeight::Bold;
            // the black member is the heaviest stroke (§9.1)
            if (member == "black")
                return FontWeight::Black;
            // an unknown member names no weight, which the checker already reported (§11.1)
            return std::nullopt;
        }

        // read a weight literal, which is written as the Weight set's member (§11.1)
        std::optional<FontWeight> literal_weight(const Expression& expression) {
            // the expression must be a field access on the Weight set (§11.1)
            const auto* access = dynamic_cast<const FieldAccessExpression*>(&expression);
            // a form other than Weight.member names no weight literal (§11.1)
            if (access == nullptr)
                return std::nullopt;
            // the object must name the builtin Weight set, because a record field is another expression (§11.1)
            const auto* set = dynamic_cast<const PropertyExpression*>(access->object.get());
            // a field of another object is not a weight member (§11.1)
            if (set == nullptr || set->name != "Weight")
                return std::nullopt;
            // the written member decides the weight, which the named set declares (§9.1)
            return written_weight(access->field);
        }

        // read the fallback families written as a literal list, which the compiler can check (§11.1)
        std::vector<FallbackLiteral> literal_fallbacks(const Expression& expression) {
            // the result holds the families the author wrote as literals (§9.1)
            std::vector<FallbackLiteral> families;
            // the expression must be a bracketed list, because a computed list is unknown at compile time (§11.1)
            const auto* array = dynamic_cast<const ArrayExpression*>(&expression);
            // a non list carries no literal fallback (§11.1)
            if (array == nullptr)
                return families;
            // every element is checked, because each one is an explicit family (§9.1)
            for (const ExpressionPtr& element : array->elements) {
                // the element must be a string literal, because a computed family is unknown (§11.1)
                const std::string* family = literal_string(*element);
                // a non literal element contributes no checkable family (§11.1)
                if (family == nullptr)
                    continue;
                // the literal joins the list with the span a warning points at (§11.1)
                families.push_back(FallbackLiteral{.family = *family, .span = element->location()});
            }
            // the literals are what the caller checks against the catalog (§11.1)
            return families;
        }

        // check one node's font selection and its children, which is the whole declarative surface (§11.1)
        void check_node(const CheckedNode& node, const FontCatalog& catalog, DiagnosticList& diagnostics) {
            // the assignments naming a font selection are resolved before the checks, because they travel together (§9.1)
            const CheckedAssignment* family_assignment = nullptr;
            // the weight assignment is paired with the family, because the two decide one face (§9.1)
            const CheckedAssignment* weight_assignment = nullptr;
            // the fallback assignment is checked on its own, because a fallback is a list of families (§9.1)
            const CheckedAssignment* fallback_assignment = nullptr;
            // every assignment is inspected, because a node may set any of the three properties (§9.1)
            for (const CheckedAssignment& assignment : node.assignments) {
                // the family assignment is what a weighted selection anchors to (§9.1)
                if (assignment.name == "font_family")
                    family_assignment = &assignment;
                // the weight assignment selects a face within the family (§9.1)
                else if (assignment.name == "font_weight")
                    weight_assignment = &assignment;
                // the fallback assignment names the families tried after the named one (§9.1)
                else if (assignment.name == "font_fallback")
                    fallback_assignment = &assignment;
            }
            // only elements that declare font properties are checked, because a component may own that name (§9.1)
            const bool font_element = node.element_type == "Label" || node.element_type == "TextField";
            // the family is known only from a string literal, because a computed value is runtime data (§11.1)
            std::optional<std::string> family;
            // a run with no family assignment resolves the default, which the build named (§9.1)
            bool uses_default = family_assignment == nullptr;
            // a literal family is read from the assignment, which is what the catalog can check (§11.1)
            if (family_assignment != nullptr) {
                // the value is a string literal only when the expression kind says so (§11.1)
                const std::string* written = literal_string(*family_assignment->value);
                // a literal family joins the check, because the catalog can resolve it (§9.1)
                if (written != nullptr)
                    family = *written;
            }
            // an empty family names the default, which is what the engine resolves when no family is written (§9.1)
            if (family.has_value() && family->empty()) {
                // the empty name is cleared, because the default family is the one the engine resolves (§9.1)
                family.reset();
                // the weight check follows the default family, because no family was named (§9.1)
                uses_default = true;
            }
            // the weight is known only when the author wrote a Weight member literal (§11.1)
            std::optional<FontWeight> weight;
            // a literal weight is read from the assignment, which is what the catalog can check (§11.1)
            if (weight_assignment != nullptr)
                weight = literal_weight(*weight_assignment->value);
            // the fallback literals are read before the family check, because a fallback can rescue a selection (§9.1)
            std::vector<FallbackLiteral> fallbacks;
            // a written fallback list contributes the families the engine tries after the named one (§9.1)
            if (fallback_assignment != nullptr)
                fallbacks = literal_fallbacks(*fallback_assignment->value);
            // the weight a literal selection asks for is the written one, or regular when none is written (§9.1)
            const FontWeight requested = weight.value_or(FontWeight::Regular);
            // a chain carries the selection when the named family or any fallback carries the requested weight (§9.1)
            auto chain_carries = [&catalog, &fallbacks, requested](std::string_view first) {
                // the named family answers first, because it is what the author asked for (§9.1)
                if (catalog.carries(first, requested))
                    return true;
                // each fallback follows, because a named family may be absent or lack the weight (§15, AD-34)
                for (const FallbackLiteral& fallback : fallbacks) {
                    // a fallback that carries the weight rescues the selection (§9.1)
                    if (catalog.carries(fallback.family, requested))
                        return true;
                }
                // no family in the chain carries the weight, which the caller reports (§9.1)
                return false;
            };
            // a literal family is checked against the faces the target carries (§9.1)
            if (font_element && family.has_value() && (weight_assignment == nullptr || weight.has_value())) {
                // the chain is checked, because a fallback may supply the face the named family lacks (§9.1)
                if (!chain_carries(*family)) {
                    // a named family the target does not carry is reported at the assignment (§11.1)
                    if (!catalog.carries(*family))
                        diagnostics.add(Diagnostic::Severity::Error, family_assignment->span, "E0510", "the font family '" + *family + "' is not carried by this target");
                    // a carried family that lacks the weight is reported at the weight the author wrote (§11.1)
                    else if (weight.has_value())
                        diagnostics.add(Diagnostic::Severity::Error, weight_assignment->span, "E0511", "the font family '" + *family + "' carries no requested weight");
                    // a family with no written weight asks for regular, which the chain lacks (§9.1)
                    else
                        diagnostics.add(Diagnostic::Severity::Error, family_assignment->span, "E0511", "the font family '" + *family + "' carries no regular face");
                }
            }
            // a written weight with no literal family resolves the default family, which the build named (§9.1)
            else if (font_element && weight_assignment != nullptr && weight.has_value() && uses_default) {
                // the default chain decides the face only when the build named a default family (§9.1)
                if (!catalog.default_family().empty() && !chain_carries(catalog.default_family())) {
                    // a default family the target does not carry is reported at the weight (§11.1)
                    if (!catalog.carries(catalog.default_family()))
                        diagnostics.add(Diagnostic::Severity::Error, weight_assignment->span, "E0512", "the default font family '" + catalog.default_family() + "' is not carried by this target");
                    // a carried default that lacks the weight is reported at the weight (§11.1)
                    else
                        diagnostics.add(Diagnostic::Severity::Error, weight_assignment->span, "E0511", "the default font family '" + catalog.default_family() + "' carries no requested weight");
                }
            }
            // a computed weight is the engine's, but an absent family with no fallback is still certain (§9.1)
            if (font_element && family.has_value() && weight_assignment != nullptr && !weight.has_value() && !catalog.carries(*family)) {
                // a carried fallback may still resolve the computed weight, which the compiler cannot know (§9.1)
                const bool any_fallback = std::any_of(fallbacks.begin(), fallbacks.end(), [&catalog](const FallbackLiteral& fallback) { return catalog.carries(fallback.family); });
                // no carried fallback means the selection cannot resolve at any weight, which is an error (§9.1)
                if (!any_fallback)
                    diagnostics.add(Diagnostic::Severity::Error, family_assignment->span, "E0510", "the font family '" + *family + "' is not carried by this target");
            }
            // every literal fallback is checked, because a fallback is explicit even when the family is computed (§9.1)
            for (const FallbackLiteral& fallback : fallbacks) {
                // a component without font properties is not checked, because its property name is its own (§9.1)
                if (!font_element)
                    continue;
                // a fallback the target does not carry is a warning, because a later fallback may still resolve (§9.1)
                if (!catalog.carries(fallback.family)) {
                    // the missing fallback is reported at the family the author wrote (§11.1)
                    diagnostics.add(Diagnostic::Severity::Warning, fallback.span, "W0513", "the fallback font family '" + fallback.family + "' is not carried by this target");
                }
                // a carried fallback without the weight cannot rescue the selection, which is a warning (§9.1)
                else if (weight.has_value() && !catalog.carries(fallback.family, *weight)) {
                    // the missing weight is reported at the family the author wrote (§11.1)
                    diagnostics.add(Diagnostic::Severity::Warning, fallback.span, "W0513", "the fallback font family '" + fallback.family + "' carries no requested weight");
                }
            }
            // every child carries its own selection, which the walk reaches next (§11.1)
            for (const std::shared_ptr<CheckedNode>& child : node.children)
                check_node(*child, catalog, diagnostics);
            // a repeat body carries the selection of each row, which is checked like any other node (§11.1)
            if (node.repeat_body != nullptr)
                check_node(*node.repeat_body, catalog, diagnostics);
            // a conditional body carries the selection of the guarded node, which is checked like any other node (§11.1)
            if (node.conditional_body != nullptr)
                check_node(*node.conditional_body, catalog, diagnostics);
        }
    } // namespace

    void check_font_usage(const CheckedFile& file, const FontCatalog& catalog, DiagnosticList& diagnostics) {
        // a build that declared no face keeps the platform font path, so there is nothing to validate (§9.1)
        if (catalog.count() == 0u)
            return;
        // the checks collect their problems first, because an inherited tree is walked more than once (§11.1)
        DiagnosticList found;
        // a file-level problem points at the first component, because a span of zero names no source (§11.1)
        SourceSpan anchor;
        // the first component's span is the closest source a file-level problem has (§11.1)
        if (!file.components.empty())
            anchor = file.components.front().span;
        // a build with faces must name the default family, because a bare selection resolves it (§9.1)
        if (catalog.default_family().empty())
            found.add(Diagnostic::Severity::Error, anchor, "E0512", "no default font family is declared for this target");
        // a named default must be carried, because the engine resolves it by name (§9.1)
        else if (!catalog.carries(catalog.default_family()))
            found.add(Diagnostic::Severity::Error, anchor, "E0512", "the default font family '" + catalog.default_family() + "' is not carried by this target");
        // the default family must carry regular, because a bare selection resolves that weight (§9.1)
        else if (!catalog.carries(catalog.default_family(), FontWeight::Regular))
            found.add(Diagnostic::Severity::Error, anchor, "E0512", "the default font family '" + catalog.default_family() + "' carries no regular face");
        // every component's tree is checked, because a selection may sit anywhere in the declarative surface (§11.1)
        for (const CheckedComponent& component : file.components) {
            // a component without a root declares no tree, which the checker already refused (§11.1)
            if (component.root != nullptr)
                check_node(*component.root, catalog, found);
        }
        // every problem is copied once, because a derived component clones its base tree with the same spans (§11.1)
        for (const Diagnostic& diagnostic : found.diagnostics()) {
            // a problem already reported at the same span with the same text is a clone, not a second defect (§11.1)
            const bool duplicate = std::any_of(diagnostics.diagnostics().begin(), diagnostics.diagnostics().end(), [&diagnostic](const Diagnostic& existing) { return existing.code == diagnostic.code && existing.message == diagnostic.message && existing.span == diagnostic.span; });
            // a new problem joins the caller's list, which is what the driver prints (§11.1)
            if (!duplicate)
                diagnostics.add(diagnostic);
        }
    }
} // namespace albor::compiler
