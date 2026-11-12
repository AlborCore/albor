#include <memory>

#include <gtest/gtest.h>

#include "albor/commands/command_registry.h"
#include "albor/commands/undo_stack.h"
#include "albor/focus/focus_manager.h"
#include "albor/input/event_filter.h"
#include "albor/input/input_controller.h"
#include "albor/input/input_dispatcher.h"
#include "albor/ui/focus_target.h"
#include "albor/ui/ui_event.h"
#include "albor/ui/ui_node.h"

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

        // how many cancel events this node was told, which a cancelled drag receives once (§8.3)
        std::size_t cancels = 0;

        // how many release events this node was told, which a cancel must never synthesize (§8.3)
        std::size_t releases = 0;

        // whether this node claims the events it is offered (§8.3)
        bool claims = false;

        // the controller a press cancels, which models a surface hiding during its own press (§8.3)
        InputController* cancelling_controller = nullptr;

        // whether this node reports itself as visible, because a hidden node is not a hit (§8.3)
        bool shown = true;

        // whether this node reports itself as disabled, because a disabled node takes no focus (§8.3)
        bool blocked = false;

        // whether this node accepts the tab stop, because a drag target may decline it (§8.3)
        bool accepts = true;

        // the last event kind and button the node was offered, which the button tests read (§8.3)
        EventKind last_kind = EventKind::PointerDown;
        PointerButton last_button = PointerButton::Left;

        // the last pointer position the node was offered, which a cancel carries from the last move (§8.3)
        Point last_position{};

        // the reason the last cancel named, which separates an escape from a lost capture (§8.3)
        PointerCancelReason last_cancel_reason = PointerCancelReason::CaptureLost;

        // how a pointer press treats the focus, which a test sets to model a preserving control (§8.3)
        PointerFocusPolicy policy = PointerFocusPolicy::Take;

        [[nodiscard]] bool disabled() const override {
            // the flag is the reported state, which the focus ring reads (§8.3)
            return blocked;
        }

        [[nodiscard]] bool accepts_focus() const override {
            // the flag is what the tab walk reads, and a drag target may decline it (§8.3)
            return accepts;
        }

        [[nodiscard]] PointerFocusPolicy pointer_focus_policy() const override {
            // the policy is what the pointer decision reads, and a test controls it here (§8.3)
            return policy;
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
            // a cancel and a release are counted apart, because that difference is the contract (§8.3)
            if (event.kind == EventKind::PointerCancel)
                cancels++;
            if (event.kind == EventKind::PointerUp)
                releases++;
            // the delivery is counted before the claim, so a refusing node is still observable (§8.3)
            deliveries++;
            // the kind, the button and the position are kept, because the routing tests assert what arrived (§8.3)
            last_kind = event.kind;
            last_button = event.button;
            last_position = event.position;
            // the reason is kept for the cancel tests, because a cancel is the only kind that names one (§8.3)
            if (event.kind == EventKind::PointerCancel)
                last_cancel_reason = event.cancel_reason;
            // a claiming node marks the event handled, which stops the walk (§8.3)
            if (claims)
                event.handled = true;
            // a press may take its surface off screen, which ends any drag the controller would arm (§8.3)
            if (cancelling_controller != nullptr && event.kind == EventKind::PointerDown)
                cancelling_controller->cancel_pointer_capture();
        }
    };
    // the focus tracking test double, which records what the manager told it (§8.3)
    class FocusRecordingNode final : public UiNode, public FocusTarget
    {
    public:
        // whether the node believes it holds the focus (§8.3)
        bool focused = false;

        // whether the manager told the node to paint the keyboard cue (§8.3)
        bool cue_visible = false;

        // how many times the manager told this node about the focus (§8.3)
        std::size_t told = 0;

        // how many times the manager told this node about the cue (§8.3)
        std::size_t cue_told = 0;

        void set_focused(bool value) override {
            // the state is stored, because that is what the assertion reads (§8.3)
            this->focused = value;
            // a direct focus report is programmatic, so it carries the cue like a widget does (§8.3)
            cue_visible = value;
            // the count rises on every report, so a test can see it happened at all (§8.3)
            told++;
        }

        void set_focus_visible(bool visible) override {
            // the cue is stored, because that is what the assertion reads (§8.3)
            cue_visible = visible;
            // the count rises on every report, so a test can see it happened at all (§8.3)
            cue_told++;
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

TEST(EventFilterChecks, PointerCancelIsAPointerEvent) {
    // arrange
    Event event{.kind = EventKind::PointerCancel};
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
    Event cancel{.kind = EventKind::PointerCancel};
    Event wheel{.kind = EventKind::Wheel};
    // act assert
    EXPECT_TRUE(EventFilter::positioned(down));
    EXPECT_TRUE(EventFilter::positioned(up));
    EXPECT_TRUE(EventFilter::positioned(move));
    EXPECT_TRUE(EventFilter::positioned(cancel));
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
    // the root is arranged, because a pointer event is routed only into the geometry it reaches (§8.3)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
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
    // the root is arranged, because a pointer event is routed only into the geometry it reaches (§8.3)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
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
    // the root is arranged, because a pointer event is routed only into the geometry it reaches (§8.3)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
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
    // the root is arranged, because a pointer event is routed only into the geometry it reaches (§8.3)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
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
    // the root is arranged, because a pointer event is routed only into the geometry it reaches (§8.3)
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
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

TEST(FocusManagerChecks, APointerRequestKeepsTheCueHidden) {
    // arrange a tracked node that can take the focus (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act the focus arrives from a pointer
    manager.request_focus(&node, FocusReason::Pointer);
    // assert the node owns the keys without painting the keyboard cue (§8.3)
    EXPECT_TRUE(node.focused);
    EXPECT_FALSE(node.cue_visible);
    EXPECT_FALSE(manager.focus_visible());
    EXPECT_EQ(manager.focused(), &node);
}

TEST(FocusManagerChecks, AKeyboardRequestShowsTheCue) {
    // arrange a tracked node that can take the focus (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act the focus arrives from the keyboard
    manager.request_focus(&node, FocusReason::Keyboard);
    // assert the node owns the keys and paints the cue, which is the tab path (§8.3)
    EXPECT_TRUE(node.focused);
    EXPECT_TRUE(node.cue_visible);
    EXPECT_TRUE(manager.focus_visible());
    EXPECT_EQ(node.cue_told, 1u);
}

TEST(FocusManagerChecks, AProgrammaticRequestShowsTheCue) {
    // arrange a tracked node that can take the focus (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    // act the focus arrives from a programmatic request, which a dialog initial focus uses (§8.3)
    manager.request_focus(&node, FocusReason::Programmatic);
    // assert a programmatic focus carries the cue, because it is a navigation context (§8.3)
    EXPECT_TRUE(node.focused);
    EXPECT_TRUE(node.cue_visible);
    EXPECT_TRUE(manager.focus_visible());
}

TEST(FocusManagerChecks, APointerRequestOnTheFocusedNodeHidesTheCue) {
    // arrange a node that the keyboard focused first (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node, FocusReason::Keyboard);
    ASSERT_TRUE(node.cue_visible);
    // the first request pushed the cue once, which is the baseline the pointer write must move (§8.3)
    const std::size_t cue_reports = node.cue_told;
    // act the pointer presses the same node, which changes the modality and not the owner (§8.3)
    manager.request_focus(&node, FocusReason::Pointer);
    // assert the owner stayed and only the cue moved, because a pointer does not mark the control (§8.3)
    EXPECT_EQ(manager.focused(), &node);
    EXPECT_FALSE(node.cue_visible);
    EXPECT_EQ(node.cue_told, cue_reports + 1);
    EXPECT_EQ(manager.change_count(), 1u);
}

TEST(FocusManagerChecks, RevealFocusShowsTheCueWithoutMovingTheOwner) {
    // arrange a pointer focused node, which carries no cue (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node, FocusReason::Pointer);
    ASSERT_FALSE(node.cue_visible);
    // act a key interaction asks for the cue
    manager.reveal_focus();
    // assert the cue appears without a second focus change, because only the modality moved (§8.3)
    EXPECT_EQ(manager.focused(), &node);
    EXPECT_TRUE(node.cue_visible);
    EXPECT_EQ(manager.change_count(), 1u);
}

TEST(FocusManagerChecks, HideFocusCueKeepsTheOwner) {
    // arrange a keyboard focused node, which carries the cue (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node, FocusReason::Keyboard);
    ASSERT_TRUE(node.cue_visible);
    // act a pointer press lowers the cue on the current owner
    manager.hide_focus_cue();
    // assert the owner survives the modality change, because a click does not clear the focus (§8.3)
    EXPECT_EQ(manager.focused(), &node);
    EXPECT_FALSE(node.cue_visible);
    EXPECT_EQ(manager.change_count(), 1u);
}

TEST(FocusManagerChecks, LosingTheKeyStatusHidesTheCueWithoutDroppingTheOwner) {
    // arrange a keyboard focused node in a window that holds key status (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node, FocusReason::Keyboard);
    ASSERT_TRUE(node.cue_visible);
    // act the window loses key status, which hides the cue but keeps the owner (§8.3)
    manager.set_window_active(false);
    // assert the owner survived while the cue went away, which is the inactive window rule (§8.3)
    EXPECT_EQ(manager.focused(), &node);
    EXPECT_FALSE(node.cue_visible);
    EXPECT_FALSE(manager.focus_visible());
    EXPECT_EQ(manager.change_count(), 1u);
}

TEST(FocusManagerChecks, ReturningTheKeyStatusRestoresTheCue) {
    // arrange a keyboard focused node whose window lost key status (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node, FocusReason::Keyboard);
    manager.set_window_active(false);
    ASSERT_FALSE(node.cue_visible);
    // act the window regains key status
    manager.set_window_active(true);
    // assert the cue the owner carried returns, without a new focus change (§8.3)
    EXPECT_EQ(manager.focused(), &node);
    EXPECT_TRUE(node.cue_visible);
    EXPECT_TRUE(manager.focus_visible());
}

TEST(FocusManagerChecks, AKeyStatusReturnDoesNotShowAPointerCue) {
    // arrange a pointer focused node whose window lost key status (§8.3)
    FocusManager manager;
    FocusRecordingNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}));
    manager.request_focus(&node, FocusReason::Pointer);
    manager.set_window_active(false);
    // act the window regains key status
    manager.set_window_active(true);
    // assert a pointer origin stays unmarked, because the key status restores what was there (§8.3)
    EXPECT_FALSE(node.cue_visible);
    EXPECT_FALSE(manager.focus_visible());
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

TEST(InputControllerChecks, APressThatHidesItsSurfaceDoesNotArmCapture) {
    // arrange a node whose press hides the native surface, which is a popup row that commits a choice (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.cancelling_controller = &controller;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    controller.deliver(press);
    // assert the hidden surface armed no capture, because its release may never arrive (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_FALSE(controller.capturing());
    EXPECT_EQ(controller.focus().focused(), nullptr);
    // act press again, which must route rather than be swallowed by a stale drag (§8.3)
    Event second{.kind = EventKind::PointerDown, .position = Point{12.0f, 12.0f}};
    controller.deliver(second);
    // assert the second press reached the tree, so reopening the surface accepts a click (§8.3)
    EXPECT_EQ(root.deliveries, 2u);
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

TEST(InputControllerChecks, APointerPressOnAPreservingControlKeepsTheCaret) {
    // arrange a field and a toolbar action under a claiming container, with the caret on the field (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    auto field = std::make_unique<FocusRecordingNode>();
    field->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    FocusRecordingNode* field_node = field.get();
    auto action = std::make_unique<RecordingNode>();
    action->claims = true;
    action->policy = PointerFocusPolicy::Preserve;
    action->arrange(Rect::from_origin_size(Point{60.0f, 0.0f}, Size{40.0f, 40.0f}));
    root.add_child(std::move(field));
    root.add_child(std::move(action));
    controller.attach(root);
    controller.focus().request_focus(field_node, FocusReason::Keyboard);
    ASSERT_EQ(controller.focus().focused(), field_node);
    ASSERT_TRUE(field_node->cue_visible);
    // act a primary press lands on the action, which claims it (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{70.0f, 10.0f}};
    controller.deliver(press);
    // assert the action ran and captured, while the field kept the caret and lost only the cue (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(controller.focus().focused(), field_node);
    EXPECT_TRUE(controller.capturing());
    EXPECT_FALSE(field_node->cue_visible);
    EXPECT_FALSE(controller.focus().focus_visible());
}

TEST(InputControllerChecks, APointerPressOnANoneControlKeepsTheCaret) {
    // arrange a keyboard focused field and a control that declines pointer focus (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    auto field = std::make_unique<FocusRecordingNode>();
    field->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    FocusRecordingNode* field_node = field.get();
    auto action = std::make_unique<RecordingNode>();
    action->claims = true;
    action->policy = PointerFocusPolicy::None;
    action->arrange(Rect::from_origin_size(Point{60.0f, 0.0f}, Size{40.0f, 40.0f}));
    root.add_child(std::move(field));
    root.add_child(std::move(action));
    controller.attach(root);
    controller.focus().request_focus(field_node, FocusReason::Keyboard);
    ASSERT_EQ(controller.focus().focused(), field_node);
    // act a primary press lands on the none control, which claims it (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{70.0f, 10.0f}};
    controller.deliver(press);
    // assert the none control captured without taking the caret, while the pointer lowered the cue (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(controller.focus().focused(), field_node);
    EXPECT_TRUE(controller.capturing());
    EXPECT_FALSE(field_node->cue_visible);
}

TEST(InputControllerChecks, AnUnfocusableClaimingPressCapturesWithoutTheCaret) {
    // arrange a node that claims the press but declines the tab stop, beside a keyboard focused field (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.accepts = false;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 80.0f}));
    auto field = std::make_unique<RecordingNode>();
    field->arrange(Rect::from_origin_size(Point{40.0f, 40.0f}, Size{20.0f, 20.0f}));
    RecordingNode* field_node = field.get();
    root.add_child(std::move(field));
    controller.attach(root);
    controller.focus().request_focus(field_node, FocusReason::Keyboard);
    ASSERT_TRUE(controller.focus().focus_visible());
    // act a primary press lands on the unfocusable surface, which claims it (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    // assert the capture belongs to the claiming node whether or not the caret moved (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(controller.focus().focused(), field_node);
    EXPECT_TRUE(controller.capturing());
    // assert the cue stayed, because the press landed on no focusable control (§8.3)
    EXPECT_TRUE(controller.focus().focus_visible());
}

TEST(InputControllerChecks, APressOnEmptySpaceKeepsTheCaretAndTheCue) {
    // arrange a field focused by the keyboard under a container that claims the surface (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    auto field = std::make_unique<RecordingNode>();
    field->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    RecordingNode* field_node = field.get();
    root.add_child(std::move(field));
    controller.attach(root);
    controller.focus().request_focus(field_node, FocusReason::Keyboard);
    ASSERT_TRUE(controller.focus().focus_visible());
    // act a primary press lands outside every control
    Event press{.kind = EventKind::PointerDown, .position = Point{200.0f, 200.0f}};
    controller.deliver(press);
    // assert empty space preserves the caret and its cue, because no control was pressed (§8.3)
    EXPECT_EQ(controller.focus().focused(), field_node);
    EXPECT_TRUE(controller.focus().focus_visible());
}

TEST(InputControllerChecks, APointerPressOnAPointerFocusedControlHidesTheCue) {
    // arrange a field focused by the keyboard, which carries the cue (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    auto field = std::make_unique<RecordingNode>();
    field->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    RecordingNode* field_node = field.get();
    root.add_child(std::move(field));
    controller.attach(root);
    controller.focus().request_focus(field_node, FocusReason::Keyboard);
    ASSERT_TRUE(controller.focus().focus_visible());
    // act the pointer presses the same field, which takes pointer focus (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    // assert the owner stayed while the cue went away, because a pointer is not a keyboard origin (§8.3)
    EXPECT_EQ(controller.focus().focused(), field_node);
    EXPECT_FALSE(controller.focus().focus_visible());
}

TEST(InputControllerChecks, AKeyRevealsAPointerFocusedCue) {
    // arrange a field focused by a pointer, which carries no cue (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    auto field = std::make_unique<RecordingNode>();
    field->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    RecordingNode* field_node = field.get();
    root.add_child(std::move(field));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    ASSERT_EQ(controller.focus().focused(), field_node);
    ASSERT_FALSE(controller.focus().focus_visible());
    // act a key reaches the focused control, which is the keyboard modality (§8.3)
    Event key{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    controller.deliver(key);
    // assert the cue returned without moving the owner, which is the focus visible rule (§8.3)
    EXPECT_EQ(controller.focus().focused(), field_node);
    EXPECT_TRUE(controller.focus().focus_visible());
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
    // assert the release closed the drag without a cancel, so the next press starts a fresh one (§8.3)
    EXPECT_EQ(root.deliveries, 2u);
    EXPECT_EQ(root.releases, 1u);
    EXPECT_EQ(root.cancels, 0u);
    EXPECT_EQ(root.last_kind, EventKind::PointerUp);
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

TEST(InputControllerChecks, EscapeCancelsACaptureWithACancelNotARelease) {
    // arrange a captured drag that moved away from where it started (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{40.0f, 40.0f}};
    controller.deliver(move);
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act
    controller.deliver(escape);
    // assert the escape swallowed the drag and told the captured node it was cancelled at the last point (§8.3)
    EXPECT_TRUE(escape.handled);
    EXPECT_EQ(root.deliveries, 3u);
    EXPECT_EQ(root.cancels, 1u);
    EXPECT_EQ(root.releases, 0u);
    EXPECT_EQ(root.last_kind, EventKind::PointerCancel);
    EXPECT_EQ(root.last_position, (Point{40.0f, 40.0f}));
    EXPECT_EQ(root.last_cancel_reason, PointerCancelReason::Escape);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, ASecondEscapeAfterACancelTellsNoSecondCancel) {
    // arrange a drag that escape already cancelled (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event first{.kind = EventKind::KeyDown, .key = Key::Escape};
    controller.deliver(first);
    ASSERT_EQ(root.cancels, 1u);
    const std::size_t after_cancel = root.deliveries;
    Event second{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act a second escape arrives with no drag left to end (§8.3)
    controller.deliver(second);
    // assert the second key reached the focus but produced no cancel of its own (§8.3)
    EXPECT_EQ(root.cancels, 1u);
    EXPECT_EQ(root.deliveries, after_cancel + 1u);
    EXPECT_EQ(root.last_kind, EventKind::KeyDown);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, ACancelClearsTheSelectionWithoutApplyingTheZoom) {
    // arrange a chart-shaped node that commits the zoom on release and drops the selection on cancel (§8.3)
    class ChartNode final : public UiNode
    {
    public:
        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the node fills the offer, because the gesture is not about its extent (§8.2)
            out_size = constraints.constrain(Size{50.0f, 50.0f});
            m_measured_size = out_size;
        }

        [[nodiscard]] bool hit_test(Point) const override {
            // every press lands on the chart, which is the gesture surface (§8.3)
            return true;
        }

        void dispatch_event(Event& event) override {
            // a press claims the drag, which is what arms the capture (§8.3)
            if (event.kind == EventKind::PointerDown) {
                event.handled = true;
                return;
            }
            // a release applies the current selection, which is the commit path (§8.3)
            if (event.kind == EventKind::PointerUp) {
                zoom_window = selection;
                event.handled = true;
                return;
            }
            // a cancel clears the selection and never applies the zoom window (§8.3)
            if (event.kind == EventKind::PointerCancel) {
                selection = 0;
                last_reason = event.cancel_reason;
                event.handled = true;
            }
        }

        // the zoom window the release would apply, where zero means it applied nothing (§8.3)
        int zoom_window = 0;

        // the transient selection the drag carries, where zero means it was cleared (§8.3)
        int selection = 1;

        // the reason the cancel named, which the chart path asserts (§8.3)
        PointerCancelReason last_reason = PointerCancelReason::CaptureLost;
    };
    InputController controller;
    ChartNode chart;
    chart.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(chart);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{30.0f, 30.0f}};
    controller.deliver(move);
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act
    controller.deliver(escape);
    // assert the selection was cleared and the zoom window was never applied (§8.3)
    EXPECT_EQ(chart.last_reason, PointerCancelReason::Escape);
    EXPECT_EQ(chart.selection, 0);
    EXPECT_EQ(chart.zoom_window, 0);
    // act a second drag ends in a real release, which is the path that applies the zoom (§8.3)
    Event second{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(second);
    chart.selection = 2;
    Event release{.kind = EventKind::PointerUp, .position = Point{30.0f, 30.0f}};
    controller.deliver(release);
    // assert the release applied the selection, so the cancel is what kept the first zoom unapplied (§8.3)
    EXPECT_EQ(chart.zoom_window, 2);
}

TEST(InputControllerChecks, CancellingACaptureTellsTheNodeTheLastPointerPosition) {
    // arrange a captured drag that moved before the window lost the interaction (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{30.0f, 40.0f}};
    controller.deliver(move);
    // act
    controller.cancel_pointer_capture();
    // assert exactly one cancel reached the node at the last point, and no release was synthesized (§8.3)
    EXPECT_EQ(root.cancels, 1u);
    EXPECT_EQ(root.releases, 0u);
    EXPECT_EQ(root.last_kind, EventKind::PointerCancel);
    EXPECT_EQ(root.last_position, (Point{30.0f, 40.0f}));
    EXPECT_EQ(root.last_cancel_reason, PointerCancelReason::CaptureLost);
    EXPECT_FALSE(controller.capturing());
    // act a repeated cancel finds no capture to end (§8.3)
    controller.cancel_pointer_capture();
    // assert
    EXPECT_EQ(root.cancels, 1u);
}

TEST(InputControllerChecks, AnIncomingPointerCancelEndsTheCapture) {
    // arrange a captured drag and a cancel the controller is handed rather than synthesizing (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    Event cancel{.kind = EventKind::PointerCancel, .position = Point{20.0f, 20.0f}, .cancel_reason = PointerCancelReason::WindowHidden};
    // act
    controller.deliver(cancel);
    // assert the incoming cancel was claimed, cleared the capture and carried its reason to the node (§8.3)
    EXPECT_TRUE(cancel.handled);
    EXPECT_FALSE(controller.capturing());
    EXPECT_EQ(root.cancels, 1u);
    EXPECT_EQ(root.last_position, (Point{20.0f, 20.0f}));
    EXPECT_EQ(root.last_cancel_reason, PointerCancelReason::WindowHidden);
}

TEST(InputControllerChecks, ASecondPressAfterACancelStartsAFreshDrag) {
    // arrange a captured drag that the window cancelled (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    controller.cancel_pointer_capture();
    ASSERT_FALSE(controller.capturing());
    Event second{.kind = EventKind::PointerDown, .position = Point{20.0f, 20.0f}};
    // act
    controller.deliver(second);
    // assert the next press reached the tree and armed a new drag, because the cancel left nothing behind (§8.3)
    EXPECT_TRUE(second.handled);
    EXPECT_TRUE(controller.capturing());
    EXPECT_EQ(root.cancels, 1u);
}

TEST(InputControllerChecks, RemovingTheCapturedNodeCancelsTheDragBeforeItLeaves) {
    // arrange a captured child that the application is about to remove (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    child->claims = true;
    child->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    RecordingNode* captured = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    ASSERT_TRUE(controller.capturing());
    // act the subtree leaves the tree, which is a capture loss no release can announce (§8.3)
    const std::unique_ptr<UiNode> removed = root.remove_child(captured);
    // assert the node was told while it was still alive, and the controller keeps no stale capture (§8.3)
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(captured->cancels, 1u);
    EXPECT_EQ(captured->releases, 0u);
    EXPECT_EQ(captured->last_cancel_reason, PointerCancelReason::Detached);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, HidingTheCapturedNodeCancelsTheDrag) {
    // arrange a captured child that hides itself, which is the second detaching source (§8.3)
    InputController controller;
    RecordingNode root;
    auto child = std::make_unique<RecordingNode>();
    child->claims = true;
    child->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    RecordingNode* captured = child.get();
    root.add_child(std::move(child));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    ASSERT_TRUE(controller.capturing());
    // act the captured node hides, which is a capture loss no release announces (§8.3)
    captured->set_visible(false);
    // assert the node was told once while it was still alive, and the capture is gone (§8.3)
    EXPECT_EQ(captured->cancels, 1u);
    EXPECT_EQ(captured->last_cancel_reason, PointerCancelReason::Detached);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, HidingAnAncestorCancelsTheCapturedDescendantsDrag) {
    // arrange a captured grandchild under a container the application hides (§8.3)
    InputController controller;
    RecordingNode root;
    auto branch = std::make_unique<RecordingNode>();
    auto child = std::make_unique<RecordingNode>();
    child->claims = true;
    child->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    RecordingNode* captured = child.get();
    branch->add_child(std::move(child));
    branch->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{30.0f, 30.0f}));
    root.add_child(std::move(branch));
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    ASSERT_TRUE(controller.capturing());
    // act the ancestor hides, which takes the captured descendant off the tree with it (§8.3)
    root.child_at(0)->set_visible(false);
    // assert the descendant was told once and the capture does not outlive it (§8.3)
    EXPECT_EQ(captured->cancels, 1u);
    EXPECT_EQ(captured->last_cancel_reason, PointerCancelReason::Detached);
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, ACancelWithNoLiveCaptureTouchesNothing) {
    // arrange a hovered tree with no drag, which is the state a stray cancel finds (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event hover{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    controller.deliver(hover);
    ASSERT_EQ(controller.hovered(), &root);
    const std::size_t deliveries = root.deliveries;
    const std::size_t enters = root.enters;
    Event cancel{.kind = EventKind::PointerCancel, .position = Point{20.0f, 20.0f}};
    // act a cancel arrives with no capture to end (§8.3)
    controller.deliver(cancel);
    // assert it is left unhandled and the hover chain is untouched (§8.3)
    EXPECT_FALSE(cancel.handled);
    EXPECT_EQ(root.deliveries, deliveries);
    EXPECT_EQ(root.enters, enters);
    EXPECT_EQ(controller.hovered(), &root);
}

TEST(InputControllerChecks, AnEscapeTheControlDeclinedStaysUnhandled) {
    // arrange a tree whose root claims nothing, which is what a plain container does (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.focus().request_focus(&root);
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act
    controller.deliver(escape);
    // assert the key is left for the window, which is what lets a dialog dismiss on it (§8.3)
    EXPECT_FALSE(escape.handled);
    EXPECT_EQ(root.deliveries, 1u);
}

TEST(InputControllerChecks, AnEscapeTheControlClaimsIsHandled) {
    // arrange a control that claims the key, which is what a popup does (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    controller.focus().request_focus(&root);
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act
    controller.deliver(escape);
    // assert the claim reaches the window, so a dialog does not also act on it (§8.3)
    EXPECT_TRUE(escape.handled);
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
    EXPECT_TRUE(controller.focus().focus_visible());
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
    EXPECT_TRUE(controller.focus().focus_visible());
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

TEST(InputControllerChecks, ASecondPressWhileCapturingReachesTheCapturedNode) {
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
    // assert the capture owned the press, so the drag keeps flowing (§8.3)
    EXPECT_TRUE(second.handled);
    EXPECT_EQ(root.deliveries, 2u);
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
    auto save_owned = commands.declare("file.save", "Save");
    Command& save = *save_owned;
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
    auto save_owned = commands.declare("file.save", "Save");
    Command& save = *save_owned;
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
    auto save_owned = commands.declare("file.save", "Save");
    Command& save = *save_owned;
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
    auto save_owned = commands.declare("file.save", "Save");
    Command& save = *save_owned;
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

TEST(InputControllerChecks, ASecondaryPressRaisesAContextMenuRequestAtTheNodeLocalPoint) {
    // arrange a node away from the origin, which is what makes the local point visible (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{40.0f, 30.0f}, Size{100.0f, 80.0f}));
    Point requested{};
    int requests = 0;
    root.set_on_context_menu([&requested, &requests](Point point) {
        requested = point;
        requests++;
    });
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{70.0f, 50.0f}, .button = PointerButton::Right};
    // act
    controller.deliver(press);
    // assert the node reads the point against its own corner, which is the chart-local position (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(requests, 1);
    EXPECT_FLOAT_EQ(requested.x, 30.0f);
    EXPECT_FLOAT_EQ(requested.y, 20.0f);
}

TEST(InputControllerChecks, AContextMenuRequestBubblesToTheNearestHandler) {
    // arrange a child without a handler inside a parent that carries one (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    auto child = std::make_unique<RecordingNode>();
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{40.0f, 40.0f}));
    RecordingNode* inner = child.get();
    root.add_child(std::move(child));
    Point requested{};
    int requests = 0;
    root.set_on_context_menu([&requested, &requests](Point point) {
        requested = point;
        requests++;
    });
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{20.0f, 20.0f}, .button = PointerButton::Right};
    // act
    controller.deliver(press);
    // assert the nearest handler took the request, and the child carried none of its own (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(requests, 1);
    EXPECT_FALSE(inner->has_on_context_menu());
    EXPECT_FLOAT_EQ(requested.x, 20.0f);
    EXPECT_FLOAT_EQ(requested.y, 20.0f);
}

