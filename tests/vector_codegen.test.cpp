#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "record_panel.h"
#include "runtime/binding_engine.h"
#include "scroll_log.h"
#include "task_list.h"
#include "text/text_engine.h"
#include "ui/scene_builder.h"
#include "vector_panel.h"
#include "window/window_metrics.h"

using namespace albor;

TEST(VectorCodegen, ADecimalListBecomesTheDeclaredCollection) {
    // arrange a panel over the engines its body reads, which is what an application constructs (§11.3)
    BindingEngine engine;
    TextEngine* text_engine = nullptr;
    VectorPanel panel(&engine, text_engine);
    // act
    const std::vector<int>& numbers = panel.numbers();
    // assert the written literal reached the property, which is what the facade presents (§11.3)
    ASSERT_EQ(numbers.size(), 3u);
    EXPECT_EQ(numbers.at(0), 1);
    EXPECT_EQ(numbers.at(2), 3);
}

TEST(VectorCodegen, ATextListKeepsItsWrittenOrder) {
    // arrange
    BindingEngine engine;
    VectorPanel panel(&engine, nullptr);
    // act
    const std::vector<std::string>& titles = panel.titles();
    // assert the order written is the order kept, because a collection preserves it (§8.1)
    ASSERT_EQ(titles.size(), 2u);
    EXPECT_EQ(titles.at(0), "one");
    EXPECT_EQ(titles.at(1), "two");
}

TEST(VectorCodegen, AnEmptyListIsAnEmptyCollection) {
    // arrange
    BindingEngine engine;
    VectorPanel panel(&engine, nullptr);
    // act
    const std::vector<int>& nothing = panel.nothing();
    // assert the declared element type survives value initialisation, which is what the target supplies (§8.1)
    EXPECT_TRUE(nothing.empty());
}

TEST(VectorCodegen, ANestedListKeepsItsInnerCollections) {
    // arrange
    BindingEngine engine;
    VectorPanel panel(&engine, nullptr);
    // act
    const std::vector<std::vector<int>>& grid = panel.grid();
    // assert each inner list is a collection in its own right, which is what nesting means (§8.1)
    ASSERT_EQ(grid.size(), 2u);
    ASSERT_EQ(grid.at(0).size(), 2u);
    EXPECT_EQ(grid.at(0).at(1), 2);
    EXPECT_EQ(grid.at(1).at(0), 3);
}

TEST(VectorCodegen, ACollectionIsSettableFromTheHost) {
    // arrange
    BindingEngine engine;
    VectorPanel panel(&engine, nullptr);
    // act the host replaces the collection, which is the imperative write path (§7.1, AD-24)
    panel.set_numbers(std::vector<int>{7, 8});
    // assert the read reflects the write, because the accessor goes through the property (§14.1)
    ASSERT_EQ(panel.numbers().size(), 2u);
    EXPECT_EQ(panel.numbers().at(0), 7);
}

TEST(VectorCodegen, ARecordCollectionCarriesTheGeneratedStruct) {
    // arrange a panel over the engines its body reads, which is what an application constructs (§11.3)
    BindingEngine engine;
    RecordPanel panel(&engine, nullptr);
    // act the host fills the collection with the generated record type (§11.2, AD-17)
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}});
    // assert the record survived the round trip, which is what the manifest declared (§11.2, AD-17)
    ASSERT_EQ(panel.tasks().size(), 1U);
    EXPECT_EQ(panel.tasks().at(0).title, "first");
    // assert the manifest default survived, because the glue generator re-emits it (§11.2)
    EXPECT_FALSE(panel.tasks().at(0).done);
}

TEST(VectorCodegen, AKeyedRecordKeepsItsIdentityAcrossAWrite) {
    // arrange
    BindingEngine engine;
    RecordPanel panel(&engine, nullptr);
    // act two rows are written, which is what a repeater would materialize (§18.1, AD-13)
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "one", .done = false}, Task{.key = "b", .title = "two", .done = true}});
    // act one row changes, which is what a later keyed reconciliation has to match by identity (§18.1, AD-13)
    panel.set_tasks(std::vector<Task>{Task{.key = "b", .title = "two", .done = true}, Task{.key = "a", .title = "one", .done = false}});
    // assert the keys moved with their rows, which is what identity means (§18.1, AD-11, AD-13)
    ASSERT_EQ(panel.tasks().size(), 2U);
    EXPECT_EQ(panel.tasks().at(0).key, "b");
    EXPECT_EQ(panel.tasks().at(1).key, "a");
}

