#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "runtime/binding_engine.h"
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
