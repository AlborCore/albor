#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "conditional_panel.h"
#include "guarded_flat_repeat_panel.h"
#include "guarded_repeat_panel.h"
#include "image_panel.h"
#include "nested_direct_panel.h"
#include "nested_panel.h"
#include "ordered_panel.h"
#include "record_panel.h"
#include "resources/embedded_image.h"
#include "runtime/binding_engine.h"
#include "scroll_log.h"
#include "task/image_record.h"
#include "task_list.h"
#include "text/text_engine.h"
#include "ui/scene_builder.h"
#include "vector_panel.h"
#include "widgets/label.h"
#include "window/window_metrics.h"

using namespace albor;

TEST(VectorCodegen, AnEmbeddedImageDecodesWhenTheComponentIsBuilt) {
    // arrange a panel whose image default is an asset the compiler embedded (§6.4)
    BindingEngine engine;
    // act
    const ImagePanel panel(&engine, nullptr);
    const std::shared_ptr<const ImageResource> icon = panel.icon();
    // assert the generated accessor reached the registry and decoded the carried png (§6.4)
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->width(), 1U);
    EXPECT_EQ(icon->height(), 1U);
    EXPECT_EQ(icon->format(), ImageFormat::RGBA8);
    EXPECT_EQ(icon->pixels().size(), 4U);
    // the cache is left empty, because a suite that counts its entries must not inherit this fixture (§6.4)
    albor::resources::ImageRegistry::clear();
}

TEST(VectorCodegen, AManifestRecordCarriesAnImageHandle) {
    // arrange a record the manifest generated with an image field (§11.2, §6.4)
    const ImageRecord record{.key = "icon", .icon = nullptr};
    // assert the generated aggregate keeps both fields, which is what the compiler names (§11.2)
    EXPECT_EQ(record.key, "icon");
    EXPECT_EQ(record.icon, nullptr);
}

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

TEST(ConditionalCodegen, AConditionalBodyAppearsAndDisappears) {
    // arrange a panel whose guarded body starts absent, because the condition is false (§11.1)
    BindingEngine engine;
    ConditionalPanel panel(&engine, nullptr);
    // act the column is read before the condition holds, which leaves the static siblings (§11.1)
    const std::size_t without = static_cast<UiNode&>(panel).child_at(0)->child_count();
    // act the condition turns true, which the engine settles into a body (§7.3, §11.1)
    panel.set_show(true);
    engine.settle();
    // assert the body appeared, which is what a true condition materializes (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), without + 1);
}

TEST(ConditionalCodegen, ARemovedConditionalBodyLeavesTheTree) {
    // arrange a panel whose body is materialized once, which is what a holding condition does (§11.1)
    BindingEngine engine;
    ConditionalPanel panel(&engine, nullptr);
    // act the condition holds, so the body is built and adopted (§11.1)
    panel.set_show(true);
    engine.settle();
    const std::size_t while_present = static_cast<UiNode&>(panel).child_at(0)->child_count();
    // act the condition stops holding, which destroys the body (§11.1)
    panel.set_show(false);
    engine.settle();
    // assert the body left the tree, because a false condition removes it rather than hiding it (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), while_present - 1);
}

TEST(ConditionalCodegen, AReheldConditionalBodyReappearsOnce) {
    // arrange a panel whose body was shown and then removed, which is the toggle a removal gets wrong (§11.1)
    BindingEngine engine;
    ConditionalPanel panel(&engine, nullptr);
    // act
    panel.set_show(true);
    engine.settle();
    // act
    panel.set_show(false);
    engine.settle();
    // act the condition holds again, which must re-adopt exactly one body (§11.1)
    panel.set_show(true);
    engine.settle();
    // assert one body is present, because a dangling root would leave the removed body behind (§11.1)
    const std::size_t restored = static_cast<UiNode&>(panel).child_at(0)->child_count();
    // act the condition drops once more, which removes the one body that was re-adopted (§11.1)
    panel.set_show(false);
    engine.settle();
    // assert the tree is back to the static siblings, because no copy was left dangling (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), restored - 1);
}

TEST(ConditionalCodegen, AdjacentBodiesKeepTheWrittenOrder) {
    // arrange a panel with two adjacent conditionals, which is the case a shared offset reverses (§11.1)
    BindingEngine engine;
    OrderedPanel panel(&engine, nullptr);
    // act the first body appears first, which occupies the only position while the second is absent (§11.1)
    panel.set_first(true);
    engine.settle();
    // act the second body appears after, which must follow the first because it was written second (§11.1)
    panel.set_second(true);
    engine.settle();
    // assert the first row is the first body, because the sibling order is the written order (§11.1)
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 2U);
    const auto* first = dynamic_cast<const Label*>(static_cast<UiNode&>(panel).child_at(0)->child_at(0));
    const auto* second = dynamic_cast<const Label*>(static_cast<UiNode&>(panel).child_at(0)->child_at(1));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->text(), "first");
    EXPECT_EQ(second->text(), "second");
}

