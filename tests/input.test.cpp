#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "focus/focus_manager.h"
#include "input/event_filter.h"
#include "input/input_dispatcher.h"
#include "ui/ui_node.h"

using namespace albor;

namespace
{
    // the node test double, which records the events it was offered and how many times (§8.3)
    class RecordingNode final : public UiNode
    {
    public:
        // how many events this node was offered (§8.3)
        std::size_t deliveries = 0;

        // whether this node claims the events it is offered (§8.3)
        bool claims = false;

        // whether this node reports itself as visible, because a hidden node is not a hit (§8.3)
        bool shown = true;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the node fills whatever it is offered, because the routing tests do not exercise layout (§8.2)
            out_size = constraints.constrain(Size{10.0f, 10.0f});
            m_measured_size = out_size;
        }

        [[nodiscard]] bool hit_test(Point logical_point) const override {
            // a hidden node is never a hit, which is what the focus traversal also relies on (§8.3)
            if (!shown)
                return false;
            // the node hits anywhere inside its bounds, so a test controls the point (§8.3)
            return logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height;
        }

        void dispatch_event(Event& event) override {
            // the delivery is counted before the claim, so a refusing node is still observable (§8.3)
            deliveries++;
            // a claiming node marks the event handled, which stops the walk (§8.3)
            if (claims)
                event.handled = true;
        }
    };
} // namespace

TEST(EventFilterChecks, PointerDownIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerDown};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, PointerUpIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerUp};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, PointerMoveIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerMove};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, WheelIsNotAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::Wheel};
    // act
    // assert
    EXPECT_FALSE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, KeyDownIsAKeyboardEvent) {
    // arrange
    Event event{.kind = EventKind::KeyDown};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_keyboard(event));
}

TEST(EventFilterChecks, KeyUpIsAKeyboardEvent) {
    // arrange
    Event event{.kind = EventKind::KeyUp};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_keyboard(event));
}

TEST(EventFilterChecks, PointerDownIsNotAKeyboardEvent) {
    // arrange
    Event event{.kind = EventKind::PointerDown};
    // act
    // assert
    EXPECT_FALSE(EventFilter::is_keyboard(event));
}

TEST(EventFilterChecks, WheelIsTheOnlyScrollEvent) {
    // arrange
    Event wheel{.kind = EventKind::Wheel};
    Event move{.kind = EventKind::PointerMove};
    Event key{.kind = EventKind::KeyDown};
    Event text{.kind = EventKind::TextInput};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_scroll(wheel));
    EXPECT_FALSE(EventFilter::is_scroll(move));
    EXPECT_FALSE(EventFilter::is_scroll(key));
    EXPECT_FALSE(EventFilter::is_scroll(text));
}

TEST(EventFilterChecks, TextInputIsTheOnlyTextEvent) {
    // arrange
    Event text{.kind = EventKind::TextInput};
    Event move{.kind = EventKind::PointerMove};
    Event key{.kind = EventKind::KeyUp};
    Event wheel{.kind = EventKind::Wheel};
    // act
    // assert
    EXPECT_TRUE(EventFilter::is_text(text));
    EXPECT_FALSE(EventFilter::is_text(move));
    EXPECT_FALSE(EventFilter::is_text(key));
    EXPECT_FALSE(EventFilter::is_text(wheel));
}

TEST(EventFilterChecks, PointerAndWheelEventsCarryAPosition) {
    // arrange
    Event down{.kind = EventKind::PointerDown};
    Event up{.kind = EventKind::PointerUp};
    Event move{.kind = EventKind::PointerMove};
    Event wheel{.kind = EventKind::Wheel};
    // act
    // assert
    EXPECT_TRUE(EventFilter::positioned(down));
    EXPECT_TRUE(EventFilter::positioned(up));
    EXPECT_TRUE(EventFilter::positioned(move));
    EXPECT_TRUE(EventFilter::positioned(wheel));
}

TEST(EventFilterChecks, KeyAndTextEventsCarryNoPosition) {
    // arrange
    Event key_down{.kind = EventKind::KeyDown};
    Event key_up{.kind = EventKind::KeyUp};
    Event text{.kind = EventKind::TextInput};
    // act
    // assert
    EXPECT_FALSE(EventFilter::positioned(key_down));
    EXPECT_FALSE(EventFilter::positioned(key_up));
    EXPECT_FALSE(EventFilter::positioned(text));
}

