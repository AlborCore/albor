#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "albor/application/tooltip_controller.h"
#include "albor/core/constraints.h"
#include "albor/input/input_controller.h"
#include "albor/style/theme.h"
#include "albor/style/theme_builder.h"
#include "albor/text/text_engine.h"
#include "albor/ui/tooltip.h"
#include "albor/ui/tooltip_view.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/widget.h"

using namespace albor;

namespace
{
    // the text double that measures one line per wrapped chunk, which keeps the tooltip checks off any shaper (§9.1)
    class StubTextEngine final : public TextEngine
    {
    public:
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // an empty run carries no glyphs, so the bubble measures to its padding (§9.1)
            if (text.empty())
                return TextLayout{};
            // the double advances half a font size per byte, which stands in for shaping without a font backend (§9.1)
            const float natural = static_cast<float>(text.size()) * font_size * 0.5f;
            // the run wraps at the offered width, which is what a real engine does for a long tooltip (§9.1)
            const float width = max_width > 0.0f ? std::min(natural, max_width) : natural;
            // the height is one line per wrapped chunk, which is what the bubble frames (§8.2)
            const float lines = width > 0.0f ? std::ceil(natural / width) : 1.0f;
            TextLayout layout;
            // the measured extent is what a tooltip bubble sizes itself around (§8.2)
            layout.size = Size{.width = width, .height = font_size * 1.4f * std::max(1.0f, lines)};
            return layout;
        }

        [[nodiscard]] bool ready() const override { return true; }
    };
} // namespace

TEST(TooltipLifecycle, AHoveredTargetShowsAfterTheArmDelay) {
    // arrange a root and a target the controller can present over (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Save the document");
    target.arrange(Rect::from_origin_size(Point{100.0f, 100.0f}, Size{80.0f, 24.0f}));
    // act hover the target and run half the delay, which must not show a bubble (§8.3)
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY * 0.5f);
    // assert the tooltip is still arming and nothing was added to the root (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Arming);
    EXPECT_TRUE(root.overlays().empty());
    // act run the whole delay, which is the conventional half second (§8.3)
    tips.advance(TooltipController::ARM_DELAY);
    // assert the bubble is an overlay carrying the target's text (§8.3)
    ASSERT_EQ(root.overlays().size(), 1U);
    const auto* view = dynamic_cast<const TooltipView*>(root.overlays().front().get());
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->text(), "Save the document");
    EXPECT_EQ(tips.phase(), TooltipPhase::Visible);
}

