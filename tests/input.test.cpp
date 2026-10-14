#include <memory>

#include <gtest/gtest.h>

#include "commands/command_registry.h"
#include "commands/undo_stack.h"
#include "focus/focus_manager.h"
#include "input/event_filter.h"
#include "input/input_controller.h"
#include "input/input_dispatcher.h"
#include "ui/focus_target.h"
#include "ui/ui_event.h"
#include "ui/ui_node.h"

using namespace albor;

namespace
{
    // the node test double, which records the events it was offered and how many times (§8.3)
    class RecordingNode final : public UiNode
    {
    public:
        // how many routed events this node was offered (§8.3)
        std::size_t deliveries = 0;

        // how many pointer enter transitions this node was told (§8.3)
        std::size_t enters = 0;

        // how many pointer leave transitions this node was told (§8.3)
        std::size_t leaves = 0;

        // whether this node claims the events it is offered (§8.3)
        bool claims = false;

        // whether this node reports itself as visible, because a hidden node is not a hit (§8.3)
        bool shown = true;

        // whether this node reports itself as disabled, because a disabled node takes no focus (§8.3)
        bool blocked = false;

        [[nodiscard]] bool disabled() const override {
            // the flag is the reported state, which the focus ring reads (§8.3)
            return blocked;
        }

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
            // a synthesized transition is counted on its own, because it is not a routed input event (§8.3)
            if (event.kind == EventKind::PointerEnter) {
                enters++;
                return;
            }
            if (event.kind == EventKind::PointerLeave) {
                leaves++;
                return;
            }
            // the delivery is counted before the claim, so a refusing node is still observable (§8.3)
            deliveries++;
            // a claiming node marks the event handled, which stops the walk (§8.3)
            if (claims)
                event.handled = true;
        }
    };
    // the focus tracking test double, which records what the manager told it (§8.3)
    class FocusRecordingNode final : public UiNode, public FocusTarget
    {
    public:
        // whether the node believes it holds the focus (§8.3)
        bool focused = false;

        // how many times the manager told this node about the focus (§8.3)
        std::size_t told = 0;

        void set_focused(bool value) override {
            // the state is stored, because that is what the assertion reads (§8.3)
            this->focused = value;
            // the count rises on every report, so a test can see it happened at all (§8.3)
            told++;
        }

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the node fills whatever it is offered, because the focus tests do not exercise layout (§8.2)
            out_size = constraints.constrain(Size{10.0f, 10.0f});
            m_measured_size = out_size;
        }

        [[nodiscard]] bool hit_test(Point logical_point) const override {
            // the node hits anywhere inside its bounds, so a test controls the point (§8.3)
            return logical_point.x >= 0.0f && logical_point.y >= 0.0f;
        }
    };

} // namespace

TEST(EventFilterChecks, PointerDownIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerDown};
    // act assert
    EXPECT_TRUE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, PointerUpIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerUp};
    // act assert
    EXPECT_TRUE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, PointerMoveIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerMove};
    // act assert
    EXPECT_TRUE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, WheelIsNotAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::Wheel};
    // act assert
    EXPECT_FALSE(EventFilter::is_pointer(event));
}

TEST(EventFilterChecks, KeyDownIsAKeyboardEvent) {
    // arrange
    Event event{.kind = EventKind::KeyDown};
    // act assert
    EXPECT_TRUE(EventFilter::is_keyboard(event));
}

TEST(EventFilterChecks, KeyUpIsAKeyboardEvent) {
    // arrange
    Event event{.kind = EventKind::KeyUp};
    // act assert
    EXPECT_TRUE(EventFilter::is_keyboard(event));
}

TEST(EventFilterChecks, PointerDownIsNotAKeyboardEvent) {
    // arrange
    Event event{.kind = EventKind::PointerDown};
    // act assert
    EXPECT_FALSE(EventFilter::is_keyboard(event));
}

TEST(EventFilterChecks, WheelIsTheOnlyScrollEvent) {
    // arrange
    Event wheel{.kind = EventKind::Wheel};
    Event move{.kind = EventKind::PointerMove};
    Event key{.kind = EventKind::KeyDown};
    Event text{.kind = EventKind::TextInput};
    // act assert
    EXPECT_TRUE(EventFilter::is_scroll(wheel));
    EXPECT_FALSE(EventFilter::is_scroll(move));
    EXPECT_FALSE(EventFilter::is_scroll(key));
    EXPECT_FALSE(EventFilter::is_scroll(text));
}