TEST(InputControllerChecks, AnUnhandledContextMenuRequestLeavesTheEventOpen) {
    // arrange a tree with no context-menu handler anywhere, which claims no event of its own (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}, .button = PointerButton::Right};
    // act
    controller.deliver(press);
    // assert the request is left for the application, because no node wanted it (§8.3)
    EXPECT_FALSE(press.handled);
}

TEST(InputControllerChecks, AMiddlePressReachesTheTreeWithItsButton) {
    // arrange a node that claims every press it is offered (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}, .button = PointerButton::Middle};
    // act
    controller.deliver(press);
    // assert the middle button arrives as a press that names itself, which a control may claim (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(root.last_kind, EventKind::PointerDown);
    EXPECT_EQ(root.last_button, PointerButton::Middle);
    EXPECT_TRUE(controller.capturing());
}

TEST(InputControllerChecks, AMiddleReleaseEndsTheDragTheMiddlePressStarted) {
    // arrange a captured middle press (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}, .button = PointerButton::Middle};
    controller.deliver(press);
    // act
    Event release{.kind = EventKind::PointerUp, .position = Point{12.0f, 12.0f}, .button = PointerButton::Middle};
    controller.deliver(release);
    // assert the matching release drops the capture, so the next press starts a new gesture (§8.3)
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, ASecondaryReleaseDoesNotEndAPrimaryDrag) {
    // arrange a captured primary press (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    ASSERT_TRUE(controller.capturing());
    // act a secondary release arrives while the primary drag is live (§8.3)
    Event release{.kind = EventKind::PointerUp, .position = Point{20.0f, 20.0f}, .button = PointerButton::Right};
    controller.deliver(release);
    // assert the drag is still captured, because only its own button ends it (§8.3)
    EXPECT_TRUE(controller.capturing());
    // act the primary release ends it
    Event primary{.kind = EventKind::PointerUp, .position = Point{20.0f, 20.0f}};
    controller.deliver(primary);
    // assert the primary release is the one that drops the capture (§8.3)
    EXPECT_FALSE(controller.capturing());
}