TEST(TooltipLifecycle, AnAdjacentTargetShowsAfterTheWarmDelay) {
    // arrange two targets and one shown bubble, which is the adjacent control case (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode first;
    first.set_tooltip("Cut");
    first.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    UiNode second;
    second.set_tooltip("Copy");
    second.arrange(Rect::from_origin_size(Point{110.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&first);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act move onto the adjacent target and run a very short pass (§8.3)
    tips.set_target(&second);
    tips.advance(0.001f);
    // assert the old bubble left and the new one is still arming, because the warm tick is 50 ms (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Arming);
    EXPECT_TRUE(root.overlays().empty());
    // act run the warm delay, which is what makes a toolbar scan quick (§8.3)
    tips.advance(TooltipController::WARM_ARM_DELAY);
    // assert the adjacent target presents after 50 ms rather than 500 ms (§8.3)
    ASSERT_EQ(root.overlays().size(), 1U);
    const auto* view = dynamic_cast<const TooltipView*>(root.overlays().front().get());
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->text(), "Copy");
}

TEST(TooltipLifecycle, ASecondAdjacentTargetStaysWarm) {
    // arrange a shown bubble and a third control, which a quick toolbar scan reaches before the second opens (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode first;
    first.set_tooltip("Cut");
    first.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    UiNode second;
    second.set_tooltip("Copy");
    second.arrange(Rect::from_origin_size(Point{110.0f, 20.0f}, Size{80.0f, 24.0f}));
    UiNode third;
    third.set_tooltip("Paste");
    third.arrange(Rect::from_origin_size(Point{200.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&first);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act move to the second, then to the third before the second opens (§8.3)
    tips.set_target(&second);
    tips.advance(TooltipController::WARM_ARM_DELAY * 0.5f);
    tips.set_target(&third);
    tips.advance(TooltipController::WARM_ARM_DELAY);
    // assert the third still opens quickly, because the warmth is not reset by an unshown target (§8.3)
    ASSERT_EQ(root.overlays().size(), 1U);
    const auto* view = dynamic_cast<const TooltipView*>(root.overlays().front().get());
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->text(), "Paste");
}

TEST(TooltipLifecycle, AGapWithinTheGraceKeepsTheWarmDelay) {
    // arrange a shown bubble whose pointer leaves for a moment, which starts the grace (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode first;
    first.set_tooltip("Cut");
    first.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    UiNode second;
    second.set_tooltip("Copy");
    second.arrange(Rect::from_origin_size(Point{110.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&first);
    tips.advance(TooltipController::ARM_DELAY);
    tips.set_target(nullptr);
    ASSERT_EQ(tips.phase(), TooltipPhase::Warmup);
    // act spend part of the grace and land on the adjacent target (§8.3)
    tips.advance(TooltipController::WARM_GRACE * 0.5f);
    tips.set_target(&second);
    tips.advance(TooltipController::WARM_ARM_DELAY);
    // assert the warmth survived the gap, so the adjacent target still opens quickly (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Visible);
    EXPECT_EQ(root.overlays().size(), 1U);
}

TEST(TooltipLifecycle, TheGraceExpiresToHidden) {
    // arrange a shown bubble whose pointer left, which starts the grace (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Cut");
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    tips.set_target(nullptr);
    ASSERT_EQ(tips.phase(), TooltipPhase::Warmup);
    // act run the whole grace, because nothing adjacent was reached (§8.3)
    tips.advance(TooltipController::WARM_GRACE);
    // assert the controller is idle and the next target would be cold again (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Hidden);
    EXPECT_TRUE(root.overlays().empty());
}

TEST(TooltipLifecycle, AVisibleTooltipDismissesAfterTenSeconds) {
    // arrange a shown bubble, which waits the fixed timeout before it leaves (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Cut");
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act run the whole visible timeout, because the pointer never left (§8.3)
    tips.advance(TooltipController::VISIBLE_TIMEOUT);
    // assert the bubble left and the warmth still covers the next target (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Warmup);
    EXPECT_TRUE(root.overlays().empty());
}

TEST(TooltipLifecycle, APressDismissesAndSuppressesUntilThePointerMoves) {
    // arrange a shown bubble, which a press dismisses at once (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Cut");
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act press, which dismisses the bubble even though the pointer stays (§8.3)
    tips.dismiss();
    // assert the bubble left and the same target stays quiet (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Hidden);
    EXPECT_TRUE(root.overlays().empty());
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    EXPECT_EQ(tips.phase(), TooltipPhase::Hidden);
    // act leave the target and come back, which clears the suppression (§8.3)
    tips.pointer_left();
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    // assert a fresh hover arms again, because the pointer moved on (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Visible);
}

TEST(TooltipLifecycle, ALongTooltipWrapsInsideThePlate) {
    // arrange a target whose text is far wider than the bubble ceiling (§8.2, §8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip(std::string(120, 'x'));
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    // act
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    // assert the plate stays inside the ceiling and its padding does not eat the run (§8.2, §8.3)
    ASSERT_EQ(root.overlays().size(), 1U);
    const auto* view = dynamic_cast<const TooltipView*>(root.overlays().front().get());
    ASSERT_NE(view, nullptr);
    const TooltipStyle& style = view->style();
    EXPECT_LE(view->bounds().width, style.max_width);
    EXPECT_GE(view->bounds().width, view->layout().size.width + style.padding_horizontal * 2.0f);
}

TEST(TooltipLifecycle, ADismissalSurvivesAnUnrelatedGesture) {
    // arrange a shown bubble that a press dismissed, which leaves the target suppressed (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Cut");
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    tips.dismiss();
    // act dismiss again from a hidden controller, which a later key or wheel does (§8.3)
    tips.dismiss();
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    // assert the same target stays quiet, because the pointer never left it (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Hidden);
    EXPECT_TRUE(root.overlays().empty());
}

TEST(TooltipLifecycle, ABlurDoesNotSuppressTheTarget) {
    // arrange a shown bubble, which a window that loses key status hides (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Cut");
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act
    tips.dismiss_transient();
    // assert a returning window may show the same target again, because the pointer never moved (§8.3)
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    EXPECT_EQ(tips.phase(), TooltipPhase::Visible);
}

TEST(TooltipLifecycle, AChangedTextRebuildsTheVisibleBubble) {
    // arrange a shown bubble whose target text changes under the pointer (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.set_tooltip("Cut");
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act change the text, which the next frame notices (§8.3)
    target.set_tooltip("Cut the row");
    tips.advance(0.016f);
    // assert the stale bubble left and the new text arms with the warm delay (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Arming);
    EXPECT_TRUE(root.overlays().empty());
    tips.advance(TooltipController::WARM_ARM_DELAY);
    // assert the rebuilt bubble carries the new text (§8.3)
    ASSERT_EQ(root.overlays().size(), 1U);
    const auto* view = dynamic_cast<const TooltipView*>(root.overlays().front().get());
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->text(), "Cut the row");
}

TEST(TooltipTarget, AContainerTooltipCoversItsDescendant) {
    // arrange a container with a tooltip and a leaf without one, which is the sidebar row case (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    auto row_owned = std::make_unique<UiNode>();
    UiNode* row = row_owned.get();
    root.add_child(std::move(row_owned));
    row->set_tooltip("Transient files");
    auto leaf_owned = std::make_unique<UiNode>();
    UiNode* leaf = leaf_owned.get();
    row->add_child(std::move(leaf_owned));
    // act point the controller at the leaf, which carries no tooltip itself (§8.3)
    tips.set_target(leaf);
    // assert the nearest tooltipped ancestor is the target, so the row covers its content (§8.3)
    EXPECT_EQ(tips.target(), row);
}

TEST(TooltipTarget, TheInnerTooltipWinsOverItsParent) {
    // arrange a container and a leaf that both carry a tooltip (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    auto row_owned = std::make_unique<UiNode>();
    UiNode* row = row_owned.get();
    root.add_child(std::move(row_owned));
    row->set_tooltip("Transient files");
    auto leaf_owned = std::make_unique<UiNode>();
    UiNode* leaf = leaf_owned.get();
    row->add_child(std::move(leaf_owned));
    leaf->set_tooltip("Rename the file");
    // act
    tips.set_target(leaf);
    // assert the leaf wins, because the deepest tooltip is the one the pointer names (§8.3)
    EXPECT_EQ(tips.target(), leaf);
}

TEST(TooltipTarget, ARichBuilderSuppliesThePresentation) {
    // arrange a target whose tooltip is a subtree rather than a string (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    UiNode target;
    target.arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    target.set_tooltip_builder([] {
        // the rich bubble is a real node, so the measure and arrange path is exercised (§8.2, §8.3)
        TextLayout layout;
        layout.size = Size{.width = 80.0f, .height = 20.0f};
        auto bubble = std::make_unique<TooltipView>("Rich", layout);
        bubble->set_explicit_id("rich-bubble");
        return bubble;
    });
    // act
    tips.set_target(&target);
    tips.advance(TooltipController::ARM_DELAY);
    // assert the presented overlay is the subtree the builder returned (§8.3)
    ASSERT_EQ(root.overlays().size(), 1U);
    EXPECT_EQ(root.overlays().front()->identity().explicit_id, "rich-bubble");
    // assert the controller measured and placed the rich subtree where the bubble belongs (§8.2, §8.3)
    const Rect expected = place_tooltip(target.bounds(), Size{.width = 98.0f, .height = 30.0f}, root.bounds(), 6.0f, 4.0f);
    EXPECT_EQ(root.overlays().front()->bounds(), expected);
}

TEST(TooltipViewChecks, ATooltipOverlayTakesNoPointerInput) {
    // arrange a placed bubble, which sits over the control that owns it (§8.3)
    TooltipStyle style;
    TextLayout layout;
    layout.size = Size{.width = 60.0f, .height = 18.0f};
    TooltipView view("Tip", layout, style);
    view.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{78.0f, 28.0f}));
    // act
    const bool hit = view.hit_test(Point{20.0f, 20.0f});
    // assert the point inside the bubble is not a hit, so the hover stays on the control (§8.3)
    EXPECT_FALSE(hit);
}

TEST(TooltipViewChecks, AMoveDamagesTheOldCast) {
    // arrange a placed bubble whose cast paints outside the plate (§6.1, §6.2)
    TooltipStyle style;
    TextLayout layout;
    layout.size = Size{.width = 60.0f, .height = 18.0f};
    auto owned = std::make_unique<TooltipView>("Tip", layout, style);
    TooltipView* view = owned.get();
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    root.add_overlay(std::move(owned));
    view->place(Rect::from_origin_size(Point{50.0f, 50.0f}, Size{80.0f, 24.0f}), root.bounds());
    const Rect old_visual = view->visual_bounds();
    root.clear_damage_region();
    // act move the bubble, which a scrolled target asks for (§8.3)
    view->place(Rect::from_origin_size(Point{200.0f, 150.0f}, Size{80.0f, 24.0f}), root.bounds());
    // assert the damage covers the old cast, because its fringe is still on the surface (§6.1)
    const std::optional<Rect>& damage = root.damage_region();
    ASSERT_TRUE(damage.has_value());
    EXPECT_LE(damage->x, old_visual.x);
    EXPECT_LE(damage->y, old_visual.y);
    EXPECT_GE(damage->x + damage->width, old_visual.x + old_visual.width);
    EXPECT_GE(damage->y + damage->height, old_visual.y + old_visual.height);
}

TEST(TooltipViewChecks, TheBubbleFramesItsRunWithTheStylePadding) {
    // arrange a run of known extent and the default padding, which the bubble adds on every side (§8.2)
    TextLayout layout;
    layout.size = Size{.width = 60.0f, .height = 18.0f};
    TooltipStyle style;
    TooltipView view("Tip", layout, style);
    // act
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the run plus the padding is the extent the controller places (§8.2, §8.3)
    EXPECT_EQ(view.bubble_size(), (Size{.width = 78.0f, .height = 28.0f}));
    EXPECT_EQ(measured, (Size{.width = 78.0f, .height = 28.0f}));
}

TEST(TooltipPlacement, TheBubbleStaysBelowWithRoom) {
    // arrange an anchor in the middle of the view, where the bubble fits below (§8.3)
    const Rect viewport = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f});
    const Rect anchor = Rect::from_origin_size(Point{100.0f, 100.0f}, Size{80.0f, 24.0f});
    // act
    const Rect bubble = place_tooltip(anchor, Size{.width = 120.0f, .height = 40.0f}, viewport, 6.0f, 4.0f);
    // assert the bubble hangs below the anchor by the gap and is centred on it (§8.3)
    EXPECT_FLOAT_EQ(bubble.y, 130.0f);
    EXPECT_FLOAT_EQ(bubble.x, 80.0f);
}