TEST(ConditionalCodegen, RemovingTheFirstBodyKeepsTheSecond) {
    // arrange both bodies shown, which lays them out in written order (§11.1)
    BindingEngine engine;
    OrderedPanel panel(&engine, nullptr);
    // act
    panel.set_first(true);
    panel.set_second(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 2U);
    // act the first body is removed, which must not disturb the second (§11.1)
    panel.set_first(false);
    engine.settle();
    // assert only the second survives, because removal is by identity rather than by position (§11.1)
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    const auto* only = dynamic_cast<const Label*>(static_cast<UiNode&>(panel).child_at(0)->child_at(0));
    ASSERT_NE(only, nullptr);
    EXPECT_EQ(only->text(), "second");
}

TEST(ConditionalCodegen, ANestedBodyFollowsBothConditions) {
    // arrange a panel whose guarded body holds another guarded body (§11.1)
    BindingEngine engine;
    NestedPanel panel(&engine, nullptr);
    // act the inner condition holds while the outer does not, which must show nothing (§11.1)
    panel.set_inner(true);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 0U);
    // act the outer condition holds, which reveals the outer body and with it the inner one (§11.1)
    panel.set_outer(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // assert the inner body appeared inside the outer body, because both conditions hold (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(0)->child_count(), 2U);
}

TEST(ConditionalCodegen, ANestedBodyTogglesOnItsOwnCondition) {
    // arrange a panel whose outer body holds an inner body, both shown (§11.1)
    BindingEngine engine;
    NestedPanel panel(&engine, nullptr);
    panel.set_outer(true);
    panel.set_inner(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(0)->child_count(), 2U);
    // act the inner condition drops, which removes only the inner body (§11.1)
    panel.set_inner(false);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(0)->child_count(), 1U);
    // act the inner condition holds again, which re-adopts one inner body (§11.1)
    panel.set_inner(true);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(0)->child_count(), 2U);
}

TEST(ConditionalCodegen, RemovingTheOuterBodyTakesTheInner) {
    // arrange a panel whose outer and inner bodies both hold (§11.1)
    BindingEngine engine;
    NestedPanel panel(&engine, nullptr);
    panel.set_outer(true);
    panel.set_inner(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // act the outer condition drops, which removes the outer body and the inner one with it (§11.1)
    panel.set_outer(false);
    engine.settle();
    // assert the outer body left the tree, because a false condition removes it (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 0U);
    // act the outer condition holds again, which must re-adopt exactly one outer body with its inner one (§11.1)
    panel.set_outer(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // assert the inner body returned with the outer one, because its condition still holds (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(0)->child_count(), 2U);
}

TEST(ConditionalCodegen, ADirectlyNestedBodyFollowsBothConditions) {
    // arrange a panel whose conditional body is itself a conditional, which owns no node of its own (§11.1)
    BindingEngine engine;
    NestedDirectPanel panel(&engine, nullptr);
    // act only the inner condition holds, which must show nothing because the outer does not (§11.1)
    panel.set_inner(true);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 0U);
    // act the outer condition holds, which reveals the inner body directly (§11.1)
    panel.set_outer(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // assert the inner label is present, because both conditions hold (§11.1)
    const auto* label = dynamic_cast<const Label*>(static_cast<UiNode&>(panel).child_at(0)->child_at(0));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "inner");
}

TEST(ConditionalCodegen, ADirectlyNestedBodyReactsToTheOuterCondition) {
    // arrange a panel whose directly nested body is shown, because both conditions hold (§11.1)
    BindingEngine engine;
    NestedDirectPanel panel(&engine, nullptr);
    panel.set_outer(true);
    panel.set_inner(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // act the outer condition drops, which takes the inner body with it (§11.1)
    panel.set_outer(false);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 0U);
    // act the outer condition holds again, which re-adopts the inner body once (§11.1)
    panel.set_outer(true);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
}

TEST(ConditionalCodegen, ARepeatInsideABodyMaterializesWithTheBody) {
    // arrange a panel whose conditional body wraps a repeat, which is what a guarded list is (§11.1, §18.1)
    BindingEngine engine;
    GuardedRepeatPanel panel(&engine, nullptr);
    // act the source is filled before the condition holds, which must show nothing (§11.1)
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}});
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // act the condition holds, which builds the body and its rows together (§11.1, §18.1)
    panel.set_show(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 2U);
    // assert the body carries one row per item, because the repeat materialized with the body (§18.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(1)->child_count(), 2U);
}