TEST(InputControllerChecks, ASecondaryPressDuringACaptureReachesTheCapturedNode) {
    // arrange a captured primary press on a node without a menu handler (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    ASSERT_TRUE(controller.capturing());
    // act a secondary press lands outside the captured node (§8.3)
    Event secondary{.kind = EventKind::PointerDown, .position = Point{200.0f, 200.0f}, .button = PointerButton::Right};
    controller.deliver(secondary);
    // assert the capture owned the request, so the drag keeps flowing (§8.3)
    EXPECT_EQ(root.deliveries, 2u);
    EXPECT_TRUE(controller.capturing());
}

TEST(InputControllerChecks, ShiftF10RaisesAContextMenuRequestAtTheFocusedNode) {
    // arrange a focusable node with a context-menu handler (§8.3)
    InputController controller;
    UiNode root;
    auto node = std::make_unique<FocusRecordingNode>();
    node->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{40.0f, 20.0f}));
    FocusRecordingNode* target = node.get();
    root.add_child(std::move(node));
    Point requested{};
    int requests = 0;
    target->set_on_context_menu([&requested, &requests](Point point) {
        requested = point;
        requests++;
    });
    controller.attach(root);
    controller.focus().request_focus(target);
    ASSERT_TRUE(target->focused);
    Event key{.kind = EventKind::KeyDown, .key = Key::F10};
    key.modifiers.shift = true;
    // act
    controller.deliver(key);
    // assert the request opens at the centre of the focused node, read against its own corner (§8.3)
    EXPECT_TRUE(key.handled);
    EXPECT_EQ(requests, 1);
    EXPECT_FLOAT_EQ(requested.x, 20.0f);
    EXPECT_FLOAT_EQ(requested.y, 10.0f);
}