TEST(TooltipPlacement, TheBubbleFlipsAboveNearTheBottom) {
    // arrange an anchor near the bottom, where a bubble below would leave the view (§8.3)
    const Rect viewport = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f});
    const Rect anchor = Rect::from_origin_size(Point{100.0f, 260.0f}, Size{80.0f, 24.0f});
    // act
    const Rect bubble = place_tooltip(anchor, Size{.width = 120.0f, .height = 40.0f}, viewport, 6.0f, 4.0f);
    // assert the bubble flipped above and still keeps its inset from the top (§8.3)
    EXPECT_LT(bubble.y + bubble.height, anchor.y);
    EXPECT_GE(bubble.y, 4.0f);
}

TEST(TooltipPlacement, TheBubbleClampsInsideTheViewport) {
    // arrange an anchor at each side edge, where the centred bubble would leave the view (§8.3)
    const Rect viewport = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f});
    const Size size{.width = 120.0f, .height = 40.0f};
    // act
    const Rect leading = place_tooltip(Rect::from_origin_size(Point{0.0f, 100.0f}, Size{40.0f, 24.0f}), size, viewport, 6.0f, 4.0f);
    const Rect trailing = place_tooltip(Rect::from_origin_size(Point{360.0f, 100.0f}, Size{40.0f, 24.0f}), size, viewport, 6.0f, 4.0f);
    // assert both edges clamp to the inset rather than clipping the text (§8.3)
    EXPECT_FLOAT_EQ(leading.x, 4.0f);
    EXPECT_FLOAT_EQ(trailing.x, 276.0f);
}

