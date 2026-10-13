#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

#include "core/clipboard.h"
#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "core/paint.h"
#include "core/text_range.h"

using namespace albor;

namespace
{
    // the contract test double, which holds the text in memory so a test asserts on §3.5 rather than on a platform (§3.5)
    class HoldingClipboard final : public Clipboard
    {
    public:
        // what the double carries, which a test reads back and a copy replaces (§3.5)
        std::string carried;

        // whether the double reports itself as able to copy (§3.5)
        bool usable = true;

        [[nodiscard]] std::string text() const override {
            // an unusable surface reads nothing, which is what an absent pasteboard reports (§3.5)
            if (!usable)
                return {};
            return carried;
        }

        void set_text(std::string_view text) override {
            // an unusable surface writes nothing, so a copy is dropped rather than crashing (§3.5)
            if (!usable)
                return;
            // the double owns its copy, because the caller owns the view (§3.5)
            carried = std::string(text);
        }

        [[nodiscard]] bool has_text() const override {
            // a surface that cannot be reached carries nothing, which is what a paste checks first (§3.5)
            if (!usable)
                return false;
            return !carried.empty();
        }

        [[nodiscard]] bool ready() const override {
            // the flag a test sets is the whole answer, so an unreachable surface can be exercised (§3.5)
            return usable;
        }
    };
    // the process clipboard test double, which stands in for a platform pasteboard behind the installed factory (§3.5)
    class ProcessClipboard final : public Clipboard
    {
    public:
        void set_text(std::string_view text) override {
            // the double owns its copy, because the caller owns the view (§3.5)
            carried = std::string(text);
        }

        [[nodiscard]] std::string text() const override {
            // the content is what a copy reads back (§3.5)
            return carried;
        }

        [[nodiscard]] bool has_text() const override {
            // a surface with content from none answers no (§3.5)
            return !carried.empty();
        }

        [[nodiscard]] bool ready() const override {
            // the double is always reachable, because it is a test double (§3.5)
            return true;
        }

    private:
        // what the surface carries, which a copy replaces (§3.5)
        std::string carried;
    };

} // namespace

TEST(ClipboardContractChecks, StartsWithoutAPlatformInstalled) {
    // arrange act
    const ClipboardFactory& factory = clipboard_factory();
    // assert the framework reports no surface rather than fabricating one (§3.5)
    EXPECT_FALSE(static_cast<bool>(factory));
}

TEST(ClipboardContractChecks, CreateRefusesWithoutAPlatform) {
    // arrange act
    std::unique_ptr<Clipboard> clipboard = create_clipboard();
    // assert an uninstalled platform means the framework has no way to copy (§3.5)
    EXPECT_EQ(clipboard, nullptr);
}

TEST(ClipboardContractChecks, AnInstalledPlatformIsBuilt) {
    // arrange
    set_clipboard_factory([] { return std::make_unique<HoldingClipboard>(); });
    // act
    std::unique_ptr<Clipboard> clipboard = create_clipboard();
    // assert
    ASSERT_NE(clipboard, nullptr);
    EXPECT_TRUE(clipboard->ready());
    // the factory is uninstalled again, because the next test reads the empty one (§3.5)
    set_clipboard_factory(nullptr);
}

TEST(ClipboardContractChecks, AnUninstalledFactoryIsTheEmptyOne) {
    // arrange
    set_clipboard_factory(nullptr);
    // act
    const ClipboardFactory& factory = clipboard_factory();
    // assert a removed platform leaves no factory rather than a stale one (§3.5)
    EXPECT_FALSE(static_cast<bool>(factory));
}

TEST(ClipboardContractChecks, AClipboardStartsCarryingNothing) {
    // arrange
    HoldingClipboard clipboard;
    // act
    const std::string carried = clipboard.text();
    // assert a surface nobody copied into has nothing to hand out (§3.5)
    EXPECT_TRUE(carried.empty());
    EXPECT_FALSE(clipboard.has_text());
}