TEST(EventFilterChecks, CommittedAndPreEditTextAreTheTextEvents) {
    // arrange
    Event text{.kind = EventKind::TextInput};
    Event composing{.kind = EventKind::Composing};
    Event move{.kind = EventKind::PointerMove};
    Event key{.kind = EventKind::KeyUp};
    Event wheel{.kind = EventKind::Wheel};
    // act assert
    EXPECT_TRUE(EventFilter::is_text(text));
    EXPECT_TRUE(EventFilter::is_text(composing));
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
    // act assert
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
    Event composing{.kind = EventKind::Composing};
    // act assert
    EXPECT_FALSE(EventFilter::positioned(key_down));
    EXPECT_FALSE(EventFilter::positioned(key_up));
    EXPECT_FALSE(EventFilter::positioned(text));
    EXPECT_FALSE(EventFilter::positioned(composing));
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
    // assert the child is offered before the root, which is what makes the deepest handler win (§8.3)
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
    // assert the later sibling is on top, so it is offered first (§8.3)
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
    // assert the topmost sibling swallowed the event, so the one below never saw it (§8.3)
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
    // assert the leaf claimed it, so no ancestor of it acts (§8.3)
    EXPECT_EQ(branch_ptr->deliveries, 0u);
    EXPECT_EQ(root.deliveries, 0u);
}

TEST(InputDispatcherChecks, ALeafIsOfferedOnceThroughContainersThatDoNotWalk) {
    // arrange
    UiNode root;
    auto middle = std::make_unique<UiNode>();
    auto leaf = std::make_unique<RecordingNode>();
    RecordingNode* leaf_ptr = leaf.get();
    middle->add_child(std::move(leaf));
    root.add_child(std::move(middle));
    Event event{.kind = EventKind::PointerDown};
    // act
    InputDispatcher::dispatch(root, event);
    // assert the dispatcher walks the tree alone, so the leaf sees the event exactly once (§8.3)
    EXPECT_EQ(leaf_ptr->deliveries, 1u);
}

