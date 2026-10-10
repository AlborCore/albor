#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "core/dev_hook.h"
#include "devrt/ir.h"
#include "devrt/reload_adapter.h"
#include "ui/ui_node.h"

using namespace albor;
using namespace albor::devrt;

namespace
{
    // the element the factory builds, which is a node with no meaning of its own (§12.1)
    std::unique_ptr<UiNode> build_element(const std::string& element) {
        // the node is built, because a reload needs something to instantiate (§12.1)
        std::unique_ptr<UiNode> node = std::make_unique<UiNode>();
        // the element name becomes the identity a test can read back, because the extension adds no widget type (§12.1)
        node->set_explicit_id(element);
        // the node is returned whole, because the tree adopts it (§8.1)
        return node;
    }

    // find a descendant carrying an id, which is how a test asserts on a patch (§12.1, AD-11)
    UiNode* find_by_id(UiNode& root, const std::string& id) {
        // the node the caller wants is recorded during the walk, because visit cannot return a value (§12.1)
        UiNode* found = nullptr;
        // the walk visits every node, which is what a deep search needs (§12.1)
        root.visit([&](UiNode& node) {
            // the first match wins, because ids are unique by the author's contract (§18.1, AD-11)
            if (found == nullptr && node.identity().explicit_id == id)
                found = &node;
        });
        // the node is returned, or nothing when no node carries the id (§12.1)
        return found;
    }

    // count every node of a tree, which is how a test asserts on additions and removals (§12.1)
    std::size_t count_nodes(UiNode& root) {
        // the count starts at zero, because the walk visits the root among the rest (§8.1)
        std::size_t count = 0;
        // the tree is walked, because a patch may add at any depth, and the walk includes the root (§8.1)
        root.visit([&](UiNode&) { count++; });
        // the count is returned whole, which is what a test asserts on (§12.1)
        return count;
    }
} // namespace

TEST(DevIrChecks, AnIndentedDefinitionReadsAsATree) {
    // arrange a definition with a root, a property and two children (§12.1)
    const std::string text = "node Panel id=root\n"
                             "  prop spacing 12\n"
                             "  node Label id=title\n"
                             "  node Button id=ok\n";
    // act
    const std::optional<IrNode> parsed = parse_ir(text);
    // assert the structure the indentation encoded (§12.1)
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->element, "Panel");
    EXPECT_EQ(parsed->id, "root");
    ASSERT_EQ(parsed->properties.size(), 1U);
    EXPECT_EQ(parsed->properties.at(0).name, "spacing");
    ASSERT_EQ(parsed->children.size(), 2U);
    EXPECT_EQ(parsed->children.at(0).id, "title");
}

TEST(DevIrChecks, ADeepDefinitionNestsToItsDepth) {
    // arrange a definition three levels deep, which is where a shallow reader would flatten it (§12.1)
    const std::string text = "node A id=a\n  node B id=b\n    node C id=c\n";
    // act
    const std::optional<IrNode> parsed = parse_ir(text);
    // assert each level sits under the one above it (§12.1)
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->children.size(), 1U);
    ASSERT_EQ(parsed->children.at(0).children.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).children.at(0).id, "c");
}

TEST(DevIrChecks, ADefinitionRoundTripsThroughTheFormat) {
    // arrange
    IrNode root;
    // the root carries an id and a child, which exercises both fields (§12.1)
    root.element = "Panel";
    // the id is set, because identity is what a reload matches (§12.1, AD-11)
    root.id = "root";
    // the child carries a property, because that is what the format must survive (§12.1)
    root.children.push_back(IrNode{.element = "Label", .id = "title", .properties = {IrProperty{.name = "text", .value = "hello"}}, .children = {}});
    // act
    const std::optional<IrNode> parsed = parse_ir(write_ir(root));
    // assert the whole structure survived, because a reload reads what a writer produced (§12.1)
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, "root");
    ASSERT_EQ(parsed->children.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).id, "title");
    ASSERT_EQ(parsed->children.at(0).properties.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).properties.at(0).value, "hello");
}

TEST(DevIrChecks, AMalformedDefinitionIsRefused) {
    // assert each of these describes nothing, so a malformed reload cannot touch a running tree (§12.1)
    EXPECT_FALSE(parse_ir("").has_value());
    EXPECT_FALSE(parse_ir("not a node\n").has_value());
    EXPECT_FALSE(parse_ir("node Panel id=a\nnode Other id=b\n").has_value());
}

