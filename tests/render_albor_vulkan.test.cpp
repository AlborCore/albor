#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/paint.h"
#include "albor/render/render-albor/albor_vulkan_support.h"

using namespace albor;
using namespace albor::render;

TEST(VulkanShaderDataChecks, AcceptsTheSixModuleContainer) {
    // arrange a container with the header and six modules, each carrying the spirv magic
    std::vector<std::byte> container;
    for (char letter : {'A', 'L', 'B', 'V', 'K', 'S', 'P', 'V'})
        container.push_back(static_cast<std::byte>(letter));
    for (int module = 0; module < 6; module++) {
        const std::uint32_t length = 8;
        container.push_back(static_cast<std::byte>(length & 0xFFu));
        container.push_back(static_cast<std::byte>((length >> 8u) & 0xFFu));
        container.push_back(static_cast<std::byte>((length >> 16u) & 0xFFu));
        container.push_back(static_cast<std::byte>((length >> 24u) & 0xFFu));
        // the little endian magic is the first word of every spirv module
        for (std::uint32_t octet : {0x03u, 0x02u, 0x23u, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u})
            container.push_back(static_cast<std::byte>(octet));
    }
    // act
    const VulkanShaderData data = parse_vulkan_shader_data(container);
    // assert every section is handed back and carries a loadable module
    ASSERT_EQ(data.modules.size(), 6u);
    for (std::size_t index = 0; index < data.modules.size(); index++)
        EXPECT_EQ(data.modules[index].size(), 8u);
    EXPECT_TRUE(data.complete());
}

TEST(VulkanShaderDataChecks, RefusesAForeignContainer) {
    // arrange a container whose header names another shader family
    std::vector<std::byte> container;
    for (char letter : {'A', 'L', 'B', 'D', 'X', 'B', 'C', '1'})
        container.push_back(static_cast<std::byte>(letter));
    // act
    const VulkanShaderData data = parse_vulkan_shader_data(container);
    // assert nothing is handed back, because no module can be trusted
    EXPECT_TRUE(data.modules[0].empty());
    EXPECT_FALSE(data.complete());
}

TEST(VulkanShaderDataChecks, RefusesAContainerTruncatedInsideALengthPrefix) {
    // arrange a header followed by two bytes of a length prefix, which no module can follow
    std::vector<std::byte> container;
    for (char letter : {'A', 'L', 'B', 'V', 'K', 'S', 'P', 'V'})
        container.push_back(static_cast<std::byte>(letter));
    container.push_back(std::byte{0x08});
    container.push_back(std::byte{0x00});
    // act
    const VulkanShaderData data = parse_vulkan_shader_data(container);
    // assert the prefix the container cannot complete refuses the whole artifact
    EXPECT_TRUE(data.modules[0].empty());
    EXPECT_FALSE(data.complete());
}

TEST(VulkanShaderDataChecks, RefusesATruncatedModule) {
    // arrange a header and a length that names more bytes than the container carries
    std::vector<std::byte> container;
    for (char letter : {'A', 'L', 'B', 'V', 'K', 'S', 'P', 'V'})
        container.push_back(static_cast<std::byte>(letter));
    for (std::uint32_t octet : {0x08u, 0x00u, 0x00u, 0x00u, 0x03u, 0x02u})
        container.push_back(static_cast<std::byte>(octet));
    // act
    const VulkanShaderData data = parse_vulkan_shader_data(container);
    // assert the parse stops before a module it cannot read
    EXPECT_TRUE(data.modules[0].empty());
    EXPECT_FALSE(data.complete());
}

TEST(VulkanShaderDataChecks, RefusesTrailingBytes) {
    // arrange a valid six module container with one byte after the last module
    std::vector<std::byte> container;
    for (char letter : {'A', 'L', 'B', 'V', 'K', 'S', 'P', 'V'})
        container.push_back(static_cast<std::byte>(letter));
    for (int module = 0; module < 6; module++) {
        for (std::uint32_t octet : {0x04u, 0x00u, 0x00u, 0x00u, 0x03u, 0x02u, 0x23u, 0x07u})
            container.push_back(static_cast<std::byte>(octet));
    }
    container.push_back(std::byte{0});
    // act
    const VulkanShaderData data = parse_vulkan_shader_data(container);
    // assert a container with bytes the format does not explain is refused
    EXPECT_TRUE(data.modules[0].empty());
    EXPECT_FALSE(data.complete());
}