TEST(InputDispatcherChecks, ABubbleDoesNotReachTheSiblingOfTheFocusedNode) {
    // arrange
    UiNode root;
    auto first = std::make_unique<RecordingNode>();
    auto second = std::make_unique<RecordingNode>();
    RecordingNode* first_ptr = first.get();
    RecordingNode* second_ptr = second.get();
    root.add_child(std::move(first));
    root.add_child(std::move(second));
    Event event{.kind = EventKind::KeyDown};
    // act
    InputDispatcher::dispatch_to_focused(root, event, first_ptr);
    // assert the bubble climbs the ancestors only, so the sibling never sees the key (§8.3)
    EXPECT_EQ(first_ptr->deliveries, 1u);
    EXPECT_EQ(second_ptr->deliveries, 0u);
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
    // assert the focused node gets the key first, and the root only sees it if the child refused (§8.3)
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
    // assert each ancestor is offered in turn until one claims the event (§8.3)
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
    // assert the root has no parent, so the bubble ends there without walking off the tree (§8.3)
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
    // assert a key press must reach something, so the tree walk stands in for the missing focus (§8.3)
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
    // assert a stale pointer that no longer belongs to the tree is not routed to (§8.3)
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

TEST(FocusManagerChecks, AFocusTargetIsToldWhenTheFocusMovesOntoIt) {
    // arrange a node that tracks the focus, which is what a caret is (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    manager.request_focus(&node);
    // assert the node knows it holds the focus, because a caret follows it (§8.3)
    EXPECT_TRUE(node.focused);
    EXPECT_EQ(node.told, 1u);
}

TEST(FocusManagerChecks, AFocusTargetIsToldWhenTheFocusMovesOffIt) {
    // arrange two tracked nodes, one holding the focus (§8.3)
    FocusManager manager;
    FocusRecordingNode first;
    FocusRecordingNode second;
    first.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    second.arrange(Rect::from_origin_size(Point{20.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&first);
    // act the focus moves to the other node
    manager.request_focus(&second);
    // assert the first was told it lost the focus, which is what ends a blink (§8.3)
    EXPECT_FALSE(first.focused);
    EXPECT_TRUE(second.focused);
}

TEST(FocusManagerChecks, LosingTheFocusTellsTheTargetToo) {
    // arrange a tracked node holding the focus (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node);
    // act the window loses its focus
    manager.clear_focus();
    // assert the node was told, because a caret must not keep blinking (§8.3)
    EXPECT_FALSE(node.focused);
    EXPECT_EQ(manager.focused(), nullptr);
}

TEST(FocusManagerChecks, ANodeThatDoesNotTrackTheFocusIsUntouched) {
    // arrange an ordinary node, which the manager focuses without knowing anything else (§8.3)
    FocusManager manager;
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act
    manager.request_focus(&node);
    // assert the focus landed, which is all an ordinary node is asked (§8.3)
    EXPECT_EQ(manager.focused(), &node);
}

TEST(FocusManagerChecks, StartsWithNothingFocused) {
    // arrange
    FocusManager manager;
    // act assert
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
    // assert a node with no area has nothing to point at (§8.3)
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
    // assert the focus ring moves, so both nodes must redraw (§8.3)
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
    // assert the root is first in depth first order, so tab lands on it (§8.3)
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
    // assert the ring wraps, so tab past the last node returns to the first (§8.3)
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
    // assert going backward from nothing lands on the last node of the ring (§8.3)
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
    // assert going backward past the first node returns to the last (§8.3)
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
    // assert the hidden node never enters the ring, so tab lands on the visible one (§8.3)
    EXPECT_EQ(manager.focused(), visible_ptr);
}

TEST(FocusManagerChecks, TraversalSkipsDisabledNodes) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    auto blocked = std::make_unique<RecordingNode>();
    auto usable = std::make_unique<RecordingNode>();
    RecordingNode* usable_ptr = usable.get();
    blocked->blocked = true;
    root.add_child(std::move(blocked));
    root.add_child(std::move(usable));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    usable_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&root);
    // act
    manager.focus_next(root);
    // assert the disabled node never enters the ring, so tab lands on the usable one (§8.3)
    EXPECT_EQ(manager.focused(), usable_ptr);
}

TEST(FocusManagerChecks, FocusableRefusesADisabledNode) {
    // arrange
    RecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    node.blocked = true;
    // act assert
    EXPECT_FALSE(FocusManager::focusable(node));
}

TEST(FocusManagerChecks, TraversalOnATreeWithNothingFocusableDoesNothing) {
    // arrange
    FocusManager manager;
    RecordingNode root;
    // act
    manager.focus_next(root);
    // assert an empty ring has no node to land on, so the focus stays where it was (§8.3)
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

TEST(InputControllerChecks, StartsWithNoTree) {
    // arrange
    InputController controller;
    // act assert
    EXPECT_EQ(controller.root(), nullptr);
    EXPECT_FALSE(controller.capturing());
    // a delivery with no tree is dropped, which cannot dereference anything (§8.3)
    Event event{.kind = EventKind::KeyDown};
    controller.deliver(event);
    EXPECT_FALSE(event.handled);
}

TEST(InputControllerChecks, AttachStoresTheTree) {
    // arrange
    InputController controller;
    RecordingNode root;
    // act
    controller.attach(root);
    // assert
    EXPECT_EQ(controller.root(), &root);
}

TEST(InputControllerChecks, AClaimedPressMovesTheCaret) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    controller.deliver(press);
    // assert
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(controller.focus().focused(), &root);
    EXPECT_TRUE(controller.capturing());
}

TEST(InputControllerChecks, AnUnclaimedPressFocusesNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    controller.deliver(press);
    // assert the press was answered by nobody, so the caret and the capture stay idle (§8.3)
    EXPECT_FALSE(press.handled);
    EXPECT_EQ(controller.focus().focused(), nullptr);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, APressInEmptySpaceFocusesNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{100.0f, 100.0f}};
    // act
    controller.deliver(press);
    // assert the press missed every node, so nothing took the caret (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(controller.focus().focused(), nullptr);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, APressOnADisabledNodeFocusesNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.blocked = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    controller.deliver(press);
    // assert the disabled node could not take the caret, so the press has no target (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(controller.focus().focused(), nullptr);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, ADragReachesTheCapturedNodeOutsideItsBounds) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{400.0f, 400.0f}};
    // act
    controller.deliver(move);
    // assert the captured node keeps the drag, so the node outside its bounds is skipped (§8.3)
    EXPECT_EQ(root.deliveries, 2u);
    EXPECT_TRUE(move.handled);
    EXPECT_TRUE(controller.capturing());
}