TEST(ClipboardContractChecks, ACopyReplacesWhatWasThere) {
    // arrange
    HoldingClipboard clipboard;
    clipboard.carried = "before";
    // act
    clipboard.set_text("after");
    // assert a copy replaces rather than adds, which is what the pasteboard contract is (§3.5)
    EXPECT_EQ(clipboard.text(), "after");
}

TEST(ClipboardContractChecks, AnEmptyCopyClearsTheClipboard) {
    // arrange
    HoldingClipboard clipboard;
    clipboard.carried = "before";
    // act
    clipboard.set_text("");
    // assert copying nothing leaves nothing, which is what a cut of a selection then delete is (§3.5)
    EXPECT_TRUE(clipboard.text().empty());
    EXPECT_FALSE(clipboard.has_text());
}

TEST(ClipboardContractChecks, ACopyKeepsTheTextItWasGiven) {
    // arrange a source the caller owns, which the surface is handed as a view (§3.5)
    std::string source = "borrowed";
    HoldingClipboard clipboard;
    // act
    clipboard.set_text(source);
    // act again the caller drops its own copy, which is what a view means (§3.5)
    source.clear();
    // assert the copy survived the caller dropping its own copy, because the surface owns what it was handed (§3.5)
    EXPECT_EQ(clipboard.text(), "borrowed");
    EXPECT_TRUE(clipboard.has_text());
}

TEST(ClipboardContractChecks, MultiByteTextSurvivesACopy) {
    // arrange
    HoldingClipboard clipboard;
    // act
    clipboard.set_text("\xC3\xA9t\xC3\xA9");
    // assert the surface is a text surface, so two byte characters are not truncated (§3.5)
    EXPECT_EQ(clipboard.text(), "\xC3\xA9t\xC3\xA9");
}

TEST(ClipboardContractChecks, AnUnreachableSurfaceCarriesNothing) {
    // arrange
    HoldingClipboard clipboard;
    clipboard.usable = false;
    // act
    clipboard.set_text("dropped");
    // assert a surface that cannot be reached is a refusal rather than a crash (§3.5)
    EXPECT_FALSE(clipboard.ready());
    EXPECT_TRUE(clipboard.text().empty());
    EXPECT_FALSE(clipboard.has_text());
}

TEST(Size, DefaultHasNoExtent) {
    // arrange, act
    const Size size;
    // assert
    EXPECT_FLOAT_EQ(size.width, 0.0f);
    EXPECT_FLOAT_EQ(size.height, 0.0f);
}

TEST(Size, EqualityComparesBothComponents) {
    // arrange, act
    const Size first{4.0f, 8.0f};
    const Size same{4.0f, 8.0f};
    const Size other_width{4.0f, 9.0f};
    const Size other_height{5.0f, 8.0f};
    // assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other_width);
    EXPECT_NE(first, other_height);
}

TEST(Point, DefaultIsTheOrigin) {
    // arrange, act
    const Point point;
    // assert
    EXPECT_FLOAT_EQ(point.x, 0.0f);
    EXPECT_FLOAT_EQ(point.y, 0.0f);
}

TEST(Point, EqualityComparesBothComponents) {
    // arrange, act
    const Point first{1.0f, 2.0f};
    const Point same{1.0f, 2.0f};
    const Point other_x{1.0f, 3.0f};
    const Point other_y{0.0f, 2.0f};
    // assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other_x);
    EXPECT_NE(first, other_y);
}

TEST(Rect, EmptyWhenWidthIsZero) {
    // arrange, act
    const Rect rect{.width = 0.0f, .height = 10.0f};
    // assert
    EXPECT_TRUE(rect.empty());
}

TEST(Rect, EmptyWhenHeightIsZero) {
    // arrange, act
    const Rect rect{.width = 10.0f, .height = 0.0f};
    // assert
    EXPECT_TRUE(rect.empty());
}

TEST(Rect, EmptyWhenAnExtentIsNegative) {
    // arrange, act
    const Rect negative_width{.width = -1.0f, .height = 10.0f};
    const Rect negative_height{.width = 10.0f, .height = -1.0f};
    // assert
    EXPECT_TRUE(negative_width.empty());
    EXPECT_TRUE(negative_height.empty());
}

