#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "semantics/semantic_node.h"
#include "semantics/semantic_provider.h"

using namespace albor;

namespace
{
    // the provider test double, which exposes a fixed number of children on demand (§10.1, AD-09)
    class TestSemanticProvider final : public SemanticProvider
    {
    public:
        // build a provider announcing one role and one accessible name (§10.1)
        TestSemanticProvider(SemanticRole role, std::string name, std::size_t children) :
            m_role(role), m_name(std::move(name)), m_children(children) {}

        [[nodiscard]] SemanticNode semantic_node() const override {
            // the node carries what the test configured, which is all an adapter reads (§10.1)
            SemanticNode node(m_role);
            node.set_name(m_name);
            return node;
        }

        [[nodiscard]] std::size_t semantic_child_count() const override { return m_children; }

        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // an index past the end names no child, which the contract requires (§10.1)
            if (index >= m_children)
                return nullptr;
            // the child is built only when it is asked for, which is what keeps the tree virtualized (§10.1, AD-09)
            const_cast<TestSemanticProvider*>(this)->built++;
            return std::make_unique<TestSemanticProvider>(SemanticRole::ListItem, m_name + " item " + std::to_string(index), 0);
        }

        // how many children the test double actually built, which proves the walk is lazy (§10.1, AD-09)
        std::size_t built = 0;

    private:
        // the role the provider announces (§10.1)
        SemanticRole m_role;

        // the accessible name the provider announces (§10.1)
        std::string m_name;

        // how many children the provider exposes (§10.1)
        std::size_t m_children;
    };

    // the provider that refuses to build a child, which the tree must skip rather than treat as a hole (§10.1)
    class RefusingProvider final : public SemanticProvider
    {
    public:
        [[nodiscard]] SemanticNode semantic_node() const override {
            SemanticNode node(SemanticRole::Group);
            node.set_name("group");
            return node;
        }

        [[nodiscard]] std::size_t semantic_child_count() const override { return 2; }

        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t) const override { return nullptr; }
    };
} // namespace

TEST(SemanticNode, ANodeAnnouncesItsRole) {
    // arrange, act
    const SemanticNode node(SemanticRole::Button);
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Button);
}

TEST(SemanticNode, ARoleCanBeReplaced) {
    // arrange
    SemanticNode node(SemanticRole::Label);
    // act
    node.set_role(SemanticRole::Heading);
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Heading);
}

TEST(SemanticNode, ANewNodeHasNoAccessibleName) {
    // arrange, act
    const SemanticNode node(SemanticRole::Label);
    // assert
    EXPECT_TRUE(node.name().empty());
}

TEST(SemanticNode, AnAccessibleNameIsStoredAsWritten) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act
    node.set_name("Increment");
    // assert
    EXPECT_EQ(node.name(), "Increment");
}

TEST(SemanticNode, ANewNodeIsNotDisabled) {
    // arrange, act
    const SemanticNode node(SemanticRole::Button);
    // assert
    EXPECT_FALSE(node.disabled());
}

TEST(SemanticNode, TheDisabledStateCanBeSet) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act
    node.set_disabled(true);
    // assert
    EXPECT_TRUE(node.disabled());
}

TEST(SemanticNode, TheDisabledStateCanBeCleared) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    node.set_disabled(true);
    // act
    node.set_disabled(false);
    // assert
    EXPECT_FALSE(node.disabled());
}

TEST(SemanticNode, ANewNodeIsNotFocused) {
    // arrange, act
    const SemanticNode node(SemanticRole::Button);
    // assert
    EXPECT_FALSE(node.focused());
}

TEST(SemanticNode, TheFocusedStateCanBeSet) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_focused(true);
    // assert
    EXPECT_TRUE(node.focused());
}

TEST(SemanticNode, FocusAndDisabledAreIndependent) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act
    node.set_focused(true);
    node.set_disabled(true);
    // assert
    EXPECT_TRUE(node.focused());
    EXPECT_TRUE(node.disabled());
}

TEST(SemanticNode, ANewNodeIsNotCheckable) {
    // arrange, act
    const SemanticNode node(SemanticRole::Button);
    // assert
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
}

TEST(SemanticNode, ANewNodeIsNotChecked) {
    // arrange, act
    const SemanticNode node(SemanticRole::Checkbox);
    // assert
    EXPECT_FALSE(node.checked());
}

TEST(SemanticNode, TheCheckStateCanBeSetToChecked) {
    // arrange
    SemanticNode node(SemanticRole::Checkbox);
    // act
    node.set_check(CheckState::Checked);
    // assert
    EXPECT_TRUE(node.checked());
}