TEST(InputDispatcherChecks, RoutesToTheRootWhenItHasNoChildren) {
    // arrange
    RecordingNode root;
    root.claims = true;
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(InputDispatcherChecks, RoutesToTheDeepestChildFirst) {
    // arrange
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    // the child is offered before the root, which is what makes the deepest handler win (§8.3)
    EXPECT_EQ(child_ptr->deliveries, 1u);
    EXPECT_EQ(root.deliveries, 1u);
}

TEST(InputDispatcherChecks, StopsAtTheChildThatClaims) {
    // arrange
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    child->claims = true;
    root.add_child(std::move(child));
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    EXPECT_EQ(root.deliveries, 0u);
    EXPECT_TRUE(event.handled);
}

TEST(InputDispatcherChecks, OffersTheLastChildFirst) {
    // arrange
    RecordingNode root;
    auto first = std::make_unique<RecordingNode>();
    auto second = std::make_unique<RecordingNode>();
    RecordingNode* first_ptr = first.get();
    RecordingNode* second_ptr = second.get();
    root.add_child(std::move(first));
    root.add_child(std::move(second));
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    // the later sibling is on top, so it is offered first (§8.3)
    EXPECT_EQ(second_ptr->deliveries, 1u);
    EXPECT_EQ(first_ptr->deliveries, 1u);
}

TEST(InputDispatcherChecks, TheTopmostChildStopsTheWalk) {
    // arrange
    RecordingNode root;
    auto below = std::make_unique<RecordingNode>();
    auto top = std::make_unique<RecordingNode>();
    top->claims = true;
    RecordingNode* below_ptr = below.get();
    root.add_child(std::move(below));
    root.add_child(std::move(top));
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    // the topmost sibling swallowed the event, so the one below never saw it (§8.3)
    EXPECT_EQ(below_ptr->deliveries, 0u);
}

TEST(InputDispatcherChecks, DescendsThroughNestedLevels) {
    // arrange
    RecordingNode root;
    auto branch = std::make_unique<RecordingNode>();
    auto leaf = std::make_unique<RecordingNode>();
    leaf->claims = true;
    RecordingNode* branch_ptr = branch.get();
    branch->add_child(std::move(leaf));
    root.add_child(std::move(branch));
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    // the leaf claimed it, so no ancestor of it acts (§8.3)
    EXPECT_EQ(branch_ptr->deliveries, 0u);
    EXPECT_EQ(root.deliveries, 0u);
}

TEST(InputDispatcherChecks, AnAlreadyClaimedEventIsNotRouted) {
    // arrange
    RecordingNode root;
    Event event{.kind = EventKind::PointerDown};
    event.handled = true;
    // act
    InputDispatcher::dispatch(root, event);
    // assert
    EXPECT_EQ(root.deliveries, 0u);
}

TEST(InputDispatcherChecks, ReportsWhetherTheTreeIsHit) {
    // arrange
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    // act
    const bool inside = InputDispatcher::hits(root, Point{10.0f, 10.0f});
    const bool outside = InputDispatcher::hits(root, Point{100.0f, 100.0f});
    // assert
    EXPECT_TRUE(inside);
    EXPECT_FALSE(outside);
}

TEST(InputDispatcherChecks, AHiddenChildIsNotAHit) {
    // arrange
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    child->shown = false;
    child->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    root.add_child(std::move(child));
    // act
    const bool hit = InputDispatcher::hits(root, Point{10.0f, 10.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(InputDispatcherChecks, RoutesToTheFocusedNodeFirst) {
    // arrange
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, child_ptr);
    // assert
    // the focused node gets the key first, and the root only sees it if the child refused (§8.3)
    EXPECT_EQ(child_ptr->deliveries, 1u);
    EXPECT_EQ(root.deliveries, 1u);
}

TEST(InputDispatcherChecks, AFocusedClaimStopsTheBubble) {
    // arrange
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    child->claims = true;
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, child_ptr);
    // assert
    EXPECT_EQ(root.deliveries, 0u);
    EXPECT_TRUE(event.handled);
}

TEST(InputDispatcherChecks, BubblesFromTheFocusedNodeToTheRoot) {
    // arrange
    RecordingNode root;
    auto branch = std::make_unique<RecordingNode>();
    auto leaf = std::make_unique<RecordingNode>();
    RecordingNode* branch_ptr = branch.get();
    branch->add_child(std::move(leaf));
    root.add_child(std::move(branch));
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, leaf.get());
    // assert
    // each ancestor is offered in turn until one claims the event (§8.3)
    EXPECT_EQ(branch_ptr->deliveries, 1u);
    EXPECT_EQ(root.deliveries, 1u);
}

TEST(InputDispatcherChecks, AnAncestorCanClaimTheBubble) {
    // arrange
    RecordingNode root;
    root.claims = true;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, child_ptr);
    // assert
    EXPECT_TRUE(event.handled);
}