TEST(VectorCodegen, ANestedRecordCollectionCarriesItsStructs) {
    // arrange
    BindingEngine engine;
    RecordPanel panel(&engine, nullptr);
    // act a group of tasks, which is what a nested collection holds (§8.1)
    panel.set_grouped(std::vector<std::vector<Task>>{{Task{.key = "a", .title = "only", .done = false}}});
    // assert each level is its own collection, which is what nesting means (§8.1)
    ASSERT_EQ(panel.grouped().size(), 1U);
    ASSERT_EQ(panel.grouped().at(0).size(), 1U);
    EXPECT_EQ(panel.grouped().at(0).at(0).title, "only");
}

TEST(RepeatCodegen, ARepeatMaterializesOneRowPerItem) {
    // arrange a list over the engines its body reads, which is what an application constructs (§18.1, AD-13)
    BindingEngine engine;
    TaskList list(&engine, nullptr);
    // act the host writes two tasks, which the repeat then materializes (§18.1, AD-13)
    list.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}});
    // act the engine settles, because a write marks the repeat dirty rather than running it (§7.3)
    engine.settle();
    // assert the column carries the two rows plus the siblings written around the repeat (§8.1, §18.1)
    ASSERT_EQ(static_cast<UiNode&>(list).child_at(0)->child_count(), 4U);
}

TEST(RepeatCodegen, ARepeatKeepsItsRowsInItemOrder) {
    // arrange
    BindingEngine engine;
    TaskList list(&engine, nullptr);
    // act three tasks written in a known order (§18.1)
    list.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}, Task{.key = "c", .title = "third", .done = false}});
    // act the engine settles, because a write marks the repeat dirty rather than running it (§7.3)
    engine.settle();
    // assert the rows follow the item order, because the parent order is the item order (§8.1, §18.1)
    ASSERT_EQ(static_cast<UiNode&>(list).child_at(0)->child_count(), 5U);
}

TEST(RepeatCodegen, ARemovedItemRemovesOnlyItsRow) {
    // arrange
    BindingEngine engine;
    TaskList list(&engine, nullptr);
    // act three tasks are shown
    list.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}, Task{.key = "c", .title = "third", .done = false}});
    // act the engine settles, because a write marks the repeat dirty rather than running it (§7.3)
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(list).child_at(0)->child_count(), 5U);
    // act the middle task is removed, which is the case a positional loop gets wrong (§18.1, AD-13)
    list.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "c", .title = "third", .done = false}});
    // act the engine settles, because a write marks the repeat dirty rather than running it (§7.3)
    engine.settle();
    // assert only that row went, because a reconcile matches by identity and not by position (§18.1, AD-13)
    EXPECT_EQ(static_cast<UiNode&>(list).child_at(0)->child_count(), 4U);
}

TEST(RepeatCodegen, AnEmptySourceMaterializesNoRows) {
    // arrange
    BindingEngine engine;
    TaskList list(&engine, nullptr);
    // act the source is emptied, which leaves only the siblings written around the repeat (§18.1)
    list.set_tasks(std::vector<Task>{});
    // act the engine settles, because a write marks the repeat dirty rather than running it (§7.3)
    engine.settle();
    // assert the two siblings survive, because a repeat owns no row of its own (§8.1, §18.1)
    EXPECT_EQ(static_cast<UiNode&>(list).child_at(0)->child_count(), 2U);
}

TEST(RepeatCodegen, ARowSurvivesItsItemMoving) {
    // arrange a list whose rows were materialized once, which is what a filled list is (§18.1, AD-13)
    BindingEngine engine;
    TaskList list(&engine, nullptr);
    // act three tasks, whose rows are then shown (§18.1)
    list.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}, Task{.key = "c", .title = "third", .done = false}});
    // act the engine settles, because a write marks the repeat dirty rather than running it (§7.3)
    engine.settle();
    // act the first row is recorded, which is what the move must preserve (§18.1, AD-13)
    UiNode* first = static_cast<UiNode&>(list).child_at(0)->child_at(1);
    // act the order is reversed, which is the case that separates identity from position (§18.1, AD-13)
    list.set_tasks(std::vector<Task>{Task{.key = "c", .title = "third", .done = false}, Task{.key = "b", .title = "second", .done = false}, Task{.key = "a", .title = "first", .done = false}});
    // act
    engine.settle();
    // assert the same node now shows the last item, because the row moved rather than being rebuilt (§18.1, AD-13)
    EXPECT_EQ(static_cast<UiNode&>(list).child_at(0)->child_at(3), first);
}

