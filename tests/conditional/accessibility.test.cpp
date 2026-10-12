#include <cstddef>

#include <gtest/gtest.h>

#include "accessibility/accessibility_adapter.h"
#include "semantics/semantic_node.h"
#include "semantics/semantic_provider.h"

using namespace albor;

namespace
{
    // the label child the button publishes, which carries an accessible name of its own (§10.1)
    class LabelProvider final : public SemanticProvider
    {
    public:
        // the index is what distinguishes one child from another, so an order assertion can read it (§10.1)
        explicit LabelProvider(std::size_t index) :
            m_index(index) {}

        SemanticNode semantic_node() const override {
            // the node is static text, which assistive technology announces without an action (§10.1)
            SemanticNode node{SemanticRole::Label};
            // the name carries the index, so a reader can tell which child it reached (§10.1)
            node.set_name("label" + std::to_string(m_index));
            return node;
        }

        std::size_t semantic_child_count() const override { return 0; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t) const override { return nullptr; }

    private:
        // which child this provider is, which the name carries (§10.1)
        std::size_t m_index;
    };

    // the provider a suite publishes, which is a button over two labels (§10.1, AD-09)
    class ButtonProvider final : public SemanticProvider
    {
    public:
        SemanticNode semantic_node() const override {
            // the node is a button with an accessible name, which is the minimum assistive technology reads (§10.1)
            SemanticNode node{SemanticRole::Button};
            // the name is what a screen reader speaks, so it is the whole point of the node (§10.1)
            node.set_name("Save");
            // the action is what assistive technology invokes when it presses the control (§10.1)
            (void)node.add_action("activate", [this] { m_presses++; });
            return node;
        }

        std::size_t semantic_child_count() const override { return 2; }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // an index past the end names no child, which the protocol treats as absent (§10.1)
            if (index >= 2)
                return nullptr;
            // the child is a label carrying the index, which makes the order observable (§10.1)
            return std::make_unique<LabelProvider>(index);
        }

        // how many times assistive technology pressed the control, which the tree copy cannot observe (§10.2)
        mutable std::size_t m_presses = 0;
    };

} // namespace

TEST(AccessibilityBridge, ABuildThatHasABridgeSaysSo) {
    // assert the bridge exists on this platform, because the macos adapter is compiled (§10.2)
    EXPECT_TRUE(accessibility::AccessibilityAdapter::available());
}

TEST(AccessibilityBridge, TheFactoryHandsBackABridge) {
    // act
    const std::unique_ptr<accessibility::AccessibilityAdapter> adapter = accessibility::create_adapter();
    // assert
    ASSERT_NE(adapter, nullptr);
}

TEST(AccessibilityBridge, ANewBridgeHasPublishedNothing) {
    // arrange
    const std::unique_ptr<accessibility::AccessibilityAdapter> adapter = accessibility::create_adapter();
    // assert a bridge that has never published reports nothing (§10.2)
    EXPECT_EQ(adapter->published_node_count(), 0U);
}

TEST(AccessibilityBridge, PublishingReportsHowManyNodesItCarries) {
    // arrange
    const std::unique_ptr<accessibility::AccessibilityAdapter> adapter = accessibility::create_adapter();
    SemanticTree tree;
    tree.build_from(ButtonProvider());
    // act
    adapter->publish(tree);
    // assert the count is the whole tree, which is the coverage a test reads (§10.2)
    EXPECT_EQ(adapter->published_node_count(), 3U);
}

TEST(AccessibilityBridge, PublishingTwiceReplacesRatherThanAccumulates) {
    // arrange
    const std::unique_ptr<accessibility::AccessibilityAdapter> adapter = accessibility::create_adapter();
    SemanticTree first;
    first.build_from(ButtonProvider());
    SemanticTree second;
    second.build_from(ButtonProvider());
    // act
    adapter->publish(first);
    adapter->publish(second);
    // assert assistive technology reads one tree at a time, so the count is not the sum (§10.2)
    EXPECT_EQ(adapter->published_node_count(), 3U);
}

TEST(AccessibilityBridge, PublishingWithoutAWindowIsStillAccepted) {
    // arrange
    const std::unique_ptr<accessibility::AccessibilityAdapter> adapter = accessibility::create_adapter();
    SemanticTree tree;
    tree.build_from(ButtonProvider());
    // act, assert a headless publication is held, because automation publishes with no screen reader (§10.2)
    adapter->publish(tree);
    EXPECT_EQ(adapter->published_node_count(), 3U);
}

TEST(AccessibilityBridge, ADetachedBridgePublishesNothing) {
    // arrange
    const std::unique_ptr<accessibility::AccessibilityAdapter> adapter = accessibility::create_adapter();
    SemanticTree tree;
    tree.build_from(ButtonProvider());
    adapter->publish(tree);
    // act
    adapter->detach_window();
    // assert a closed window takes the publication, because assistive technology must not read a dead tree (§5.2)
    EXPECT_EQ(adapter->published_node_count(), 0U);
}