TEST(Rect, NotEmptyWithAPositiveExtent) {
    // arrange, act
    const Rect rect{.width = 1.0f, .height = 1.0f};
    // assert
    EXPECT_FALSE(rect.empty());
}

TEST(Rect, EmptinessIgnoresTheOrigin) {
    // arrange, act
    const Rect rect{.x = 100.0f, .y = 200.0f, .width = 2.0f, .height = 2.0f};
    // assert
    EXPECT_FALSE(rect.empty());
}

TEST(Rect, FromOriginSizeKeepsTheOrigin) {
    // arrange
    const Point origin{10.0f, 20.0f};
    const Size size{30.0f, 40.0f};
    // act
    const Rect rect = Rect::from_origin_size(origin, size);
    // assert
    EXPECT_FLOAT_EQ(rect.x, 10.0f);
    EXPECT_FLOAT_EQ(rect.y, 20.0f);
}

TEST(Rect, FromOriginSizeKeepsTheExtent) {
    // arrange
    const Point origin{10.0f, 20.0f};
    const Size size{30.0f, 40.0f};
    // act
    const Rect rect = Rect::from_origin_size(origin, size);
    // assert
    EXPECT_FLOAT_EQ(rect.width, 30.0f);
    EXPECT_FLOAT_EQ(rect.height, 40.0f);
}

TEST(Rect, IntersectKeepsTheOverlapOfTwoOverlappingRectangles) {
    // arrange
    const Rect outer = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f});
    const Rect inner = Rect::from_origin_size(Point{20.0f, 30.0f}, Size{100.0f, 100.0f});
    // act
    const Rect overlap = outer.intersect(inner);
    // assert the overlap starts where the inner starts and ends where the outer ends (§6.2)
    EXPECT_FLOAT_EQ(overlap.x, 20.0f);
    EXPECT_FLOAT_EQ(overlap.y, 30.0f);
    EXPECT_FLOAT_EQ(overlap.width, 80.0f);
    EXPECT_FLOAT_EQ(overlap.height, 70.0f);
}

TEST(Rect, IntersectOfDisjointRectanglesIsEmpty) {
    // arrange
    const Rect left = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f});
    const Rect right = Rect::from_origin_size(Point{50.0f, 50.0f}, Size{10.0f, 10.0f});
    // act
    const Rect overlap = left.intersect(right);
    // assert a rectangle that only touches reports no area rather than a negative extent (§6.2)
    EXPECT_TRUE(overlap.empty());
}

TEST(Rect, IntersectWithAContainerIsTheContainedRectangle) {
    // arrange
    const Rect narrow = Rect::from_origin_size(Point{10.0f, 10.0f}, Size{10.0f, 10.0f});
    const Rect wide = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f});
    // act
    const Rect overlap = narrow.intersect(wide);
    // assert a wider clip cannot widen a narrower one, which is what keeps a nested container inside its parent (§6.2)
    EXPECT_EQ(overlap, narrow);
}

TEST(Rect, FromOriginSizeWithAPositiveExtentIsNotEmpty) {
    // arrange
    const Point origin{0.0f, 0.0f};
    const Size size{5.0f, 5.0f};
    // act
    const Rect rect = Rect::from_origin_size(origin, size);
    // assert
    EXPECT_FALSE(rect.empty());
}

TEST(Rect, EqualityComparesOriginAndExtent) {
    // arrange, act
    const Rect first{1.0f, 2.0f, 3.0f, 4.0f};
    const Rect same{1.0f, 2.0f, 3.0f, 4.0f};
    const Rect other_origin{0.0f, 2.0f, 3.0f, 4.0f};
    const Rect other_extent{1.0f, 2.0f, 3.0f, 5.0f};
    // assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other_origin);
    EXPECT_NE(first, other_extent);
}

TEST(Transform, DefaultIsTheIdentity) {
    // arrange, act
    const Transform transform;
    // assert
    EXPECT_FLOAT_EQ(transform.scale_x, 1.0f);
    EXPECT_FLOAT_EQ(transform.scale_y, 1.0f);
    EXPECT_FLOAT_EQ(transform.skew_x, 0.0f);
    EXPECT_FLOAT_EQ(transform.skew_y, 0.0f);
    EXPECT_FLOAT_EQ(transform.translate_x, 0.0f);
    EXPECT_FLOAT_EQ(transform.translate_y, 0.0f);
}