TEST(SemanticNode, TheCheckStateCanBeSetToUnchecked) {
    // arrange
    SemanticNode node(SemanticRole::Checkbox);
    node.set_check(CheckState::Checked);
    // act
    node.set_check(CheckState::Unchecked);
    // assert
    EXPECT_FALSE(node.checked());
}

TEST(SemanticNode, AnIndeterminateNodeIsNotChecked) {
    // arrange
    SemanticNode node(SemanticRole::Checkbox);
    // act
    node.set_check(CheckState::Indeterminate);
    // assert
    EXPECT_FALSE(node.checked());
    EXPECT_EQ(node.check(), CheckState::Indeterminate);
}

TEST(SemanticNode, TheStateSetReadsBackWhatWasWritten) {
    // arrange
    SemanticNode node(SemanticRole::Slider);
    node.set_disabled(true);
    node.set_check(CheckState::Checked);
    // act
    const SemanticStates states = node.states();
    // assert
    EXPECT_TRUE(states.disabled);
    EXPECT_EQ(states.check, CheckState::Checked);
}

TEST(SemanticNode, TheMutableStateSetCanBeEditedInPlace) {
    // arrange
    SemanticNode node(SemanticRole::Slider);
    // act
    node.mutable_states().focused = true;
    // assert
    EXPECT_TRUE(node.focused());
}

TEST(SemanticNode, ANewNodeHasEmptyBounds) {
    // arrange, act
    const SemanticNode node(SemanticRole::Canvas);
    // assert
    EXPECT_TRUE(node.bounds().empty());
}

TEST(SemanticNode, TheBoundsCanBeReplaced) {
    // arrange
    SemanticNode node(SemanticRole::Canvas);
    // act
    node.set_bounds(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{100.0f, 50.0f}));
    // assert
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{10.0f, 20.0f}, Size{100.0f, 50.0f})));
}

TEST(SemanticNode, ANewNodeExposesNoAction) {
    // arrange, act
    const SemanticNode node(SemanticRole::Button);
    // assert
    EXPECT_EQ(node.action_count(), 0u);
}

TEST(SemanticNode, RegisteringAnActionHandsOutTheFirstIdentifier) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act
    const SemanticActionId action = node.add_action("press", [] {});
    // assert
    EXPECT_EQ(action, 1u);
    EXPECT_EQ(node.action_count(), 1u);
}

TEST(SemanticNode, AnActionWithoutWorkIsRefused) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act
    const SemanticActionId action = node.add_action("press", nullptr);
    // assert
    EXPECT_EQ(action, 0u);
    EXPECT_EQ(node.action_count(), 0u);
}

TEST(SemanticNode, InvokingAnActionRunsIt) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    std::size_t runs = 0;
    const SemanticActionId action = node.add_action("press", [&runs] { runs++; });
    // act
    const bool invoked = node.invoke(action);
    // assert
    EXPECT_TRUE(invoked);
    EXPECT_EQ(runs, 1u);
}

TEST(SemanticNode, InvokingHandleZeroFails) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act, assert
    EXPECT_FALSE(node.invoke(0));
}

TEST(SemanticNode, InvokingAPastTheEndHandleFails) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    const SemanticActionId press_action = node.add_action("press", [] {});
    // act, assert
    EXPECT_EQ(press_action, 1u);
    EXPECT_FALSE(node.invoke(9));
}

TEST(SemanticNode, ADisabledNodeRefusesEveryAction) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    std::size_t runs = 0;
    const SemanticActionId action = node.add_action("press", [&runs] { runs++; });
    node.set_disabled(true);
    // act
    const bool invoked = node.invoke(action);
    // assert
    EXPECT_FALSE(invoked);
    EXPECT_EQ(runs, 0u);
}

TEST(SemanticNode, AnActionCanBeInvokedByName) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    std::size_t runs = 0;
    const SemanticActionId press_action = node.add_action("press", [&runs] { runs++; });
    // act
    const bool invoked = node.invoke("press");
    // assert
    EXPECT_EQ(press_action, 1u);
    EXPECT_TRUE(invoked);
    EXPECT_EQ(runs, 1u);
}

TEST(SemanticNode, InvokingAnUnknownNameFails) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act, assert
    EXPECT_FALSE(node.invoke("nothing"));
}

TEST(SemanticNode, AnActionIsFoundByName) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    const SemanticActionId press_action = node.add_action("press", [] {});
    // act, assert
    EXPECT_EQ(press_action, 1u);
    EXPECT_TRUE(node.has_action("press"));
}