TEST(InputDispatcherChecks, TheRootClaimStopsAtTheRoot) {
    // arrange
    RecordingNode root;
    root.claims = true;
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, &root);
    // assert
    // the root has no parent, so the bubble ends there without walking off the tree (§8.3)
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(InputDispatcherChecks, ANullFocusFallsBackToTheWholeTree) {
    // arrange
    RecordingNode root;
    root.claims = true;
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, nullptr);
    // assert
    // a key press must reach something, so the tree walk stands in for the missing focus (§8.3)
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(InputDispatcherChecks, AFocusFromAnotherTreeFallsBack) {
    // arrange
    RecordingNode root;
    RecordingNode stranger;
    root.claims = true;
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, &stranger);
    // assert
    // a stale pointer that no longer belongs to the tree is not routed to (§8.3)
    EXPECT_EQ(stranger.deliveries, 0u);
    EXPECT_EQ(root.deliveries, 1u);
}

TEST(InputDispatcherChecks, AnAlreadyClaimedEventIsNotRoutedToFocus) {
    // arrange
    RecordingNode root;
    Event event{.kind = EventKind::KeyDown};
    event.handled = true;
    // act
    InputDispatcher::dispatch_to_focused(root, event, &root);
    // assert
    EXPECT_EQ(root.deliveries, 0u);
}

TEST(FocusManagerChecks, StartsWithNothingFocused) {
    // arrange
    FocusManager manager;
    // act
    // assert
    EXPECT_EQ(manager.focused(), nullptr);
    EXPECT_EQ(manager.change_count(), 0u);
}

TEST(FocusManagerChecks, RequestFocusStoresTheNode) {
    // arrange
    FocusManager manager;
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    manager.request_focus(&node);
    // assert
    EXPECT_EQ(manager.focused(), &node);
    EXPECT_TRUE(manager.has_focus(&node));
    EXPECT_EQ(manager.change_count(), 1u);
}

TEST(FocusManagerChecks, RequestFocusRefusesANullNode) {
    // arrange
    FocusManager manager;
    // act
    manager.request_focus(nullptr);
    // assert
    EXPECT_EQ(manager.focused(), nullptr);
    EXPECT_EQ(manager.change_count(), 0u);
}

TEST(FocusManagerChecks, RequestFocusRefusesAnUnarrangedNode) {
    // arrange
    FocusManager manager;
    RecordingNode node;
    // act
    manager.request_focus(&node);
    // assert
    // a node with no area has nothing to point at (§8.3)
    EXPECT_EQ(manager.focused(), nullptr);
}

TEST(FocusManagerChecks, RequestFocusRefusesAHiddenNode) {
    // arrange
    FocusManager manager;
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    node.set_visible(false);
    // act
    manager.request_focus(&node);
    // assert
    EXPECT_EQ(manager.focused(), nullptr);
}

TEST(FocusManagerChecks, RefocusingTheSameNodeChangesNothing) {
    // arrange
    FocusManager manager;
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node);
    // act
    manager.request_focus(&node);
    // assert
    EXPECT_EQ(manager.change_count(), 1u);
}

TEST(FocusManagerChecks, MovingFocusInvalidatesBothNodes) {
    // arrange
    FocusManager manager;
    RecordingNode first;
    RecordingNode second;
    first.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    second.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&first);
    first.clear_dirty();
    second.clear_dirty();
    // act
    manager.request_focus(&second);
    // assert
    // the focus ring moves, so both nodes must redraw (§8.3)
    EXPECT_TRUE(first.render_dirty());
    EXPECT_TRUE(second.render_dirty());
}

TEST(FocusManagerChecks, ClearFocusDropsTheNode) {
    // arrange
    FocusManager manager;
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node);
    // act
    manager.clear_focus();
    // assert
    EXPECT_EQ(manager.focused(), nullptr);
    EXPECT_EQ(manager.change_count(), 2u);
}