TEST(InputControllerChecks, TheMenuKeyRaisesAContextMenuRequest) {
    // arrange a focusable node with a context-menu handler (§8.3)
    InputController controller;
    UiNode root;
    auto node = std::make_unique<FocusRecordingNode>();
    node->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    FocusRecordingNode* target = node.get();
    root.add_child(std::move(node));
    int requests = 0;
    target->set_on_context_menu([&requests](Point) { requests++; });
    controller.attach(root);
    controller.focus().request_focus(target);
    Event key{.kind = EventKind::KeyDown, .key = Key::Menu};
    // act
    controller.deliver(key);
    // assert the menu key is the keyboard route to the same request (§8.3)
    EXPECT_TRUE(key.handled);
    EXPECT_EQ(requests, 1);
}

TEST(InputControllerChecks, AHoverOrAPrimaryPressRaisesNoContextMenuRequest) {
    // arrange a node that carries a handler, so a wrong route would be visible (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    int requests = 0;
    root.set_on_context_menu([&requests](Point) { requests++; });
    controller.attach(root);
    // act a hover and a primary press cross the node (§8.3)
    Event move{.kind = EventKind::PointerMove, .position = Point{10.0f, 10.0f}};
    controller.deliver(move);
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    controller.deliver(press);
    // assert neither gesture is a menu request, which keeps the menu out of the activation path (§8.3)
    EXPECT_EQ(requests, 0);
}