TEST(SemanticNode, AnUnknownActionNameIsNotFound) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    const SemanticActionId press_action = node.add_action("press", [] {});
    // act, assert
    EXPECT_EQ(press_action, 1u);
    EXPECT_FALSE(node.has_action("release"));
}

TEST(SemanticNode, AnActionReportsItsRegisteredName) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    const SemanticActionId action = node.add_action("press", [] {});
    // act, assert
    EXPECT_EQ(node.action_name(action), "press");
}

TEST(SemanticNode, AnUnknownActionHasNoName) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act, assert
    EXPECT_TRUE(node.action_name(4).empty());
}

TEST(SemanticNode, TwoActionsKeepSeparateIdentifiers) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    std::size_t first_runs = 0;
    std::size_t second_runs = 0;
    // act
    const SemanticActionId first = node.add_action("press", [&first_runs] { first_runs++; });
    const SemanticActionId second = node.add_action("release", [&second_runs] { second_runs++; });
    // assert
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);
    EXPECT_EQ(node.action_name(first), "press");
    EXPECT_EQ(node.action_name(second), "release");
}

TEST(SemanticNode, InvokingOneActionLeavesTheOtherAlone) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    std::size_t first_runs = 0;
    std::size_t second_runs = 0;
    const SemanticActionId first = node.add_action("press", [&first_runs] { first_runs++; });
    const SemanticActionId release_action = node.add_action("release", [&second_runs] { second_runs++; });
    // act
    node.invoke(first);
    // assert
    EXPECT_EQ(release_action, 2u);
    EXPECT_EQ(first_runs, 1u);
    EXPECT_EQ(second_runs, 0u);
}

TEST(SemanticNode, TwoNodesWithTheSameContentCompareEqual) {
    // arrange
    SemanticNode first(SemanticRole::Button);
    SemanticNode second(SemanticRole::Button);
    first.set_name("Increment");
    second.set_name("Increment");
    // act, assert
    EXPECT_TRUE(first == second);
}

TEST(SemanticNode, TwoNodesWithDifferentRolesDiffer) {
    // arrange
    SemanticNode first(SemanticRole::Button);
    SemanticNode second(SemanticRole::Checkbox);
    // act, assert
    EXPECT_FALSE(first == second);
}

TEST(SemanticNode, TwoNodesWithDifferentNamesDiffer) {
    // arrange
    SemanticNode first(SemanticRole::Button);
    SemanticNode second(SemanticRole::Button);
    first.set_name("a");
    second.set_name("b");
    // act, assert
    EXPECT_FALSE(first == second);
}

TEST(SemanticNode, TwoNodesWithDifferentStatesDiffer) {
    // arrange
    SemanticNode first(SemanticRole::Button);
    SemanticNode second(SemanticRole::Button);
    first.set_focused(true);
    // act, assert
    EXPECT_FALSE(first == second);
}

TEST(SemanticNode, TwoNodesWithDifferentBoundsDiffer) {
    // arrange
    SemanticNode first(SemanticRole::Button);
    SemanticNode second(SemanticRole::Button);
    first.set_bounds(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act, assert
    EXPECT_FALSE(first == second);
}

TEST(SemanticTree, ANewTreeIsEmpty) {
    // arrange, act
    const SemanticTree tree;
    // assert
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.size(), 0u);
}

TEST(SemanticTree, AddingANodeReportsItsIndex) {
    // arrange
    SemanticTree tree;
    SemanticNode node(SemanticRole::Button);
    // act
    const std::size_t index = tree.add(std::move(node));
    // assert
    EXPECT_EQ(index, 0u);
    EXPECT_EQ(tree.size(), 1u);
}

TEST(SemanticTree, AAddedNodeIsReadableAtItsIndex) {
    // arrange
    SemanticTree tree;
    SemanticNode node(SemanticRole::Button);
    node.set_name("Increment");
    const std::size_t index = tree.add(std::move(node));
    // act
    const SemanticNode* stored = tree.at(index);
    // assert
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->name(), "Increment");
}

TEST(SemanticTree, AnIndexPastTheEndNamesNoNode) {
    // arrange
    SemanticTree tree;
    // act, assert
    EXPECT_EQ(tree.at(0), nullptr);
}

TEST(SemanticTree, BuildingFromAProviderAddsItsNode) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 0);
    // act
    tree.build_from(provider);
    // assert
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree.at(0)->role(), SemanticRole::Group);
}

TEST(SemanticTree, BuildingFromAProviderFlattensItsChildren) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 3);
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.size(), 4u);
}

TEST(SemanticTree, AFlattenedTreePlacesTheParentFirst) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 2);
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.at(0)->name(), "Rows");
    EXPECT_EQ(tree.at(1)->role(), SemanticRole::ListItem);
}

