#include <gtest/gtest.h>

#include <string>

#include "compiler/value_type.h"

using namespace albor::compiler;

namespace
{
    // a builtin that takes children, which is what a common property has to reach (§8.2)
    const char* CONTAINER = "Row";

    // a builtin leaf, which a common property has to reach as well (§8.2)
    const char* LEAF = "Label";
} // namespace

TEST(AlignType, TheAlignmentKindPrintsItsOwnName) {
    // arrange, act
    const ValueType type = scalar(TypeKind::Align);
    // assert the spelling names the kind, so a diagnostic about it reads correctly (§11.1)
    EXPECT_TRUE(type.is(TypeKind::Align));
    EXPECT_EQ(type.to_string(), "align");
}

TEST(AlignType, AlignmentAssignsOnlyFromAlignment) {
    // assert the same kind assigns, and nothing widens into it (§11.1)
    EXPECT_TRUE(assignable(scalar(TypeKind::Align), scalar(TypeKind::Align)));
    EXPECT_FALSE(assignable(scalar(TypeKind::Int), scalar(TypeKind::Align)));
    EXPECT_FALSE(assignable(scalar(TypeKind::String), scalar(TypeKind::Align)));
}

TEST(NamedValueSets, TheAlignmentSetIsDeclared) {
    // arrange, act
    const NamedValueSet* set = find_named_value_set("Align");
    // assert
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->kind, TypeKind::Align);
}

TEST(NamedValueSets, AnUndeclaredSetResolvesToNothing) {
    // arrange, act
    const NamedValueSet* set = find_named_value_set("Orientation");
    // assert an unknown namespace falls through to the property read path rather than claiming a value (§11.1)
    EXPECT_EQ(set, nullptr);
}

TEST(NamedValueSets, EveryAlignmentMemberResolves) {
    // arrange
    const NamedValueSet* set = find_named_value_set("Align");
    ASSERT_NE(set, nullptr);
    // act, assert each written member names a member of the set (§11.1)
    for (const char* written : {"start", "center", "end", "stretch"})
        EXPECT_NE(set->find_member(written), nullptr);
}

TEST(NamedValueSets, AnUndeclaredMemberResolvesToNothing) {
    // arrange
    const NamedValueSet* set = find_named_value_set("Align");
    ASSERT_NE(set, nullptr);
    // act
    const NamedValueMember* member = set->find_member("middle");
    // assert
    EXPECT_EQ(member, nullptr);
}

TEST(NamedValueSets, AWrittenMemberAndItsEnumeratorDiffer) {
    // arrange
    const NamedValueSet* set = find_named_value_set("Align");
    ASSERT_NE(set, nullptr);
    // act
    const NamedValueMember* member = set->find_member("center");
    // assert the language writes lower case and the generated code names the enumerator (§11.3, §8.2)
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->written, "center");
    EXPECT_EQ(member->code, "Center");
}

TEST(CommonProperties, EveryBuiltinCarriesBothAxes) {
    // arrange
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // act, assert every declared type arranges inside a parent, so every one carries both axes (§8.2)
    for (const ElementType& type : catalog.types()) {
        EXPECT_NE(type.find_property("horizontal_align"), nullptr) << type.name;
        EXPECT_NE(type.find_property("vertical_align"), nullptr) << type.name;
    }
}

TEST(CommonProperties, TheAlignmentPropertiesCarryTheAlignmentType) {
    // arrange
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    const ElementProperty* horizontal = catalog.find(LEAF)->find_property("horizontal_align");
    // assert the property resolves a written member, because its type is the named set (§8.2)
    ASSERT_NE(horizontal, nullptr);
    EXPECT_TRUE(horizontal->type.is(TypeKind::Align));
}

TEST(CommonProperties, ACommonPropertyDoesNotDisplaceItsOwn) {
    // arrange
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    const ElementProperty* spacing = catalog.find(CONTAINER)->find_property("spacing");
    // assert appending the common set left the declared property alone (§8.2)
    ASSERT_NE(spacing, nullptr);
    EXPECT_TRUE(spacing->type.is(TypeKind::Float));
}

TEST(CommonProperties, ANativeTypeCarriesTheCommonProperties) {
    // arrange
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // act a manifest component joins the catalogue (§11.2, AD-17)
    catalog.add_native(ElementType{.name = "WaveformView", .properties = {}, .signals = {}, .accepts_children = false});
    // assert it arranges like a builtin one (§8.2, AD-17)
    const ElementType* native = catalog.find("WaveformView");
    ASSERT_NE(native, nullptr);
    EXPECT_NE(native->find_property("horizontal_align"), nullptr);
    EXPECT_NE(native->find_property("vertical_align"), nullptr);
}
