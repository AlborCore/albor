#include <string>

#include <gtest/gtest.h>

#include "core/json.h"

using namespace albor::core;

TEST(JsonChecks, AnObjectRoundTrips) {
    // arrange
    Json value;
    value.set("name", Json(std::string("albor")));
    value.set("count", Json(3.0));
    // act
    const std::optional<Json> parsed = Json::parse(value.to_string());
    // assert
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->member("name")->as_string(), "albor");
    EXPECT_EQ(parsed->member("count")->as_number(), 3.0);
}

TEST(JsonChecks, AnAbsentMemberReadsAsNothing) {
    // arrange
    const std::optional<Json> parsed = Json::parse(R"({"a":1})");
    // act
    const Json* missing = parsed->member("b");
    // assert
    EXPECT_EQ(missing, nullptr);
}

TEST(JsonChecks, AnArrayRoundTrips) {
    // act
    const std::optional<Json> parsed = Json::parse(R"([1,"two",true,null])");
    // assert
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->as_array().size(), 4U);
    EXPECT_EQ(parsed->as_array().at(1).as_string(), "two");
}

TEST(JsonChecks, AnEmptyObjectAndArrayParse) {
    // assert both are legal values, which a reply may carry (§10.3)
    EXPECT_TRUE(Json::parse("{}").has_value());
    EXPECT_TRUE(Json::parse("[]").has_value());
}

TEST(JsonChecks, StringsResolveTheirEscapes) {
    // act
    const std::optional<Json> parsed = Json::parse(R"("a\nb\tc\"d\\e")");
    // assert
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->as_string(), "a\nb\tc\"d\\e");
}

TEST(JsonChecks, AUnicodeEscapeBecomesUtf8) {
    // act the four hex digits are the code point, which is what the wire means (§10.3)
    const std::optional<Json> parsed = Json::parse(R"("é")");
    // assert
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->as_string(), "\xc3\xa9");
}

TEST(JsonChecks, StringsEscapeOnTheWayOut) {
    // arrange a value holding the characters json reserves
    Json value{std::string("a\"b\\c\nd")};
    // act
    const std::string text = value.to_string();
    // assert the round trip preserves it, which is what escaping is for (§10.3)
    const std::optional<Json> parsed = Json::parse(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->as_string(), value.as_string());
}

TEST(JsonChecks, LiteralsParse) {
    // assert the three literals are values, and a prefix of one is not (§10.3)
    EXPECT_TRUE(Json::parse("true")->as_bool());
    EXPECT_FALSE(Json::parse("false")->as_bool());
    EXPECT_EQ(Json::parse("null")->kind(), Json::Kind::Null);
    EXPECT_FALSE(Json::parse("tru").has_value());
}

TEST(JsonChecks, MalformedTextDoesNotParse) {
    // assert each of these is refused rather than half accepted (§10.3)
    EXPECT_FALSE(Json::parse("{").has_value());
    EXPECT_FALSE(Json::parse("[1,]").has_value());
    EXPECT_FALSE(Json::parse(R"({"a" 1})").has_value());
    EXPECT_FALSE(Json::parse(R"("unterminated)").has_value());
    EXPECT_FALSE(Json::parse(R"({"a":1} trailing)").has_value());
}

TEST(JsonChecks, NestedValuesRoundTrip) {
    // arrange
    Json inner;
    inner.set("id", Json(std::string("root")));
    Json outer;
    outer.set("result", inner);
    // act
    const std::optional<Json> parsed = Json::parse(outer.to_string());
    // assert
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->member("result")->member("id")->as_string(), "root");
}

TEST(JsonChecks, AWholeNumberWritesWithoutAFraction) {
    // assert a client reads an integer as an integer, which is what a version or an id is (§10.3)
    EXPECT_EQ(Json(1.0).to_string(), "1");
    EXPECT_EQ(Json(-3.0).to_string(), "-3");
}