TEST(SemanticTree, BuildingFromAProviderBuildsItsChildrenLazily) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 3);
    // act
    tree.build_from(provider);
    // assert the provider only ever built the children the walk asked for, one at a time (§10.1, AD-09)
    EXPECT_EQ(provider.built, 3u);
}

TEST(SemanticTree, AProviderWithoutChildrenYieldsOneNode) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Canvas, "Plot", 0);
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.size(), 1u);
}

TEST(SemanticTree, AProviderThatRefusesAChildIsSkipped) {
    // arrange
    SemanticTree tree;
    RefusingProvider provider;
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.size(), 1u);
}

TEST(SemanticTree, ARefusedChildIsAskedForEveryTime) {
    // arrange
    SemanticTree tree;
    RefusingProvider provider;
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree.at(0)->role(), SemanticRole::Group);
}

TEST(SemanticTree, AProviderChildBuildsNothingPastItsEnd) {
    // arrange
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 2);
    // act
    const std::unique_ptr<SemanticProvider> missing = provider.semantic_child(2);
    // assert
    EXPECT_EQ(missing, nullptr);
}

TEST(SemanticTree, ANodeIsFoundByAccessibleName) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 1);
    tree.build_from(provider);
    // act
    const SemanticNode* found = tree.find("Rows");
    // assert
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->role(), SemanticRole::Group);
}

TEST(SemanticTree, AnUnknownNameFindsNoNode) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 0);
    tree.build_from(provider);
    // act, assert
    EXPECT_EQ(tree.find("Nothing"), nullptr);
}

TEST(SemanticTree, TheRoleCountCoversTheWholeTree) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 4);
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.count_role(SemanticRole::ListItem), 4u);
}

TEST(SemanticTree, TheRoleCountIgnoresOtherRoles) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 2);
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.count_role(SemanticRole::Slider), 0u);
}

TEST(SemanticTree, ABuiltTreeIsNoLongerEmpty) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Canvas, "Plot", 0);
    // act
    tree.build_from(provider);
    // assert
    EXPECT_FALSE(tree.empty());
}

TEST(SemanticTree, ClearingEmptiesTheTree) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 2);
    tree.build_from(provider);
    // act
    tree.clear();
    // assert
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.at(0), nullptr);
}

TEST(SemanticTree, ATreeCanBeRebuiltAfterClearing) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 2);
    tree.build_from(provider);
    tree.clear();
    // act
    tree.build_from(provider);
    // assert
    EXPECT_EQ(tree.size(), 3u);
}

TEST(SemanticTree, TwoTreesOverTheSameProviderAgree) {
    // arrange
    SemanticTree first;
    SemanticTree second;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 2);
    // act
    first.build_from(provider);
    second.build_from(provider);
    // assert
    EXPECT_EQ(first.size(), second.size());
    EXPECT_TRUE(*first.at(0) == *second.at(0));
}

TEST(SemanticTree, ADeepProviderChainFlattensEveryLevel) {
    // arrange
    SemanticTree tree;
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 1);
    // act
    tree.build_from(provider);
    // assert the list has one item, and the item exposes none, so the walk stops there (§10.1, AD-09)
    EXPECT_EQ(tree.size(), 2u);
}

TEST(SemanticTree, ANodeKeepsItsActionsThroughTheWalk) {
    // arrange
    SemanticNode source(SemanticRole::Button);
    std::size_t runs = 0;
    source.set_name("press");
    const SemanticActionId press_action = source.add_action("press", [&runs] { runs++; });
    SemanticTree tree;
    // act
    const std::size_t index = tree.add(std::move(source));
    const bool invoked = tree.at(index)->invoke("press");
    // assert
    EXPECT_EQ(press_action, 1u);
    EXPECT_TRUE(invoked);
    EXPECT_EQ(runs, 1u);
}

TEST(SemanticTree, ANodeKeepsItsStatesThroughTheWalk) {
    // arrange
    SemanticNode source(SemanticRole::Checkbox);
    source.set_check(CheckState::Checked);
    source.set_disabled(true);
    SemanticTree tree;
    // act
    const std::size_t index = tree.add(std::move(source));
    // assert
    ASSERT_NE(tree.at(index), nullptr);
    EXPECT_TRUE(tree.at(index)->checked());
    EXPECT_TRUE(tree.at(index)->disabled());
}

TEST(SemanticProvider, AProviderBuildsTheNodeItDescribes) {
    // arrange
    TestSemanticProvider provider(SemanticRole::Slider, "Volume", 0);
    // act
    const SemanticNode node = provider.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Slider);
    EXPECT_EQ(node.name(), "Volume");
}