TEST(InputControllerChecks, AMiddlePressDoesNotMoveTheFocus) {
    // arrange a claiming container holding two focusable nodes, with the caret on the first (§8.3)
    InputController controller;
    RecordingNode root;
    root.claims = true;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    auto first = std::make_unique<FocusRecordingNode>();
    first->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    FocusRecordingNode* first_node = first.get();
    auto second = std::make_unique<FocusRecordingNode>();
    second->arrange(Rect::from_origin_size(Point{60.0f, 0.0f}, Size{40.0f, 40.0f}));
    FocusRecordingNode* second_node = second.get();
    root.add_child(std::move(first));
    root.add_child(std::move(second));
    controller.attach(root);
    controller.focus().request_focus(first_node);
    ASSERT_TRUE(first_node->focused);
    // act a middle press lands on the other node and the container claims it (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{70.0f, 10.0f}, .button = PointerButton::Middle};
    controller.deliver(press);
    // assert the press was claimed, so the focus rule was actually reached (§8.3)
    ASSERT_TRUE(press.handled);
    // assert the caret stayed where it was, because only the primary button moves it (§8.3)
    EXPECT_EQ(controller.focus().focused(), first_node);
    EXPECT_FALSE(second_node->focused);
}

TEST(InputControllerChecks, ADisabledChildLeavesTheContextMenuToItsParent) {
    // arrange a disabled child with a handler under a parent that carries one too (§8.3)
    InputController controller;
    RecordingNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    auto child = std::make_unique<RecordingNode>();
    child->blocked = true;
    child->arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{40.0f, 40.0f}));
    RecordingNode* inner = child.get();
    root.add_child(std::move(child));
    int parent_requests = 0;
    int child_requests = 0;
    root.set_on_context_menu([&parent_requests](Point) { parent_requests++; });
    inner->set_on_context_menu([&child_requests](Point) { child_requests++; });
    controller.attach(root);
    Event press{.kind = EventKind::PointerDown, .position = Point{20.0f, 20.0f}, .button = PointerButton::Right};
    // act
    controller.deliver(press);
    // assert the parent answered, because a disabled node refuses the request like every other input (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(parent_requests, 1);
    EXPECT_EQ(child_requests, 0);
}
