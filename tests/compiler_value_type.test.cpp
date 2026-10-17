#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "compiler/value_type.h"

using namespace albor::compiler;

namespace
{
    // the four builtin conversion targets, which the rules are written over (§11.1)
    const std::vector<std::string> CONVERSION_NAMES = {"string", "int", "float", "bool"};
} // namespace

TEST(ValueType, DefaultsToTheErrorType) {
    // arrange, act
    const ValueType type;
    // assert
    EXPECT_TRUE(type.is_error());
    EXPECT_EQ(type.kind(), TypeKind::Error);
    EXPECT_EQ(type.to_string(), "<error>");
}

TEST(ValueType, ScalarKeepsItsKindAndCarriesNoName) {
    // arrange, act
    const ValueType type = scalar(TypeKind::Int);
    // assert
    EXPECT_TRUE(type.is(TypeKind::Int));
    EXPECT_FALSE(type.is_error());
    EXPECT_TRUE(type.is_numeric());
    EXPECT_TRUE(type.native_name().empty());
    // a scalar carries no element, so the error type stands in for it (§11.1)
    EXPECT_TRUE(type.element().is_error());
}

TEST(ValueType, ReportsNumericKindsOnly) {
    // arrange, act, assert
    EXPECT_TRUE(scalar(TypeKind::Int).is_numeric());
    EXPECT_TRUE(scalar(TypeKind::Float).is_numeric());
    EXPECT_FALSE(scalar(TypeKind::String).is_numeric());
    EXPECT_FALSE(scalar(TypeKind::Bool).is_numeric());
    EXPECT_FALSE(scalar(TypeKind::Color).is_numeric());
    EXPECT_FALSE(scalar(TypeKind::Length).is_numeric());
}

TEST(ValueType, ParameterizedRendersItsElement) {
    // arrange, act
    const ValueType snapshot = ValueType::parameterized(TypeKind::Snapshot, ValueType::native("WaveformData"));
    const ValueType vector = ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::Int));
    // assert
    EXPECT_TRUE(snapshot.is(TypeKind::Snapshot));
    EXPECT_EQ(snapshot.to_string(), "Snapshot<WaveformData>");
    EXPECT_EQ(vector.to_string(), "vector<int>");
}

TEST(ValueType, NestedParametersRenderToAnyDepth) {
    // arrange
    const ValueType inner = ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::String));
    // act
    const ValueType outer = ValueType::parameterized(TypeKind::Snapshot, inner);
    // assert
    EXPECT_EQ(outer.to_string(), "Snapshot<vector<string>>");
    EXPECT_EQ(outer.element().to_string(), "vector<string>");
}

TEST(ValueType, ParameterizedElementIsSharedRatherThanCopied) {
    // arrange, act
    const ValueType element = ValueType::parameterized(TypeKind::Snapshot, scalar(TypeKind::Int));
    // assert
    const ValueType same = ValueType::parameterized(TypeKind::Snapshot, scalar(TypeKind::Int));
    EXPECT_TRUE(element.equals(same));
    EXPECT_FALSE(element.equals(ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::Int))));
}

TEST(ValueType, NativeCarriesItsName) {
    // arrange, act
    const ValueType type = ValueType::native("WaveformView");
    // assert
    EXPECT_TRUE(type.is(TypeKind::Native));
    EXPECT_EQ(type.native_name(), "WaveformView");
    EXPECT_EQ(type.to_string(), "WaveformView");
}

TEST(ValueType, ComparesTheElementsOfAParameterForm) {
    // arrange
    const ValueType strings = ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::String));
    const ValueType ints = ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::Int));
    // act, assert
    EXPECT_TRUE(strings.equals(ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::String))));
    EXPECT_FALSE(strings.equals(ints));
}

TEST(ValueType, EqualityComparesKindAndPayload) {
    // arrange, act, assert
    EXPECT_TRUE(scalar(TypeKind::Float).equals(scalar(TypeKind::Float)));
    EXPECT_FALSE(scalar(TypeKind::Float).equals(scalar(TypeKind::Int)));
    EXPECT_TRUE(ValueType::native("A").equals(ValueType::native("A")));
    EXPECT_FALSE(ValueType::native("A").equals(ValueType::native("B")));
}

TEST(ValueType, RendersEveryKindTheWayTheAuthorWritesIt) {
    // arrange, act, assert
    EXPECT_EQ(scalar(TypeKind::Void).to_string(), "void");
    EXPECT_EQ(scalar(TypeKind::Bool).to_string(), "bool");
    EXPECT_EQ(scalar(TypeKind::Int).to_string(), "int");
    EXPECT_EQ(scalar(TypeKind::Float).to_string(), "float");
    EXPECT_EQ(scalar(TypeKind::String).to_string(), "string");
    EXPECT_EQ(scalar(TypeKind::Color).to_string(), "color");
    EXPECT_EQ(scalar(TypeKind::Length).to_string(), "length");
}

