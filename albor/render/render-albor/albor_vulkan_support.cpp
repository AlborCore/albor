#include "albor/render/render-albor/albor_vulkan_support.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace albor::render
{
    namespace
    {
        // the header the committed container carries, so an artifact from another family is refused here (§6.3)
        constexpr char CONTAINER_MAGIC[8] = {'A', 'L', 'B', 'V', 'K', 'S', 'P', 'V'};

        // the first word of every spirv module, which a little endian read sees on every host the backend builds on (§6.3)
        constexpr std::uint32_t SPIRV_MAGIC = 0x07230203u;

        // read one little endian length the container prefix carries (§6.3)
        [[nodiscard]] std::uint32_t read_length(const std::byte* bytes) {
            // the four octets are assembled in the order the build script writes them (§6.3)
            const auto* octets = reinterpret_cast<const std::uint8_t*>(bytes);
            return static_cast<std::uint32_t>(octets[0]) | (static_cast<std::uint32_t>(octets[1]) << 8u) | (static_cast<std::uint32_t>(octets[2]) << 16u) | (static_cast<std::uint32_t>(octets[3]) << 24u);
        }

        // read the first word of a module, which names the spirv container (§6.3)
        [[nodiscard]] std::uint32_t read_spirv_magic(std::span<const std::byte> module) {
            // a module shorter than its own header cannot carry a word, which the caller reports as empty (§6.3)
            if (module.size() < sizeof(std::uint32_t))
                return 0;
            const auto* octets = reinterpret_cast<const std::uint8_t*>(module.data());
            return static_cast<std::uint32_t>(octets[0]) | (static_cast<std::uint32_t>(octets[1]) << 8u) | (static_cast<std::uint32_t>(octets[2]) << 16u) | (static_cast<std::uint32_t>(octets[3]) << 24u);
        }

        // clamp one requested axis into the surface range, where zero names no limit (§6.3)
        [[nodiscard]] std::uint32_t clamp_axis(float value, std::uint32_t maximum) {
            // a non finite or non positive request carries no pixel, so one is the floor the surface accepts (§6.3)
            if (!std::isfinite(value) || value < 1.0f)
                return 1;
            // the float is narrowed after the floor, which keeps the cast defined (§6.3)
            const float floored = std::floor(value);
            // a surface that names no limit keeps the request, bounded by the uint32 range (§6.3)
            if (maximum == 0)
                return floored >= 4294967295.0f ? 4294967295u : static_cast<std::uint32_t>(floored);
            // the surface limit is the ceiling a request cannot exceed (§6.3)
            return static_cast<std::uint32_t>(std::min(floored, static_cast<float>(maximum)));
        }
    } // namespace

    bool VulkanShaderData::complete() const {
        // every module must carry the spirv header, because a pipeline cannot build from an empty stage (§6.3)
        return std::all_of(modules.begin(), modules.end(), [](std::span<const std::byte> module) { return read_spirv_magic(module) == SPIRV_MAGIC; });
    }

    VulkanShaderData parse_vulkan_shader_data(std::span<const std::byte> container) {
        // the magic names the container format, so an artifact from another build is refused here (§6.3)
        if (container.size() <= sizeof(CONTAINER_MAGIC) || std::memcmp(container.data(), CONTAINER_MAGIC, sizeof(CONTAINER_MAGIC)) != 0)
            return {};
        VulkanShaderData data;
        std::size_t offset = sizeof(CONTAINER_MAGIC);
        // every module is a length prefixed blob, in the order the build script stitches them (§6.3)
        for (std::span<const std::byte>& module : data.modules) {
            // a length prefix that runs past the artifact names a truncated container (§6.3)
            if (offset + sizeof(std::uint32_t) > container.size())
                return {};
            const std::uint32_t length = read_length(container.data() + offset);
            offset += sizeof(std::uint32_t);
            // an empty module or one whose blob runs past the artifact is not a shader the device loads (§6.3)
            if (length == 0 || offset + length > container.size())
                return {};
            module = container.subspan(offset, length);
            offset += length;
        }
        // a container with bytes after the sixth module belongs to another build (§6.3)
        if (offset != container.size())
            return {};
        return data;
    }

    VulkanBlendTerms vulkan_blend_terms(CompositionMode mode) {
        // the table mirrors the direct3d one field for field, so both gpu backends blend alike (§6.2, AD-05)
        switch (mode) {
        case CompositionMode::SourceIn:
            return {.color_source = VulkanBlendFactor::DestinationAlpha, .color_destination = VulkanBlendFactor::InverseSource1Alpha, .alpha_source = VulkanBlendFactor::DestinationAlpha, .alpha_destination = VulkanBlendFactor::InverseSource1Alpha, .exact = true};
        case CompositionMode::SourceOut:
            return {.color_source = VulkanBlendFactor::InverseDestinationAlpha, .color_destination = VulkanBlendFactor::InverseSourceAlpha, .alpha_source = VulkanBlendFactor::InverseDestinationAlpha, .alpha_destination = VulkanBlendFactor::InverseSourceAlpha, .exact = false};
        case CompositionMode::Atop:
            return {.color_source = VulkanBlendFactor::DestinationAlpha, .color_destination = VulkanBlendFactor::InverseSourceAlpha, .alpha_source = VulkanBlendFactor::DestinationAlpha, .alpha_destination = VulkanBlendFactor::InverseSourceAlpha, .exact = false};
        case CompositionMode::DestinationOver:
            return {.color_source = VulkanBlendFactor::InverseDestinationAlpha, .color_destination = VulkanBlendFactor::One, .alpha_source = VulkanBlendFactor::InverseDestinationAlpha, .alpha_destination = VulkanBlendFactor::One, .exact = false};
        case CompositionMode::DestinationIn:
            return {.color_source = VulkanBlendFactor::Zero, .color_destination = VulkanBlendFactor::InverseSourceAlpha, .alpha_source = VulkanBlendFactor::Zero, .alpha_destination = VulkanBlendFactor::InverseSourceAlpha, .exact = false};
        case CompositionMode::DestinationOut:
            return {.color_source = VulkanBlendFactor::Zero, .color_destination = VulkanBlendFactor::InverseSourceAlpha, .alpha_source = VulkanBlendFactor::Zero, .alpha_destination = VulkanBlendFactor::InverseSourceAlpha, .exact = false};
        case CompositionMode::DestinationAtop:
            return {.color_source = VulkanBlendFactor::InverseDestinationAlpha, .color_destination = VulkanBlendFactor::InverseSource1Color, .alpha_source = VulkanBlendFactor::One, .alpha_destination = VulkanBlendFactor::InverseSource1Alpha, .exact = true};
        case CompositionMode::Lighter:
            return {.color_source = VulkanBlendFactor::One, .color_destination = VulkanBlendFactor::One, .alpha_source = VulkanBlendFactor::One, .alpha_destination = VulkanBlendFactor::One, .exact = false};
        case CompositionMode::Copy:
            return {.color_source = VulkanBlendFactor::One, .color_destination = VulkanBlendFactor::InverseSource1Alpha, .alpha_source = VulkanBlendFactor::One, .alpha_destination = VulkanBlendFactor::InverseSource1Alpha, .exact = true};
        case CompositionMode::Xor:
            return {.color_source = VulkanBlendFactor::InverseDestinationAlpha, .color_destination = VulkanBlendFactor::InverseSourceAlpha, .alpha_source = VulkanBlendFactor::InverseDestinationAlpha, .alpha_destination = VulkanBlendFactor::InverseSourceAlpha, .exact = false};
        case CompositionMode::SourceOver:
            break;
        }
        // source over is the default every other mode falls back to, which matches the hal order (§6.2)
        return {.color_source = VulkanBlendFactor::One, .color_destination = VulkanBlendFactor::InverseSourceAlpha, .alpha_source = VulkanBlendFactor::One, .alpha_destination = VulkanBlendFactor::InverseSourceAlpha, .exact = false};
    }

    VulkanExtent clamp_vulkan_extent(float width, float height, std::uint32_t maximum_width, std::uint32_t maximum_height) {
        // each axis is clamped on its own, because a surface can constrain width and height differently (§6.3)
        return {.width = clamp_axis(width, maximum_width), .height = clamp_axis(height, maximum_height)};
    }
} // namespace albor::render