TEST(TooltipLifecycle, ARemovedTargetDismissesTheVisibleTooltip) {
    // arrange a shown bubble over a target that is about to leave the tree (§8.3)
    UiNode root;
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{400.0f, 300.0f}));
    StubTextEngine text;
    TooltipController tips(root, text);
    InputController input;
    input.attach(root);
    input.set_subtree_released_handler([&tips](UiNode* node) { tips.forget(node); });
    auto owned = std::make_unique<UiNode>();
    UiNode* row = owned.get();
    root.add_child(std::move(owned));
    row->set_tooltip("Transient files");
    row->arrange(Rect::from_origin_size(Point{20.0f, 20.0f}, Size{80.0f, 24.0f}));
    tips.set_target(row);
    tips.advance(TooltipController::ARM_DELAY);
    ASSERT_EQ(tips.phase(), TooltipPhase::Visible);
    // act remove the row, which reports the subtree before it dies (§8.3)
    const std::unique_ptr<UiNode> removed = root.remove_child(row);
    // assert the bubble left and no timer names the destroyed node (§8.3)
    EXPECT_EQ(tips.phase(), TooltipPhase::Hidden);
    EXPECT_TRUE(root.overlays().empty());
    EXPECT_EQ(tips.target(), nullptr);
}

TEST(TooltipStyleChecks, TheThemedStyleResolvesTheBubbleTokens) {
    // arrange a preset theme, which an application installs over the window (§8.4)
    const Theme theme = Theme::pro_dark(Accents{});
    // act
    const TooltipStyle style = tooltip_style(theme);
    // assert every token the bubble paints comes from the theme rather than a literal (§8.4)
    EXPECT_EQ(style.background, theme.palette().panel_background[state_index(ControlState::Normal)]);
    EXPECT_EQ(style.border, theme.palette().panel_border[state_index(ControlState::Normal)]);
    EXPECT_EQ(style.text, theme.palette().label_foreground[state_index(ControlState::Normal)]);
    EXPECT_FLOAT_EQ(style.corner_radius, theme.metrics().panel_radius);
    EXPECT_FLOAT_EQ(style.font_size, theme.typography().caption);
}
