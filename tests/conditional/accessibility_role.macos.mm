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
            return node;
        }

        std::size_t semantic_child_count() const override {
            // only the viewport holds a child, because a label is a leaf (§10.1)
            return m_role == SemanticRole::ScrollView ? 1U : 0U;
        }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // the single child is a label, so the viewport has a hierarchy to announce (§10.2)
            if (index != 0)
                return nullptr;
            auto label = std::make_unique<ViewportProvider>();
            // the child is static text, which is what a row inside a viewport is (§10.1)
            label->m_role = SemanticRole::Label;
            // the child carries a name, which a reader speaks (§10.1)
            label->m_name = "first row";
            return label;
        }

        // the role this provider publishes, which the child overrides to be something else (§10.1)
        SemanticRole m_role = SemanticRole::ScrollView;

        // the name this provider publishes, which a reader speaks (§10.1)
        std::string m_name = "Log";
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
