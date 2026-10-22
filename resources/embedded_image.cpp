#include "resources/embedded_image.h"

#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

#include "core/content_hash.h"
#include "render/image_resource.h"
#include "render/resource_factory.h"
#include "resources/png_decoder.h"

namespace albor::resources
{
    namespace
    {
        // scale every colour channel by its alpha, which is the premultiplied layout the backend uploads (§6.4, AD-26)
        void premultiply(std::span<std::uint8_t> pixels) {
            // every pixel carries four channels, so the walk advances one whole pixel at a time (§6.4)
            for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
                // the alpha is what every colour channel is scaled by (§6.4)
                const std::uint32_t alpha = pixels[index + 3];
                // each colour channel is rounded back to eight bits, because png stores straight alpha (§6.4)
                for (std::size_t channel = 0; channel < 3; channel++)
                    pixels[index + channel] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(pixels[index + channel]) * alpha + 127u) / 255u);
            }
        }

        // the decoded images and the factory that stamps them, which one process shares (§6.4)
        struct RegistryState
        {
            // the lock guards both the map and the factory, because a render thread may resolve a hash (§10.5, AD-18)
            std::mutex mutex;

            // the factory stamps every image from the process wide identifier source (§6.4, AD-06)
            ResourceFactory factory;

            // decoded images are keyed by the hash of their encoded bytes, so the same png decodes once (§6.4)
            std::unordered_map<std::uint64_t, std::shared_ptr<const ImageResource>> images;
        };

        // the state is one per process, because registration and lookup must meet in the same cache (§6.4)
        RegistryState& registry() {
            // function local state is constructed on first use, so startup registration is safe (§6.4)
            static RegistryState state;
            // the one state is handed back by reference, because every caller shares it (§6.4)
            return state;
        }
    } // namespace

    std::shared_ptr<const ImageResource> ImageRegistry::register_image(EmbeddedImage image) {
        // bytes that are not there cannot decode into a picture, so they are refused before hashing (§6.4)
        if (image.bytes.empty())
            return nullptr;
        // the encoded bytes are the identity, because two decodes of the same png must meet in one cache entry (§6.4)
        image.content_hash = hash_bytes(image.bytes);
        // the lock is held over the cache and the decode, so two workers cannot decode the same png twice (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry().mutex);
        // an image already registered under those bytes is shared rather than decoded again (§6.4)
        const auto existing = registry().images.find(image.content_hash);
        // a hit hands back the retained handle, which the caller keeps alive past the lock (§6.4, AD-18)
        if (existing != registry().images.end())
            return existing->second;
        // the encoder's bytes become straight rgba through libpng, which owns the chunk walk and inflate (§6.4)
        std::optional<DecodedPixels> decoded = decode_png(image.bytes);
        // bytes that do not decode are refused rather than cached as a blank image (§6.4)
        if (!decoded)
            return nullptr;
        // the resource contract is premultiplied, which the decoder explicitly does not produce (§6.4, AD-26)
        premultiply(decoded->rgba);
        // the factory validates the extent and stamps the process wide image identifier (§6.4, AD-06)
        const std::shared_ptr<const ImageResource> resource = registry().factory.create_image(decoded->width, decoded->height, ImageFormat::RGBA8, decoded->rgba);
        // a refused resource is not cached, so a later registration retries instead of returning null forever (§6.4)
        if (!resource)
            return nullptr;
        // the decoded image joins the cache under its encoded hash, so a later registration shares it (§6.4)
        registry().images.emplace(image.content_hash, resource);
        // the retained handle is what the caller uses and what every later lookup returns (§6.4, AD-18)
        return resource;
    }

    std::shared_ptr<const ImageResource> ImageRegistry::find_hash(std::uint64_t content_hash) {
        // the lock is held over the search, because a registration may land mid walk (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry().mutex);
        // the hash is the cache key, because the encoded bytes are the image's identity (§6.4)
        const auto found = registry().images.find(content_hash);
        // a miss is a refusal rather than a blank image, because the caller asked for bytes nobody carried (§6.4)
        if (found == registry().images.end())
            return nullptr;
        // the retained handle is shared, so the caller outlives the lock (§6.4, AD-18)
        return found->second;
    }

    std::shared_ptr<const ImageResource> ImageRegistry::load_file(const std::filesystem::path& path) {
        // a file that cannot be opened has no bytes to decode, so it is refused before reading (§6.4)
        std::ifstream file(path, std::ios::binary);
        // the failed open is the ordinary missing file case, which a development tool reports as null (§6.4)
        if (!file)
            return nullptr;
        // the bytes are read whole, because the registry hashes the encoded png before it decodes it (§6.4)
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // the file is handed to the same registration path, so a file and an embedded asset share one cache entry (§6.4)
        return register_image(EmbeddedImage{.bytes = std::move(bytes)});
    }

    std::size_t ImageRegistry::count() {
        // the lock is held over the read, because a registration may land mid read (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry().mutex);
        // the count is what a test asserts on, because decoding is otherwise invisible (§6.4)
        return registry().images.size();
    }

    void ImageRegistry::clear() {
        // the lock is held over the drop, because a backend may still be reading a handle (§10.5, AD-18)
        const std::lock_guard<std::mutex> lock(registry().mutex);
        // the cache is emptied, so one test's registration cannot answer another's (§2.2)
        registry().images.clear();
    }
} // namespace albor::resources