TEST(ValueType, ExplicitConstructorBuildsAScalar) {
    // arrange, act
    const ValueType type{TypeKind::Color};
    // assert
    EXPECT_TRUE(type.is(TypeKind::Color));
    EXPECT_TRUE(type.equals(scalar(TypeKind::Color)));
}

TEST(BuiltinConversion, MapsEachNameToItsTargetKind) {
    // arrange, act, assert
    TypeKind kind = TypeKind::Error;
    EXPECT_TRUE(builtin_conversion_target("string", kind));
    EXPECT_EQ(kind, TypeKind::String);
    EXPECT_TRUE(builtin_conversion_target("int", kind));
    EXPECT_EQ(kind, TypeKind::Int);
    EXPECT_TRUE(builtin_conversion_target("float", kind));
    EXPECT_EQ(kind, TypeKind::Float);
    EXPECT_TRUE(builtin_conversion_target("bool", kind));
    EXPECT_EQ(kind, TypeKind::Bool);
}

TEST(BuiltinConversion, RefusesAnyOtherName) {
    // arrange
    TypeKind kind = TypeKind::Bool;
    // act, assert
    EXPECT_FALSE(builtin_conversion_target("length", kind));
    EXPECT_FALSE(builtin_conversion_target("color", kind));
    EXPECT_FALSE(builtin_conversion_target("", kind));
    EXPECT_FALSE(builtin_conversion_target("String", kind));
}

TEST(BuiltinConversion, EveryDefinedNameIsReachable) {
    // arrange, act
    std::size_t found = 0;
    // assert
    for (const std::string& name : CONVERSION_NAMES) {
        TypeKind kind = TypeKind::Error;
        if (builtin_conversion_target(name, kind))
            found++;
    }
    EXPECT_EQ(found, CONVERSION_NAMES.size());
}

TEST(ConversionRules, EveryScalarMayBecomeAString) {
    // arrange, act, assert
    EXPECT_TRUE(conversion_allowed(TypeKind::Bool, TypeKind::String));
    EXPECT_TRUE(conversion_allowed(TypeKind::Int, TypeKind::String));
    EXPECT_TRUE(conversion_allowed(TypeKind::Float, TypeKind::String));
    EXPECT_TRUE(conversion_allowed(TypeKind::String, TypeKind::String));
    EXPECT_TRUE(conversion_allowed(TypeKind::Color, TypeKind::String));
    EXPECT_TRUE(conversion_allowed(TypeKind::Length, TypeKind::String));
}

TEST(ConversionRules, OnlyANumberOrTextMayBecomeANumber) {
    // arrange, act, assert
    EXPECT_TRUE(conversion_allowed(TypeKind::Int, TypeKind::Int));
    EXPECT_TRUE(conversion_allowed(TypeKind::Float, TypeKind::Float));
    EXPECT_TRUE(conversion_allowed(TypeKind::String, TypeKind::Int));
    EXPECT_TRUE(conversion_allowed(TypeKind::String, TypeKind::Float));
    EXPECT_FALSE(conversion_allowed(TypeKind::Bool, TypeKind::Int));
    EXPECT_FALSE(conversion_allowed(TypeKind::Color, TypeKind::Float));
    EXPECT_FALSE(conversion_allowed(TypeKind::Length, TypeKind::Int));
}

TEST(ConversionRules, EveryScalarMayBecomeABool) {
    // arrange, act, assert
    EXPECT_TRUE(conversion_allowed(TypeKind::Bool, TypeKind::Bool));
    EXPECT_TRUE(conversion_allowed(TypeKind::String, TypeKind::Bool));
    EXPECT_TRUE(conversion_allowed(TypeKind::Color, TypeKind::Bool));
    EXPECT_TRUE(conversion_allowed(TypeKind::Length, TypeKind::Bool));
}

TEST(ConversionRules, TheErrorTypeConvertsToAndFromAnything) {
    // arrange, act, assert
    EXPECT_TRUE(conversion_allowed(TypeKind::Error, TypeKind::String));
    EXPECT_TRUE(conversion_allowed(TypeKind::Bool, TypeKind::Error));
    EXPECT_TRUE(conversion_allowed(TypeKind::Error, TypeKind::Error));
}

TEST(ConversionRules, NoTargetExistsBeyondTheFourKinds) {
    // arrange, act, assert
    EXPECT_FALSE(conversion_allowed(TypeKind::Int, TypeKind::Color));
    EXPECT_FALSE(conversion_allowed(TypeKind::Int, TypeKind::Length));
    EXPECT_FALSE(conversion_allowed(TypeKind::Bool, TypeKind::Void));
}