TEST(FocusManagerChecks, ClearFocusOnAnIdleManagerChangesNothing) {
    // arrange
    FocusManager manager;
    // act
    manager.clear_focus();
    // assert
    EXPECT_EQ(manager.change_count(), 0u);
}

TEST(FocusManagerChecks, HasFocusIsFalseForANullNode) {
    // arrange
    FocusManager manager;
    // act
    const bool has_it = manager.has_focus(nullptr);
    // assert
    EXPECT_FALSE(has_it);
}

TEST(FocusManagerChecks, HasFocusIsFalseForAnotherNode) {
    // arrange
    FocusManager manager;
    RecordingNode focused;
    RecordingNode other;
    focused.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    other.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&focused);
    // act
    const bool has_it = manager.has_focus(&other);
    // assert
    EXPECT_FALSE(has_it);
}

TEST(FocusManagerChecks, AFocusChangeReachesTheHandler) {
    // arrange
    FocusManager manager;
    std::size_t notified = 0;
    manager.set_focus_changed_handler([&notified] { notified++; });
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    manager.request_focus(&node);
    // assert
    EXPECT_EQ(notified, 1u);
}

TEST(FocusManagerChecks, FocusableNeedsVisibilityAndArea) {
    // arrange
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    const bool visible_and_sized = FocusManager::focusable(node);
    node.set_visible(false);
    const bool hidden = FocusManager::focusable(node);
    // assert
    EXPECT_TRUE(visible_and_sized);
    EXPECT_FALSE(hidden);
}

TEST(FocusManagerChecks, FocusNextWalksTheTreeDepthFirst) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto first = std::make_unique<RecordingNode>();
    auto second = std::make_unique<RecordingNode>();
    RecordingNode* first_ptr = first.get();
    RecordingNode* second_ptr = second.get();
    root.add_child(std::move(first));
    root.add_child(std::move(second));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    first_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    second_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    manager.focus_next(root);
    // assert
    // the root is first in depth first order, so tab lands on it (§8.3)
    EXPECT_EQ(manager.focused(), &root);
}

TEST(FocusManagerChecks, FocusNextAdvancesToTheNextNode) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&root);
    // act
    manager.focus_next(root);
    // assert
    EXPECT_EQ(manager.focused(), child_ptr);
}

TEST(FocusManagerChecks, FocusNextWrapsAroundTheEnd) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(child_ptr);
    // act
    manager.focus_next(root);
    // assert
    // the ring wraps, so tab past the last node returns to the first (§8.3)
    EXPECT_EQ(manager.focused(), &root);
}

TEST(FocusManagerChecks, FocusPreviousStartsAtTheLastNode) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    manager.focus_previous(root);
    // assert
    // going backward from nothing lands on the last node of the ring (§8.3)
    EXPECT_EQ(manager.focused(), child_ptr);
}

TEST(FocusManagerChecks, FocusPreviousMovesBackwards) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(child_ptr);
    // act
    manager.focus_previous(root);
    // assert
    EXPECT_EQ(manager.focused(), &root);
}

TEST(FocusManagerChecks, FocusPreviousWrapsAroundTheStart) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&root);
    // act
    manager.focus_previous(root);
    // assert
    // going backward past the first node returns to the last (§8.3)
    EXPECT_EQ(manager.focused(), child_ptr);
}

TEST(FocusManagerChecks, TraversalSkipsUnfocusableNodes) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto hidden = std::make_unique<RecordingNode>();
    auto visible = std::make_unique<RecordingNode>();
    RecordingNode* visible_ptr = visible.get();
    hidden->set_visible(false);
    root.add_child(std::move(hidden));
    root.add_child(std::move(visible));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    visible_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&root);
    // act
    manager.focus_next(root);
    // assert
    // the hidden node never enters the ring, so tab lands on the visible one (§8.3)
    EXPECT_EQ(manager.focused(), visible_ptr);
}

TEST(FocusManagerChecks, TraversalOnATreeWithNothingFocusableDoesNothing) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    // act
    manager.focus_next(root);
    // assert
    // an empty ring has no node to land on, so the focus stays where it was (§8.3)
    EXPECT_EQ(manager.focused(), nullptr);
    EXPECT_EQ(manager.change_count(), 0u);
}

TEST(FocusManagerChecks, BackwardTraversalOnAnEmptyRingDoesNothing) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    // act
    manager.focus_previous(root);
    // assert
    EXPECT_EQ(manager.focused(), nullptr);
}
