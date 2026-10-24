#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "tools/albor-codegen/depfile.h"

TEST(DepfileChecks, EscapesMakeSyntaxAndKeepsUtf8) {
    // arrange
    const std::filesystem::path syntax = "/tmp/a b#c.ui";
    const std::filesystem::path plain = "plain/path.ui";
    // act
    const std::string escaped = albor::codegen::depfile_path(syntax);
    const std::string encoded = albor::codegen::utf8_path(plain);
    // assert
    EXPECT_EQ(escaped, "/tmp/a\\ b\\#c.ui");
    EXPECT_EQ(encoded, "plain/path.ui");
}

TEST(DepfileChecks, EscapesBackslashRunsAroundMakeSyntax) {
    // arrange a host where a backslash is a file character, because windows reads it as a separator (§14.2)
    if (std::filesystem::path::preferred_separator != '/')
        GTEST_SKIP();
    // arrange a backslash that precedes a word, a space or a hash (§14.2)
    const std::filesystem::path before_word = "/tmp/a\\d.ui";
    const std::filesystem::path before_space = "/tmp/a\\ b.ui";
    const std::filesystem::path before_hash = "/tmp/a\\#b.ui";
    // act
    const std::string word_text = albor::codegen::depfile_path(before_word);
    const std::string space_text = albor::codegen::depfile_path(before_space);
    const std::string hash_text = albor::codegen::depfile_path(before_hash);
    // assert a literal backslash before a word stays single, while the separators take their escape (§14.2)
    EXPECT_EQ(word_text, "/tmp/a\\d.ui");
    EXPECT_EQ(space_text, "/tmp/a\\\\\\ b.ui");
    EXPECT_EQ(hash_text, "/tmp/a\\\\#b.ui");
}

TEST(DepfileChecks, EscapesDollarSignsForMake) {
    // arrange a path with a dollar, which make reads as a variable (§14.2)
    const std::filesystem::path path = "/tmp/a$b.ui";
    // act
    const std::string escaped = albor::codegen::depfile_path(path);
    // assert
    EXPECT_EQ(escaped, "/tmp/a$$b.ui");
}

TEST(DepfileChecks, WritesOneRulePerGeneratedUnit) {
    // arrange
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-depfile-writer";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const std::vector<albor::compiler::GeneratedFile> files{
        {.path = "c.h", .content = "header"},
        {.path = "c.cpp", .content = "source"},
    };
    const std::filesystem::path output = root / "out dir#1";
    const std::filesystem::path input = root / "source file.ui";
    const std::vector<std::filesystem::path> assets{root / "assets" / "icon file#1.png"};
    const std::filesystem::path depfile = root / "deps dir" / "unit file.d";
    // act
    const bool written = albor::codegen::write_depfile(depfile, files, output, input, assets);
    std::ifstream stream(depfile, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // assert
    EXPECT_TRUE(written);
    const std::string prefix = albor::codegen::depfile_path(root);
    EXPECT_EQ(content, prefix + "/out\\ dir\\#1/c.h: " + prefix + "/source\\ file.ui " + prefix + "/assets/icon\\ file\\#1.png\n" + prefix + "/out\\ dir\\#1/c.cpp: " + prefix + "/source\\ file.ui " + prefix + "/assets/icon\\ file\\#1.png\n");
    std::filesystem::remove_all(root, ignored);
}

TEST(DepfileChecks, RefusesADirectoryThatCannotBeCreated) {
    // arrange
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-depfile-blocked";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    const std::filesystem::path blocker = root / "not-a-directory";
    std::ofstream(blocker, std::ios::binary) << "x";
    const std::vector<albor::compiler::GeneratedFile> files{{.path = "c.h", .content = "header"}};
    // act
    const bool written = albor::codegen::write_depfile(blocker / "unit.d", files, root / "out", root / "c.ui", {});
    // assert
    EXPECT_FALSE(written);
    std::filesystem::remove_all(root, ignored);
}

TEST(DepfileChecks, RefusesAStreamThatCannotOpen) {
    // arrange
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-depfile-stream";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const std::filesystem::path depfile = root / "unit.d";
    std::filesystem::create_directories(depfile);
    const std::vector<albor::compiler::GeneratedFile> files{{.path = "c.h", .content = "header"}};
    // act
    const bool written = albor::codegen::write_depfile(depfile, files, root / "out", root / "c.ui", {});
    // assert
    EXPECT_FALSE(written);
    std::filesystem::remove_all(root, ignored);
}
