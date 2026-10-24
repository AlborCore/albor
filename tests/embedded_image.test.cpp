#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "core/content_hash.h"
#include "render/image_resource.h"
#include "render/resource_factory.h"
#include "resources/embedded_image.h"

TEST(EmbeddedImageChecks, DecodesEmbeddedBytesOnceAndSharesTheHandle) {
    // arrange
    constexpr std::uint8_t RGBA_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xDF, 0xE0, 0xD0, 0xC0, 0x00, 0x02, 0x5C, 0x22, 0x72, 0xFF, 0x81, 0x80, 0x11, 0x00, 0x33, 0x44, 0x06, 0x79, 0x51, 0x43, 0x97, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    albor::resources::EmbeddedImage lying;
    lying.content_hash = 42;
    lying.bytes.assign(RGBA_PNG, RGBA_PNG + sizeof RGBA_PNG);
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> first = albor::resources::ImageRegistry::register_image(lying);
    const std::shared_ptr<const albor::ImageResource> second = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(RGBA_PNG, RGBA_PNG + sizeof RGBA_PNG)});
    const std::shared_ptr<const albor::ImageResource> found = albor::resources::ImageRegistry::find_hash(albor::hash_bytes(RGBA_PNG));
    // assert
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, found);
    EXPECT_EQ(albor::resources::ImageRegistry::find_hash(42), nullptr);
    EXPECT_EQ(first->width(), 2u);
    EXPECT_EQ(first->height(), 2u);
    ASSERT_EQ(first->pixels().size(), 16u);
    EXPECT_EQ(first->pixels()[0], 128u);
    EXPECT_EQ(first->pixels()[1], 64u);
    EXPECT_EQ(first->pixels()[2], 32u);
    EXPECT_EQ(first->pixels()[3], 128u);
    EXPECT_EQ(first->pixels()[4], 0u);
    EXPECT_EQ(first->pixels()[12], 1u);
    EXPECT_EQ(first->pixels()[13], 1u);
    EXPECT_EQ(first->pixels()[14], 1u);
    EXPECT_EQ(first->pixels()[15], 1u);
    EXPECT_EQ(albor::resources::ImageRegistry::count(), 1u);
    albor::resources::ImageRegistry::clear();
    EXPECT_EQ(albor::resources::ImageRegistry::count(), 0u);
    EXPECT_EQ(albor::resources::ImageRegistry::find_hash(albor::hash_bytes(RGBA_PNG)), nullptr);
}

TEST(EmbeddedImageChecks, RefusesEmptyInvalidAndUnreadableBytes) {
    // arrange
    constexpr std::uint8_t INVALID[] = {1, 2, 3, 4};
    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "albor-embedded-image-missing.png";
    std::error_code ignored;
    std::filesystem::remove(missing, ignored);
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> empty = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{});
    const std::shared_ptr<const albor::ImageResource> invalid = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(INVALID, INVALID + sizeof INVALID)});
    const std::shared_ptr<const albor::ImageResource> missing_file = albor::resources::ImageRegistry::load_file(missing);
    // assert
    EXPECT_EQ(empty, nullptr);
    EXPECT_EQ(invalid, nullptr);
    EXPECT_EQ(missing_file, nullptr);
    EXPECT_EQ(albor::resources::ImageRegistry::count(), 0u);
}

TEST(EmbeddedImageChecks, LoadsTheSameBytesFromAFileAsEmbedded) {
    // arrange
    constexpr std::uint8_t RGBA_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xDF, 0xE0, 0xD0, 0xC0, 0x00, 0x02, 0x5C, 0x22, 0x72, 0xFF, 0x81, 0x80, 0x11, 0x00, 0x33, 0x44, 0x06, 0x79, 0x51, 0x43, 0x97, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "albor-embedded-image-load.png";
    {
        // the fixture is written whole, because the registry reads encoded bytes rather than a path (§6.4)
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(RGBA_PNG), sizeof RGBA_PNG);
    }
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> loaded = albor::resources::ImageRegistry::load_file(path);
    const std::shared_ptr<const albor::ImageResource> embedded = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(RGBA_PNG, RGBA_PNG + sizeof RGBA_PNG)});
    // assert
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded, embedded);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    albor::resources::ImageRegistry::clear();
}