namespace
{
    // the text engine the generated labels measure through, which stands in for a font backend (§9.1)
    class CountingTextEngine final : public TextEngine
    {
    public:
        // the advance one character contributes, in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // the height the double reports for any run, in logical pixels (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // how many runs were measured, which is what proves the rows were measured at all (§9.1)
        mutable std::size_t measurements = 0;

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, 16.0f);
        }

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // an empty run carries no glyphs, so the double measures it as nothing (§9.1)
            if (text.empty())
                return TextLayout{};
            // the measurement is counted, because a row nobody measured never reaches the engine (§9.1)
            measurements++;
            // the double shapes one glyph per character, which stands in for shaping (§9.1)
            ShapedRun::Line line;
            // the line origin is the baseline the synthesized glyphs hang from (§9.1)
            line.origin = Point{.x = 0.0f, .y = HEIGHT * 0.8f};
            // every character becomes one glyph placed a fixed advance apart (§9.1)
            for (const char character : text)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(character), .position = Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}});
            // the advance counts characters, and the ceiling clamps the result as a container expects (§8.2)
            const float advanced = static_cast<float>(text.size()) * ADVANCE;
            // the shaped run travels beside the extents, which is what a measurement hands over (§9.1)
            return TextLayout{.run = ShapedRun::make({std::move(line)}, font_size), .size = Size{.width = max_width > 0.0f ? std::min(advanced, max_width) : advanced, .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        [[nodiscard]] bool ready() const override { return true; }
    };

    // a hundred keyed rows, which is the scale the window exists to bound (§8.2, AD-13)
    std::vector<Task> many_tasks() {
        // the rows are numbered, so every key is distinct (§18.1, AD-13)
        std::vector<Task> tasks;
        // the list is reserved, because a scroll view is about lists this size (§8.2, AD-13)
        tasks.reserve(100);
        for (std::size_t index = 0; index < 100; index++)
            tasks.push_back(Task{.key = "task" + std::to_string(index), .title = "line " + std::to_string(index)});
        return tasks;
    }
} // namespace

TEST(VectorCodegen, AVirtualizedLogBuildsOnlyItsWindowAndDrawsIt) {
    // arrange
    BindingEngine engine;
    CountingTextEngine text;
    ScrollLog log(&engine, &text);
    log.set_line_height(20.0f);
    log.set_tasks(many_tasks());
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    // act the stage settles the graph and then records, which is the frame path of §6.1
    (void)engine.settle();
    SceneBuilder builder;
    const std::shared_ptr<const FrameScene> scene = builder.build(log, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert the whole list was not built, which is the whole point of the window (§8.2, AD-13)
    EXPECT_GT(text.measurements, 0u);
    EXPECT_LT(text.measurements, 100u);
    // assert the built rows reached the scene, which is what a measured row draws from (§6.1, §9.1)
    std::size_t text_commands = 0;
    for (const DrawCommand& command : scene->commands())
        if (command.kind() == DrawCommandKind::Text)
            text_commands++;
    EXPECT_GT(text_commands, 0u);
}

TEST(VectorCodegen, AVirtualizedLogSurvivesAScrollThatRebuildsTheWindow) {
    // arrange
    BindingEngine engine;
    CountingTextEngine text;
    ScrollLog log(&engine, &text);
    log.set_line_height(20.0f);
    log.set_tasks(many_tasks());
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    SceneBuilder builder;
    (void)engine.settle();
    (void)builder.build(log, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    const std::size_t measured_first = text.measurements;
    // act the log settles again, which re-evaluates the source and rebuilds the window (§7.3, §8.2)
    log.set_line_height(21.0f);
    (void)engine.settle();
    (void)builder.build(log, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert rows were measured again, which is what a rebuilt window needs (§8.2, AD-13)
    EXPECT_GT(text.measurements, measured_first);
}
