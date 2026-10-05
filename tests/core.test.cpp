#include <gtest/gtest.h>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "core/paint.h"

using namespace albor;

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

TEST(Color, CarriesComponentsOutsideTheRangeUnchanged) {
    // arrange, act
    const Color color = Color::rgba(1.5f, -0.5f, 2.0f, 3.0f);
    // assert
    EXPECT_FLOAT_EQ(color.red, 1.5f);
    EXPECT_FLOAT_EQ(color.green, -0.5f);
    EXPECT_FLOAT_EQ(color.blue, 2.0f);
    EXPECT_FLOAT_EQ(color.alpha, 3.0f);
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