TEST(EmbeddedImageChecks, InvalidFileBytesAreRefused) {
    // arrange
    constexpr std::uint8_t INVALID[] = {1, 2, 3, 4};
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "albor-embedded-image-invalid.png";
    {
        // the fixture is written whole, because a corrupt png must not reach the cache (§6.4)
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(INVALID), sizeof INVALID);
    }
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> loaded = albor::resources::ImageRegistry::load_file(path);
    // assert
    EXPECT_EQ(loaded, nullptr);
    EXPECT_EQ(albor::resources::ImageRegistry::count(), 0u);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EmbeddedImageChecks, PremultipliesEveryAlphaBoundaryWithRounding) {
    // arrange
    constexpr std::uint8_t PREMULTIPLIED_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x60, 0x64, 0xFE, 0xDF, 0xF0, 0x1F, 0x08, 0x18, 0xB8, 0x44, 0xE4, 0x18, 0x18, 0x81, 0x00, 0x00, 0x45, 0xB0, 0x05, 0xC0, 0xE9, 0x3B, 0xE1, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> image = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(PREMULTIPLIED_PNG, PREMULTIPLIED_PNG + sizeof PREMULTIPLIED_PNG)});
    // assert
    ASSERT_NE(image, nullptr);
    ASSERT_EQ(image->pixels().size(), 16u);
    EXPECT_EQ(image->pixels()[0], 1u);
    EXPECT_EQ(image->pixels()[1], 2u);
    EXPECT_EQ(image->pixels()[2], 128u);
    EXPECT_EQ(image->pixels()[3], 128u);
    EXPECT_EQ(image->pixels()[4], 255u);
    EXPECT_EQ(image->pixels()[5], 255u);
    EXPECT_EQ(image->pixels()[6], 255u);
    EXPECT_EQ(image->pixels()[7], 255u);
    EXPECT_EQ(image->pixels()[8], 0u);
    EXPECT_EQ(image->pixels()[9], 0u);
    EXPECT_EQ(image->pixels()[10], 0u);
    EXPECT_EQ(image->pixels()[11], 0u);
    EXPECT_EQ(image->pixels()[12], 0u);
    EXPECT_EQ(image->pixels()[13], 0u);
    EXPECT_EQ(image->pixels()[14], 0u);
    EXPECT_EQ(image->pixels()[15], 1u);
    albor::resources::ImageRegistry::clear();
}

TEST(EmbeddedImageChecks, DistinctImagesProduceDistinctCacheEntries) {
    // arrange
    constexpr std::uint8_t RGBA_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xDF, 0xE0, 0xD0, 0xC0, 0x00, 0x02, 0x5C, 0x22, 0x72, 0xFF, 0x81, 0x80, 0x11, 0x00, 0x33, 0x44, 0x06, 0x79, 0x51, 0x43, 0x97, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    constexpr std::uint8_t ONE_PIXEL_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> first = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(RGBA_PNG, RGBA_PNG + sizeof RGBA_PNG)});
    const std::shared_ptr<const albor::ImageResource> second = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(ONE_PIXEL_PNG, ONE_PIXEL_PNG + sizeof ONE_PIXEL_PNG)});
    // assert
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_NE(first->id(), second->id());
    EXPECT_EQ(albor::resources::ImageRegistry::count(), 2u);
    EXPECT_EQ(albor::resources::ImageRegistry::find_hash(albor::hash_bytes(RGBA_PNG)), first);
    EXPECT_EQ(albor::resources::ImageRegistry::find_hash(albor::hash_bytes(ONE_PIXEL_PNG)), second);
    albor::resources::ImageRegistry::clear();
}