TEST(DevReloadChecks, AFirstReloadPopulatesAnEmptyTree) {
    // arrange a tree with no children, which is what a host has before its first reload (§12.1)
    UiNode root;
    // act
    const ReloadReport report = apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=title\n"), &build_element);
    // assert the child was added under the root, because the definition's root is the existing component (§12.1)
    EXPECT_EQ(report.added, 1U);
    EXPECT_EQ(count_nodes(root), 2U);
    // assert the named node is addressable, because identity is what a later edit matches (§12.1, AD-11)
    EXPECT_NE(find_by_id(root, "title"), nullptr);
}

TEST(DevReloadChecks, ASecondReloadPreservesTheNodesItMatches) {
    // arrange a tree already holding a named child (§12.1)
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=title\n"), &build_element);
    // the matched node is recorded, because preserving its instance is the whole claim (§12.1, §12.2)
    UiNode* before = find_by_id(root, "title");
    ASSERT_NE(before, nullptr);
    // act the definition is unchanged, which is what an edit that changes nothing looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=title\n"), &build_element);
    // assert the node kept its identity, which is what preserves the interaction state inside it (§12.1, §12.2)
    EXPECT_EQ(find_by_id(root, "title"), before);
    // assert nothing was added or removed, because the definition did not change (§12.1)
    EXPECT_EQ(report.added, 0U);
    EXPECT_EQ(report.removed, 0U);
    EXPECT_EQ(report.preserved, 1U);
}

TEST(DevReloadChecks, AMovedNodeFollowsItsIdRatherThanItsPosition) {
    // arrange two children, the second of which carries an id (§12.1, AD-11)
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n  node Button id=second\n"), &build_element);
    // the second node is recorded, because an edit will move it (§12.1, AD-11)
    UiNode* second = find_by_id(root, "second");
    ASSERT_NE(second, nullptr);
    // act the definition swaps the order, which is what a person editing a column does (§12.1)
    (void)apply_reload(root, *parse_ir("node Panel id=root\n  node Button id=second\n  node Label id=first\n"), &build_element);
    // assert the node kept its identity rather than the second slot, because the id is what identity means (§18.1, AD-11)
    EXPECT_EQ(find_by_id(root, "second"), second);
}

TEST(DevReloadChecks, ARemovedNodeIsDropped) {
    // arrange two children (§12.1)
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n  node Button id=second\n"), &build_element);
    ASSERT_EQ(count_nodes(root), 3U);
    // act the definition keeps only one, which is what deleting a row looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n"), &build_element);
    // assert the other is gone, because the definition no longer wants it (§12.1)
    EXPECT_EQ(report.removed, 1U);
    EXPECT_EQ(count_nodes(root), 2U);
    EXPECT_EQ(find_by_id(root, "second"), nullptr);
}

TEST(DevReloadChecks, AnAddedNodeAppears) {
    // arrange a tree with one child (§12.1)
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n"), &build_element);
    // act the definition gains a child, which is what typing a new row looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n  node Button id=second\n"), &build_element);
    // assert the addition is reported and addressable (§12.1)
    EXPECT_EQ(report.added, 1U);
    EXPECT_NE(find_by_id(root, "second"), nullptr);
}

TEST(DevReloadChecks, AReloadWithNoFactoryAddsNothingAndKeepsTheTree) {
    // arrange a tree holding one child (§12.1)
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n"), &build_element);
    // act the definition gains a child, but no factory exists to build it (§12.1, AD-02)
    const ReloadReport report = apply_reload(root, *parse_ir("node Panel id=root\n  node Label id=first\n  node Button id=second\n"), nullptr);
    // assert nothing was invented, because the extension builds no element of its own (§12.1, AD-02)
    EXPECT_EQ(report.added, 0U);
    // assert the existing node survived, because an incomplete reload must not damage a running tree (§12.1)
    EXPECT_NE(find_by_id(root, "first"), nullptr);
}

TEST(DevHookChecks, AReleaseBuildCarriesNoReloadPath) {
    // the check only means anything where no extension is linked, which the release preset is (§2.2)
#ifndef ALBOR_DEV
    // assert nothing installed a reload, because a release links no extension (§12.1, AD-02)
    EXPECT_FALSE(dev::present());
    // assert the call is safe with nothing installed, which is the whole point of the hook (§12.1, AD-02)
    dev::reload("node Panel id=root");
#endif
}

TEST(DevHookChecks, AReloadWithNothingInstalledDoesNothing) {
    // arrange a tree, which nothing is bound to (§12.1)
    UiNode root;
    // act the runtime hands a definition to a slot nobody filled (§12.1, AD-02)
    dev::reload("node Panel id=root\n  node Label id=title\n");
    // assert the tree was untouched, because a build without the extension must be unaffected (§12.1, AD-02)
    EXPECT_EQ(count_nodes(root), 1U);
}