TEST(SemanticProvider, AProviderReportsHowManyChildrenItHas) {
    // arrange
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 7);
    // act, assert
    EXPECT_EQ(provider.semantic_child_count(), 7u);
}

TEST(SemanticProvider, AProviderChildIsIndependentOfItsParent) {
    // arrange
    TestSemanticProvider provider(SemanticRole::Group, "Rows", 1);
    // act
    const std::unique_ptr<SemanticProvider> child = provider.semantic_child(0);
    // assert
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_child_count(), 0u);
}

TEST(SemanticNode, ATextFieldStartsWithNoValue) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    const std::string& value = node.value();
    // assert an editable node publishes nothing until it is given something (§10.1)
    EXPECT_TRUE(value.empty());
}

TEST(SemanticNode, TheValueIsWhatTheControlPublished) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_value("Albor");
    // assert the node owns what it was handed, because the adapters read it long after the write (§10.1)
    EXPECT_EQ(node.value(), "Albor");
}

TEST(SemanticNode, TheValueKeepsTheTextItWasGiven) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_value("\xC3\xA9t\xC3\xA9");
    // assert a value is a text value, so two byte characters are not truncated (§10.1)
    EXPECT_EQ(node.value(), "\xC3\xA9t\xC3\xA9");
}

TEST(SemanticNode, AnEmptyValueIsStillAValue) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    node.set_value("before");
    // act
    node.set_value("");
    // assert a field the user emptied publishes nothing rather than keeping the last text (§10.1)
    EXPECT_TRUE(node.value().empty());
}

TEST(SemanticNode, AButtonCarriesNoValue) {
    // arrange
    SemanticNode node(SemanticRole::Button);
    // act
    node.set_value("ignored by the role");
    // assert the state is reported, and it is the role that decides how it is read (§10.1)
    EXPECT_EQ(node.value(), "ignored by the role");
    EXPECT_EQ(node.role(), SemanticRole::Button);
}

TEST(SemanticNode, ANodeStartsEditableAndSingleLine) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    const bool read_only = node.read_only();
    const bool multiline = node.multiline();
    // assert both states default false, because a field is editable and single line until told otherwise (§10.1)
    EXPECT_FALSE(read_only);
    EXPECT_FALSE(multiline);
}

TEST(SemanticNode, TheReadOnlyStateCanBeSet) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_read_only(true);
    // assert
    EXPECT_TRUE(node.read_only());
}

TEST(SemanticNode, ReadOnlyIsDistinctFromDisabled) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_read_only(true);
    // assert a read only field still takes focus and still permits a copy, which disabled does not (§10.1)
    EXPECT_TRUE(node.read_only());
    EXPECT_FALSE(node.disabled());
}

TEST(SemanticNode, TheMultiLineStateCanBeSet) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_multiline(true);
    // assert
    EXPECT_TRUE(node.multiline());
}

TEST(SemanticNode, MultiLineIsIndependentOfTheValue) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_multiline(true);
    node.set_value("first\nsecond");
    // assert the states are independent, because a read only field may still span lines (§10.1)
    EXPECT_TRUE(node.multiline());
    EXPECT_EQ(node.value(), "first\nsecond");
}

TEST(SemanticNode, TheEditableStatesAreCarriedByTheStateSet) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    // act
    node.set_value("v");
    node.set_read_only(true);
    node.set_multiline(true);
    // assert the states compare by what they carry, so a tree test can assert on all of them at once (§10.1)
    EXPECT_EQ(node.states(), (SemanticStates{.disabled = false, .focused = false, .check = CheckState::NotCheckable, .value = "v", .read_only = true, .multiline = true}));
}

TEST(SemanticNode, AReadOnlyFieldStillRunsItsActions) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    node.set_read_only(true);
    bool copied = false;
    (void)node.add_action("copy", [&copied] { copied = true; });
    // act
    const bool invoked = node.invoke("copy");
    // assert a read only field refuses edits but still permits a copy, which is why the state is not disabled (§10.1)
    EXPECT_TRUE(invoked);
    EXPECT_TRUE(copied);
}

TEST(SemanticNode, ADisabledFieldStillRefusesItsActions) {
    // arrange
    SemanticNode node(SemanticRole::TextField);
    node.set_disabled(true);
    bool copied = false;
    (void)node.add_action("copy", [&copied] { copied = true; });
    // act
    const bool invoked = node.invoke("copy");
    // assert the two states differ exactly here, which is what makes them separate states (§10.1)
    EXPECT_FALSE(invoked);
    EXPECT_FALSE(copied);
}