TEST(VulkanShaderDataChecks, RefusesAnEmptyContainer) {
    // arrange an empty span, which a host without the shader artifact hands out
    const std::vector<std::byte> empty;
    // act
    const VulkanShaderData data = parse_vulkan_shader_data(empty);
    // assert no module is handed back
    for (const std::span<const std::byte> module : data.modules)
        EXPECT_TRUE(module.empty());
    EXPECT_FALSE(data.complete());
}

TEST(VulkanBlendTermsChecks, NamesEveryCompositionMode) {
    // arrange the eleven modes the hal declares
    const CompositionMode modes[] = {CompositionMode::SourceOver, CompositionMode::SourceIn, CompositionMode::SourceOut, CompositionMode::Atop, CompositionMode::DestinationOver, CompositionMode::DestinationIn, CompositionMode::DestinationOut, CompositionMode::DestinationAtop, CompositionMode::Lighter, CompositionMode::Copy, CompositionMode::Xor};
    // act
    std::size_t exact = 0;
    for (CompositionMode mode : modes) {
        const VulkanBlendTerms terms = vulkan_blend_terms(mode);
        // assert every mode names a factor for the colour and the alpha lanes
        EXPECT_TRUE(terms.color_source != VulkanBlendFactor::Zero || terms.color_destination != VulkanBlendFactor::Zero || mode == CompositionMode::DestinationIn || mode == CompositionMode::DestinationOut);
        if (terms.exact)
            exact++;
    }
    // assert the three destination dependent modes use the exact fragment, which dual source blending reads
    EXPECT_EQ(exact, 3u);
}

TEST(VulkanBlendTermsChecks, MarksTheDualSourceModes) {
    // act
    const VulkanBlendTerms source_in = vulkan_blend_terms(CompositionMode::SourceIn);
    const VulkanBlendTerms copy = vulkan_blend_terms(CompositionMode::Copy);
    const VulkanBlendTerms destination_atop = vulkan_blend_terms(CompositionMode::DestinationAtop);
    const VulkanBlendTerms source_over = vulkan_blend_terms(CompositionMode::SourceOver);
    // assert only the three modes the direct3d table marks exact read the second source
    EXPECT_TRUE(source_in.exact);
    EXPECT_TRUE(copy.exact);
    EXPECT_TRUE(destination_atop.exact);
    EXPECT_FALSE(source_over.exact);
    EXPECT_EQ(source_in.color_destination, VulkanBlendFactor::InverseSource1Alpha);
    EXPECT_EQ(copy.color_destination, VulkanBlendFactor::InverseSource1Alpha);
    EXPECT_EQ(destination_atop.color_destination, VulkanBlendFactor::InverseSource1Color);
}

TEST(VulkanExtentChecks, ClampsARequestIntoTheSurfaceRange) {
    // act
    const VulkanExtent clamped = clamp_vulkan_extent(1024.0f, 768.0f, 640u, 480u);
    // assert each axis takes the surface limit
    EXPECT_EQ(clamped.width, 640u);
    EXPECT_EQ(clamped.height, 480u);
}

TEST(VulkanExtentChecks, KeepsARequestWithoutASurfaceLimit) {
    // act
    const VulkanExtent kept = clamp_vulkan_extent(800.0f, 600.0f, 0u, 0u);
    // assert a zero limit means the request is kept
    EXPECT_EQ(kept.width, 800u);
    EXPECT_EQ(kept.height, 600u);
}

TEST(VulkanExtentChecks, FloorsAZeroRequestAtOnePixel) {
    // act
    const VulkanExtent floored = clamp_vulkan_extent(0.0f, -4.0f, 0u, 0u);
    // assert a surface with no area still names the one pixel a target allocates
    EXPECT_EQ(floored.width, 1u);
    EXPECT_EQ(floored.height, 1u);
}