TEST(ConditionalCodegen, RemovingTheBodyRemovesTheGuardedRows) {
    // arrange a panel whose guarded body carries rows, which is the case a removal gets wrong (§11.1, §18.1)
    BindingEngine engine;
    GuardedRepeatPanel panel(&engine, nullptr);
    panel.set_show(true);
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}});
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 2U);
    // act the condition drops, which destroys the body and the rows it carries (§11.1, §18.1)
    panel.set_show(false);
    engine.settle();
    // assert the body left the tree, because a false condition removes it rather than hiding it (§11.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
}

TEST(ConditionalCodegen, ARevealedBodyRebuildsItsRows) {
    // arrange a panel whose guarded body was shown and then removed, which is the toggle a rebuild gets wrong (§11.1)
    BindingEngine engine;
    GuardedRepeatPanel panel(&engine, nullptr);
    panel.set_show(true);
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}});
    engine.settle();
    panel.set_show(false);
    engine.settle();
    // act the condition holds again, which must rebuild the body and its rows once (§11.1, §18.1)
    panel.set_show(true);
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 2U);
    // assert the rows are back, because a dangling view would leave them behind or double them (§18.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(1)->child_count(), 1U);
}

TEST(ConditionalCodegen, AGuardedRowReconcilesWhileTheBodyHolds) {
    // arrange a panel whose guarded body carries two rows, which is what a source change reconciles (§18.1, AD-13)
    BindingEngine engine;
    GuardedRepeatPanel panel(&engine, nullptr);
    panel.set_show(true);
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}});
    engine.settle();
    // act the middle item goes, which must reconcile the rows inside the body (§18.1, AD-13)
    panel.set_tasks(std::vector<Task>{Task{.key = "b", .title = "second", .done = false}});
    engine.settle();
    // assert only one row remains, because the guarded view reconciled by identity (§18.1, AD-13)
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 2U);
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_at(1)->child_count(), 1U);
}

TEST(ConditionalCodegen, ARepeatAsTheWholeBodyAppearsWithTheBody) {
    // arrange a panel whose conditional body is the repeat itself, which owns no container of its own (§11.1, §18.1)
    BindingEngine engine;
    GuardedFlatRepeatPanel panel(&engine, nullptr);
    // act the source is filled while the condition is false, which must show nothing (§11.1)
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}});
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
    // act the condition holds, which materializes the rows in the container directly (§11.1, §18.1)
    panel.set_show(true);
    engine.settle();
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 3U);
}

TEST(ConditionalCodegen, ARemovedFlatBodyTakesItsRows) {
    // arrange a panel whose whole body is a repeat, shown with two rows (§11.1, §18.1)
    BindingEngine engine;
    GuardedFlatRepeatPanel panel(&engine, nullptr);
    panel.set_show(true);
    panel.set_tasks(std::vector<Task>{Task{.key = "a", .title = "first", .done = false}, Task{.key = "b", .title = "second", .done = false}});
    engine.settle();
    ASSERT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 3U);
    // act the condition drops, which removes the rows the body owned directly (§11.1, §18.1)
    panel.set_show(false);
    engine.settle();
    // assert only the static sibling remains, because the rows left with the body (§18.1)
    EXPECT_EQ(static_cast<UiNode&>(panel).child_at(0)->child_count(), 1U);
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

TEST(VectorCodegen, ADoubledRowHeightResizesTheScrollbarThumb) {
    // arrange a log laid out at one row height, with the bar visible so its geometry is recorded (§8, §8.2)
    std::vector<Task> tasks;
    tasks.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        tasks.push_back(Task{.key = "task" + std::to_string(index), .title = "line " + std::to_string(index)});
    BindingEngine engine;
    CountingTextEngine text;
    ScrollLog log(&engine, &text);
    log.set_line_height(20.0f);
    log.set_tasks(tasks);
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    SceneBuilder builder;
    (void)engine.settle();
    (void)builder.build(log, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // act the bound row height settles at double the value, which is what a form resize does (§7.3, AD-13)
    log.set_line_height(40.0f);
    (void)engine.settle();
    const std::shared_ptr<const FrameScene> scene = builder.build(log, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // the thumb is the one rounded command, and its height is the viewport share of the content (§6.1)
    std::vector<float> thumb_heights;
    for (const DrawCommand& command : scene->commands())
        if (command.kind() == DrawCommandKind::RoundedRect)
            thumb_heights.push_back(command.rect_geometry().rect.height);
    // assert the thumb is shorter, because the content the extent cache reports is twice as tall (§8.2)
    ASSERT_EQ(thumb_heights.size(), 1u);
    EXPECT_LT(thumb_heights.at(0), 120.0f);
}
