// the macos half of the accessibility bridge: a role is only reachable once it hands its tree over (§10.2, AD-16)

#import <AppKit/AppKit.h>

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "accessibility/accessibility_adapter.h"
#include "core/native_window_handle.h"
#include "semantics/semantic_provider.h"

using namespace albor;

namespace
{
    // a viewport with one child, which is the smallest tree a reader can walk into (§10.1)
    class ViewportProvider final : public SemanticProvider
    {
    public:
        SemanticNode semantic_node() const override {
            // the node carries the role and name this provider publishes, which the bridge maps one for one (§10.1)
            SemanticNode node{m_role};
            // the name is what a reader speaks, so it belongs on the node (§10.1)
            node.set_name(m_name);
            // the selected state is what a tab item announces, which the radio button value reads (§10.1)
            node.set_selected(m_selected);
            // the check state is what a radio button announces beside its value (§10.1)
            node.set_check(m_check);
            return node;
        }

        std::size_t semantic_child_count() const override {
            // the container roles hold a child, because a viewport, a tab group and a radio group walk into one (§10.1)
            return m_role == SemanticRole::ScrollView || m_role == SemanticRole::TabView || m_role == SemanticRole::RadioGroup ? 1U : 0U;
        }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // the single child is the role the test asked for, so the walk has a hierarchy to announce (§10.2)
            if (index != 0)
                return nullptr;
            auto label = std::make_unique<ViewportProvider>();
            // the child carries the role the container test asked for, which is a label by default (§10.1)
            label->m_role = m_child_role;
            // the child carries a name, which a reader speaks (§10.1)
            label->m_name = m_child_name;
            // a tab item and a radio choice carry a selected state, which a reader speaks (§10.1)
            label->m_selected = m_child_selected;
            // a radio choice also carries a check state, which its value reads (§10.1)
            label->m_check = m_child_check;
            return label;
        }

        // the role this provider publishes, which the child overrides to be something else (§10.1)
        SemanticRole m_role = SemanticRole::ScrollView;

        // the name this provider publishes, which a reader speaks (§10.1)
        std::string m_name = "Log";

        // whether this provider reports itself selected, which a tab item test sets (§10.1)
        bool m_selected = false;

        // the check state this provider publishes, which a radio choice test sets (§10.1)
        CheckState m_check = CheckState::NotCheckable;

        // the role the single child publishes, which a tab test sets to a tab item (§10.1)
        SemanticRole m_child_role = SemanticRole::Label;

        // the name the single child publishes, which a reader speaks (§10.1)
        std::string m_child_name = "first row";

        // whether the single child reports itself selected, which a tab item test sets (§10.1)
        bool m_child_selected = false;

        // the check state the single child publishes, which a radio choice test sets (§10.1)
        CheckState m_child_check = CheckState::NotCheckable;
    };

    // a window the bridge publishes into, which the suite owns and releases (§5.2)
    class WindowFixture
    {
    public:
        WindowFixture() :
            // the window needs a content view, because that is where the tree is announced (§10.2)
            window([[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 320, 240) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO]) {
            // the bridge is built for this platform, which is the one this suite exercises (§10.2)
            adapter = accessibility::create_adapter();
            // the window is handed over as an opaque handle, because the framework names no platform type (§3.5, AD-05)
            adapter->attach_window(NativeWindowHandle::from_object((__bridge const void*)window));
        }

        ~WindowFixture() {
            // the bridge lets go before the window dies, so a reader never sees released elements (§10.2)
            adapter->detach_window();
        }

        // the window under test, which the suite reads the announced tree from (§10.2)
        NSWindow* window = nil;

        // the bridge under test, which is what the suite publishes into (§10.2)
        std::unique_ptr<accessibility::AccessibilityAdapter> adapter;
    };

    // the announced children of the window's content view, which is where the published root appears (§10.2)
    NSArray* announced_children(NSWindow* window) { return window.contentView.accessibilityChildren; }
} // namespace

TEST(AccessibilityRole, TheAnnouncedRootCarriesTheSemanticRole) {
    // arrange a tree whose root is a viewport, which is the node whose role is at issue (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    // act
    fixture.adapter->publish(tree);
    // assert the window announces exactly the published root, because that is what makes it reachable (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    // assert a viewport announces as the scroll area assistive technology scrolls (§10.2, AD-08)
    EXPECT_EQ([(NSView*)children.firstObject accessibilityRole], NSAccessibilityScrollAreaRole);
}