TEST(InputControllerChecks, AReleaseEndsTheCapture) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event release{.kind = EventKind::PointerUp, .position = Point{400.0f, 400.0f}};
    // act
    controller.deliver(release);
    // assert the release closed the drag, so the next press starts a fresh one (§8.3)
    EXPECT_EQ(root.deliveries, 2u);
    EXPECT_TRUE(release.handled);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, AnUngrabbedMoveRoutesByPosition) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event move{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    // act
    controller.deliver(move);
    // assert the unclaimed move is offered to the tree, which is the hover path (§8.3)
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, EscapeCancelsACapture) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act
    controller.deliver(escape);
    // assert the escape swallowed the drag before the tree saw it (§8.3)
    EXPECT_TRUE(escape.handled);
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, TabMovesTheCaretForward) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    controller.attach(root);
    Event tab{.kind = EventKind::KeyDown, .key = Key::Tab};
    // act
    controller.deliver(tab);
    // assert the tab moved the caret forward and was claimed, so the tree saw nothing (§8.3)
    EXPECT_TRUE(tab.handled);
    EXPECT_EQ(controller.focus().focused(), &root);
    EXPECT_EQ(child_ptr->deliveries, 0u);
}

TEST(InputControllerChecks, ShiftTabMovesTheCaretBackward) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    controller.attach(root);
    controller.focus().request_focus(&root);
    Event shift_tab{.kind = EventKind::KeyDown, .key = Key::Tab};
    shift_tab.modifiers.shift = true;
    // act
    controller.deliver(shift_tab);
    // assert the shift tab moved the caret backward, so the last node of the ring lands (§8.3)
    EXPECT_TRUE(shift_tab.handled);
    EXPECT_EQ(controller.focus().focused(), child_ptr);
}

TEST(InputControllerChecks, AModifiedTabIsRoutedToTheTree) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event tab{.kind = EventKind::KeyDown, .key = Key::Tab};
    tab.modifiers.control = true;
    // act
    controller.deliver(tab);
    // assert the control tab is not a traversal, so the tree owns it (§8.3)
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_FALSE(tab.handled);
}

TEST(InputControllerChecks, AKeyDownReachesTheFocusedNode) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event key{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    controller.deliver(key);
    // assert the caret owns the key, so the focused node handled it (§8.3)
    EXPECT_TRUE(key.handled);
    EXPECT_EQ(root.deliveries, 2u);
}

TEST(InputControllerChecks, AKeyUpIsIgnored) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event key_up{.kind = EventKind::KeyUp, .key = Key::Return};
    // act
    controller.deliver(key_up);
    // assert the key up carries no traversal and no activation (§8.3)
    EXPECT_FALSE(key_up.handled);
    EXPECT_EQ(root.deliveries, 0u);
}

TEST(InputControllerChecks, ATextEventReachesTheFocusedWalk) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    controller.attach(root);
    // act
    Event event{.kind = EventKind::TextInput, .text = "a"};
    controller.deliver(event);
    // assert the text channel routes through the focused walk, which the dispatcher serves (§8.3)
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(InputControllerChecks, AComposingEventReachesTheFocusedWalk) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    controller.attach(root);
    // act
    Event event{.kind = EventKind::Composing, .text = "\xE3\x81\x82"};
    controller.deliver(event);
    // assert a pre-edit walks the same channel as a commit, because the field the focus names answers both (§8.3)
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(InputControllerChecks, AComposingEventIsClaimedSeparatelyFromACommit) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    controller.attach(root);
    // act
    Event marked{.kind = EventKind::Composing, .text = "\xE3\x81\x82"};
    controller.deliver(marked);
    Event committed{.kind = EventKind::TextInput, .text = "\xE3\x81\x82"};
    controller.deliver(committed);
    // assert the pre-edit and the commit arrive as two events, which is what makes the two states distinct (§8.3)
    EXPECT_EQ(root.deliveries, 2u);
    EXPECT_TRUE(committed.handled);
}

TEST(InputControllerChecks, ASecondPressWhileCapturingIsDropped) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event first{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(first);
    // act
    Event second{.kind = EventKind::PointerDown, .position = Point{20.0f, 20.0f}};
    controller.deliver(second);
    // assert the capture was left alone, so the drag keeps flowing (§8.3)
    EXPECT_FALSE(second.handled);
    EXPECT_EQ(root.deliveries, 1u);
    EXPECT_TRUE(controller.capturing());
}

