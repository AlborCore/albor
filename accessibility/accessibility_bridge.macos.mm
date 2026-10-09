#include "accessibility/accessibility_platform.h"

// the application kit accessibility protocol is the macos side of the bridge (§10.2, AD-16)
#import <AppKit/AppKit.h>

#include <string_view>
#include <vector>

// the element class is named ahead, because a publication holds one before the interface is declared (§10.2)
@class AlborAccessibilityElement;

namespace
{
    // the role the application kit announces for a semantic role, which the bridge maps one to one (§10.2)
    NSAccessibilityRole native_role(albor::SemanticRole role) {
        // a switch covers every role, because the two enumerations are meant to correspond (§10.1, §10.2)
        switch (role) {
        case albor::SemanticRole::None:
            return NSAccessibilityUnknownRole;
        case albor::SemanticRole::Button:
            return NSAccessibilityButtonRole;
        case albor::SemanticRole::Checkbox:
            return NSAccessibilityCheckBoxRole;
        case albor::SemanticRole::Heading:
        case albor::SemanticRole::Label:
            return NSAccessibilityStaticTextRole;
        case albor::SemanticRole::Slider:
            return NSAccessibilitySliderRole;
        case albor::SemanticRole::Canvas:
        case albor::SemanticRole::Group:
        case albor::SemanticRole::ListItem:
            return NSAccessibilityGroupRole;
        case albor::SemanticRole::TextField:
            return NSAccessibilityTextFieldRole;
        case albor::SemanticRole::Image:
            return NSAccessibilityImageRole;
        case albor::SemanticRole::Window:
            return NSAccessibilityWindowRole;
        }
        // a role outside the enumeration announces nothing, which is the quiet default (§10.2)
        return NSAccessibilityUnknownRole;
    }

    // the action a push control carries, which the bridge invokes when assistive technology presses it (§10.1)
    constexpr std::string_view PRESS_ACTION = "activate";

    // one published semantic node, which an application kit element reads rather than copies (§10.2)
    struct Published
    {
        // the semantic node itself, which the bridge owns so no pointer can dangle (§10.2)
        albor::SemanticNode node;

        // the index the node took in the published tree, which is how its children are found (§10.2)
        std::size_t index = 0;

        // the element assistive technology holds, which the bridge owns (§10.2)
        AlborAccessibilityElement* element = nil;
    };

} // namespace

// the bridge an element asks for its children, which is how a hierarchy is answered (§10.2)
@protocol AlborAccessibilityOwner <NSObject>
// hand out the elements published as the children of one, which is what the protocol asks for (§10.2)
- (NSArray*)child_elements_of_index:(std::size_t)index;
@end

// the work that answers a child query, which the bridge installs as a block (§10.2)
typedef NSArray* (^AlborAccessibilityChildren)(std::size_t);

// the object that conforms to the protocol on the bridge's behalf, which objective c requires (§10.2)
@interface AlborAccessibilityBridge : NSObject <AlborAccessibilityOwner>
// the work that answers a child query, which the bridge installs once (§10.2)
@property(nonatomic, copy) AlborAccessibilityChildren children_provider;
@end

@implementation AlborAccessibilityBridge
// override: the answer is the installed work, which is the only place the hierarchy lives (§10.2)
- (NSArray*)child_elements_of_index:(std::size_t)index {
    // a bridge with no work has no children, which is the quiet default (§10.2)
    return self.children_provider == nil ? nil : self.children_provider(index);
}
@end

// one application kit element per semantic node, which is the unit assistive technology reads (§10.2)
@interface AlborAccessibilityElement : NSAccessibilityElement
// the bridge that published this element, which is where the hierarchy is answered from (§10.2)
@property(nonatomic, weak) id<AlborAccessibilityOwner> owner;
// the publication this element reads, which the protocol does not define a property for (§10.2)
@property(nonatomic, assign) const void* publication;
@end

@implementation AlborAccessibilityElement
// override: the role comes from the semantic node, which is the single source of truth (§10.1, §10.2)
- (NSAccessibilityRole)accessibilityRole {
    // the publication this element names is the one the bridge assigned (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // an element with no publication announces nothing, which is the quiet default (§10.2)
    return published == nullptr ? NSAccessibilityUnknownRole : native_role(published->node.role());
}

// override: the label is the accessible name, which is what a screen reader speaks (§10.1)
- (NSString*)accessibilityLabel {
    // the publication this element names is the one the bridge assigned (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // a node with no name has no label, which assistive technology reads as empty (§10.2)
    return published == nullptr ? nil : [NSString stringWithUTF8String:published->node.name().c_str()];
}

// override: the enabled flag is the disabled state read backwards, because the two disagree (§10.1)
- (BOOL)isAccessibilityEnabled {
    // the publication this element names is the one the bridge assigned (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // a node with no state is enabled, which is the quiet default (§10.2)
    return published == nullptr ? YES : !published->node.disabled();
}

