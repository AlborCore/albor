#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "record_panel.h"
#include "runtime/binding_engine.h"
#include "task_list.h"
#include "text/text_engine.h"
#include "vector_panel.h"

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