TEST(InputControllerChecks, APressFocusesTheDeepestChildUnderThePoint) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->claims = true;
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    // assert the deepest focusable node under the press took the caret (§8.3)
    EXPECT_EQ(controller.focus().focused(), child_ptr);
    EXPECT_TRUE(controller.capturing());
}

TEST(InputControllerChecks, TheFocusStateIsReadableThroughAConstView) {
    // arrange
    InputController controller;
    RecordingNode root;
    controller.attach(root);
    // act
    const InputController& view = controller;
    // assert
    EXPECT_EQ(view.focus().focused(), nullptr);
}

TEST(InputControllerChecks, AHiddenChildHandsTheFocusToTheRoot) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.claims = true;
    auto child = std::make_unique<RecordingNode>();
    child->shown = false;
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    // assert a child that misses the point hides nothing inside itself (§8.3)
    EXPECT_EQ(controller.focus().focused(), &root);
    EXPECT_TRUE(controller.capturing());
}

TEST(HoverChecks, AFirstMoveOverANodeTellsItEntered) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event move{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    // act
    controller.deliver(move);
    // assert the transition is synthesized at the crossing, not awaited from the platform (§8.3)
    EXPECT_EQ(root.enters, 1u);
    EXPECT_EQ(root.leaves, 0u);
}