// override: the focused flag is the state assistive technology reads (§10.1)
- (BOOL)isAccessibilityFocused {
    // the publication this element names is the one the bridge assigned (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // a node with no state is not focused, which is the quiet default (§10.2)
    return published == nullptr ? NO : published->node.focused();
}

// override: the value of a checkable node, which the application kit reads as a string (§10.1)
- (NSString*)accessibilityValue {
    // the publication this element names is the one the bridge assigned (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // only a checkable node carries a value, because nothing else has one to report (§10.1)
    if (published == nullptr || published->node.check() == albor::CheckState::NotCheckable)
        return nil;
    // the mixed state has no single string, so it announces its own name (§10.1)
    if (published->node.check() == albor::CheckState::Indeterminate)
        return @"mixed";
    // the two definite states announce the word assistive technology expects (§10.1)
    return published->node.checked() ? @"1" : @"0";
}

// override: press is the action assistive technology sends to a push control (§10.1)
- (BOOL)accessibilityPerformPress {
    // the publication this element names is the one the bridge assigned (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // a node with no action cannot be pressed, which the protocol reports as a refusal (§10.1)
    return published != nullptr && published->node.invoke(PRESS_ACTION);
}

// override: the children come from the published tree, which is where the hierarchy lives (§10.2)
- (NSArray*)accessibilityChildren {
    // the publication this element carries the index its children are found by (§10.2)
    const Published* published = static_cast<const Published*>(self.publication);
    // an element with no publication has no children, which is the quiet default (§10.2)
    if (published == nullptr || self.owner == nil)
        return nil;
    // the owner walks the tree, which is the only place the hierarchy is recorded (§10.2)
    return [self.owner child_elements_of_index:published->index];
}
@end

namespace albor::accessibility::platform
{
    namespace
    {
        // the macos bridge, which owns what it published and the window it publishes into (§10.2)
        class MacosAccessibilityAdapter final : public AccessibilityAdapter
        {
        public:
            // the conforming object the protocol is answered through, which objective c requires (§10.2)
            AlborAccessibilityBridge* m_owner = [[AlborAccessibilityBridge alloc] init];

            void attach_window(NativeWindowHandle window) override {
                // the window this bridge publishes into, which the caller owns and the bridge does not (§5.2)
                m_window = (__bridge NSWindow*)window.object();
                // the publication is dropped, because a new window starts with nothing published (§10.2)
                clear();
            }

            void publish(const SemanticTree& tree) override {
                // the previous publication is dropped, because assistive technology reads one tree at a time (§10.2)
                clear();
                // the tree is kept whole, because the parent links are what build the hierarchy (§10.2)
                m_tree = tree;
                // the set is reserved up front, because a publication points into itself and must not move (§10.2)
                m_published.reserve(m_tree.size());
                // every node becomes an element, in the order the tree holds them (§10.2)
                for (std::size_t index = 0; index < m_tree.size(); index++) {
                    // the node is copied into the bridge, which is what keeps the element's pointer valid (§10.2)
                    m_published.push_back(Published{.node = *m_tree.at(index), .index = index});
                    // the element reads the stored copy, so nothing the caller does can dangle it (§10.2)
                    m_published.back().element = [[AlborAccessibilityElement alloc] init];
                    // the element reaches its node through the publication, which is the one pointer it needs (§10.2)
                    m_published.back().element.publication = &m_published.back();
                    // the element is told which bridge published it, which is where children come from (§10.2)
                    m_published.back().element.owner = m_owner;
                }
                // the count is what a coverage test asserts on (§10.2)
                m_count = m_published.size();
            }

            void detach_window() override {
                // the window reference is dropped first, because the native window is going away (§5.2)
                m_window = nil;
                // the publication goes with it, because assistive technology must not read a dead tree (§10.2)
                clear();
            }

            [[nodiscard]] std::size_t published_node_count() const override { return m_count; }

        private:
            // drop every publication, which a rebuild and a detach both need (§10.2)
            void clear() {
                // the set is cleared, which releases each element and each node copy (§10.2)
                m_published.clear();
                // the tree goes with it, because a cleared bridge publishes nothing (§10.2)
                m_tree.clear();
                // the count follows the set, because a cleared bridge publishes nothing (§10.2)
                m_count = 0;
            }

            // the window the bridge publishes into, which the caller owns (§5.2, §10.2)
            NSWindow* m_window = nil;

            // the tree as published, which the parent links are read from (§10.2)
            SemanticTree m_tree;

            // what was published, which owns both the nodes and the elements (§10.2)
            std::vector<Published> m_published;

            // how many nodes the last publication held, which a coverage test reads (§10.2)
            std::size_t m_count = 0;
        };
    } // namespace

    bool available() {
        // the application kit accessibility protocol is present on every macos build this project supports (§10.2)
        return true;
    }

    std::unique_ptr<AccessibilityAdapter> build() {
        // the bridge is handed back by value, which the caller owns for the life of the application (§10.2)
        return std::make_unique<MacosAccessibilityAdapter>();
    }
} // namespace albor::accessibility::platform