TEST(AccessibilityRole, TheAnnouncedTreeCarriesEveryChild) {
    // arrange
    WindowFixture fixture;
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    // act
    fixture.adapter->publish(tree);
    // assert the root's children are announced, which is the hierarchy a reader walks (§10.2)
    NSArray* roots = announced_children(fixture.window);
    ASSERT_EQ(roots.count, 1U);
    NSArray* children = [(NSView*)roots.firstObject accessibilityChildren];
    ASSERT_EQ(children.count, 1U);
    // assert the child announces as static text and under its own name, which is what the node carries (§10.1)
    EXPECT_EQ([(NSView*)children.firstObject accessibilityRole], NSAccessibilityStaticTextRole);
    EXPECT_STREQ([(NSView*)children.firstObject accessibilityLabel].UTF8String, "first row");
}

TEST(AccessibilityRole, ATabViewAnnouncesItsGroupAndPage) {
    // arrange a tree whose root is a tab group with one tab item child (§10.1)
    WindowFixture fixture;
    ViewportProvider provider;
    provider.m_role = SemanticRole::TabView;
    provider.m_name = "Pages";
    provider.m_child_role = SemanticRole::TabItem;
    provider.m_child_name = "One";
    provider.m_child_selected = true;
    SemanticTree tree;
    tree.build_from(provider);
    // act
    fixture.adapter->publish(tree);
    // assert the root announces as a tab group, which is the container a reader navigates pages in (§10.2)
    NSArray* roots = announced_children(fixture.window);
    ASSERT_EQ(roots.count, 1U);
    EXPECT_EQ([(NSView*)roots.firstObject accessibilityRole], NSAccessibilityTabGroupRole);
    // assert the page announces as the selectable item the group holds (§10.2)
    NSArray* children = [(NSView*)roots.firstObject accessibilityChildren];
    ASSERT_EQ(children.count, 1U);
    EXPECT_EQ([(NSView*)children.firstObject accessibilityRole], NSAccessibilityRadioButtonRole);
    EXPECT_STREQ([(NSView*)children.firstObject accessibilityLabel].UTF8String, "One");
    // assert the selected state reaches the radio button value, which is what a reader speaks (§10.2)
    NSString* value = (NSString*)[(NSView*)children.firstObject accessibilityValue];
    ASSERT_NE(value, nil);
    EXPECT_STREQ(value.UTF8String, "1");
}

TEST(AccessibilityRole, TheWindowCarriesNothingBeforeAnythingIsPublished) {
    // arrange a bridge attached to a window that has published nothing (§10.2)
    WindowFixture fixture;
    // act
    fixture.adapter->publish(SemanticTree{});
    // assert the view announces nothing, because there is no tree to reach (§10.2)
    EXPECT_EQ(announced_children(fixture.window).count, 0U);
}

TEST(AccessibilityRole, ADetachedBridgeTakesItsTreeOutOfTheWindow) {
    // arrange
    WindowFixture fixture;
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    fixture.adapter->publish(tree);
    // act
    fixture.adapter->detach_window();
    // assert the window announces nothing, because a reader must not see a dead tree (§10.2)
    EXPECT_EQ(announced_children(fixture.window).count, 0U);
}

TEST(AccessibilityRole, ATextFieldAnnouncesItsValue) {
    // arrange a tree whose root is an editable field carrying text (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    // the node needs the identity the tree walk keeps, and the text a reader would edit (§10.1)
    SemanticNode* field = tree.mutable_at(0);
    field->set_value("Albor");
    // act
    fixture.adapter->publish(tree);
    // assert the window announces the field, whose value is what assistive technology edits (§10.1, §10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    EXPECT_EQ(element.accessibilityRole, NSAccessibilityTextFieldRole);
    // the value is read back as a string, because the protocol hands it over untyped (§10.2)
    NSString* value = (NSString*)element.accessibilityValue;
    // assert the value reaches the element rather than only the tree it was published from (§10.2)
    EXPECT_STREQ(value.UTF8String, "Albor");
}

TEST(AccessibilityRole, ACheckboxStillAnnouncesItsCheckStateAsItsValue) {
    // arrange a tree whose root is a checkable node, which already had a value (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Checkbox});
    tree.mutable_at(0)->set_check(CheckState::Checked);
    // act
    fixture.adapter->publish(tree);
    // assert the check state is still announced as the value, because the text branch is role scoped (§10.1)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    // the value is read back as a string, because the protocol hands it over untyped (§10.2)
    NSString* value = (NSString*)[(NSView*)children.firstObject accessibilityValue];
    EXPECT_STREQ(value.UTF8String, "1");
}

TEST(AccessibilityRole, AListAnnouncesAsAList) {
    // arrange a tree whose root is a list, which is the role at issue (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::List});
    // act
    fixture.adapter->publish(tree);
    // assert the list announces as the list role, which is the container assistive technology navigates rows in (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    EXPECT_EQ([(NSView*)children.firstObject accessibilityRole], NSAccessibilityListRole);
}

