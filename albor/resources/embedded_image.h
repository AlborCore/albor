#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace albor
{
    // the immutable pixel handle a registry hands out, which a backend retains across frames (§6.4, AD-06)
    class ImageResource;
} // namespace albor

namespace albor::resources
{
    // the encoded png a build carries, identified by the hash of the bytes rather than by a runtime path (§6.4)
    struct EmbeddedImage
    {
        // the hash of the bytes below, which the registry recomputes rather than trusts (§6.4)
        std::uint64_t content_hash = 0;

        // the encoded png itself, which a generated asset carries into the binary (§6.4)
        std::vector<std::uint8_t> bytes{};
    };

    // the decoded image cache generated code fills, so an embedded or loaded image is decoded once per process (§6.4)
    class ImageRegistry
    {
    public:
        // register encoded bytes and hand back the decoded image, which is null when the bytes are not a valid png (§6.4)
        [[nodiscard]] static std::shared_ptr<const ImageResource> register_image(EmbeddedImage image);

        // find the image a content hash names, which is null when nothing carries those bytes (§6.4)
        [[nodiscard]] static std::shared_ptr<const ImageResource> find_hash(std::uint64_t content_hash);

        // load and register a png from a file, which a development tool uses but a compiled asset never does (§6.4)
        [[nodiscard]] static std::shared_ptr<const ImageResource> load_file(const std::filesystem::path& path);

        // report how many images are decoded, which a test asserts on after registering (§6.4)
        [[nodiscard]] static std::size_t count();

        // empty the cache, which a test does so that one registration does not leak into another (§2.2)
        static void clear();
    };
} // namespace albor::resources