TEST(HoverChecks, ASecondMoveInsideTheSameNodeTellsNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event first{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    controller.deliver(first);
    // act a move inside the same node is not a transition, so only the routed move is delivered (§8.3)
    Event second{.kind = EventKind::PointerMove, .position = Point{20.0f, 20.0f}};
    controller.deliver(second);
    // assert
    EXPECT_EQ(root.enters, 1u);
    EXPECT_EQ(root.leaves, 0u);
    EXPECT_EQ(root.deliveries, 2u);
}

TEST(HoverChecks, AMoveOffANodeTellsItLeft) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event first{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    controller.deliver(first);
    // act the pointer leaves the bounds entirely (§8.3)
    Event outside{.kind = EventKind::PointerMove, .position = Point{400.0f, 400.0f}};
    controller.deliver(outside);
    // assert the exit arrives exactly at the crossing (§8.3)
    EXPECT_EQ(root.enters, 1u);
    EXPECT_EQ(root.leaves, 1u);
}

TEST(HoverChecks, CrossingBetweenTwoSiblingsTellsLeaveThenEnter) {
    // arrange two siblings side by side, so the pointer crosses one boundary (§8.3)
    InputController controller;
    RecordingNode root;
    auto first = std::make_unique<RecordingNode>();
    auto second = std::make_unique<RecordingNode>();
    RecordingNode* first_ptr = first.get();
    RecordingNode* second_ptr = second.get();
    first->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    second->arrange(Rect::from_origin_size(Point{50.0f, 0.0f}, Size{50.0f, 50.0f}));
    // the root holds both siblings, so it is arranged to span them (§8.2)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 50.0f}));
    // the last added child is the topmost, so the second is hit first (§8.3)
    root.add_child(std::move(second));
    root.add_child(std::move(first));
    controller.attach(root);
    Event over_first{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    controller.deliver(over_first);
    // act the pointer moves onto the sibling (§8.3)
    Event over_second{.kind = EventKind::PointerMove, .position = Point{60.0f, 10.0f}};
    controller.deliver(over_second);
    // assert the node left was told, and the root stayed hovered because the pointer never left it (§8.3)
    EXPECT_EQ(first_ptr->enters, 1u);
    EXPECT_EQ(first_ptr->leaves, 1u);
    EXPECT_EQ(second_ptr->enters, 1u);
    EXPECT_EQ(second_ptr->leaves, 0u);
    EXPECT_EQ(root.enters, 1u);
    EXPECT_EQ(root.leaves, 0u);
}

TEST(HoverChecks, AChildEnterIsToldAfterItsParentLeftNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    // the root is arranged first, because its arrange pushes the same bounds into every child (§8.2)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // the child is arranged after, so it holds the smaller box the test needs (§8.2)
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    // act the pointer lands on the child, which is inside the root too (§8.3)
    Event landed{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(landed);
    // assert both are told, because both contain the point (§8.3)
    EXPECT_EQ(child_ptr->enters, 1u);
    EXPECT_EQ(root.enters, 1u);
}

TEST(HoverChecks, AChildLeavingKeepsTheRootHovered) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    // the root is arranged first, because its arrange pushes the same bounds into every child (§8.2)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // the child is arranged after, so it holds the smaller box the test needs (§8.2)
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(inside);
    // act the pointer moves onto the root but off the child (§8.3)
    Event first{.kind = EventKind::PointerMove, .position = Point{80.0f, 80.0f}};
    controller.deliver(first);
    // assert the inner node left while the outer one did not (§8.3)
    EXPECT_EQ(child_ptr->leaves, 1u);
    EXPECT_EQ(root.leaves, 0u);
}

TEST(HoverChecks, APressLearnsWhatItLandedOn) {
    // arrange a press with no preceding move, which is what a real click is (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    // assert the press synthesized the enter it needed, so a hover visual does not lag the click (§8.3)
    EXPECT_EQ(root.enters, 1u);
}

TEST(HoverChecks, TheHoveredNodeIsTheInnermostUnderThePointer) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    // the root is arranged first, because its arrange pushes the same bounds into every child (§8.2)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // the child is arranged after, so it holds the smaller box the test needs (§8.2)
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    // act
    Event landed{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(landed);
    // assert the child covers the root, so it owns the point (§8.3)
    EXPECT_EQ(controller.hovered(), child_ptr);
}

TEST(HoverChecks, NothingIsHoveredOverEmptySpace) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    // act
    Event first{.kind = EventKind::PointerMove, .position = Point{400.0f, 400.0f}};
    controller.deliver(first);
    // assert
    EXPECT_EQ(controller.hovered(), nullptr);
    EXPECT_EQ(root.enters, 0u);
}

TEST(HoverChecks, AHiddenNodeIsNotHovered) {
    // arrange a node that draws nothing is not under the pointer (§8.3)
    InputController controller;
    RecordingNode root;
    RecordingNode child;
    auto hidden = std::make_unique<RecordingNode>();
    RecordingNode* hidden_ptr = hidden.get();
    hidden->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    // the double refuses its own hit, which is how a hidden node behaves (§8.3)
    hidden->shown = false;
    root.add_child(std::move(hidden));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // the hidden child is arranged after, so it holds the smaller box under the point (§8.2)
    hidden_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    // act the pointer lands where the hidden child is (§8.3)
    Event first{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(first);
    // assert the hidden node is skipped and the root still contains the point (§8.3)
    EXPECT_EQ(hidden_ptr->enters, 0u);
    EXPECT_EQ(root.enters, 1u);
}

TEST(HoverChecks, AKeyDownSynthesizesNoTransition) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    // act a key carries no position, so there is no pointer to place (§8.3)
    Event key{.kind = EventKind::KeyDown, .key = Key::Return};
    controller.deliver(key);
    // assert
    EXPECT_EQ(root.enters, 0u);
    EXPECT_EQ(root.leaves, 0u);
}

TEST(HoverChecks, ARefreshReplaysTheExitOfContentThatIsGone) {
    // arrange a hovered child that the application is about to remove (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(inside);
    ASSERT_EQ(child_ptr->enters, 1u);
    // act the subtree is hidden under the pointer, and the controller is told the tree moved (§8.3)
    child_ptr->shown = false;
    controller.refresh_hover();
    // assert the exit was replayed, so no widget keeps a hover pointing at content that is gone (§8.3)
    EXPECT_EQ(child_ptr->leaves, 1u);
}

TEST(HoverChecks, ARefreshLeavesNothingHoveredOverAHiddenSubtree) {
    // arrange a whole subtree that the application hides under the pointer (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(inside);
    ASSERT_EQ(controller.hovered(), child_ptr);
    // act the subtree is hidden and the root no longer contains the point either (§8.3)
    child_ptr->shown = false;
    root.shown = false;
    controller.refresh_hover();
    // assert the chain is empty rather than pointing at a hidden node (§8.3)
    EXPECT_EQ(controller.hovered(), nullptr);
}

TEST(HoverChecks, ARefreshOnACleanControllerDoesNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    // act a controller that never saw a pointer has nothing to replay (§8.3)
    controller.refresh_hover();
    // assert
    EXPECT_EQ(controller.hovered(), nullptr);
    EXPECT_EQ(root.enters, 0u);
    EXPECT_EQ(root.leaves, 0u);
}

TEST(HoverChecks, RemovingAHoveredSubtreeReplaysItsExitBeforeItDies) {
    // arrange a hovered child the application is about to remove (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(inside);
    ASSERT_EQ(child_ptr->enters, 1u);
    // act the child is removed and destroyed, with no refresh call from the application (§8.3)
    const std::unique_ptr<UiNode> removed = root.remove_child(child_ptr);
    ASSERT_NE(removed, nullptr);
    ASSERT_EQ(child_ptr->leaves, 1u);
    // assert the chain no longer points at the destroyed node, so no later move can reach it (§8.3)
    EXPECT_EQ(controller.hovered(), &root);
}

TEST(HoverChecks, AMoveAfterTheRemovalReachesNothingGone) {
    // arrange the same removal, and then a move that used to walk a destroyed node (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(inside);
    const std::unique_ptr<UiNode> removed = root.remove_child(child_ptr);
    ASSERT_NE(removed, nullptr);
    // act the pointer moves across the area the child used to occupy (§8.3)
    Event afterwards{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(afterwards);
    // assert the destroyed node was not told again, because it is no longer in the chain (§8.3)
    EXPECT_EQ(child_ptr->enters, 1u);
    EXPECT_EQ(child_ptr->leaves, 1u);
}

TEST(HoverChecks, HidingAHoveredNodeReplaysItsExit) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{15.0f, 15.0f}};
    controller.deliver(inside);
    ASSERT_EQ(child_ptr->enters, 1u);
    // act the node hides itself under the pointer, which needs no application call (§8.3)
    child_ptr->set_visible(false);
    // assert the exit was replayed, so no widget keeps a hover on a node that draws nothing (§8.3)
    EXPECT_EQ(child_ptr->leaves, 1u);
    EXPECT_EQ(controller.hovered(), &root);
}