TEST(AccessibilityRole, ASelectedRowAnnouncesItsSelectedState) {
    // arrange a list whose single row is the chosen one (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    std::size_t list = tree.add(SemanticNode{SemanticRole::List});
    std::size_t row = tree.add(SemanticNode{SemanticRole::ListItem}, list);
    tree.mutable_at(row)->set_selected(true);
    // act
    fixture.adapter->publish(tree);
    // assert the chosen row announces itself selected, which is the state a reader speaks (§10.2)
    NSArray* roots = announced_children(fixture.window);
    ASSERT_EQ(roots.count, 1U);
    NSArray* rows = [(NSView*)roots.firstObject accessibilityChildren];
    ASSERT_EQ(rows.count, 1U);
    // the element answers the legacy getter, because selection is declared on the NSAccessibility protocol (§10.2)
    EXPECT_TRUE([(id)rows.firstObject isAccessibilitySelected]);
}

TEST(AccessibilityRole, AnUnchosenRowDoesNotAnnounceSelection) {
    // arrange a list whose single row is not the chosen one (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    std::size_t list = tree.add(SemanticNode{SemanticRole::List});
    tree.add(SemanticNode{SemanticRole::ListItem}, list);
    // act
    fixture.adapter->publish(tree);
    // assert an unselected row announces no selection, because the state defaults quiet (§10.2)
    NSArray* roots = announced_children(fixture.window);
    ASSERT_EQ(roots.count, 1U);
    NSArray* rows = [(NSView*)roots.firstObject accessibilityChildren];
    ASSERT_EQ(rows.count, 1U);
    // the element answers the legacy getter, because selection is declared on the NSAccessibility protocol (§10.2)
    EXPECT_FALSE([(id)rows.firstObject isAccessibilitySelected]);
}

TEST(AccessibilityRole, APlainLabelAnnouncesNoValue) {
    // arrange a tree whose root carries no value of any kind (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Label});
    // act
    fixture.adapter->publish(tree);
    // assert a role with nothing to report announces nothing rather than an empty string (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    EXPECT_EQ([(NSView*)children.firstObject accessibilityValue], nil);
}

TEST(AccessibilityRole, ARadioGroupAnnouncesItsChoices) {
    // arrange a radio group whose single choice is the selected one (§10.1)
    WindowFixture fixture;
    ViewportProvider provider;
    provider.m_role = SemanticRole::RadioGroup;
    provider.m_name = "Modes";
    provider.m_child_role = SemanticRole::RadioButton;
    provider.m_child_name = "One";
    provider.m_child_selected = true;
    provider.m_child_check = CheckState::Checked;
    SemanticTree tree;
    tree.build_from(provider);
    // act
    fixture.adapter->publish(tree);
    // assert the root announces as a radio group, which is the container a reader navigates choices in (§10.2)
    NSArray* roots = announced_children(fixture.window);
    ASSERT_EQ(roots.count, 1U);
    EXPECT_EQ([(NSView*)roots.firstObject accessibilityRole], NSAccessibilityRadioGroupRole);
    // assert the choice announces as a radio button under its own name (§10.2)
    NSArray* choices = [(NSView*)roots.firstObject accessibilityChildren];
    ASSERT_EQ(choices.count, 1U);
    NSView* choice = (NSView*)choices.firstObject;
    EXPECT_EQ(choice.accessibilityRole, NSAccessibilityRadioButtonRole);
    EXPECT_STREQ(choice.accessibilityLabel.UTF8String, "One");
    // assert the chosen choice announces both its selection and its checked value (§10.2)
    EXPECT_TRUE([(id)choice isAccessibilitySelected]);
    NSString* value = (NSString*)choice.accessibilityValue;
    ASSERT_NE(value, nil);
    EXPECT_STREQ(value.UTF8String, "1");
}

TEST(AccessibilityRole, ANumericStepperAnnouncesItsValueAndStepping) {
    // arrange a stepper carrying a number and the two actions assistive technology drives (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::NumericStepper});
    SemanticNode* stepper = tree.mutable_at(0);
    stepper->set_name("Effort");
    stepper->set_value("3.5");
    std::size_t increments = 0;
    std::size_t decrements = 0;
    (void)stepper->add_action("increment", [&increments] { increments++; });
    (void)stepper->add_action("decrement", [&decrements] { decrements++; });
    // act
    fixture.adapter->publish(tree);
    // assert the stepper announces as an incrementor carrying its number (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    EXPECT_EQ(element.accessibilityRole, NSAccessibilityIncrementorRole);
    NSNumber* value = (NSNumber*)element.accessibilityValue;
    ASSERT_NE(value, nil);
    EXPECT_DOUBLE_EQ(value.doubleValue, 3.5);
    // act the two stepping actions reach the published node (§10.2)
    EXPECT_TRUE([(id)element accessibilityPerformIncrement]);
    EXPECT_TRUE([(id)element accessibilityPerformDecrement]);
    // assert each action ran the work the node carries, which is what changes the number (§10.1)
    EXPECT_EQ(increments, 1u);
    EXPECT_EQ(decrements, 1u);
}

