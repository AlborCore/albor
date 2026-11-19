#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "albor/core/paint.h"

namespace albor::render
{
    // how many entry points the committed vulkan container carries, in the order `build/shaders.sh` stitches them (§6.3)
    constexpr std::size_t VULKAN_SHADER_MODULES = 6;

    // the compiled vulkan stages the device loads, which the committed container carries (§6.3, AD-16)
    struct VulkanShaderData
    {
        // the modules in container order, which a corrupt artifact leaves empty
        std::array<std::span<const std::byte>, VULKAN_SHADER_MODULES> modules{};

        // report whether every module carries the spirv header, which is what the device can load (§6.3)
        [[nodiscard]] bool complete() const;
    };

    // parse the committed vulkan container, which a truncated or foreign artifact reports as empty (§6.3)
    [[nodiscard]] VulkanShaderData parse_vulkan_shader_data(std::span<const std::byte> container);

    // one blend factor the portable table names, which the vulkan backend maps onto its own enum (§6.2)
    enum class VulkanBlendFactor
    {
        // the factor contributes nothing
        Zero,

        // the factor contributes one
        One,

        // the source alpha
        SourceAlpha,

        // one minus the source alpha
        InverseSourceAlpha,

        // the destination alpha
        DestinationAlpha,

        // one minus the destination alpha
        InverseDestinationAlpha,

        // the second source alpha, which dual source blending reads
        Source1Alpha,

        // one minus the second source alpha
        InverseSource1Alpha,

        // the second source colour, which the destination atop mode reads
        Source1Color,

        // one minus the second source colour
        InverseSource1Color,
    };

    // the blend terms one composition mode evaluates, which the pipeline table bakes (§6.2)
    struct VulkanBlendTerms
    {
        // the factor the colour source is multiplied by
        VulkanBlendFactor color_source = VulkanBlendFactor::One;

        // the factor the colour destination is multiplied by
        VulkanBlendFactor color_destination = VulkanBlendFactor::InverseSourceAlpha;

        // the factor the alpha source is multiplied by
        VulkanBlendFactor alpha_source = VulkanBlendFactor::One;

        // the factor the alpha destination is multiplied by
        VulkanBlendFactor alpha_destination = VulkanBlendFactor::InverseSourceAlpha;

        // whether the mode needs the dual source fragment, which reads the destination through a second output
        bool exact = false;
    };

    // map a composition mode onto the factors the vulkan blender evaluates, which mirrors the direct3d table (§6.2, AD-05)
    [[nodiscard]] VulkanBlendTerms vulkan_blend_terms(CompositionMode mode);

    // the physical extent a swapchain is built at, which the surface capabilities clamp (§6.3)
    struct VulkanExtent
    {
        // the pixel width, which is at least one
        std::uint32_t width = 1;

        // the pixel height, which is at least one
        std::uint32_t height = 1;
    };

    // clamp a requested extent into the range a surface accepts, where a zero limit means no limit (§6.3)
    [[nodiscard]] VulkanExtent clamp_vulkan_extent(float width, float height, std::uint32_t maximum_width, std::uint32_t maximum_height);
} // namespace albor::render