TEST(EmbeddedImageChecks, EveryResourceKindSharesTheIdentifierSource) {
    // arrange
    const albor::Point points[] = {albor::Point{.x = 0.0f, .y = 0.0f}, albor::Point{.x = 1.0f, .y = 1.0f}};
    const albor::PathVerb verbs[] = {albor::PathVerb::MoveTo, albor::PathVerb::LineTo};
    const std::uint8_t pixels[] = {255, 0, 0, 255};
    albor::PictureRecorder recorder;
    recorder.draw_rect(albor::Rect{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f}, albor::Paint{});
    albor::ResourceFactory factory;
    // act
    const std::shared_ptr<const albor::LineBatch> batch = factory.create_line_batch(points);
    const std::shared_ptr<const albor::PathResource> path = factory.create_path(verbs, points);
    const std::shared_ptr<const albor::PictureResource> picture = factory.create_picture(recorder);
    const std::shared_ptr<const albor::ImageResource> image = factory.create_image(1, 1, albor::ImageFormat::RGBA8, pixels);
    // assert
    ASSERT_NE(batch, nullptr);
    ASSERT_NE(path, nullptr);
    ASSERT_NE(picture, nullptr);
    ASSERT_NE(image, nullptr);
    EXPECT_NE(batch->id(), path->id());
    EXPECT_NE(batch->id(), picture->id());
    EXPECT_NE(batch->id(), image->id());
    EXPECT_NE(path->id(), picture->id());
    EXPECT_NE(path->id(), image->id());
    EXPECT_NE(picture->id(), image->id());
}

TEST(EmbeddedImageChecks, ConcurrentRegistrationSharesOneDecode) {
    // arrange
    constexpr std::uint8_t RGBA_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xDF, 0xE0, 0xD0, 0xC0, 0x00, 0x02, 0x5C, 0x22, 0x72, 0xFF, 0x81, 0x80, 0x11, 0x00, 0x33, 0x44, 0x06, 0x79, 0x51, 0x43, 0x97, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    constexpr std::size_t WORKERS = 8;
    std::vector<std::shared_ptr<const albor::ImageResource>> registered(WORKERS);
    std::vector<std::shared_ptr<const albor::ImageResource>> found(WORKERS);
    std::vector<std::thread> workers;
    albor::resources::ImageRegistry::clear();
    // act
    for (std::size_t index = 0; index < WORKERS; index++) {
        workers.emplace_back([&registered, &found, index, &RGBA_PNG] {
            registered[index] = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(RGBA_PNG, RGBA_PNG + sizeof RGBA_PNG)});
            found[index] = albor::resources::ImageRegistry::find_hash(albor::hash_bytes(RGBA_PNG));
        });
    }
    for (std::thread& worker : workers)
        worker.join();
    // assert
    ASSERT_NE(registered[0], nullptr);
    for (std::size_t index = 0; index < WORKERS; index++) {
        EXPECT_EQ(registered[index], registered[0]);
        EXPECT_EQ(found[index], registered[0]);
    }
    EXPECT_EQ(albor::resources::ImageRegistry::count(), 1u);
    albor::resources::ImageRegistry::clear();
}

TEST(EmbeddedImageChecks, FactoriesAndTheRegistryHandOutDistinctIdentifiers) {
    // arrange
    constexpr std::uint8_t ONE_PIXEL_PNG[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    const std::uint8_t pixels[] = {255, 0, 0, 255};
    albor::ResourceFactory first_factory;
    albor::ResourceFactory second_factory;
    albor::resources::ImageRegistry::clear();
    // act
    const std::shared_ptr<const albor::ImageResource> first = first_factory.create_image(1, 1, albor::ImageFormat::RGBA8, pixels);
    const std::shared_ptr<const albor::ImageResource> second = second_factory.create_image(1, 1, albor::ImageFormat::RGBA8, pixels);
    const std::shared_ptr<const albor::ImageResource> registered = albor::resources::ImageRegistry::register_image(albor::resources::EmbeddedImage{.bytes = std::vector<std::uint8_t>(ONE_PIXEL_PNG, ONE_PIXEL_PNG + sizeof ONE_PIXEL_PNG)});
    // assert
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(registered, nullptr);
    EXPECT_NE(first->id(), second->id());
    EXPECT_NE(first->id(), registered->id());
    EXPECT_NE(second->id(), registered->id());
    albor::resources::ImageRegistry::clear();
}