TEST(AccessibilityRole, ACheckboxPressReachesItsToggleAction) {
    // arrange a checkable node with the one action its portable table names (§10.1, §10.2)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Checkbox});
    tree.mutable_at(0)->set_name("Show grid");
    std::size_t toggles = 0;
    (void)tree.mutable_at(0)->add_action("toggle", [&toggles] { toggles++; });
    // act
    fixture.adapter->publish(tree);
    // assert the checkbox announces as a checkbox and its press runs the toggle the table declares (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    EXPECT_EQ(element.accessibilityRole, NSAccessibilityCheckBoxRole);
    EXPECT_TRUE([(id)element accessibilityPerformPress]);
    // assert the application's toggle ran exactly once, because the bridge routed press through the table (§10.1)
    EXPECT_EQ(toggles, 1u);
}

TEST(AccessibilityRole, ASliderRangeWriteReachesItsNode) {
    // arrange an enabled slider with a range and a write that records the target (§10.1, §10.2)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Slider});
    SemanticNode* slider = tree.mutable_at(0);
    slider->set_name("Gain");
    double written = 0.0;
    slider->set_range(SemanticRange{.value = 0.25, .minimum = 0.0, .maximum = 1.0, .step = 0.05}, [&written](double value) { written = value; });
    // act
    fixture.adapter->publish(tree);
    // assert the page publishes the slider before a reader writes its value (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    [(id)element setAccessibilityValue:@(0.8)];
    // assert the node's write ran with the target a reader asked for (§10.1, §10.2)
    EXPECT_DOUBLE_EQ(written, 0.8);
}

TEST(AccessibilityRole, ADisabledSliderRefusesTheStepItsTableOffers) {
    // arrange a disabled slider with a range and the two step actions (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Slider});
    SemanticNode* slider = tree.mutable_at(0);
    slider->set_name("Gain");
    slider->set_value("0.25");
    slider->set_disabled(true);
    std::size_t steps = 0;
    slider->set_range(SemanticRange{.value = 0.25, .minimum = 0.0, .maximum = 1.0, .step = 0.05}, [](double) {});
    (void)slider->add_action("increment", [&steps] { steps++; });
    (void)slider->add_action("decrement", [&steps] { steps++; });
    // act
    fixture.adapter->publish(tree);
    // assert the slider announces its bounded value, which the range pattern carries (§10.1, §10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    EXPECT_EQ(element.accessibilityRole, NSAccessibilitySliderRole);
    NSNumber* value = (NSNumber*)element.accessibilityValue;
    ASSERT_NE(value, nil);
    EXPECT_DOUBLE_EQ(value.doubleValue, 0.25);
    // act the bridge offers the step the table names, while the node is disabled (§10.2)
    EXPECT_FALSE([(id)element accessibilityPerformIncrement]);
    EXPECT_FALSE([(id)element accessibilityPerformDecrement]);
    // assert no action ran, because a disabled node refuses every action in one place (§10.1)
    EXPECT_EQ(steps, 0u);
}

TEST(AccessibilityRole, AMenuItemAnnouncesItsRoleAndName) {
    // arrange a menu row with an author id and a label (§10.1, §18.2)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::MenuItem});
    tree.mutable_at(0)->set_explicit_id("menu_save");
    tree.mutable_at(0)->set_name("Save");
    // act
    fixture.adapter->publish(tree);
    // assert the row announces as a menu item, which is what assistive technology presents in a menu (§10.2)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    EXPECT_EQ(element.accessibilityRole, NSAccessibilityMenuItemRole);
    EXPECT_STREQ(element.accessibilityLabel.UTF8String, "Save");
}

TEST(AccessibilityRole, AnImageAnnouncesItsRoleNameAndDisabledState) {
    // arrange an image whose name and disabled state are what a reader announces (§10.1)
    WindowFixture fixture;
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Image});
    tree.mutable_at(0)->set_name("Company logo");
    tree.mutable_at(0)->set_disabled(true);
    // act
    fixture.adapter->publish(tree);
    // assert the image role, its name and its disabled flag reach the native element (§10.2, AD-16)
    NSArray* children = announced_children(fixture.window);
    ASSERT_EQ(children.count, 1U);
    NSView* element = (NSView*)children.firstObject;
    EXPECT_EQ(element.accessibilityRole, NSAccessibilityImageRole);
    EXPECT_STREQ(element.accessibilityLabel.UTF8String, "Company logo");
    EXPECT_FALSE([(id)element isAccessibilityEnabled]);
}