TEST(HoverChecks, RemovingANodeThatWasNeverHoveredTellsNothing) {
    // arrange
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{20.0f, 20.0f}));
    root.add_child(std::move(child));
    controller.attach(root);
    // act the pointer was never over it, so there is no chain entry to exit (§8.3)
    const std::unique_ptr<UiNode> removed = root.remove_child(child_ptr);
    // assert
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(child_ptr->leaves, 0u);
}

TEST(HoverChecks, RemovingADeeplyNestedSubtreeExitsEveryDescendant) {
    // arrange a hovered grandchild under a hovered child (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    auto grandchild = std::make_unique<RecordingNode>();
    RecordingNode* child_ptr = child.get();
    RecordingNode* grandchild_ptr = grandchild.get();
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 60.0f}));
    grandchild_ptr->arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{20.0f, 20.0f}));
    child_ptr->add_child(std::move(grandchild));
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    child_ptr->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 60.0f}));
    grandchild_ptr->arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{20.0f, 20.0f}));
    controller.attach(root);
    Event inside{.kind = EventKind::PointerMove, .position = Point{25.0f, 25.0f}};
    controller.deliver(inside);
    ASSERT_EQ(grandchild_ptr->enters, 1u);
    ASSERT_EQ(child_ptr->enters, 1u);
    // act the child holding the grandchild is removed (§8.3)
    const std::unique_ptr<UiNode> removed = root.remove_child(child_ptr);
    ASSERT_NE(removed, nullptr);
    // assert the deepest node was told first, so no widget kept a hover on a node that is going (§8.3)
    EXPECT_EQ(grandchild_ptr->leaves, 1u);
    EXPECT_EQ(child_ptr->leaves, 1u);
    EXPECT_EQ(root.leaves, 0u);
}

TEST(ShortcutRoutingChecks, AWindowWithNoRegistryRoutesNoAccelerator) {
    // arrange
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event key{.kind = EventKind::KeyDown, .key = Key::S};
    key.modifiers.command = true;
    // act
    controller.deliver(key);
    // assert an application that declared nothing leaves the key to the tree (§18.5)
    EXPECT_EQ(controller.command_registry(), nullptr);
    EXPECT_EQ(controller.shortcut_invocations(), std::size_t{0});
    EXPECT_FALSE(key.handled);
}