TEST(Transform, DefaultLeavesEveryPointUnchanged) {
    // arrange
    const Transform transform;
    const Point point{7.0f, 9.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_EQ(mapped, (Point{7.0f, 9.0f}));
}

TEST(Transform, TranslationAddsTheOffset) {
    // arrange
    const Transform transform = Transform::translation(5.0f, -3.0f);
    const Point point{1.0f, 1.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_EQ(mapped, (Point{6.0f, -2.0f}));
}

TEST(Transform, TranslationTouchesNothingButTheOffset) {
    // arrange, act
    const Transform transform = Transform::translation(5.0f, 5.0f);
    // assert
    EXPECT_FLOAT_EQ(transform.scale_x, 1.0f);
    EXPECT_FLOAT_EQ(transform.scale_y, 1.0f);
    EXPECT_FLOAT_EQ(transform.skew_x, 0.0f);
    EXPECT_FLOAT_EQ(transform.skew_y, 0.0f);
}

TEST(Transform, ScalingMultipliesBothCoordinates) {
    // arrange
    const Transform transform = Transform::scaling(2.0f, 3.0f);
    const Point point{4.0f, 5.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_EQ(mapped, (Point{8.0f, 15.0f}));
}

TEST(Transform, ScalingTouchesNothingButTheScale) {
    // arrange, act
    const Transform transform = Transform::scaling(2.0f, 2.0f);
    // assert
    EXPECT_FLOAT_EQ(transform.translate_x, 0.0f);
    EXPECT_FLOAT_EQ(transform.translate_y, 0.0f);
    EXPECT_FLOAT_EQ(transform.skew_x, 0.0f);
    EXPECT_FLOAT_EQ(transform.skew_y, 0.0f);
}

TEST(Transform, ScalingByOneIsTheIdentity) {
    // arrange
    const Transform transform = Transform::scaling(1.0f, 1.0f);
    const Point point{11.0f, 13.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_EQ(mapped, (Point{11.0f, 13.0f}));
}

TEST(Transform, ApplyScalesBeforeItTranslates) {
    // arrange
    Transform transform = Transform::scaling(2.0f, 2.0f);
    transform.translate_x = 10.0f;
    transform.translate_y = 20.0f;
    const Point point{1.0f, 2.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_EQ(mapped, (Point{12.0f, 24.0f}));
}

TEST(Transform, ApplyAddsTheVerticalCoordinateToTheHorizontalResult) {
    // arrange
    Transform transform;
    transform.skew_x = 2.0f;
    const Point point{1.0f, 10.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_FLOAT_EQ(mapped.x, 21.0f);
}

TEST(Transform, ApplyAddsTheHorizontalCoordinateToTheVerticalResult) {
    // arrange
    Transform transform;
    transform.skew_y = 2.0f;
    const Point point{10.0f, 1.0f};
    // act
    const Point mapped = transform.apply(point);
    // assert
    EXPECT_FLOAT_EQ(mapped.y, 21.0f);
}

TEST(Transform, EqualityComparesEveryComponent) {
    // arrange, act
    const Transform first = Transform::translation(1.0f, 2.0f);
    const Transform same = Transform::translation(1.0f, 2.0f);
    const Transform other_offset = Transform::translation(1.0f, 3.0f);
    const Transform other_scale = Transform::scaling(2.0f, 1.0f);
    // assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other_offset);
    EXPECT_NE(first, other_scale);
}

TEST(Transform, ThenMapsThroughTheInnerTransformFirst) {
    // arrange
    const Transform scaled = Transform::scaling(2.0f, 2.0f);
    const Transform translated = Transform::translation(10.0f, 0.0f);
    const Point point{3.0f, 4.0f};
    // act
    const Transform composed = scaled.then(translated);
    const Point mapped = composed.apply(point);
    // assert
    EXPECT_EQ(mapped, (Point{16.0f, 8.0f}));
}

TEST(Transform, ThenMultipliesBothScales) {
    // arrange
    const Transform scaled = Transform::scaling(2.0f, 2.0f);
    // act
    const Transform twice = scaled.then(Transform::scaling(3.0f, 3.0f));
    // assert
    EXPECT_EQ(twice, Transform::scaling(6.0f, 6.0f));
}

TEST(Transform, ThenAddsBothTranslations) {
    // arrange, act
    const Transform shifted = Transform::translation(5.0f, 0.0f).then(Transform::translation(3.0f, 0.0f));
    // assert
    EXPECT_EQ(shifted, Transform::translation(8.0f, 0.0f));
}

TEST(Transform, ThenComposesSkewAndScaleInTheirOrder) {
    // arrange
    Transform skewed_inner;
    skewed_inner.scale_x = 2.0f;
    skewed_inner.skew_x = 3.0f;
    skewed_inner.scale_y = 1.0f;
    Transform skewed_outer;
    skewed_outer.skew_y = 5.0f;
    // act
    const Transform composed = skewed_inner.then(skewed_outer);
    // assert
    EXPECT_FLOAT_EQ(composed.scale_x, 2.0f);
    EXPECT_FLOAT_EQ(composed.skew_x, 3.0f);
    EXPECT_FLOAT_EQ(composed.skew_y, 10.0f);
    EXPECT_FLOAT_EQ(composed.scale_y, 16.0f);
    EXPECT_EQ(composed.apply(Point{1.0f, 1.0f}), (Point{5.0f, 26.0f}));
}

TEST(Color, DefaultIsOpaqueBlack) {
    // arrange, act
    const Color color;
    // assert
    EXPECT_FLOAT_EQ(color.red, 0.0f);
    EXPECT_FLOAT_EQ(color.green, 0.0f);
    EXPECT_FLOAT_EQ(color.blue, 0.0f);
    EXPECT_FLOAT_EQ(color.alpha, 1.0f);
}

TEST(Color, RgbIsAlwaysOpaque) {
    // arrange, act
    const Color color = Color::rgb(0.25f, 0.5f, 0.75f);
    // assert
    EXPECT_FLOAT_EQ(color.alpha, 1.0f);
}

TEST(Color, RgbKeepsItsThreeComponents) {
    // arrange, act
    const Color color = Color::rgb(0.25f, 0.5f, 0.75f);
    // assert
    EXPECT_FLOAT_EQ(color.red, 0.25f);
    EXPECT_FLOAT_EQ(color.green, 0.5f);
    EXPECT_FLOAT_EQ(color.blue, 0.75f);
}

TEST(Color, RgbaKeepsItsAlpha) {
    // arrange, act
    const Color color = Color::rgba(0.1f, 0.2f, 0.3f, 0.4f);
    // assert
    EXPECT_FLOAT_EQ(color.alpha, 0.4f);
    EXPECT_FLOAT_EQ(color.red, 0.1f);
    EXPECT_FLOAT_EQ(color.green, 0.2f);
    EXPECT_FLOAT_EQ(color.blue, 0.3f);
}

TEST(Color, ClampsComponentsOutsideTheRange) {
    // arrange, act
    const Color color = Color::rgba(1.5f, -0.5f, 2.0f, 3.0f);
    // assert
    EXPECT_FLOAT_EQ(color.red, 1.0f);
    EXPECT_FLOAT_EQ(color.green, 0.0f);
    EXPECT_FLOAT_EQ(color.blue, 1.0f);
    EXPECT_FLOAT_EQ(color.alpha, 1.0f);
}

TEST(Color, ClampsAnOversizedChannel) {
    // arrange, act
    const Color color = Color::rgb(-3.0f, 0.5f, 9.0f);
    // assert
    EXPECT_FLOAT_EQ(color.red, 0.0f);
    EXPECT_FLOAT_EQ(color.green, 0.5f);
    EXPECT_FLOAT_EQ(color.blue, 1.0f);
    EXPECT_FLOAT_EQ(color.alpha, 1.0f);
}

TEST(Color, KeepsComponentsInsideTheRange) {
    // arrange, act
    const Color color = Color::rgba(0.25f, 0.5f, 0.75f, 0.5f);
    // assert
    EXPECT_FLOAT_EQ(color.red, 0.25f);
    EXPECT_FLOAT_EQ(color.green, 0.5f);
    EXPECT_FLOAT_EQ(color.blue, 0.75f);
    EXPECT_FLOAT_EQ(color.alpha, 0.5f);
}

TEST(Color, EqualityComparesEveryComponent) {
    // arrange, act
    const Color first = Color::rgb(1.0f, 0.0f, 0.0f);
    const Color same = Color::rgb(1.0f, 0.0f, 0.0f);
    const Color other_alpha = Color::rgba(1.0f, 0.0f, 0.0f, 0.5f);
    const Color other_channel = Color::rgb(0.0f, 1.0f, 0.0f);
    // assert
    EXPECT_EQ(first, same);
    EXPECT_NE(first, other_alpha);
    EXPECT_NE(first, other_channel);
}

TEST(Color, WithAlphaScalesTheExistingAlpha) {
    // arrange a colour that is already part transparent, which is what a nested fade produces (§6.3)
    const Color base = Color::rgba(0.2f, 0.4f, 0.6f, 0.5f);
    // act
    const Color dimmed = base.with_alpha(0.5f);
    // assert the alpha moves to a quarter, because coverage multiplies rather than replaces (§6.3)
    EXPECT_FLOAT_EQ(dimmed.alpha, 0.25f);
    // assert the hue is untouched, because an opacity dims coverage rather than colour (§6.3)
    EXPECT_FLOAT_EQ(dimmed.red, base.red);
    EXPECT_FLOAT_EQ(dimmed.green, base.green);
    EXPECT_FLOAT_EQ(dimmed.blue, base.blue);
}

TEST(Color, AlphaSetReplacesTheAlphaOutright) {
    // arrange a half transparent colour, which is what an explicit opacity overrides (§6.3)
    const Color base = Color::rgba(0.2f, 0.4f, 0.6f, 0.5f);
    // act
    const Color opaque = base.alpha_set(0.8f);
    // assert the alpha is the one asked for rather than a product, because a replacement is not a scale (§6.3)
    EXPECT_FLOAT_EQ(opaque.alpha, 0.8f);
    EXPECT_FLOAT_EQ(opaque.red, base.red);
}

TEST(Color, AlphaSetClampsTheValueItIsGiven) {
    // arrange, act
    const Color base = Color::rgb(0.2f, 0.4f, 0.6f);
    // assert a value from a binding that overshot is clamped rather than stored (§6.3)
    EXPECT_FLOAT_EQ(base.alpha_set(2.0f).alpha, 1.0f);
    EXPECT_FLOAT_EQ(base.alpha_set(-1.0f).alpha, 0.0f);
}

TEST(Color, TransparentizedMovesTheAlphaTowardZero) {
    // arrange an opaque colour, which is what a theme entry usually is (§6.3)
    const Color base = Color::rgb(0.2f, 0.4f, 0.6f);
    // act
    const Color faded = base.transparentized(0.25f);
    // assert the alpha drops by the fraction, which is what a de-emphasis tint is (§6.3)
    EXPECT_FLOAT_EQ(faded.alpha, 0.75f);
    EXPECT_FLOAT_EQ(faded.red, base.red);
}

TEST(Color, TransparentizedAtAFullFractionClearsTheAlpha) {
    // arrange, act
    const Color faded = Color::rgb(0.2f, 0.4f, 0.6f).transparentized(1.0f);
    // assert a full fraction leaves nothing visible, which is the point of the operation (§6.3)
    EXPECT_FLOAT_EQ(faded.alpha, 0.0f);
}

TEST(Color, TransparentizedClampsItsFraction) {
    // arrange, act
    const Color base = Color::rgb(0.2f, 0.4f, 0.6f);
    // assert a fraction from a binding that overshot is clamped into the unit range (§6.3)
    EXPECT_FLOAT_EQ(base.transparentized(3.0f).alpha, 0.0f);
    EXPECT_FLOAT_EQ(base.transparentized(-1.0f).alpha, 1.0f);
}

TEST(Paint, DefaultFillsWithOpaqueBlack) {
    // arrange, act
    const Paint paint;
    // assert
    EXPECT_TRUE(paint.fills());
}

TEST(Paint, DefaultDoesNotStroke) {
    // arrange, act
    const Paint paint;
    // assert
    EXPECT_FALSE(paint.strokes());
}

TEST(Paint, FullyTransparentFillDoesNotFill) {
    // arrange
    Paint paint;
    paint.fill = Color::rgba(1.0f, 1.0f, 1.0f, 0.0f);
    // act, assert
    EXPECT_FALSE(paint.fills());
}

TEST(Paint, VisibleStrokeWithAPositiveWidthStrokes) {
    // arrange
    Paint paint;
    paint.stroke = Color::rgb(1.0f, 0.0f, 0.0f);
    paint.stroke_width = 2.0f;
    // act, assert
    EXPECT_TRUE(paint.strokes());
}

TEST(Paint, VisibleStrokeWithNoWidthDoesNotStroke) {
    // arrange
    Paint paint;
    paint.stroke = Color::rgb(1.0f, 0.0f, 0.0f);
    paint.stroke_width = 0.0f;
    // act, assert
    EXPECT_FALSE(paint.strokes());
}

TEST(Paint, TransparentStrokeDoesNotStroke) {
    // arrange
    Paint paint;
    paint.stroke = Color::rgba(1.0f, 0.0f, 0.0f, 0.0f);
    paint.stroke_width = 3.0f;
    // act, assert
    EXPECT_FALSE(paint.strokes());
}

TEST(Paint, EqualityComparesEveryComponent) {
    // arrange
    const Paint plain;
    const Paint same;
    Paint widened;
    widened.stroke_width = 4.0f;
    // act, assert
    EXPECT_EQ(plain, same);
    EXPECT_NE(plain, widened);
}

TEST(StrokeStyle, DefaultIsOpaqueBlackOnePixelWide) {
    // arrange, act
    const StrokeStyle style;
    // assert
    EXPECT_EQ(style.color, Color::rgb(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(style.width, 1.0f);
}

TEST(NativeWindowHandle, DefaultIsInvalid) {
    // arrange, act
    const NativeWindowHandle handle;
    // assert
    EXPECT_FALSE(handle.valid());
}

TEST(NativeWindowHandle, DefaultWrapsNoObject) {
    // arrange, act
    const NativeWindowHandle handle;
    // assert
    EXPECT_EQ(handle.object(), nullptr);
}

TEST(NativeWindowHandle, WrappingAnObjectIsValid) {
    // arrange
    int layer = 0;
    // act
    const NativeWindowHandle handle = NativeWindowHandle::from_object(&layer);
    // assert
    EXPECT_TRUE(handle.valid());
}

TEST(NativeWindowHandle, HandsBackTheVerySameObject) {
    // arrange
    int layer = 0;
    // act
    const NativeWindowHandle handle = NativeWindowHandle::from_object(&layer);
    // assert
    EXPECT_EQ(handle.object(), static_cast<const void*>(&layer));
}

TEST(NativeWindowHandle, WrappingNullIsInvalid) {
    // arrange, act
    const NativeWindowHandle handle = NativeWindowHandle::from_object(nullptr);
    // assert
    EXPECT_FALSE(handle.valid());
}

TEST(NativeWindowHandle, EqualityComparesTheWrappedObject) {
    // arrange
    int layer = 0;
    // act
    const NativeWindowHandle first = NativeWindowHandle::from_object(&layer);
    const NativeWindowHandle same = NativeWindowHandle::from_object(&layer);
    // assert
    EXPECT_EQ(first, same);
}

TEST(NativeWindowHandle, DifferentObjectsAreDifferentHandles) {
    // arrange
    int first_layer = 0;
    int second_layer = 0;
    // act
    const NativeWindowHandle first = NativeWindowHandle::from_object(&first_layer);
    const NativeWindowHandle second = NativeWindowHandle::from_object(&second_layer);
    // assert
    EXPECT_NE(first, second);
}

TEST(NativeWindowHandle, DefaultEqualsAHandleWrappingNull) {
    // arrange, act
    const NativeWindowHandle empty;
    const NativeWindowHandle null_object = NativeWindowHandle::from_object(nullptr);
    // assert
    EXPECT_EQ(empty, null_object);
}

TEST(ClipboardContractChecks, TheProcessClipboardIsBuiltOnceAndKept) {
    // arrange a factory that counts how many clipboards it built (§3.5)
    auto built = std::make_shared<std::size_t>(0);
    set_clipboard_factory([built] {
        // the count rises per build, which is what the reuse assertion reads (§3.5)
        (*built)++;
        return std::make_unique<ProcessClipboard>();
    });
    // act the process clipboard is asked for twice
    Clipboard* first = process_clipboard();
    Clipboard* second = process_clipboard();
    // assert the same surface came back, because a pasteboard is a resource to hold rather than to churn (§3.5)
    EXPECT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(*built, 1u);
    // act again the platform installs a different clipboard
    set_clipboard_factory(nullptr);
}

TEST(ClipboardContractChecks, TheProcessClipboardIsNullWithoutAPlatform) {
    // arrange no installed factory, which is what a build without an adapter has (§3.5)
    set_clipboard_factory(nullptr);
    // act assert
    EXPECT_EQ(process_clipboard(), nullptr);
}

TEST(TextRangeChecks, AnAsciiDocumentAddressesItselfInUtf16Units) {
    // arrange a plain ascii document, where both encodings count the same (§8.3)
    const std::string text = "abc";
    // act assert
    EXPECT_EQ(utf16_offset_for_byte(text, 0), 0u);
    EXPECT_EQ(utf16_offset_for_byte(text, 2), 2u);
    EXPECT_EQ(utf16_length(text), 3u);
    EXPECT_EQ(byte_for_utf16_offset(text, 2), 2u);
}

TEST(TextRangeChecks, AMultiByteCharacterCountsAsOneOrMoreUtf16Units) {
    // arrange a document holding two and three byte characters (§8.3)
    const std::string text = "\xC3\xA9\xE6\x97\xA5";
    // act assert the two byte character is two bytes and one unit, and the three byte one is three and one (§8.3)
    EXPECT_EQ(utf16_offset_for_byte(text, 0), 0u);
    EXPECT_EQ(utf16_offset_for_byte(text, 2), 1u);
    EXPECT_EQ(utf16_offset_for_byte(text, 5), 2u);
    EXPECT_EQ(utf16_length(text), 2u);
}

TEST(TextRangeChecks, AFourByteCharacterCountsAsTwoUtf16Units) {
    // arrange a document holding one astral character, which utf16 spells as a surrogate pair (§8.3)
    const std::string text = "\xF0\x9F\x98\x80";
    // act assert
    EXPECT_EQ(utf16_length(text), 2u);
    EXPECT_EQ(utf16_offset_for_byte(text, 4), 2u);
    EXPECT_EQ(byte_for_utf16_offset(text, 2), 4u);
}

TEST(TextRangeChecks, AnOffsetInsideACharacterClampsToItsStart) {
    // arrange a document whose second character is two bytes wide in utf8 (§8.3)
    const std::string text = "a\u00E9b";
    // act assert an offset inside the character lands on it, because a caret cannot stand inside (§8.3)
    EXPECT_EQ(byte_for_utf16_offset(text, 1), 1u);
    EXPECT_EQ(byte_for_utf16_offset(text, 0), 0u);
    EXPECT_EQ(byte_for_utf16_offset(text, 99), 4u);
}

TEST(TextRangeChecks, AnOffsetPastTheEndLandsAtTheEnd) {
    // arrange a short document and an offset beyond it (§8.3)
    const std::string text = "abc";
    // act assert
    EXPECT_EQ(byte_for_utf16_offset(text, 50), 3u);
    EXPECT_EQ(utf16_offset_for_byte(text, 50), 3u);
}