TEST(Assignability, MatchesExactlyWhenTheTypesAgree) {
    // arrange, act, assert
    EXPECT_TRUE(assignable(scalar(TypeKind::Int), scalar(TypeKind::Int)));
    EXPECT_TRUE(assignable(scalar(TypeKind::String), scalar(TypeKind::String)));
    EXPECT_TRUE(assignable(ValueType::native("A"), ValueType::native("A")));
}

TEST(Assignability, NeverWidensNumericsImplicitly) {
    // arrange, act, assert
    EXPECT_FALSE(assignable(scalar(TypeKind::Int), scalar(TypeKind::Float)));
    EXPECT_FALSE(assignable(scalar(TypeKind::Float), scalar(TypeKind::Int)));
}

TEST(Assignability, RefusesEveryOtherMismatch) {
    // arrange, act, assert
    EXPECT_FALSE(assignable(scalar(TypeKind::Bool), scalar(TypeKind::Int)));
    EXPECT_FALSE(assignable(scalar(TypeKind::String), scalar(TypeKind::Color)));
    EXPECT_FALSE(assignable(ValueType::native("A"), ValueType::native("B")));
    EXPECT_FALSE(assignable(ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::Int)), ValueType::parameterized(TypeKind::Vector, scalar(TypeKind::Float))));
}

TEST(Assignability, ALengthAssignsIntoAFloatWhereTheDensityApplies) {
    // arrange, act, assert
    EXPECT_TRUE(assignable(scalar(TypeKind::Length), scalar(TypeKind::Float)));
    EXPECT_FALSE(assignable(scalar(TypeKind::Length), scalar(TypeKind::Int)));
}

TEST(Assignability, TheErrorTypeAssignsAnywhereWithoutComplaint) {
    // arrange, act, assert
    EXPECT_TRUE(assignable(error_type(), scalar(TypeKind::Int)));
    EXPECT_TRUE(assignable(scalar(TypeKind::Int), error_type()));
}

TEST(ElementType, FindsAPropertyByName) {
    // arrange
    const ElementType type{.name = "Label", .properties = {ElementProperty{.name = "text", .type = scalar(TypeKind::String), .assignable = true}}, .signals = {}, .accepts_children = false};
    // act
    const ElementProperty* found = type.find_property("text");
    // assert
    ASSERT_TRUE(found != nullptr);
    EXPECT_TRUE(found->type.is(TypeKind::String));
    EXPECT_TRUE(found->assignable);
}

TEST(ElementType, ReportsAnAbsentProperty) {
    // arrange
    const ElementType type{.name = "Label", .properties = {}, .signals = {}, .accepts_children = false};
    // act, assert
    EXPECT_TRUE(type.find_property("text") == nullptr);
}

TEST(ElementType, FindsASignalAndItsParameterTypes) {
    // arrange
    const ElementType type{.name = "Slider", .properties = {}, .signals = {ElementSignal{.name = "changed", .parameter_types = {scalar(TypeKind::Float)}}}, .accepts_children = false};
    // act
    const ElementSignal* found = type.find_signal("changed");
    // assert
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->parameter_types.size(), 1U);
    EXPECT_TRUE(found->parameter_types.at(0).is(TypeKind::Float));
    EXPECT_TRUE(type.find_signal("clicked") == nullptr);
}

TEST(ElementCatalog, HoldsEveryBuiltinWidget) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    EXPECT_TRUE(catalog.find("Column") != nullptr);
    EXPECT_TRUE(catalog.find("Row") != nullptr);
    EXPECT_TRUE(catalog.find("Padding") != nullptr);
    EXPECT_TRUE(catalog.find("Stack") != nullptr);
    EXPECT_TRUE(catalog.find("Grid") != nullptr);
    EXPECT_TRUE(catalog.find("Label") != nullptr);
    EXPECT_TRUE(catalog.find("Button") != nullptr);
    EXPECT_TRUE(catalog.find("Slider") != nullptr);
    EXPECT_TRUE(catalog.find("CheckBox") != nullptr);
    EXPECT_TRUE(catalog.find("Canvas") != nullptr);
    EXPECT_TRUE(catalog.find("ScrollView") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox") != nullptr);
}

TEST(ElementCatalog, ReportsAnAbsentElementType) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    EXPECT_TRUE(catalog.find("WaveformView") == nullptr);
    EXPECT_TRUE(catalog.find("") == nullptr);
}