TEST(ShortcutRoutingChecks, AModifiedAcceleratorRunsBeforeTheFocusedNode) {
    // arrange
    CommandRegistry commands;
    Command& save = commands.declare("file.save", "Save");
    std::size_t runs = 0;
    InvocationSource route = InvocationSource::Pointer;
    save.set_action([&runs, &route](const CommandInvocation& invocation) {
        runs++;
        route = invocation.source;
    });
    commands.shortcuts().bind(Shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}}, save.id());
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.set_command_registry(&commands);
    Event key{.kind = EventKind::KeyDown, .key = Key::S};
    key.modifiers.command = true;
    // act
    controller.deliver(key);
    // assert a modified key is an application shortcut, which the standards reserve before the caret (§18.5)
    EXPECT_EQ(runs, std::size_t{1});
    EXPECT_EQ(route, InvocationSource::Keyboard);
    EXPECT_EQ(root.deliveries, std::size_t{0});
    EXPECT_EQ(controller.shortcut_invocations(), std::size_t{1});
}

TEST(ShortcutRoutingChecks, AnUnmodifiedAcceleratorRunsOnlyAfterTheCaretDeclines) {
    // arrange
    CommandRegistry commands;
    Command& save = commands.declare("file.save", "Save");
    std::size_t runs = 0;
    save.set_action([&runs](const CommandInvocation&) { runs++; });
    commands.shortcuts().bind(Shortcut{.key = Key::S}, save.id());
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.set_command_registry(&commands);
    Event claimed{.kind = EventKind::KeyDown, .key = Key::S};
    Event declined{.kind = EventKind::KeyDown, .key = Key::S};
    // act
    root.claims = true;
    controller.deliver(claimed);
    // the caret stops claiming the second key, which is what declining the key looks like to the route (§18.5)
    root.claims = false;
    controller.deliver(declined);
    // assert a text control always wins over an unbound letter, because typing must not fire an action (§18.5)
    EXPECT_EQ(runs, std::size_t{1});
    EXPECT_TRUE(claimed.handled);
    EXPECT_EQ(controller.shortcut_invocations(), std::size_t{1});
}

TEST(ShortcutRoutingChecks, ADisabledCommandStillConsumesItsKey) {
    // arrange
    CommandRegistry commands;
    Command& save = commands.declare("file.save", "Save");
    std::size_t runs = 0;
    save.set_action([&runs](const CommandInvocation&) { runs++; });
    commands.shortcuts().bind(Shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}}, save.id());
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.set_command_registry(&commands);
    save.set_enabled(false);
    Event key{.kind = EventKind::KeyDown, .key = Key::S};
    key.modifiers.command = true;
    // act
    controller.deliver(key);
    // assert the key does not fall through to another consumer, which would make a disabled item look live (§18.5)
    EXPECT_EQ(runs, std::size_t{0});
    EXPECT_EQ(root.deliveries, std::size_t{0});
    EXPECT_EQ(controller.shortcut_invocations(), std::size_t{1});
}

TEST(ShortcutRoutingChecks, AnUnboundAcceleratorBelongsToTheCaret) {
    // arrange
    CommandRegistry commands;
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.set_command_registry(&commands);
    Event key{.kind = EventKind::KeyDown, .key = Key::S};
    key.modifiers.command = true;
    // act
    controller.deliver(key);
    // assert a registry with no binding leaves the key exactly where it was (§18.5)
    EXPECT_TRUE(key.handled);
    EXPECT_EQ(root.deliveries, std::size_t{1});
    EXPECT_EQ(controller.shortcut_invocations(), std::size_t{0});
}

TEST(ShortcutRoutingChecks, TwoAcceleratorsIntoOneCommandContributeTwoHistoryEntries) {
    // arrange
    CommandRegistry commands;
    Command& save = commands.declare("file.save", "Save");
    save.set_action([](const CommandInvocation&) {});
    save.set_undo_entry(UndoEntry{.label = "Save", .undo = [](const CommandInvocation&) {}, .redo = [](const CommandInvocation&) {}});
    commands.shortcuts().bind(Shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}}, save.id());
    commands.shortcuts().bind(Shortcut{.key = Key::F1}, save.id());
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.set_command_registry(&commands);
    // act
    Event first{.kind = EventKind::KeyDown, .key = Key::S};
    first.modifiers.command = true;
    controller.deliver(first);
    Event second{.kind = EventKind::KeyDown, .key = Key::F1};
    controller.deliver(second);
    // assert each route records through the one shared object, so the history sits behind the command (§18.4)
    EXPECT_EQ(commands.undo_stack().depth(), std::size_t{2});
}
