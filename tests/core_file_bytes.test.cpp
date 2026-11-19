#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "albor/core/file_bytes.h"

TEST(CoreFileBytes, ReadsARegularFileWhole) {
    // arrange a file carrying every byte boundary, which a text read would corrupt (§6.4)
    const std::uint8_t payload[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "albor-core-file-bytes.bin";
    {
        // the payload is written whole, because the read must return exactly these bytes (§6.4)
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(payload), sizeof payload);
    }
    // act
    const std::optional<std::vector<std::uint8_t>> bytes = albor::core::read_regular_file(path);
    // assert the read carries the file unchanged, because the caller embeds the exact bytes (§6.4)
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(*bytes, std::vector<std::uint8_t>(payload, payload + sizeof payload));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(CoreFileBytes, RefusesAPathThatNamesNoRegularFile) {
    // arrange a directory and a path that names nothing, which are the two shapes a read must refuse (§6.4, #246)
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "albor-core-file-bytes-directory";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    // the missing path is cleared first, because a leftover would change the answer (§6.4)
    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "albor-core-file-bytes-missing.bin";
    std::filesystem::remove(missing, ignored);
    // act
    const std::optional<std::vector<std::uint8_t>> from_directory = albor::core::read_regular_file(directory);
    const std::optional<std::vector<std::uint8_t>> from_missing = albor::core::read_regular_file(missing);
    // assert neither path yields bytes, because a directory read throws on libstdc++ (§6.4, #246)
    EXPECT_FALSE(from_directory.has_value());
    EXPECT_FALSE(from_missing.has_value());
    std::filesystem::remove_all(directory, ignored);
}

TEST(CoreFileBytes, RefusesARegularFileItCannotOpen) {
#if defined(_WIN32)
    // a windows regular file stays readable for its owner, so the open refusal has no portable fixture here (§6.4)
    GTEST_SKIP() << "a windows regular file cannot be made unreadable for its owner";
#else
    // a process that may read anything cannot produce the refusal, which is reported rather than silently passing (§6.4)
    if (::geteuid() == 0)
        GTEST_SKIP() << "a root process opens a file whose permissions deny it";
    // arrange a regular file whose read permission is removed, which passes the probe and fails the open (§6.4)
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "albor-core-file-bytes-unreadable.bin";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    {
        // the file is written first, because the probe must see a regular file and the open must be what fails (§6.4)
        std::ofstream file(path, std::ios::binary);
        file << "x";
    }
    // the owner loses every right on the file, which is what refuses the open rather than the probe (§6.4)
    std::filesystem::permissions(path, std::filesystem::perms::none, std::filesystem::perm_options::replace, ignored);
    // act
    const std::optional<std::vector<std::uint8_t>> bytes = albor::core::read_regular_file(path);
    // the rights are restored before the assert, so the cleanup below always removes the fixture (§6.4)
    std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ignored);
    // assert a regular file that cannot be opened carries no bytes, which is the refusal after a successful probe (§6.4)
    EXPECT_FALSE(bytes.has_value());
    std::filesystem::remove(path, ignored);
#endif
}