TEST(ElementCatalog, HoldsTheContainersAndTheLeavesApart) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    EXPECT_TRUE(catalog.find("Column")->accepts_children);
    EXPECT_TRUE(catalog.find("Padding")->accepts_children);
    EXPECT_TRUE(catalog.find("Stack")->accepts_children);
    EXPECT_TRUE(catalog.find("Grid")->accepts_children);
    EXPECT_TRUE(catalog.find("ListBox")->accepts_children);
    EXPECT_FALSE(catalog.find("Label")->accepts_children);
    EXPECT_FALSE(catalog.find("Button")->accepts_children);
    EXPECT_FALSE(catalog.find("Slider")->accepts_children);
    EXPECT_FALSE(catalog.find("CheckBox")->accepts_children);
    EXPECT_FALSE(catalog.find("Canvas")->accepts_children);
}

TEST(ElementCatalog, DeclaresTheLengthPropertiesAsAssignableFloats) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert the lengths stay assignable floats, which a `px` value resolves against (§11.1, AD-27)
    EXPECT_TRUE(catalog.find("Column")->find_property("spacing")->type.is(TypeKind::Float));
    EXPECT_TRUE(catalog.find("Padding")->find_property("padding")->type.is(TypeKind::Float));
    EXPECT_TRUE(catalog.find("Label")->find_property("font_size")->type.is(TypeKind::Float));
    // assert the lengths accept an assignment, because a length lands on them through the checker (§11.1)
    EXPECT_TRUE(catalog.find("Column")->find_property("spacing")->assignable);
    EXPECT_TRUE(catalog.find("Label")->find_property("font_size")->assignable);
}

TEST(ElementCatalog, ExposesTheHandledSignals) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    EXPECT_TRUE(catalog.find("Button")->find_signal("clicked") != nullptr);
    EXPECT_TRUE(catalog.find("Slider")->find_signal("changed") != nullptr);
    EXPECT_TRUE(catalog.find("CheckBox")->find_signal("toggled") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_signal("selection_changed") != nullptr);
    EXPECT_TRUE(catalog.find("Label")->find_signal("clicked") == nullptr);
    EXPECT_TRUE(catalog.find("Column")->find_signal("clicked") == nullptr);
}

TEST(ElementCatalog, ExposesTheListBoxProperties) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    ASSERT_TRUE(catalog.find("ListBox") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("selected") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("row_height") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("scrollbar") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("disabled") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("viewport_width") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("viewport_height") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("overscan") != nullptr);
    EXPECT_TRUE(catalog.find("ListBox")->find_property("selected")->type.is(TypeKind::Int));
    EXPECT_TRUE(catalog.find("ListBox")->find_property("viewport_height")->type.is(TypeKind::Float));
}

TEST(ElementCatalog, ExposesTheListBoxActivatedSignal) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    const ElementSignal* activated = catalog.find("ListBox")->find_signal("activated");
    ASSERT_TRUE(activated != nullptr);
    ASSERT_EQ(activated->parameter_types.size(), 1U);
    EXPECT_TRUE(activated->parameter_types.at(0).is(TypeKind::String));
    EXPECT_EQ(activated->parameter_names.at(0), "key");
}

TEST(ElementCatalog, AddsANativeTypeFromAManifest) {
    // arrange
    ElementCatalog catalog = ElementCatalog::with_builtins();
    const ElementType native{.name = "WaveformView", .properties = {ElementProperty{.name = "line_color", .type = scalar(TypeKind::Color), .assignable = true}}, .signals = {ElementSignal{.name = "selection_changed", .parameter_types = {scalar(TypeKind::Float)}}}, .accepts_children = false};
    // act
    catalog.add_native(native);
    // assert
    ASSERT_TRUE(catalog.find("WaveformView") != nullptr);
    EXPECT_TRUE(catalog.find("WaveformView")->find_property("line_color") != nullptr);
    EXPECT_TRUE(catalog.find("WaveformView")->find_signal("selection_changed") != nullptr);
    EXPECT_EQ(catalog.types().size(), 14U);
}

TEST(ElementCatalog, StartsWithNothingBeyondTheBuiltins) {
    // arrange, act
    const ElementCatalog catalog = ElementCatalog::with_builtins();
    // assert
    EXPECT_EQ(catalog.types().size(), 13U);
}

TEST(ElementCatalog, RefusesToShadowABuiltinWithAManifest) {
    // arrange
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    catalog.add_native(ElementType{.name = "Label", .properties = {}, .signals = {}, .accepts_children = true});
    // assert
    EXPECT_FALSE(catalog.find("Label")->accepts_children);
    EXPECT_EQ(catalog.types().size(), 13U);
}

TEST(ElementCatalog, RefusesToRedefineANativeDataType) {
    // arrange
    ElementCatalog catalog = ElementCatalog::with_builtins();
    // act
    catalog.add_native_data("WaveformData");
    catalog.add_native_data("WaveformData");
    // assert
    EXPECT_TRUE(catalog.holds_native("WaveformData"));
}
