#include "albor/render/render-albor/albor_backend_support.h"
#include "albor/render/render-albor/albor_vulkan_diagnostics.h"

#if defined(ALBOR_VULKAN_WAYLAND)
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#if defined(ALBOR_VULKAN_XCB)
#define VK_USE_PLATFORM_XCB_KHR
#endif

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "albor/core/geometry.h"
#include "albor/platform/linux/wsi_surface.h"
#include "albor/render/aged_cache.h"
#include "albor/render/frame_profile.h"
#include "albor/render/image_resource.h"
#include "albor/render/render-albor/albor_batch.h"
#include "albor/render/render-albor/albor_geometry.h"
#include "albor/render/render-albor/albor_glyph_cache.h"
#include "albor/render/render-albor/albor_render_context.h"
#include "albor/render/render-albor/albor_shaders.h"
#include "albor/render/render-albor/albor_vulkan_support.h"
#include "albor/render/render_context.h"
#include "albor/window/window_metrics.h"

#if defined(ALBOR_VULKAN_WAYLAND)
#include "albor/platform/linux/wayland/native_surface.linux.h"
#endif

#if defined(ALBOR_VULKAN_XCB)
#include "albor/platform/linux/x11/native_surface.linux.h"
#endif

namespace albor::render
{
    namespace
    {
        // the pixel format every target uses, which reads back as canonical rgba8 bytes (§6.3, AD-26)
        constexpr VkFormat TARGET_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;

        // the pixel format every uploaded image uses, which is the canonical premultiplied rgba8 order (§6.4, AD-26)
        constexpr VkFormat TEXTURE_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

        // how many composition modes the pipeline table carries, which indexes the array the draw binds (§6.2)
        constexpr std::size_t COMPOSITION_MODES = 11;

        // the shader names modes by index, which must follow the hal order it shapes its alpha for (§6.2)
        static_assert(static_cast<int>(CompositionMode::SourceIn) == 1 && static_cast<int>(CompositionMode::SourceOut) == 2 && static_cast<int>(CompositionMode::DestinationIn) == 5 && static_cast<int>(CompositionMode::DestinationAtop) == 7 && static_cast<int>(CompositionMode::Copy) == 9, "the shader composition constants must follow the hal order");

        // the structured vertex stride is the one the shader indexes, so the two layouts cannot drift (§6.3)
        static_assert(sizeof(BatchVertex) == 32, "the batch vertex must stay the stride the shader indexes");

        // one draw state, which both shader stages read as a packed buffer, aligned as the hlsl cbuffer pads it (§6.3)
        struct alignas(16) DrawUniforms
        {
            // the premultiplied colour of the draw
            float color[4];

            // the first row of the inverse clip matrix, followed by its translation
            float inverse0[4];

            // the second row of the inverse clip matrix, followed by its translation
            float inverse1[4];

            // the clip half extents, followed by the per axis feather scales
            float extent_scale[4];

            // the clip corner radius, which zero leaves the clip rectangular
            float extra[4];

            // the target extent in device pixels, which projects a position into clip space
            float target_size[2];

            // the ramp a folded coverage folds by, which a plain draw ignores
            float ramp;

            // the coverage kind, where one folds and zero passes the lateral seed through
            std::int32_t kind;

            // whether the draw is cut by a scissor at all
            std::int32_t scissor_enabled;

            // whether the draw samples a texture rather than a flat colour
            std::int32_t has_texture;

            // the composition the draw blends with, which the shader reads its residual alpha from
            std::int32_t blend;

            // the first vertex of the draw, which the vulkan shader adds like the direct3d one (AD-05)
            std::int32_t base_vertex;
        };

        // the hlsl cbuffer pads to a sixteen byte boundary, so the host side must carry the same size
        static_assert(sizeof(DrawUniforms) == 112, "the draw uniforms must match the hlsl layout");

        // the per shadow state, which the silhouette, the blur and the composite passes read (§6.2)
        struct alignas(16) ShadowUniforms
        {
            // the premultiplied colour of the cast
            float color[4];

            // the caster's half extent in its own space, and the offscreen origin on zw
            float shape[4];

            // the corner radius, the blur direction and the uv tap step
            float params[4];

            // the first row of the inverse that maps a device point into the caster's space
            float inverse0[4];

            // the second row of that inverse, which finishes the mapping
            float inverse1[4];
        };

        // the shadow struct pads to a sixteen byte boundary, which the hlsl side mirrors
        static_assert(sizeof(ShadowUniforms) == 80, "the shadow uniforms must match the hlsl layout");

        // the descriptor bindings the shader shifts place on the one set layout every pipeline uses (§6.3)
        constexpr std::uint32_t UNIFORM_BINDING = 0;
        constexpr std::uint32_t PAINT_BINDING = 10;
        constexpr std::uint32_t BLUR_BINDING = 11;
        constexpr std::uint32_t SAMPLER_BINDING = 20;
        constexpr std::uint32_t VERTEX_BINDING = 30;

        // how many descriptors one frame may allocate, which bounds a scene with thousands of draws (§6.3)
        constexpr std::uint32_t DESCRIPTOR_CAPACITY = 8192;

        // one gpu image with the view the passes bind, which owns its memory (§6.3)
        struct Image
        {
            // the image the passes write or sample
            VkImage image = VK_NULL_HANDLE;

            // the memory the image is bound to
            VkDeviceMemory memory = VK_NULL_HANDLE;

            // the view the passes bind
            VkImageView view = VK_NULL_HANDLE;

            // the physical width of the image
            std::uint32_t width = 0;

            // the physical height of the image
            std::uint32_t height = 0;

            // how many mip levels the image carries
            std::uint32_t levels = 1;

            // the layout the image is currently in
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

            Image() = default;
            ~Image();

            Image(const Image&) = delete;
            Image& operator=(const Image&) = delete;
            Image(Image&& other) noexcept;
            Image& operator=(Image&& other) noexcept;

            // release the view, memory and handle the image carries, which a move and a destructor call
            void destroy();

            // report whether the image carries something a pass can bind
            [[nodiscard]] bool valid() const;
        };

        // one buffer with the mapping a host visible upload reads, which owns its memory (§6.3)
        struct Buffer
        {
            // the buffer the stage reads
            VkBuffer buffer = VK_NULL_HANDLE;

            // the memory the buffer is bound to
            VkDeviceMemory memory = VK_NULL_HANDLE;

            // the mapping a host visible buffer writes through
            void* mapped = nullptr;

            // the byte size of the buffer
            VkDeviceSize size = 0;

            Buffer() = default;
            ~Buffer();

            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;
            Buffer(Buffer&& other) noexcept;
            Buffer& operator=(Buffer&& other) noexcept;

            // release the mapping, memory and handle the buffer carries, which a move and a destructor call
            void destroy();

            // report whether the buffer carries a range a stage can read
            [[nodiscard]] bool valid() const;
        };

        // the compiled stages the device loads, which the committed container carries (§6.3, AD-16)
        struct ShaderData
        {
            // the batch vertex stage
            std::span<const std::byte> vertex;

            // the fragment every ordinary draw runs
            std::span<const std::byte> fragment;

            // the fragment that writes the blend control dual source blending reads
            std::span<const std::byte> fragment_exact;

            // the vertex stage the two offscreen passes share
            std::span<const std::byte> shadow_vertex;

            // the fragment that rasterizes the rounded silhouette
            std::span<const std::byte> shadow_shape;

            // the fragment that runs one separable gaussian pass
            std::span<const std::byte> shadow_blur;
        };

        // the device wide state every surface draws through, which outlives them (§6.3)
        struct DeviceState
        {
            // the instance every surface and swapchain hangs from
            VkInstance instance = VK_NULL_HANDLE;

            // the physical device the backend draws with
            VkPhysicalDevice physical = VK_NULL_HANDLE;

            // the properties the extent and alignment decisions read
            VkPhysicalDeviceProperties properties{};

            // the memory types the buffers and images allocate from
            VkPhysicalDeviceMemoryProperties memory{};

            // the logical device every surface draws through
            VkDevice device = VK_NULL_HANDLE;

            // the queue family the one queue belongs to
            std::uint32_t queue_family = 0;

            // the queue every frame is submitted on
            VkQueue queue = VK_NULL_HANDLE;

            // the pool the one-time device uploads are recorded from
            VkCommandPool command_pool = VK_NULL_HANDLE;

            // the one command buffer the device build uploads through, which no frame records into (§6.3)
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;

            // the fence the device build waits on
            VkFence fence = VK_NULL_HANDLE;

            // the layout every pipeline binds its descriptors through
            VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;

            // the layout every pipeline is built over
            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

            // the batch vertex stage every draw runs
            VkShaderModule vertex_shader = VK_NULL_HANDLE;

            // the fragment every ordinary draw runs
            VkShaderModule fragment_shader = VK_NULL_HANDLE;

            // the fragment that writes the blend control dual source blending reads
            VkShaderModule fragment_exact = VK_NULL_HANDLE;

            // the vertex stage the two offscreen passes share
            VkShaderModule shadow_vertex = VK_NULL_HANDLE;

            // the fragment that rasterizes a rounded silhouette
            VkShaderModule shadow_shape = VK_NULL_HANDLE;

            // the fragment that runs one separable gaussian pass
            VkShaderModule shadow_blur = VK_NULL_HANDLE;

            // the pipeline each composition mode binds, indexed by the mode the draw carries (§6.2)
            std::array<VkPipeline, COMPOSITION_MODES> pipelines{};

            // the pipelines that read the destination, which the modes a fixed blender cannot express bind (§6.2)
            std::array<VkPipeline, COMPOSITION_MODES> exact_pipelines{};

            // the pipeline that rasterizes a rounded silhouette, which a cast is blurred from (§6.2)
            VkPipeline shadow_shape_pipeline = VK_NULL_HANDLE;

            // the pipeline that runs one separable gaussian pass over the silhouette (§6.2)
            VkPipeline shadow_blur_pipeline = VK_NULL_HANDLE;

            // the sampler every draw reads its texture through
            VkSampler sampler = VK_NULL_HANDLE;

            // the one pixel white texture a solid draw samples, which keeps one shader path for every draw
            Image white;

            // a one vertex buffer the passes that read no vertex bind, which keeps their descriptor set valid (§6.3)
            Buffer dummy_vertices;

            // the aligned stride one uniform block occupies, which every surface rings its own blocks through
            VkDeviceSize uniform_stride = 0;

            // whether the target format supports the blit a differently sized present needs (§6.3)
            bool target_blittable = false;

            // whether the uploaded texture format supports the blit chain a mip map needs (§6.4)
            bool texture_blittable = false;

            // whether the instance carries the wayland surface extension
            bool wayland_extension = false;

            // whether the instance carries the xcb surface extension
            bool xcb_extension = false;

            // whether the device carries the swapchain extension
            bool swapchain_extension = false;

            // whether the shared state finished building, which every later call reads (§6.3)
            bool built = false;

            // whether the build was already tried, so a failed host never retries it (§6.3)
            bool attempted = false;

            // whether the driver removed the device mid session, which no later call can draw through (§6.3)
            std::atomic<bool> lost{false};
        };

        // the process wide device state, which the first surface builds and every later one reuses (§6.3)
        DeviceState& device_state() {
            // the state is never destroyed, because the driver owns its objects for the process lifetime (§6.3)
            static DeviceState* state = new DeviceState();
            return *state;
        }

        // the lock that guards the first build of the shared state across threads (§10.5)
        std::mutex& device_mutex() {
            // one mutex guards the lazy build, which no later access needs again (§10.5)
            static std::mutex mutex;
            return mutex;
        }

        // the lock that serializes the queue, because every surface submits on the one graphics queue (§6.3)
        std::mutex& queue_mutex() {
            // the mutex is held only around the submit and the present, because the queue is the shared object (§6.3)
            static std::mutex mutex;
            return mutex;
        }

        // count the frames the process submitted, which a test reads to prove a windowed frame passed the present (§6.3)
        std::atomic<std::uint64_t>& submitted_frames() {
            // the counter lives for the process, because one present is one frame whichever surface submitted it (§6.3)
            static std::atomic<std::uint64_t> frames{0};
            return frames;
        }

        // count the device memory allocations the process made, which a steady frame must not raise (§6.3, AD-06)
        std::atomic<std::uint64_t>& device_allocations() {
            // the counter lives for the process, because every allocation comes from the one device (§6.3)
            static std::atomic<std::uint64_t> allocations{0};
            return allocations;
        }

        // record a driver loss, which every surface reads as invalid rather than presenting a frozen frame (§6.3)
        void note_result(VkResult result) {
            // only a lost device ends the session, which every other failure leaves recoverable (§6.3)
            if (result == VK_ERROR_DEVICE_LOST)
                device_state().lost.store(true);
        }

        // map one portable blend factor onto the vulkan enum the pipeline table bakes (§6.2)
        [[nodiscard]] VkBlendFactor blend_factor(VulkanBlendFactor factor) {
            // every portable factor names the same arithmetic the vulkan blender evaluates (§6.2)
            switch (factor) {
            case VulkanBlendFactor::Zero:
                return VK_BLEND_FACTOR_ZERO;
            case VulkanBlendFactor::One:
                return VK_BLEND_FACTOR_ONE;
            case VulkanBlendFactor::SourceAlpha:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case VulkanBlendFactor::InverseSourceAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case VulkanBlendFactor::DestinationAlpha:
                return VK_BLEND_FACTOR_DST_ALPHA;
            case VulkanBlendFactor::InverseDestinationAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case VulkanBlendFactor::Source1Alpha:
                return VK_BLEND_FACTOR_SRC1_ALPHA;
            case VulkanBlendFactor::InverseSource1Alpha:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            case VulkanBlendFactor::Source1Color:
                return VK_BLEND_FACTOR_SRC1_COLOR;
            case VulkanBlendFactor::InverseSource1Color:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            }
            // source over is the fallback every unknown factor leaves, which matches the hal order (§6.2)
            return VK_BLEND_FACTOR_ONE;
        }

        // round an offset up to the alignment the driver demands for a uniform range (§6.3)
        [[nodiscard]] VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
            // an alignment of zero carries no constraint, which a driver cannot report but a test can (§6.3)
            if (alignment == 0)
                return value;
            return (value + alignment - 1) / alignment * alignment;
        }

        // round an offscreen extent up to a bucket, so a cast that wobbles by a pixel keeps its target (§6.2, AD-06)
        [[nodiscard]] std::uint32_t bucket_extent(std::uint32_t value) {
            // the bucket trades a few pixels of memory for a target a wobbling cast keeps (§6.2, AD-06)
            constexpr std::uint32_t SCRATCH_BUCKET = 16;
            return (value + SCRATCH_BUCKET - 1u) / SCRATCH_BUCKET * SCRATCH_BUCKET;
        }

        // find the memory type a resource needs, which reports the first that satisfies every property (§6.3)
        [[nodiscard]] std::optional<std::uint32_t> find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags properties) {
            // the physical device names the memory types the resource may allocate from (§6.3)
            const VkPhysicalDeviceMemoryProperties& memory = device_state().memory;
            for (std::uint32_t index = 0; index < memory.memoryTypeCount; index++) {
                // a type the resource cannot use is passed over, because binding would fail (§6.3)
                if ((bits & (1u << index)) == 0)
                    continue;
                // the first type carrying every property is the one the resource takes (§6.3)
                if ((memory.memoryTypes[index].propertyFlags & properties) == properties)
                    return index;
            }
            // a resource with no acceptable type answers nothing, which the caller refuses (§6.3)
            return std::nullopt;
        }

        // report whether the instance advertises an extension, which the surface paths read before they enable one (§6.3)
        [[nodiscard]] bool has_instance_extension(const char* name, const std::vector<VkExtensionProperties>& extensions) {
            // every advertised extension is compared by name, because the loader reports no stable handle (§6.3)
            return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
        }

        // report whether the device advertises an extension, which the swapchain path reads (§6.3)
        [[nodiscard]] bool has_device_extension(const char* name, const std::vector<VkExtensionProperties>& extensions) {
            // every advertised extension is compared by name, because the driver reports no stable handle (§6.3)
            return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
        }

        // create one image, its memory and the view every pass binds (§6.3)
        [[nodiscard]] VkResult create_image(std::uint32_t width, std::uint32_t height, std::uint32_t levels, VkFormat format, VkImageUsageFlags usage, VkImageLayout initial, Image& out) {
            // an extent with no pixel cannot be allocated, so the caller's request is refused (§6.3)
            if (width == 0 || height == 0)
                return VK_ERROR_INITIALIZATION_FAILED;
            // the description names the format the caller carries, which the target and the textures differ on (§6.3)
            VkImageCreateInfo description{};
            description.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            description.imageType = VK_IMAGE_TYPE_2D;
            description.format = format;
            description.extent = {width, height, 1};
            description.mipLevels = levels;
            description.arrayLayers = 1;
            description.samples = VK_SAMPLE_COUNT_1_BIT;
            description.tiling = VK_IMAGE_TILING_OPTIMAL;
            description.usage = usage;
            description.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            // the image is allocated device local, because every pass reads or writes it on the gpu (§6.3)
            Image image;
            image.width = width;
            image.height = height;
            image.levels = levels;
            image.layout = initial;
            VkResult result = vkCreateImage(device_state().device, &description, nullptr, &image.image);
            if (result != VK_SUCCESS)
                return result;
            // the memory requirements name the type and the size the image binds (§6.3)
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device_state().device, image.image, &requirements);
            // a resource no memory type satisfies is refused, which leaves no handle behind (§6.3)
            const std::optional<std::uint32_t> memory_type = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (!memory_type.has_value()) {
                image.destroy();
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = *memory_type;
            result = vkAllocateMemory(device_state().device, &allocation, nullptr, &image.memory);
            if (result != VK_SUCCESS) {
                // a refused allocation releases the image, so a failed frame leaks no handle (§6.3)
                image.destroy();
                return result;
            }
            // the counter records the allocation, which a steady frame must not raise (§6.3, AD-06)
            device_allocations().fetch_add(1u, std::memory_order_relaxed);
            result = vkBindImageMemory(device_state().device, image.image, image.memory, 0);
            if (result != VK_SUCCESS) {
                image.destroy();
                return result;
            }
            // the view covers the whole image, which every pass binds (§6.3)
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = image.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = levels;
            view.subresourceRange.layerCount = 1;
            result = vkCreateImageView(device_state().device, &view, nullptr, &image.view);
            if (result != VK_SUCCESS) {
                image.destroy();
                return result;
            }
            out = std::move(image);
            return VK_SUCCESS;
        }

        // create a host visible buffer and map it for the writes the cpu makes (§6.3)
        [[nodiscard]] bool create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out) {
            // a buffer with no byte cannot be created, so the caller's request is refused (§6.3)
            if (size == 0)
                return false;
            VkBufferCreateInfo description{};
            description.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            description.size = size;
            description.usage = usage;
            description.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            Buffer buffer;
            buffer.size = size;
            VkResult result = vkCreateBuffer(device_state().device, &description, nullptr, &buffer.buffer);
            if (result != VK_SUCCESS)
                return false;
            // the requirements name the type and the size the buffer binds (§6.3)
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device_state().device, buffer.buffer, &requirements);
            // a resource no memory type satisfies is refused, which releases the buffer handle (§6.3)
            const std::optional<std::uint32_t> memory_type = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (!memory_type.has_value()) {
                buffer.destroy();
                return false;
            }
            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = *memory_type;
            result = vkAllocateMemory(device_state().device, &allocation, nullptr, &buffer.memory);
            if (result != VK_SUCCESS) {
                buffer.destroy();
                return false;
            }
            // the counter records the allocation, which a steady frame must not raise (§6.3, AD-06)
            device_allocations().fetch_add(1u, std::memory_order_relaxed);
            result = vkBindBufferMemory(device_state().device, buffer.buffer, buffer.memory, 0);
            if (result != VK_SUCCESS) {
                buffer.destroy();
                return false;
            }
            // the mapping is held for the buffer's life, because every frame writes through it (§6.3)
            result = vkMapMemory(device_state().device, buffer.memory, 0, size, 0, &buffer.mapped);
            if (result != VK_SUCCESS) {
                buffer.destroy();
                return false;
            }
            out = std::move(buffer);
            return true;
        }

        // the access mask a layout leaves behind, which the barrier that leaves it reads (§6.3)
        [[nodiscard]] VkAccessFlags source_access(VkImageLayout layout) {
            // a layout that carried a write reports it, so the next pass sees the pixels (§6.3)
            switch (layout) {
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_ACCESS_SHADER_READ_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return VK_ACCESS_TRANSFER_READ_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return VK_ACCESS_TRANSFER_WRITE_BIT;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                // the presentation engine already made the image visible, so the barrier carries no source access (§6.3)
                return 0;
            default:
                return 0;
            }
        }

        // the stage a layout leaves behind, which the barrier that leaves it waits on (§6.3)
        [[nodiscard]] VkPipelineStageFlags source_stage(VkImageLayout layout) {
            // the stage that produced the last write is the one the barrier waits on (§6.3)
            switch (layout) {
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            default:
                return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            }
        }

        // move one image into a layout the next pass reads, recording the dependency the barrier needs (§6.3)
        void transition_image(VkCommandBuffer command, VkImage image, VkImageLayout& layout, VkImageLayout next, VkAccessFlags destination_access, VkPipelineStageFlags destination_stage) {
            // an image already in the asked layout needs no dependency, which keeps a frame from over synchronizing (§6.3)
            if (layout == next)
                return;
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = layout;
            barrier.newLayout = next;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            // the source of a layout the image never carried is nothing, which an undefined start names (§6.3)
            barrier.srcAccessMask = source_access(layout);
            barrier.dstAccessMask = destination_access;
            vkCmdPipelineBarrier(command, source_stage(layout), destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            // the new layout is what the next barrier leaves, which the next pass records (§6.3)
            layout = next;
        }

        // move one image into a layout, which the target path names through its own tracker (§6.3)
        void transition_image(VkCommandBuffer command, Image& image, VkImageLayout next, VkAccessFlags destination_access, VkPipelineStageFlags destination_stage) {
            // the image carries its own layout, which every transition replaces (§6.3)
            transition_image(command, image.image, image.layout, next, destination_access, destination_stage);
        }

        // move one mip range between layouts, which a chain of levels needs on its own (§6.4)
        void transition_mips(VkCommandBuffer command, VkImage image, std::uint32_t base, std::uint32_t count, VkImageLayout old_layout, VkImageLayout next_layout, VkAccessFlags source_access_mask, VkAccessFlags destination_access_mask, VkPipelineStageFlags source_stage_mask, VkPipelineStageFlags destination_stage_mask) {
            // one barrier covers the range, because every level in it leaves and enters together (§6.4)
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = old_layout;
            barrier.newLayout = next_layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = base;
            barrier.subresourceRange.levelCount = count;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = source_access_mask;
            barrier.dstAccessMask = destination_access_mask;
            vkCmdPipelineBarrier(command, source_stage_mask, destination_stage_mask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // the state one encoded frame carries, which the target and shadow passes share (§6.3)
        struct Encoder
        {
            // the command buffer the frame records into
            VkCommandBuffer command = VK_NULL_HANDLE;

            // whether a dynamic rendering pass is open
            bool rendering = false;

            // the target the open pass writes, which a repeated bind reads
            Image* target = nullptr;
        };

        // end the open pass, which every target change and every frame end calls (§6.3)
        void end_rendering(Encoder& encoder) {
            // a frame that never opened a pass has nothing to end, which the end of an empty scene is (§6.3)
            if (!encoder.rendering)
                return;
            vkCmdEndRendering(encoder.command);
            encoder.rendering = false;
            encoder.target = nullptr;
        }

        // begin a dynamic rendering pass over one target, loading or clearing it as the frame directs (§6.3)
        void begin_rendering(Encoder& encoder, Image& target, bool clear) {
            // a pass already writing this target with a load continues, because no state has to change (§6.3)
            if (encoder.rendering && encoder.target == &target && !clear)
                return;
            // every other pass ends first, because dynamic rendering carries one target at a time (§6.3)
            end_rendering(encoder);
            // the target moves into the attachment layout, which the pass writes (§6.3)
            transition_image(encoder.command, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            VkRenderingAttachmentInfo attachment{};
            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = target.view;
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.clearValue.color.float32[0] = 0.0f;
            attachment.clearValue.color.float32[1] = 0.0f;
            attachment.clearValue.color.float32[2] = 0.0f;
            attachment.clearValue.color.float32[3] = 0.0f;
            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea = {{0, 0}, {target.width, target.height}};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &attachment;
            vkCmdBeginRendering(encoder.command, &rendering);
            // the viewport covers the target, flipped so the top left device origin matches the shader (§6.3)
            VkViewport viewport{0.0f, static_cast<float>(target.height), static_cast<float>(target.width), -static_cast<float>(target.height), 0.0f, 1.0f};
            vkCmdSetViewport(encoder.command, 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {target.width, target.height}};
            vkCmdSetScissor(encoder.command, 0, 1, &scissor);
            encoder.rendering = true;
            encoder.target = &target;
        }

        // move a target the composite samples into the read only layout, which a fragment reads (§6.2)
        void sample_target(Encoder& encoder, Image& target) {
            // the pass writing the target ends before the fragment can sample it (§6.2)
            end_rendering(encoder);
            transition_image(encoder.command, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }

        // build the device, its compiled stages and the pipeline table once, which every surface shares (§6.3)
        bool build_device_state(DeviceState& state);

        // the lazy build every platform surface answers on its own (§6.2)
        bool ensure_device_state() {
            // the first build is serialized, because two surfaces may race for it (§10.5)
            const std::lock_guard<std::mutex> guard(device_mutex());
            // a state that already ran is handed out as it is, which a failed host never retries (§6.3)
            if (device_state().attempted)
                return device_state().built;
            // the build runs once, and its result is what readiness reports (§6.2)
            device_state().attempted = true;
            build_device_state(device_state());
            return device_state().built;
        }

        // a structured vertex buffer a frame slot keeps, which a steady frame rewrites (§6.3, AD-06)
        struct RetainedVertices
        {
            // the buffer the vertex stage reads as a storage buffer (§6.3)
            Buffer buffer;

            // how many vertices the buffer holds, which a larger batch grows (§6.3)
            std::size_t capacity = 0;
        };

        // an index buffer a frame slot keeps, which a steady frame rewrites instead of allocating (§6.3, AD-06)
        struct RetainedIndices
        {
            // the buffer the index stage reads (§6.3)
            Buffer buffer;

            // how many indices the buffer holds, which a larger batch grows (§6.3)
            std::size_t capacity = 0;
        };

        // one offscreen target a slot keeps, which a shadow or a layer paints into (§6.2, AD-06)
        struct ScratchTarget
        {
            // the target the pass paints and the composite samples (§6.2)
            Image image;

            // whether the current frame already took the target, which a same size pass reuses (§6.2)
            bool in_use = false;
        };

        // the identity a descriptor set is cached under, which the draws of one frame share (§6.3)
        struct DescriptorKey
        {
            // the vertex buffer the stage indexes (§6.3)
            VkBuffer vertices = VK_NULL_HANDLE;

            // the byte offset of the range the stage indexes (§6.3)
            VkDeviceSize offset = 0;

            // the texel the fragment samples, which a white texel stands in for a solid draw (§6.3, §6.4)
            VkImageView paint = VK_NULL_HANDLE;

            // the texel the blur pipeline samples, which a blur pass sources and every other draw leaves white (§6.2)
            VkImageView blur = VK_NULL_HANDLE;
        };

        // one descriptor set the slot already allocated for a key, which the next draw of that key reuses (§6.3)
        struct DescriptorEntry
        {
            // the identity the set was written for (§6.3)
            DescriptorKey key;

            // the set the draw binds, whose uniform offset rides in the dynamic offset (§6.3)
            VkDescriptorSet set = VK_NULL_HANDLE;
        };

        // one frame in flight, which owns everything the gpu reads until its fence signals (§6.3)
        struct FrameSlot
        {
            // the command buffer the frame records into (§6.3)
            VkCommandBuffer command = VK_NULL_HANDLE;

            // the fence the submit signals and the next use of the slot waits on (§6.3)
            VkFence fence = VK_NULL_HANDLE;

            // whether the fence carries a submit, which a slot the surface never presented leaves false (§6.3)
            bool submitted = false;

            // the semaphore the acquire signals and the submit waits on (§6.3)
            VkSemaphore image_available = VK_NULL_HANDLE;

            // the semaphore the submit signals and the present waits on (§6.3)
            VkSemaphore render_finished = VK_NULL_HANDLE;

            // the pool the frame allocates its descriptors from, which the next use of the slot resets (§6.3)
            VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

            // the host visible buffer the frame writes its uniforms into (§6.3)
            Buffer uniforms;

            // the next free offset in the uniform buffer (§6.3)
            VkDeviceSize uniform_cursor = 0;

            // the retained vertex buffer the batch fills (§6.3, AD-06)
            RetainedVertices vertices;

            // the retained vertex buffer the composite quads fill (§6.3, AD-06)
            RetainedVertices quad_vertices;

            // how many quad vertices this frame has written, which every composite draw reserves a range from (§6.3)
            std::size_t quad_cursor = 0;

            // the retained index buffer the batch fills (§6.3, AD-06)
            RetainedIndices indices;

            // the offscreen targets the slot keeps between its frames, which a pass indexes (§6.2, AD-06)
            std::deque<ScratchTarget> scratch;

            // the buffers the frame retired, which the next use of the slot releases (§6.3)
            std::vector<Buffer> retired;

            // the textures dropped while this slot recorded, released when the slot is recycled (§6.3, AD-06)
            std::vector<Image> retired_images;

            // the descriptor sets the frame already wrote, which a draw of the same key reuses (§6.3)
            std::vector<DescriptorEntry> descriptors;
        };

        // how many frames the backend keeps in flight, which the gpu works through while the cpu records (§6.3, AD-18b)
        constexpr std::uint32_t FRAME_SLOTS = 2;

        // the surface the backend presents into, which owns one colour target per frame (§6.2, §6.3)
        class AlborRenderSurface final : public RenderSurface
        {
        public:
            // build a windowed surface over the native handle, or an offscreen one when it is empty (§6.2)
            AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics, std::shared_ptr<std::atomic<std::uint64_t>> batch_buffer_allocations);
            ~AlborRenderSurface() override;

            AlborRenderSurface(const AlborRenderSurface&) = delete;
            AlborRenderSurface& operator=(const AlborRenderSurface&) = delete;
            AlborRenderSurface(AlborRenderSurface&&) = delete;
            AlborRenderSurface& operator=(AlborRenderSurface&&) = delete;

            [[nodiscard]] bool valid() const override;
            [[nodiscard]] const WindowMetrics& metrics() const override;
            void resize(const WindowMetrics& metrics) override;
            [[nodiscard]] bool retains_target() const override;
            bool set_damage_region(const std::optional<Rect>& damage) override;
            RenderContext& begin_frame() override;
            void end_frame() override;
            [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override;

        private:
            // the implementation map begins with the colour target, which a resize replaces (§6.2, §6.3)
            void allocate(const WindowMetrics& extent);

            // create the swapchain a window presents through, at the physical extent (§6.3)
            bool create_swapchain(const WindowMetrics& extent);

            // destroy the swapchain and the surface, which a teardown and a refused recreate share (§6.3)
            void destroy_swapchain();

            // report whether the native window a surface presents through is still alive (§10.5)
            [[nodiscard]] bool window_live() const;

            // create one frame slot, its command buffer, fence, semaphores, pool and buffers (§6.3)
            bool create_slot(FrameSlot& slot);

            // release one frame slot, which a teardown runs after the device drained (§6.3)
            void destroy_slot(FrameSlot& slot);

            // wait the fence the slot's previous frame signalled and release what that frame retired (§6.3)
            bool begin_slot(FrameSlot& slot);

            // wait every submitted slot of this surface, which a resize and a read back need (§6.3)
            void wait_for_idle();

            // recreate the slot semaphores, which a present the surface rejected leaves signalled (§6.3)
            void reset_semaphores();

            // submit the recorded command buffer, which signals the slot fence and the given semaphore (§6.3)
            bool submit_slot(FrameSlot& slot, VkSemaphore wait, VkPipelineStageFlags wait_stage, VkSemaphore signal);

            // take one offscreen target of the exact extent from the slot, growing the slot when it has none (§6.2, AD-06)
            [[nodiscard]] std::uint32_t acquire_scratch(FrameSlot& slot, std::uint32_t width, std::uint32_t height);

            // copy every image the frame carries into the texture cache, which a later frame reuses (AD-06)
            void upload_textures(FrameSlot& slot);

            // fill the retained vertex buffer, which grows only when the batch outgrows it (§6.3, AD-06)
            [[nodiscard]] bool fill_vertices(RetainedVertices& target, const BatchVertex* vertices, std::size_t count, VkBufferUsageFlags usage);

            // append one composite quad and hand out its byte offset, which keeps every deferred draw's range apart (§6.3)
            [[nodiscard]] std::uint32_t fill_quad_vertices(FrameSlot& slot, const BatchVertex* quad, std::size_t count);

            // fill the retained index buffer, which grows only when the batch outgrows it (§6.3, AD-06)
            [[nodiscard]] bool fill_indices(FrameSlot& slot, const std::vector<std::uint32_t>& indices);

            // count one retained batch buffer, which the backend reports and a steady frame must not raise (§6.3, AD-06)
            void note_batch_buffer_allocation();

            // write one uniform block into the slot's ring, which reports the aligned offset the draw binds (§6.3)
            [[nodiscard]] std::uint32_t write_uniforms(FrameSlot& slot, const void* block, std::size_t size);

            // hand out the descriptor set a draw binds, which the frame allocates and writes once per key (§6.3)
            [[nodiscard]] VkDescriptorSet descriptor_for(FrameSlot& slot, VkBuffer vertices, VkDeviceSize vertex_offset, VkDeviceSize vertex_range, VkImageView paint, VkImageView blur);

            // encode every recorded draw of the batch into the frame (§6.3)
            void encode(FrameSlot& slot);

            // record one batch draw with the pipeline its composition names (§6.2)
            void draw_batch_draw(FrameSlot& slot, Encoder& encoder, const BatchDraw& draw, Image& target);

            // blur a cast offscreen and composite it under the shape, which runs three passes (§6.2)
            void composite_shadow(FrameSlot& slot, Encoder& encoder, const BatchShadow& shadow, Image& target);

            // run one separable gaussian pass over the silhouette (§6.2)
            void blur_pass(FrameSlot& slot, Encoder& encoder, Image& source, Image& target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height);

            // composite a layer onto the target beneath it at the opacity its group names (§6.2)
            void composite_layer(FrameSlot& slot, Encoder& encoder, float opacity, Image& offscreen, Image& parent);

            // draw one six vertex quad through the ordinary fragment stage (§6.2)
            void draw_quad(FrameSlot& slot, Encoder& encoder, const BatchVertex* quad, const DrawUniforms& uniforms, VkImageView texture, VkPipeline pipeline);

            // copy the finished frame into the swapchain and submit it (§6.3)
            bool present(FrameSlot& slot);

            // follow the surface a window rebuilt for a fresh role, which a reopened popup does (§5.1, §6.3)
            bool refresh_window_surface();

            // report validity to a caller that already holds the surface lock (§6.3)
            [[nodiscard]] bool valid_locked() const;

            // the window system the surface was built over, which the file descriptor keeps for the swapchain (§6.3)
            NativeWindowHandle m_handle;

#if defined(ALBOR_VULKAN_WAYLAND)
            // the wayland state the surface presents through, which keeps the display alive (§10.5)
            std::shared_ptr<wayland::WaylandSurfaceState> m_wayland;

            // the wl_surface the wsi surface was built from, which a rebuilt role replaces (§5.1)
            wl_surface* m_wayland_surface = nullptr;
#endif

#if defined(ALBOR_VULKAN_XCB)
            // the xcb state the surface presents through, which keeps the connection alive (§10.5)
            std::shared_ptr<x11::X11SurfaceState> m_xcb;
#endif

            // the lock that serializes the surface state, which a resize and a reader both take (§10.5)
            mutable std::mutex m_mutex;

            // the metrics the surface presents at (§6.1, D9.1)
            WindowMetrics m_metrics;

            // the region the next frame may redraw, or nothing for a full target clear (§6.1)
            std::optional<Rect> m_damage;

            // whether the target carries a completed full frame, which a partial frame needs before it loads (§6.1)
            bool m_target_initialized = false;

            // the colour target every frame draws into, which the read back and the present both read (§6.3)
            Image m_target;

            // the pool the surface's frame command buffers are allocated from (§6.3)
            VkCommandPool m_command_pool = VK_NULL_HANDLE;

            // the frames the surface keeps in flight, which the cpu records ahead through (§6.3, AD-18b)
            std::array<FrameSlot, FRAME_SLOTS> m_slots{};

            // the slot the next frame uses, which advances once per submit (§6.3)
            std::uint32_t m_slot_cursor = 0;

            // the slot the last frame submitted, which a read back waits on (§6.3)
            std::uint32_t m_last_slot = 0;

            // the surface the window presents through, which a null handle leaves empty (§6.3)
            VkSurfaceKHR m_surface = VK_NULL_HANDLE;

            // the swapchain the window presents through (§5.3, §6.3)
            VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

            // the format the swapchain images carry, which the copy reads (§6.3)
            VkFormat m_swapchain_format = VK_FORMAT_UNDEFINED;

            // whether the swapchain format supports the blit a differently sized or formatted copy needs (§6.3)
            bool m_swapchain_blittable = false;

            // whether the last present reported a suboptimal image, which the next frame rebuilds (§6.3)
            bool m_swapchain_stale = false;

            // the extent the swapchain images carry, which the copy reads (§6.3)
            VkExtent2D m_swapchain_extent{0, 0};

            // the images the swapchain rotates through (§6.3)
            std::vector<VkImage> m_swapchain_images;

            // the layout each swapchain image currently carries (§6.3)
            std::vector<VkImageLayout> m_swapchain_layouts;

            // the immutable batch the frame builds, which the encoder consumes once (§6.3)
            FrameBatch m_batch;

            // the counter the backend reports, which each retained batch buffer raises once (§6.3, AD-06)
            std::shared_ptr<std::atomic<std::uint64_t>> m_batch_buffer_allocations;

            // how many frames an unreferenced texture survives, which bounds the memory a long run holds (AD-06)
            static constexpr std::uint64_t TEXTURE_IDLE_FRAMES = 120;

            // how many textures a surface aims to hold, which one frame's own uploads may briefly exceed (AD-06)
            static constexpr std::size_t MAXIMUM_CACHED_TEXTURES = 64;

            // how many offscreen targets a slot keeps before the least recently used one goes (§6.2, AD-06)
            static constexpr std::size_t MAXIMUM_SCRATCH_TARGETS = 16;

            // the textures the uploaded images became, keyed by the identifier an image carries (AD-06)
            AgedCache<std::uint64_t, Image> m_textures{TEXTURE_IDLE_FRAMES, MAXIMUM_CACHED_TEXTURES};

            // the textures a begin frame sweep dropped, which the end transfers to the slot that records the frame (AD-06)
            std::vector<Image> m_pending_retired_images;

            // the glyph paths this surface already walked, which the context borrows so a glyph walks once (§6.4)
            GlyphPathCache m_glyph_paths;

            // the expanded glyphs this surface already swept, which the context borrows so a glyph sweeps once (§6.4)
            GlyphMeshCache m_glyph_meshes;

            // the context that records the frame into the batch, which is rebuilt with it (§6.2)
            std::unique_ptr<AlborRenderContext> m_context;

            // how many frames this surface has drawn, which the eviction ages the cache by (AD-06)
            std::uint64_t m_frame_counter = 0;

            // how many frames finished on the current target, which a resize resets (§6.3)
            std::size_t m_finished = 0;

            // the metrics a resize asked for while a frame was in flight, which the end applies (§6.2)
            WindowMetrics m_pending_metrics;

            // whether a resize is waiting for the frame in flight to finish (§6.2)
            std::atomic<bool> m_resize_pending{false};

            // whether a frame began and has not ended yet (§6.1)
            std::atomic<bool> m_begun{false};

            // the read back buffer the surface keeps, which a screenshot reuses (§6.3)
            mutable Buffer m_readback;
        };

        Image::~Image() {
            // the image releases every handle it still carries, which a moved from image leaves empty (§6.3)
            destroy();
        }

        void Image::destroy() {
            // a live image releases its view, memory and handle, which the device still owns until it dies (§6.3)
            if (view != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE)
                vkDestroyImageView(device_state().device, view, nullptr);
            if (image != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE)
                vkDestroyImage(device_state().device, image, nullptr);
            if (memory != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE)
                vkFreeMemory(device_state().device, memory, nullptr);
            // every handle clears, so a second destroy releases nothing (§6.3)
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            view = VK_NULL_HANDLE;
            width = 0;
            height = 0;
            levels = 1;
            layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }

        Image::Image(Image&& other) noexcept {
            // the move takes every handle, which leaves the source empty and safe to destroy (§6.3)
            *this = std::move(other);
        }

        Image& Image::operator=(Image&& other) noexcept {
            // a self move changes nothing, because the handles would be destroyed before they are read (§6.3)
            if (this == &other)
                return *this;
            // the target releases what it held, which the move replaces (§6.3)
            destroy();
            // every handle moves, which leaves the source empty rather than duplicated (§6.3)
            image = other.image;
            memory = other.memory;
            view = other.view;
            width = other.width;
            height = other.height;
            levels = other.levels;
            layout = other.layout;
            other.image = VK_NULL_HANDLE;
            other.memory = VK_NULL_HANDLE;
            other.view = VK_NULL_HANDLE;
            other.width = 0;
            other.height = 0;
            other.levels = 1;
            other.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            return *this;
        }

        bool Image::valid() const {
            // an image a pass can bind carries its handle and the view the pipeline reads (§6.3)
            return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE;
        }

        Buffer::~Buffer() {
            // the buffer releases every handle it still carries, which a moved from buffer leaves empty (§6.3)
            destroy();
        }

        void Buffer::destroy() {
            // a live buffer releases its mapping, memory and handle, which the device still owns until it dies (§6.3)
            if (buffer != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE) {
                if (mapped != nullptr)
                    vkUnmapMemory(device_state().device, memory);
                vkDestroyBuffer(device_state().device, buffer, nullptr);
            }
            if (memory != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE)
                vkFreeMemory(device_state().device, memory, nullptr);
            // every handle clears, so a second destroy releases nothing (§6.3)
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            mapped = nullptr;
            size = 0;
        }

        Buffer::Buffer(Buffer&& other) noexcept {
            // the move takes every handle, which leaves the source empty and safe to destroy (§6.3)
            *this = std::move(other);
        }

        Buffer& Buffer::operator=(Buffer&& other) noexcept {
            // a self move changes nothing, because the handles would be destroyed before they are read (§6.3)
            if (this == &other)
                return *this;
            // the target releases what it held, which the move replaces (§6.3)
            destroy();
            // every handle moves, which leaves the source empty rather than duplicated (§6.3)
            buffer = other.buffer;
            memory = other.memory;
            mapped = other.mapped;
            size = other.size;
            other.buffer = VK_NULL_HANDLE;
            other.memory = VK_NULL_HANDLE;
            other.mapped = nullptr;
            other.size = 0;
            return *this;
        }

        bool Buffer::valid() const {
            // a buffer a stage can read carries its handle and a byte range (§6.3)
            return buffer != VK_NULL_HANDLE && size > 0;
        }

        // create one pipeline over a vertex and fragment module, which blends the terms it is given (§6.2)
        [[nodiscard]] VkPipeline create_pipeline(VkShaderModule vertex, const char* vertex_entry, VkShaderModule fragment, const char* fragment_entry, VkPrimitiveTopology topology, VulkanBlendTerms terms) {
            // the two stages are the ones the container carries, which every draw binds (§6.3)
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertex;
            stages[0].pName = vertex_entry;
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragment;
            stages[1].pName = fragment_entry;
            // the shader reads its vertices from a storage buffer, so the pipeline carries no vertex input (§6.3)
            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = topology;
            assembly.primitiveRestartEnable = VK_FALSE;
            // the viewport and scissor are dynamic, because every target carries its own extent (§6.3)
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamic_states;
            // the geometry core emits both windings, so no face is culled (§6.3)
            VkPipelineRasterizationStateCreateInfo rasterization{};
            rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterization.polygonMode = VK_POLYGON_MODE_FILL;
            rasterization.cullMode = VK_CULL_MODE_NONE;
            rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterization.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            // the one attachment blends with the terms the composition mode names (§6.2)
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = blend_factor(terms.color_source);
            attachment.dstColorBlendFactor = blend_factor(terms.color_destination);
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = blend_factor(terms.alpha_source);
            attachment.dstAlphaBlendFactor = blend_factor(terms.alpha_destination);
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blending{};
            blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blending.attachmentCount = 1;
            blending.pAttachments = &attachment;
            // the pipeline names the format the dynamic rendering pass writes, which the target carries (§6.3)
            VkPipelineRenderingCreateInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &TARGET_FORMAT;
            VkGraphicsPipelineCreateInfo description{};
            description.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            description.pNext = &rendering;
            description.stageCount = 2;
            description.pStages = stages;
            description.pVertexInputState = &vertex_input;
            description.pInputAssemblyState = &assembly;
            description.pViewportState = &viewport;
            description.pRasterizationState = &rasterization;
            description.pMultisampleState = &multisample;
            description.pColorBlendState = &blending;
            description.pDynamicState = &dynamic;
            description.layout = device_state().pipeline_layout;
            description.renderPass = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
            // a pipeline the driver refuses leaves the slot empty, which readiness reports (§6.2)
            if (vkCreateGraphicsPipelines(device_state().device, VK_NULL_HANDLE, 1, &description, nullptr, &pipeline) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            return pipeline;
        }

        // create one pipeline for the offscreen passes, which writes the cast alone (§6.2)
        [[nodiscard]] VkPipeline create_shadow_pipeline(VkShaderModule vertex, VkShaderModule fragment, const char* fragment_entry) {
            // the blend terms leave the clear behind, because the pass writes the silhouette alone (§6.2)
            return create_pipeline(vertex, "shadow_vertex", fragment, fragment_entry, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, {.color_source = VulkanBlendFactor::One, .color_destination = VulkanBlendFactor::Zero, .alpha_source = VulkanBlendFactor::One, .alpha_destination = VulkanBlendFactor::Zero, .exact = false});
        }

        // create the one white texel every imageless draw samples, which keeps one shader path for every draw (§6.3)
        [[nodiscard]] bool create_white_texture(DeviceState& state) {
            // the texture is one pixel, which every solid draw samples at its paint coordinates (§6.3)
            VkResult result = create_image(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED, state.white);
            if (result != VK_SUCCESS)
                return false;
            // the pixel is copied from a host visible staging buffer, which the command buffer reads (§6.3)
            Buffer staging;
            if (!create_host_buffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging))
                return false;
            // the canonical byte is white premultiplied opaque, which nothing draws over (§6.3)
            static const std::uint8_t WHITE_PIXEL[4] = {255u, 255u, 255u, 255u};
            std::memcpy(staging.mapped, WHITE_PIXEL, sizeof(WHITE_PIXEL));
            // the upload is recorded once, because the texture never changes (§6.3)
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(state.command_buffer, &begin) != VK_SUCCESS)
                return false;
            transition_image(state.command_buffer, state.white, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {1, 1, 1};
            vkCmdCopyBufferToImage(state.command_buffer, staging.buffer, state.white.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            transition_image(state.command_buffer, state.white, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            if (vkEndCommandBuffer(state.command_buffer) != VK_SUCCESS)
                return false;
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &state.command_buffer;
            if (vkQueueSubmit(state.queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
                return false;
            // the queue drains before the staging buffer dies, because its copy reads the mapping (§6.3)
            return vkQueueWaitIdle(state.queue) == VK_SUCCESS;
        }

        // build the device, its compiled stages and the pipeline table once, which every surface shares (§6.3)
        bool build_device_state(DeviceState& state) {
            // the instance extensions are the surface ones the loader advertises, which the wsi paths need (§6.3)
            std::uint32_t extension_count = 0;
            if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr) != VK_SUCCESS)
                return false;
            std::vector<VkExtensionProperties> extensions(extension_count);
            if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()) != VK_SUCCESS)
                return false;
            std::vector<const char*> instance_extensions{"VK_KHR_surface"};
#if defined(ALBOR_VULKAN_WAYLAND)
            // the wayland surface extension carries the display the handle names (§6.3)
            state.wayland_extension = has_instance_extension(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, extensions);
            if (state.wayland_extension)
                instance_extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
#if defined(ALBOR_VULKAN_XCB)
            // the xcb surface extension carries the connection and window the handle names (§6.3)
            state.xcb_extension = has_instance_extension(VK_KHR_XCB_SURFACE_EXTENSION_NAME, extensions);
            if (state.xcb_extension)
                instance_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
            // the application asks for the api the dynamic rendering pipeline path needs (§6.3)
            VkApplicationInfo application{};
            application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            application.pApplicationName = "albor";
            application.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
            application.pEngineName = "render-albor";
            application.engineVersion = VK_MAKE_VERSION(0, 2, 0);
            application.apiVersion = VK_API_VERSION_1_3;
            VkInstanceCreateInfo instance_description{};
            instance_description.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            instance_description.pApplicationInfo = &application;
            instance_description.enabledExtensionCount = static_cast<std::uint32_t>(instance_extensions.size());
            instance_description.ppEnabledExtensionNames = instance_extensions.data();
            if (vkCreateInstance(&instance_description, nullptr, &state.instance) != VK_SUCCESS)
                return false;
            // the physical device with a graphics queue and dynamic rendering is the one the backend draws with (§6.3)
            std::uint32_t device_count = 0;
            if (vkEnumeratePhysicalDevices(state.instance, &device_count, nullptr) != VK_SUCCESS || device_count == 0)
                return false;
            std::vector<VkPhysicalDevice> devices(device_count);
            if (vkEnumeratePhysicalDevices(state.instance, &device_count, devices.data()) != VK_SUCCESS)
                return false;
            for (VkPhysicalDevice device : devices) {
                // the api floor names the dynamic rendering entry points the backend calls (§6.3)
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(device, &properties);
                if (properties.apiVersion < VK_API_VERSION_1_3)
                    continue;
                // the device must expose a graphics queue, which every frame records on (§6.3)
                std::uint32_t family_count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
                std::vector<VkQueueFamilyProperties> families(family_count);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
                std::uint32_t family = family_count;
                for (std::uint32_t index = 0; index < family_count; index++) {
                    // the first graphics family is the one the backend submits on (§6.3)
                    if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                        family = index;
                        break;
                    }
                }
                if (family == family_count)
                    continue;
                // the dynamic rendering and dual source features are the two the pipeline path needs (§6.2, §6.3)
                VkPhysicalDeviceVulkan13Features features13{};
                features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                VkPhysicalDeviceFeatures2 features{};
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                features.pNext = &features13;
                vkGetPhysicalDeviceFeatures2(device, &features);
                if (features13.dynamicRendering != VK_TRUE || features.features.dualSrcBlend != VK_TRUE)
                    continue;
                state.physical = device;
                state.properties = properties;
                state.queue_family = family;
                break;
            }
            if (state.physical == VK_NULL_HANDLE) {
                std::fprintf(stderr, "albor-render-albor: no vulkan device with dynamic rendering and dual source blending\n");
                return false;
            }
            vkGetPhysicalDeviceMemoryProperties(state.physical, &state.memory);
            // the device extensions carry the swapchain, which a windowed surface needs (§6.3)
            std::uint32_t device_extension_count = 0;
            if (vkEnumerateDeviceExtensionProperties(state.physical, nullptr, &device_extension_count, nullptr) != VK_SUCCESS)
                return false;
            std::vector<VkExtensionProperties> device_extensions(device_extension_count);
            if (vkEnumerateDeviceExtensionProperties(state.physical, nullptr, &device_extension_count, device_extensions.data()) != VK_SUCCESS)
                return false;
            state.swapchain_extension = has_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions);
            std::vector<const char*> device_extensions_enabled;
            if (state.swapchain_extension)
                device_extensions_enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            // the queue is the graphics one, which the surface path checks for present support (§6.3)
            float priority = 1.0f;
            VkDeviceQueueCreateInfo queue_description{};
            queue_description.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_description.queueFamilyIndex = state.queue_family;
            queue_description.queueCount = 1;
            queue_description.pQueuePriorities = &priority;
            VkPhysicalDeviceVulkan13Features features13{};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features13.dynamicRendering = VK_TRUE;
            VkPhysicalDeviceFeatures features{};
            features.dualSrcBlend = VK_TRUE;
            VkDeviceCreateInfo device_description{};
            device_description.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            device_description.pNext = &features13;
            device_description.queueCreateInfoCount = 1;
            device_description.pQueueCreateInfos = &queue_description;
            device_description.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions_enabled.size());
            device_description.ppEnabledExtensionNames = device_extensions_enabled.data();
            device_description.pEnabledFeatures = &features;
            if (vkCreateDevice(state.physical, &device_description, nullptr, &state.device) != VK_SUCCESS)
                return false;
            vkGetDeviceQueue(state.device, state.queue_family, 0, &state.queue);
            // the command pool allocates the one frame command buffer, which every submit reuses (§6.3)
            VkCommandPoolCreateInfo pool{};
            pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = state.queue_family;
            if (vkCreateCommandPool(state.device, &pool, nullptr, &state.command_pool) != VK_SUCCESS)
                return false;
            VkCommandBufferAllocateInfo command_description{};
            command_description.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_description.commandPool = state.command_pool;
            command_description.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_description.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(state.device, &command_description, &state.command_buffer) != VK_SUCCESS)
                return false;
            VkFenceCreateInfo fence{};
            fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(state.device, &fence, nullptr, &state.fence) != VK_SUCCESS)
                return false;
            // the one set layout names the uniform, the two sampled images, the sampler and the vertices (§6.3)
            VkDescriptorSetLayoutBinding bindings[5]{};
            bindings[0].binding = UNIFORM_BINDING;
            bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[1].binding = PAINT_BINDING;
            bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[2].binding = BLUR_BINDING;
            bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[3].binding = SAMPLER_BINDING;
            bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[4].binding = VERTEX_BINDING;
            bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[4].descriptorCount = 1;
            bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            VkDescriptorSetLayoutCreateInfo set_description{};
            set_description.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            set_description.bindingCount = 5;
            set_description.pBindings = bindings;
            if (vkCreateDescriptorSetLayout(state.device, &set_description, nullptr, &state.set_layout) != VK_SUCCESS)
                return false;
            VkPipelineLayoutCreateInfo pipeline_layout{};
            pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout.setLayoutCount = 1;
            pipeline_layout.pSetLayouts = &state.set_layout;
            if (vkCreatePipelineLayout(state.device, &pipeline_layout, nullptr, &state.pipeline_layout) != VK_SUCCESS)
                return false;
            // the committed container carries every stage, which a corrupt artifact answers as empty (§6.3)
            const VulkanShaderData data = parse_vulkan_shader_data(vulkan_shader_data());
            if (!data.complete()) {
                std::fprintf(stderr, "albor-render-albor: the vulkan shader container could not be used\n");
                return false;
            }
            const ShaderData shaders{.vertex = data.modules[0], .fragment = data.modules[1], .fragment_exact = data.modules[2], .shadow_vertex = data.modules[3], .shadow_shape = data.modules[4], .shadow_blur = data.modules[5]};
            // each module joins the device once, which every pipeline binds (§6.3)
            const std::pair<std::span<const std::byte>, VkShaderModule*> modules[] = {{shaders.vertex, &state.vertex_shader}, {shaders.fragment, &state.fragment_shader}, {shaders.fragment_exact, &state.fragment_exact}, {shaders.shadow_vertex, &state.shadow_vertex}, {shaders.shadow_shape, &state.shadow_shape}, {shaders.shadow_blur, &state.shadow_blur}};
            for (const auto& [bytes, module] : modules) {
                VkShaderModuleCreateInfo module_description{};
                module_description.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                module_description.codeSize = bytes.size();
                module_description.pCode = reinterpret_cast<const std::uint32_t*>(bytes.data());
                if (vkCreateShaderModule(state.device, &module_description, nullptr, module) != VK_SUCCESS)
                    return false;
            }
            // the sampler every paint texture reads through, which the fragment stage names (§6.4)
            VkSamplerCreateInfo sampler{};
            sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler.magFilter = VK_FILTER_LINEAR;
            sampler.minFilter = VK_FILTER_LINEAR;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler.maxLod = VK_LOD_CLAMP_NONE;
            if (vkCreateSampler(state.device, &sampler, nullptr, &state.sampler) != VK_SUCCESS)
                return false;
            // the white texel is the paint a solid draw samples (§6.3)
            if (!create_white_texture(state))
                return false;
            // every composition mode gets its pipeline, which the draw record selects at encode time (§6.2)
            for (std::size_t index = 0; index < COMPOSITION_MODES; index++) {
                const VulkanBlendTerms terms = vulkan_blend_terms(static_cast<CompositionMode>(index));
                state.pipelines[index] = create_pipeline(state.vertex_shader, "albor_vertex", terms.exact ? state.fragment_exact : state.fragment_shader, terms.exact ? "albor_fragment_exact" : "albor_fragment", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, terms);
                if (terms.exact)
                    state.exact_pipelines[index] = state.pipelines[index];
            }
            // the silhouette and blur passes write the cast alone, which their own pipelines carry (§6.2)
            state.shadow_shape_pipeline = create_shadow_pipeline(state.shadow_vertex, state.shadow_shape, "shadow_shape");
            state.shadow_blur_pipeline = create_shadow_pipeline(state.shadow_vertex, state.shadow_blur, "shadow_blur");
            // the two blits a frame may record are checked once, so a format that cannot be blitted is known here (§6.3)
            VkFormatProperties target_properties{};
            vkGetPhysicalDeviceFormatProperties(state.physical, TARGET_FORMAT, &target_properties);
            state.target_blittable = (target_properties.optimalTilingFeatures & (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) == (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);
            VkFormatProperties texture_properties{};
            vkGetPhysicalDeviceFormatProperties(state.physical, TEXTURE_FORMAT, &texture_properties);
            state.texture_blittable = (texture_properties.optimalTilingFeatures & (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) == (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);
            // the uniform stride is the block the surfaces ring their draws through, which the driver aligns (§6.3)
            state.uniform_stride = align_up(sizeof(DrawUniforms), state.properties.limits.minUniformBufferOffsetAlignment);
            // the passes that read no vertex still bind a valid storage buffer, which their descriptor set names (§6.3)
            if (!create_host_buffer(sizeof(BatchVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, state.dummy_vertices))
                return false;
            // readiness is whether every stage and every pipeline slot came up, which the draw path binds (§6.2)
            const bool composed = std::all_of(state.pipelines.begin(), state.pipelines.end(), [](VkPipeline pipeline) { return pipeline != VK_NULL_HANDLE; });
            const bool exact = state.exact_pipelines[static_cast<std::size_t>(CompositionMode::SourceIn)] != VK_NULL_HANDLE && state.exact_pipelines[static_cast<std::size_t>(CompositionMode::Copy)] != VK_NULL_HANDLE && state.exact_pipelines[static_cast<std::size_t>(CompositionMode::DestinationAtop)] != VK_NULL_HANDLE;
            state.built = state.device != VK_NULL_HANDLE && state.queue != VK_NULL_HANDLE && state.command_buffer != VK_NULL_HANDLE && state.vertex_shader != VK_NULL_HANDLE && state.fragment_shader != VK_NULL_HANDLE && state.fragment_exact != VK_NULL_HANDLE && state.shadow_vertex != VK_NULL_HANDLE && state.shadow_shape != VK_NULL_HANDLE && state.shadow_blur != VK_NULL_HANDLE && composed && exact && state.shadow_shape_pipeline != VK_NULL_HANDLE && state.shadow_blur_pipeline != VK_NULL_HANDLE && state.white.valid() && state.sampler != VK_NULL_HANDLE && state.uniform_stride > 0;
            return state.built;
        }

        // create the surface the window presents through, which the handle names (§6.3)
        VkSurfaceKHR create_window_surface(NativeWindowHandle handle) {
            // a handle with no object names no window, which the offscreen path never asks for (§6.3)
            if (!handle.valid())
                return VK_NULL_HANDLE;
            // the common head names the window system the concrete bundle behind it belongs to (§3.5, AD-05)
            const auto* base = static_cast<const linux_wsi::Surface*>(handle.object());
            if (base == nullptr)
                return VK_NULL_HANDLE;
            switch (base->kind) {
#if defined(ALBOR_VULKAN_WAYLAND)
            case linux_wsi::Kind::Wayland: {
                // the wayland bundle carries the display and the surface the swapchain presents through (§5.3)
                const auto* native = static_cast<const wayland::WaylandNativeSurface*>(base);
                if (native->state == nullptr)
                    return VK_NULL_HANDLE;
                // the state is read under its lock, because a teardown clears it on the ui thread (§10.5)
                const std::lock_guard<std::mutex> lock(native->state->mutex);
                VkWaylandSurfaceCreateInfoKHR description{};
                description.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
                description.display = native->state->display;
                description.surface = native->state->surface;
                if (description.display == nullptr || description.surface == nullptr)
                    return VK_NULL_HANDLE;
                VkSurfaceKHR surface = VK_NULL_HANDLE;
                if (vkCreateWaylandSurfaceKHR(device_state().instance, &description, nullptr, &surface) != VK_SUCCESS)
                    return VK_NULL_HANDLE;
                // the wsi owns every buffer the surface commits from here, so the platform attaches no placeholder (§6.3)
                native->state->renderer_presenting = true;
                return surface;
            }
#endif
#if defined(ALBOR_VULKAN_XCB)
            case linux_wsi::Kind::X11: {
                // the xcb bundle carries the connection and the window the swapchain presents through (§5.3)
                const auto* native = static_cast<const x11::X11NativeSurface*>(base);
                if (native->state == nullptr)
                    return VK_NULL_HANDLE;
                // the state is read under its lock, because a teardown clears it on the ui thread (§10.5)
                const std::lock_guard<std::mutex> lock(native->state->mutex);
                VkXcbSurfaceCreateInfoKHR description{};
                description.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
                description.connection = native->state->connection;
                description.window = native->state->window;
                if (description.connection == nullptr || description.window == 0)
                    return VK_NULL_HANDLE;
                VkSurfaceKHR surface = VK_NULL_HANDLE;
                if (vkCreateXcbSurfaceKHR(device_state().instance, &description, nullptr, &surface) != VK_SUCCESS)
                    return VK_NULL_HANDLE;
                return surface;
            }
#endif
            default:
                // a window system this build did not enable falls through to the refusal below (§6.3)
                break;
            }
            return VK_NULL_HANDLE;
        }

        AlborRenderSurface::AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics, std::shared_ptr<std::atomic<std::uint64_t>> batch_buffer_allocations) :
            m_handle(handle), m_metrics(metrics), m_batch_buffer_allocations(std::move(batch_buffer_allocations)) {
            // the target is allocated at the physical extent the metrics name (§6.3, AD-27)
            allocate(m_metrics);
            // the command pool belongs to the surface, so two windows never serialize on one pool (§6.3, AD-18b)
            VkCommandPoolCreateInfo pool{};
            pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = device_state().queue_family;
            if (vkCreateCommandPool(device_state().device, &pool, nullptr, &m_command_pool) != VK_SUCCESS)
                m_command_pool = VK_NULL_HANDLE;
            // every slot owns its synchronization, its buffers and its scratch targets (§6.3, AD-18b)
            if (m_command_pool != VK_NULL_HANDLE) {
                for (FrameSlot& slot : m_slots) {
                    // a slot the device refused ends the build, which readiness reports (§6.2)
                    if (!create_slot(slot))
                        break;
                }
            }
            // a windowed surface joins the window system the handle names, which a refused surface reports (§6.3)
            if (m_handle.valid()) {
                // the common head names the concrete bundle, which the surface keeps alive while it draws (§10.5)
                const auto* base = static_cast<const linux_wsi::Surface*>(m_handle.object());
#if defined(ALBOR_VULKAN_WAYLAND)
                // a wayland handle keeps the shared state, which the display outlives through (§10.5)
                if (base != nullptr && base->kind == linux_wsi::Kind::Wayland) {
                    m_wayland = static_cast<const wayland::WaylandNativeSurface*>(base)->state;
                    // the surface the wsi is built from is recorded, because a rebuilt role replaces it (§5.1)
                    const std::lock_guard<std::mutex> lock(m_wayland->mutex);
                    m_wayland_surface = m_wayland->surface;
                }
#endif
#if defined(ALBOR_VULKAN_XCB)
                // an xcb handle keeps the shared state, which the connection outlives through (§10.5)
                if (base != nullptr && base->kind == linux_wsi::Kind::X11)
                    m_xcb = static_cast<const x11::X11NativeSurface*>(base)->state;
#endif
                m_surface = create_window_surface(m_handle);
                // a swapchain the window system refuses leaves the surface invalid rather than presentable (§6.2)
                if (m_surface != VK_NULL_HANDLE && !create_swapchain(m_metrics))
                    destroy_swapchain();
            }
        }

        AlborRenderSurface::~AlborRenderSurface() {
            // the queue drains before a surface dies, because a submitted frame may still read the target (§6.3)
            if (device_state().device != VK_NULL_HANDLE)
                vkDeviceWaitIdle(device_state().device);
            // every slot releases its buffers and scratch targets after the queue drained (§6.3)
            for (FrameSlot& slot : m_slots)
                destroy_slot(slot);
            // the image cache and the target release their memory after the queue drained (§6.3)
            destroy_swapchain();
            if (m_command_pool != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_state().device, m_command_pool, nullptr);
                m_command_pool = VK_NULL_HANDLE;
            }
            m_target = Image{};
            m_readback = Buffer{};
            // the images a sweep dropped before the last frame ended release after the device drained (§6.3, AD-06)
            m_pending_retired_images.clear();
        }

        bool AlborRenderSurface::create_slot(FrameSlot& slot) {
            DeviceState& state = device_state();
            // the slot records into its own command buffer, which the next frame of the slot resets (§6.3)
            VkCommandBufferAllocateInfo command{};
            command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command.commandPool = m_command_pool;
            command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(state.device, &command, &slot.command) != VK_SUCCESS)
                return false;
            // the fence is what the next use of the slot waits on, which is the whole pacing of the backend (§6.3)
            VkFenceCreateInfo fence{};
            fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(state.device, &fence, nullptr, &slot.fence) != VK_SUCCESS)
                return false;
            // the acquire and the present each carry one semaphore of the slot (§6.3)
            VkSemaphoreCreateInfo semaphore{};
            semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            if (vkCreateSemaphore(state.device, &semaphore, nullptr, &slot.image_available) != VK_SUCCESS)
                return false;
            if (vkCreateSemaphore(state.device, &semaphore, nullptr, &slot.render_finished) != VK_SUCCESS)
                return false;
            // the pool allocates every descriptor set the slot's frame needs, which its reset recycles (§6.3)
            VkDescriptorPoolSize sizes[4]{};
            sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            sizes[0].descriptorCount = DESCRIPTOR_CAPACITY;
            sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            sizes[1].descriptorCount = DESCRIPTOR_CAPACITY * 2u;
            sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
            sizes[2].descriptorCount = DESCRIPTOR_CAPACITY;
            sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            sizes[3].descriptorCount = DESCRIPTOR_CAPACITY;
            VkDescriptorPoolCreateInfo descriptor_pool{};
            descriptor_pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptor_pool.maxSets = DESCRIPTOR_CAPACITY;
            descriptor_pool.poolSizeCount = 4;
            descriptor_pool.pPoolSizes = sizes;
            if (vkCreateDescriptorPool(state.device, &descriptor_pool, nullptr, &slot.descriptor_pool) != VK_SUCCESS)
                return false;
            // the uniform ring is the slot's own, because the gpu reads it until the slot's fence signals (§6.3)
            return create_host_buffer(state.uniform_stride * DESCRIPTOR_CAPACITY, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, slot.uniforms);
        }

        void AlborRenderSurface::destroy_slot(FrameSlot& slot) {
            DeviceState& state = device_state();
            // the scratch targets and the retired buffers release their memory (§6.3)
            slot.scratch.clear();
            slot.retired.clear();
            // the textures the slot retired release their memory after the device drained (§6.3, AD-06)
            slot.retired_images.clear();
            slot.vertices.buffer = Buffer{};
            slot.quad_vertices.buffer = Buffer{};
            slot.indices.buffer = Buffer{};
            slot.uniforms = Buffer{};
            slot.descriptors.clear();
            if (state.device == VK_NULL_HANDLE)
                return;
            // the pool and the synchronization objects release after the device drained (§6.3)
            if (slot.descriptor_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(state.device, slot.descriptor_pool, nullptr);
                slot.descriptor_pool = VK_NULL_HANDLE;
            }
            if (slot.render_finished != VK_NULL_HANDLE) {
                vkDestroySemaphore(state.device, slot.render_finished, nullptr);
                slot.render_finished = VK_NULL_HANDLE;
            }
            if (slot.image_available != VK_NULL_HANDLE) {
                vkDestroySemaphore(state.device, slot.image_available, nullptr);
                slot.image_available = VK_NULL_HANDLE;
            }
            if (slot.fence != VK_NULL_HANDLE) {
                vkDestroyFence(state.device, slot.fence, nullptr);
                slot.fence = VK_NULL_HANDLE;
            }
            if (slot.command != VK_NULL_HANDLE && m_command_pool != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(state.device, m_command_pool, 1, &slot.command);
                slot.command = VK_NULL_HANDLE;
            }
            slot.submitted = false;
        }

        bool AlborRenderSurface::begin_slot(FrameSlot& slot) {
            DeviceState& state = device_state();
            // the frame this slot submitted two frames ago is waited, which keeps the gpu one frame ahead of the cpu (§6.3)
            if (slot.submitted) {
                const VkResult waited = vkWaitForFences(state.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
                if (waited != VK_SUCCESS) {
                    note_result(waited);
                    return false;
                }
                slot.submitted = false;
            }
            // the command buffer and the descriptor pool of the finished frame are recycled (§6.3)
            if (vkResetCommandBuffer(slot.command, 0) != VK_SUCCESS)
                return false;
            (void)vkResetDescriptorPool(state.device, slot.descriptor_pool, 0);
            // the frame starts from an empty ring, an empty quad range and an empty descriptor cache (§6.3)
            slot.uniform_cursor = 0;
            slot.quad_cursor = 0;
            slot.descriptors.clear();
            // the buffers the finished frame retired and the scratch it took are free again (§6.3, AD-06)
            slot.retired.clear();
            // the textures the finished frame dropped release now that no submitted frame reads them (§6.3, AD-06)
            slot.retired_images.clear();
            for (ScratchTarget& target : slot.scratch)
                target.in_use = false;
            // the command buffer is one time submit, because the frame is recorded once and submitted once (§6.3)
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            const VkResult result = vkBeginCommandBuffer(slot.command, &begin);
            if (result != VK_SUCCESS) {
                note_result(result);
                return false;
            }
            return true;
        }

        void AlborRenderSurface::wait_for_idle() {
            DeviceState& state = device_state();
            if (state.device == VK_NULL_HANDLE)
                return;
            // every frame the surface submitted is waited, which a resize and a read back both need (§6.3)
            for (FrameSlot& slot : m_slots) {
                if (!slot.submitted)
                    continue;
                (void)vkWaitForFences(state.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
                slot.submitted = false;
            }
            // the presentation engine may still hold the last presented image, which only a queue drain retires (§6.3)
            const std::lock_guard<std::mutex> queue_lock(queue_mutex());
            (void)vkQueueWaitIdle(state.queue);
        }

        void AlborRenderSurface::reset_semaphores() {
            DeviceState& state = device_state();
            if (state.device == VK_NULL_HANDLE)
                return;
            // the signaled semaphores can only be replaced once every submit that used them finished (§6.3)
            wait_for_idle();
            VkSemaphoreCreateInfo semaphore{};
            semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            for (FrameSlot& slot : m_slots) {
                if (slot.render_finished != VK_NULL_HANDLE)
                    vkDestroySemaphore(state.device, slot.render_finished, nullptr);
                if (slot.image_available != VK_NULL_HANDLE)
                    vkDestroySemaphore(state.device, slot.image_available, nullptr);
                slot.render_finished = VK_NULL_HANDLE;
                slot.image_available = VK_NULL_HANDLE;
                (void)vkCreateSemaphore(state.device, &semaphore, nullptr, &slot.image_available);
                (void)vkCreateSemaphore(state.device, &semaphore, nullptr, &slot.render_finished);
            }
        }

        bool AlborRenderSurface::submit_slot(FrameSlot& slot, VkSemaphore wait, VkPipelineStageFlags wait_stage, VkSemaphore signal) {
            DeviceState& state = device_state();
            if (vkEndCommandBuffer(slot.command) != VK_SUCCESS)
                return false;
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            if (wait != VK_NULL_HANDLE) {
                submit.waitSemaphoreCount = 1;
                submit.pWaitSemaphores = &wait;
                submit.pWaitDstStageMask = &wait_stage;
            }
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &slot.command;
            if (signal != VK_NULL_HANDLE) {
                submit.signalSemaphoreCount = 1;
                submit.pSignalSemaphores = &signal;
            }
            // the queue is the one object every surface shares, so only the submit is serialized (§6.3, AD-18b)
            const std::lock_guard<std::mutex> queue_lock(queue_mutex());
            (void)vkResetFences(state.device, 1, &slot.fence);
            const VkResult result = vkQueueSubmit(state.queue, 1, &submit, slot.fence);
            if (result != VK_SUCCESS) {
                note_result(result);
                return false;
            }
            slot.submitted = true;
            return true;
        }

        bool AlborRenderSurface::valid() const {
            // a caller outside the frame path reads the target under the lock its resize also takes (§10.5, D9.1)
            const std::lock_guard<std::mutex> guard(m_mutex);
            return valid_locked();
        }

        bool AlborRenderSurface::valid_locked() const {
            // the device and the colour target are both required before a frame can draw (§6.2)
            if (!device_state().built || device_state().lost.load() || !m_target.valid())
                return false;
            // a windowed surface is valid only while its window and its swapchain both stand (§6.2)
            if (m_handle.valid() && (m_surface == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE || !window_live()))
                return false;
            // a surface whose slots never came up cannot record a frame (§6.2)
            return m_command_pool != VK_NULL_HANDLE && m_slots.front().command != VK_NULL_HANDLE;
        }

        bool AlborRenderSurface::window_live() const {
#if defined(ALBOR_VULKAN_WAYLAND)
            // a wayland surface torn down clears the pointer under the state lock (§10.5)
            if (m_wayland != nullptr) {
                const std::lock_guard<std::mutex> lock(m_wayland->mutex);
                return m_wayland->surface != nullptr;
            }
#endif
#if defined(ALBOR_VULKAN_XCB)
            // an xcb window torn down zeroes the id under the state lock (§10.5)
            if (m_xcb != nullptr) {
                const std::lock_guard<std::mutex> lock(m_xcb->mutex);
                return m_xcb->window != 0;
            }
#endif
            return !m_handle.valid();
        }

        const WindowMetrics& AlborRenderSurface::metrics() const {
            // the metrics are what the executor compares the scene against (§6.1, D9.1)
            return m_metrics;
        }

        void AlborRenderSurface::allocate(const WindowMetrics& extent) {
            // the extent is the physical pixel size the device scale already applied (AD-27)
            const std::uint32_t width = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(extent.pixel_size.width));
            const std::uint32_t height = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(extent.pixel_size.height));
            // the target is allocated at the physical pixel extent, and the read back copies out of it (§6.3)
            Image target;
            if (create_image(width, height, 1, TARGET_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_UNDEFINED, target) != VK_SUCCESS) {
                m_target = Image{};
                m_finished = 0;
                return;
            }
            // the buffers took the extent, so the previous target is replaced only now (§6.2)
            m_target = std::move(target);
            // the new target carries no finished frame yet, so a read back waits for one (§6.3)
            m_finished = 0;
            // a reallocated target has no pixels to preserve, so the next frame must be full (§6.1)
            m_target_initialized = false;
        }

        bool AlborRenderSurface::create_swapchain(const WindowMetrics& extent) {
            // a surface without a window system presents through no swapchain (§6.3)
            if (m_surface == VK_NULL_HANDLE)
                return false;
            // a device without the swapchain extension cannot present, which the surface refuses (§6.3)
            if (!device_state().swapchain_extension)
                return false;
            // the surface capabilities name the extent and the image count the swapchain may carry (§6.3)
            VkSurfaceCapabilitiesKHR capabilities{};
            if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_state().physical, m_surface, &capabilities) != VK_SUCCESS)
                return false;
            // a presentation family the graphics queue does not belong to cannot present, which the surface refuses (§6.3)
            VkBool32 present_supported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(device_state().physical, device_state().queue_family, m_surface, &present_supported) != VK_SUCCESS || present_supported != VK_TRUE)
                return false;
            // the format is preferred as the target's own, so the copy needs no conversion (§6.3)
            std::uint32_t format_count = 0;
            if (vkGetPhysicalDeviceSurfaceFormatsKHR(device_state().physical, m_surface, &format_count, nullptr) != VK_SUCCESS || format_count == 0)
                return false;
            std::vector<VkSurfaceFormatKHR> formats(format_count);
            if (vkGetPhysicalDeviceSurfaceFormatsKHR(device_state().physical, m_surface, &format_count, formats.data()) != VK_SUCCESS)
                return false;
            VkSurfaceFormatKHR chosen = formats.front();
            for (const VkSurfaceFormatKHR& format : formats) {
                // the target format keeps the copy exact, while the srgb space keeps the display's convention (§6.3)
                if (format.format == TARGET_FORMAT && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    chosen = format;
                    break;
                }
            }
            // a present that scales or converts the frame blits, which both formats must support (§6.3)
            VkFormatProperties chosen_properties{};
            vkGetPhysicalDeviceFormatProperties(device_state().physical, chosen.format, &chosen_properties);
            const bool chosen_blittable = (chosen_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
            if (!chosen_blittable || !device_state().target_blittable) {
                std::fprintf(stderr, "albor-render-albor: the window surface format cannot be blitted into\n");
                return false;
            }
            // the extent is the physical pixel size the device scale already applied, clamped by the surface (§6.3, AD-27)
            VkExtent2D swapchain_extent{};
            if (capabilities.currentExtent.width != UINT32_MAX && capabilities.currentExtent.height != UINT32_MAX) {
                // a minimized window reports no area, which the one pixel floor keeps presentable (§6.3)
                swapchain_extent = {std::max<std::uint32_t>(1u, capabilities.currentExtent.width), std::max<std::uint32_t>(1u, capabilities.currentExtent.height)};
            }
            else {
                const VulkanExtent clamped = clamp_vulkan_extent(extent.pixel_size.width, extent.pixel_size.height, capabilities.maxImageExtent.width, capabilities.maxImageExtent.height);
                swapchain_extent = {clamped.width, clamped.height};
            }
            // the surface names the image count it accepts, which one more than the minimum keeps one presented (§6.3)
            std::uint32_t image_count = capabilities.minImageCount + 1u;
            if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
                image_count = capabilities.maxImageCount;
            // the composite alpha is opaque where the surface supports it, which the window owns its pixels (§6.3)
            VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            const VkCompositeAlphaFlagBitsKHR composites[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
            for (VkCompositeAlphaFlagBitsKHR candidate : composites) {
                if ((capabilities.supportedCompositeAlpha & static_cast<VkCompositeAlphaFlagsKHR>(candidate)) != 0) {
                    composite = candidate;
                    break;
                }
            }
            VkSwapchainCreateInfoKHR description{};
            description.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            description.surface = m_surface;
            description.minImageCount = image_count;
            description.imageFormat = chosen.format;
            description.imageColorSpace = chosen.colorSpace;
            description.imageExtent = swapchain_extent;
            description.imageArrayLayers = 1;
            description.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            description.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            description.preTransform = capabilities.currentTransform;
            description.compositeAlpha = composite;
            description.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            description.clipped = VK_TRUE;
            description.oldSwapchain = m_swapchain;
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            const VkResult result = vkCreateSwapchainKHR(device_state().device, &description, nullptr, &swapchain);
            if (result != VK_SUCCESS) {
                note_result(result);
                return false;
            }
            // a replaced swapchain is destroyed after the new one took the surface, which the driver requires (§6.3)
            if (m_swapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_state().device, m_swapchain, nullptr);
                m_swapchain = VK_NULL_HANDLE;
            }
            std::uint32_t image_total = 0;
            if (vkGetSwapchainImagesKHR(device_state().device, swapchain, &image_total, nullptr) != VK_SUCCESS || image_total == 0) {
                vkDestroySwapchainKHR(device_state().device, swapchain, nullptr);
                return false;
            }
            m_swapchain_images.resize(image_total);
            if (vkGetSwapchainImagesKHR(device_state().device, swapchain, &image_total, m_swapchain_images.data()) != VK_SUCCESS) {
                vkDestroySwapchainKHR(device_state().device, swapchain, nullptr);
                return false;
            }
            m_swapchain_layouts.assign(image_total, VK_IMAGE_LAYOUT_UNDEFINED);
            m_swapchain = swapchain;
            m_swapchain_format = chosen.format;
            m_swapchain_blittable = chosen_blittable;
            m_swapchain_extent = swapchain_extent;
            return true;
        }

        void AlborRenderSurface::destroy_swapchain() {
            // the swapchain is destroyed before the surface it presents through (§6.3)
            if (m_swapchain != VK_NULL_HANDLE && device_state().device != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_state().device, m_swapchain, nullptr);
                m_swapchain = VK_NULL_HANDLE;
            }
            m_swapchain_images.clear();
            m_swapchain_layouts.clear();
            m_swapchain_blittable = false;
            if (m_surface != VK_NULL_HANDLE && device_state().instance != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(device_state().instance, m_surface, nullptr);
                m_surface = VK_NULL_HANDLE;
            }
        }

        void AlborRenderSurface::resize(const WindowMetrics& metrics) {
            // metrics with no area cannot be presented, so the previous target is kept (§6.2)
            if (!metrics.valid())
                return;
            // the target and the metrics are only ever swapped under the lock every reader holds (§10.5, D9.1)
            const std::lock_guard<std::mutex> guard(m_mutex);
            // a resize while a frame is in flight waits for the frame, so no frame mixes two extents (§6.2, D9.1)
            if (m_begun.load()) {
                m_pending_metrics = metrics;
                m_resize_pending.store(true);
                return;
            }
            // the submitted frames read the target and the swapchain, so both drain before they change (§6.3)
            wait_for_idle();
            // a target replaced by a resize has no pixels to preserve, so no old damage survives it (§6.1)
            m_damage.reset();
            // the previous target is kept aside, so a refused resize leaves the surface on the extent it presented (§6.3)
            Image previous_target = std::move(m_target);
            const std::size_t previous_finished = m_finished;
            // the target is reallocated first, because a refused allocation leaves the swapchain untouched (§6.2)
            allocate(metrics);
            if (m_target.valid() && (m_surface == VK_NULL_HANDLE || create_swapchain(metrics))) {
                // the metrics are published last, so no reader sees an extent the target does not carry (§6.3)
                m_metrics = metrics;
                return;
            }
            // the new extent did not take, so the extent the window already showed is restored with its target (§6.3)
            m_target = std::move(previous_target);
            m_finished = previous_finished;
        }

        bool AlborRenderSurface::retains_target() const {
            // the private target keeps its pixels between frames, which is what a partial frame loads (§6.1, §6.3)
            return true;
        }

        bool AlborRenderSurface::set_damage_region(const std::optional<Rect>& damage) {
            // a partial frame needs a target that already carries a full frame, because it loads rather than clears (§6.1)
            if (damage.has_value() && !m_target_initialized) {
                // the hint is refused, so stage two draws the full command list instead (§6.1)
                m_damage.reset();
                return false;
            }
            // the surface receives the region before begin, so encode knows whether the target may be loaded (§6.1)
            m_damage = damage;
            return damage.has_value();
        }

        RenderContext& AlborRenderSurface::begin_frame() {
            // a frame starts from an empty batch, which the context records the scene into (§6.3)
            m_batch.vertices.clear();
            m_batch.indices.clear();
            m_batch.draws.clear();
            m_batch.layers.clear();
            m_batch.uploads.clear();
            m_batch.shadows.clear();
            // the metrics the frame records against are read on the lock, so a resize cannot land between them (§10.5)
            WindowMetrics metrics;
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                metrics = m_metrics;
                // the frame is in flight before the lock is released, which the matching end presents (§6.1)
                m_begun.store(true);
            }
            // the context records logical points onto the physical target, so the device scale is handed to it (AD-27)
            m_context = std::make_unique<AlborRenderContext>(m_batch, Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), metrics.scale_factor, &m_glyph_paths, &m_glyph_meshes);
            // the frame counter advances once per frame, which the textures record and the glyph paths age against (AD-06)
            m_frame_counter++;
            // the textures age against the same counter, which hands back the ones no draw reached (AD-06)
            std::vector<Image> swept = m_textures.begin_frame(m_frame_counter);
            for (Image& image : swept) {
                // the swept image is retired to the slot this frame records on, which releases it two frames later (AD-06)
                m_pending_retired_images.push_back(std::move(image));
            }
            // the glyph paths age against the same counter, which drops the ones no draw reached (§6.4)
            m_glyph_paths.begin_frame(m_frame_counter);
            // the glyph meshes age against the same counter too, which drops the ones no draw reached (§6.4)
            m_glyph_meshes.begin_frame(m_frame_counter);
            return *m_context;
        }

        void AlborRenderSurface::end_frame() {
            // an end without a begun frame is a no operation rather than an error (§6.1)
            if (!m_begun.load()) {
                // a damage hint that never began a frame is spent, so it cannot leak into the next one (§6.1)
                m_damage.reset();
                return;
            }
            // the present is what the frame spends after the scene is recorded, which the profile reports (§6.3)
            const FrameProfileScope present_scope;
            // a full frame is the one that establishes the target a later partial frame loads (§6.1)
            const bool full_frame = !m_damage.has_value();
            // the batch is complete once the scene is recorded, so its size is what the profile reports (§6.3)
            profile_batch(m_batch.vertices.size(), m_batch.indices.size() / 3u, m_batch.draws.size(), m_batch.shadows.size(), m_glyph_meshes.count());
            bool pending = false;
            WindowMetrics pending_metrics;
            {
                // the surface records on its own command pool, so only its own state is locked (§6.3, AD-18b)
                const std::lock_guard<std::mutex> lock(m_mutex);
                // the frame two submissions ago finished on this slot, which is the only wait a steady frame makes (§6.3)
                FrameSlot& slot = m_slots[m_slot_cursor];
                if (!begin_slot(slot)) {
                    m_begun.store(false);
                    m_damage.reset();
                    return;
                }
                // the textures the sweep dropped join the slot, which releases them once it is recycled (AD-06)
                for (Image& image : m_pending_retired_images) {
                    // the slot owns the image until its fence signals, so no in-flight frame reads it after release (AD-06)
                    slot.retired_images.push_back(std::move(image));
                }
                // the pending list hands every image to the slot, which now owns their release (AD-06)
                m_pending_retired_images.clear();
                // the images the frame carries become textures once, which a later frame reuses (AD-06)
                const FrameProfileScope upload_scope;
                upload_textures(slot);
                profile_upload(upload_scope.elapsed_ms());
                // the encode replays the recorded draws, which the clear begins (§6.3)
                const FrameProfileScope encode_scope;
                encode(slot);
                profile_encode(encode_scope.elapsed_ms());
                // the frame target moves into the transfer layout the copy and the read back read (§6.3)
                transition_image(slot.command, m_target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                const FrameProfileScope commit_scope;
                // a window that rebuilt its role presents through the surface it created, which the wsi follows (§5.1)
                const bool presentable = refresh_window_surface();
                // a windowed frame acquires, copies and presents, while an offscreen frame only submits (§6.3)
                const bool presented = presentable && m_swapchain != VK_NULL_HANDLE && window_live() ? present(slot) : false;
                // a window that was torn down retires the swapchain, so no later frame presents into it (§10.5)
                if (!presented && m_swapchain != VK_NULL_HANDLE && !window_live()) {
                    // the frames already submitted still read the swapchain, which is drained before it goes (§6.3)
                    wait_for_idle();
                    destroy_swapchain();
                }
                // a frame the present left unsubmitted still finishes its target, which a read back waits on (§6.3)
                if (!presented && !slot.submitted && slot.command != VK_NULL_HANDLE) {
                    if (!submit_slot(slot, VK_NULL_HANDLE, 0, VK_NULL_HANDLE)) {
                        // the frame never reached the queue, so its dropped textures wait for the next submit (§6.3, AD-06)
                        for (Image& image : slot.retired_images) {
                            // the image moves back to the pending list, which the next submitted frame retires (AD-06)
                            m_pending_retired_images.push_back(std::move(image));
                        }
                        // the slot owns no dropped texture until a frame recorded on it reaches the queue (AD-06)
                        slot.retired_images.clear();
                        m_begun.store(false);
                        m_damage.reset();
                        return;
                    }
                }
                profile_commit(commit_scope.elapsed_ms());
                // the slot the frame recorded on is the last submitted, which a read back waits on (§6.3)
                m_last_slot = m_slot_cursor;
                m_slot_cursor = (m_slot_cursor + 1u) % FRAME_SLOTS;
                // the frame is no longer in flight, and a resize that waited for it is taken together (§6.2)
                m_begun.store(false);
                // a full frame initializes the target, while a partial keeps the flag it found (§6.1)
                if (full_frame && m_target.valid()) {
                    m_target_initialized = true;
                    m_finished++;
                }
                // the damage is spent, so the next frame starts full unless told otherwise (§6.1)
                m_damage.reset();
                pending = m_resize_pending.exchange(false);
                pending_metrics = m_pending_metrics;
            }
            // the recorded present includes the upload, the encode and the submit, which the profile reports (§6.3)
            profile_present(present_scope.elapsed_ms());
            // a resize that waited for this frame applies now, so the next frame uses one extent (§6.2)
            if (pending)
                resize(pending_metrics);
        }

        bool AlborRenderSurface::refresh_window_surface() {
#if defined(ALBOR_VULKAN_WAYLAND)
            // a surface without a wayland window has no rebuilt surface to follow (§6.3)
            if (m_wayland == nullptr)
                return true;
            // the surface and the retired ones are read under the state lock, which a dismissal takes (§10.5)
            wl_surface* current = nullptr;
            std::vector<wl_surface*> retired;
            {
                const std::lock_guard<std::mutex> lock(m_wayland->mutex);
                current = m_wayland->surface;
                retired.swap(m_wayland->retired);
            }
            // an unchanged surface is the one the swapchain already presents through (§6.3)
            if (current == m_wayland_surface) {
                // a surface the window retired stays until the wsi releases the one it names (§10.5)
                if (!retired.empty()) {
                    const std::lock_guard<std::mutex> lock(m_wayland->mutex);
                    m_wayland->retired.insert(m_wayland->retired.end(), retired.begin(), retired.end());
                }
                return current != nullptr;
            }
            // the wsi surface still names the wl_surface the window released, so it drains before it goes (§6.3)
            wait_for_idle();
            destroy_swapchain();
            m_wayland_surface = current;
            // the retired surfaces are released now, because no wsi surface names them any more (§10.5)
            for (wl_surface* surface : retired)
                wl_surface_destroy(surface);
            // a window that rebuilt its role presents through the fresh surface, which the wsi surface follows (§5.1)
            if (current == nullptr)
                return false;
            m_surface = create_window_surface(m_handle);
            if (m_surface == VK_NULL_HANDLE)
                return false;
            return create_swapchain(m_metrics);
#else
            // a window system without a rebuilt surface presents through the one it built (§6.3)
            return true;
#endif
        }

        bool AlborRenderSurface::present(FrameSlot& slot) {
            // a swapchain the last present called stale is rebuilt before another image is acquired (§6.3)
            if (m_swapchain_stale) {
                m_swapchain_stale = false;
                // the images the presentation engine still holds are drained before their swapchain is replaced (§6.3)
                wait_for_idle();
                if (!create_swapchain(m_metrics)) {
                    note_result(VK_ERROR_OUT_OF_DATE_KHR);
                    return false;
                }
            }
            // the acquire names the image the frame copies into, which the slot's semaphore makes available (§6.3)
            std::uint32_t index = 0;
            VkResult acquired = VK_SUCCESS;
            {
                // the wait for the image is the compositor pacing, which the profile reports on its own (§6.3)
                const FrameProfileScope wait_scope;
                acquired = vkAcquireNextImageKHR(device_state().device, m_swapchain, UINT64_MAX, slot.image_available, VK_NULL_HANDLE, &index);
                profile_drawable_wait(wait_scope.elapsed_ms());
            }
            if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
                // no image was acquired, so the frame still finishes its target and the swapchain is rebuilt (§6.3)
                if (!submit_slot(slot, VK_NULL_HANDLE, 0, VK_NULL_HANDLE))
                    return false;
                wait_for_idle();
                if (!create_swapchain(m_metrics))
                    note_result(acquired);
                return true;
            }
            if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
                // the acquire failed without signalling its semaphore, so the frame only finishes its target (§6.3)
                if (!submit_slot(slot, VK_NULL_HANDLE, 0, VK_NULL_HANDLE))
                    return false;
                note_result(acquired);
                return true;
            }
            // a suboptimal image is acquired and signalled, so the frame presents and the next one rebuilds (§6.3)
            if (acquired == VK_SUBOPTIMAL_KHR)
                m_swapchain_stale = true;
            // the copy moves the finished target into the swapchain image, which the present shows (§6.3)
            VkCommandBuffer command = slot.command;
            VkImageLayout& layout = m_swapchain_layouts[index];
            transition_image(command, m_swapchain_images[index], layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            const bool same_extent = m_target.width == m_swapchain_extent.width && m_target.height == m_swapchain_extent.height;
            if (m_swapchain_format == TARGET_FORMAT && same_extent) {
                VkImageCopy region{};
                region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.srcSubresource.layerCount = 1;
                region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.dstSubresource.layerCount = 1;
                region.extent = {m_target.width, m_target.height, 1};
                vkCmdCopyImage(command, m_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapchain_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }
            else {
                // a differently sized or formatted back buffer is scaled, which maps the same frame onto it (§6.3)
                if (!m_swapchain_blittable)
                    return false;
                VkImageBlit region{};
                region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.srcSubresource.layerCount = 1;
                region.srcOffsets[1] = {static_cast<std::int32_t>(m_target.width), static_cast<std::int32_t>(m_target.height), 1};
                region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.dstSubresource.layerCount = 1;
                region.dstOffsets[1] = {static_cast<std::int32_t>(m_swapchain_extent.width), static_cast<std::int32_t>(m_swapchain_extent.height), 1};
                vkCmdBlitImage(command, m_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapchain_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
            }
            transition_image(command, m_swapchain_images[index], layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
            // the submit waits for the acquire and signals the present, which finishes the frame (§6.3)
            if (!submit_slot(slot, slot.image_available, VK_PIPELINE_STAGE_TRANSFER_BIT, slot.render_finished))
                return false;
            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &slot.render_finished;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &m_swapchain;
            present_info.pImageIndices = &index;
            const VkResult presented = vkQueuePresentKHR(device_state().queue, &present_info);
            if (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR)
                submitted_frames().fetch_add(1);
            note_result(presented);
            // a present the surface rejected leaves its semaphore signalled, so the next frame rebuilds both (§6.3)
            if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR) {
                reset_semaphores();
                m_swapchain_stale = true;
            }
            // the frame returned without waiting for the display, so the cpu records the next one (§6.3, AD-18b)
            return true;
        }

        std::vector<std::uint8_t> AlborRenderSurface::read_pixels() const {
            // the read records on the surface's own buffers, so the lock serializes it with the frames (§6.3)
            const std::lock_guard<std::mutex> lock(m_mutex);
            // a read before the first finished frame reports nothing (§6.3)
            if (!valid_locked() || m_finished == 0 || m_begun.load())
                return {};
            const std::uint32_t width = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(m_metrics.pixel_size.width));
            const std::uint32_t height = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(m_metrics.pixel_size.height));
            // the read back buffer is allocated once, which a second read reuses (§6.3, AD-06)
            const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
            if (!m_readback.valid() || m_readback.size < bytes)
                m_readback = Buffer{};
            if (!m_readback.valid()) {
                if (!create_host_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_readback))
                    return {};
            }
            // every submitted frame is waited, so the target carries the pixels the read copies (§6.3)
            AlborRenderSurface& surface = const_cast<AlborRenderSurface&>(*this);
            surface.wait_for_idle();
            // the read records on the slot that submitted the last frame, which the wait left free (§6.3)
            FrameSlot& slot = surface.m_slots[m_last_slot];
            if (vkResetCommandBuffer(slot.command, 0) != VK_SUCCESS)
                return {};
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(slot.command, &begin) != VK_SUCCESS)
                return {};
            transition_image(slot.command, surface.m_target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(slot.command, m_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback.buffer, 1, &region);
            if (!surface.submit_slot(slot, VK_NULL_HANDLE, 0, VK_NULL_HANDLE))
                return {};
            if (vkWaitForFences(device_state().device, 1, &slot.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
                return {};
            slot.submitted = false;
            // the compositor hands the bytes bgra and the canonical order is rgba, so the read swaps the channels (AD-26)
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
            const auto* source = static_cast<const std::uint8_t*>(m_readback.mapped);
            for (std::uint32_t y = 0; y < height; y++) {
                auto* out = pixels.data() + static_cast<std::size_t>(y) * 4u * static_cast<std::size_t>(width);
                for (std::uint32_t x = 0; x < width; x++) {
                    const std::uint8_t* texel = source + (static_cast<std::size_t>(y) * width + x) * 4u;
                    out[x * 4u + 0u] = texel[2u];
                    out[x * 4u + 1u] = texel[1u];
                    out[x * 4u + 2u] = texel[0u];
                    out[x * 4u + 3u] = texel[3u];
                }
            }
            return pixels;
        }

        void AlborRenderSurface::upload_textures(FrameSlot& slot) {
            VkCommandBuffer command = slot.command;
            // a texture a draw references stays resident, which the eviction reads as its last use (AD-06)
            for (const BatchDraw& draw : m_batch.draws) {
                // a draw that samples no texture reaches nothing, which is the case of a solid fill (AD-06)
                if (draw.texture != 0)
                    m_textures.touch(draw.texture);
            }
            // every image the frame carries becomes a texture the cache holds by its identifier (AD-06)
            for (const BatchUpload& upload : m_batch.uploads) {
                // an upload with no image or an identifier already cached needs no texture this frame (AD-06)
                if (upload.id == 0 || upload.image == nullptr || m_textures.peek(upload.id) != nullptr)
                    continue;
                // the pixels are premultiplied rgba8, which the uploaded format already matches (§6.3, AD-26)
                const std::size_t width = upload.image->width();
                const std::size_t height = upload.image->height();
                if (width == 0 || height == 0 || upload.image->format() != ImageFormat::RGBA8)
                    continue;
                // a full mip chain keeps a minified image from aliasing, which the reference also avoids (§6.4, AD-26)
                std::uint32_t levels = 1;
                // a format the device cannot blit keeps one level rather than recording a refused blit (§6.4)
                if (device_state().texture_blittable) {
                    for (std::size_t size = std::max(width, height); size > 1; size >>= 1u)
                        levels++;
                }
                Image texture;
                if (create_image(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), levels, TEXTURE_FORMAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_UNDEFINED, texture) != VK_SUCCESS)
                    continue;
                // the upload copies the row packed bytes into level zero, which the mip chain is generated from (§6.4)
                const VkDeviceSize bytes = static_cast<VkDeviceSize>(upload.image->row_bytes()) * static_cast<VkDeviceSize>(height);
                Buffer staging;
                if (!create_host_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging))
                    continue;
                std::memcpy(staging.mapped, upload.image->pixels().data(), static_cast<std::size_t>(bytes));
                // every level starts in the transfer destination layout, which the copy and the blits write (§6.4)
                transition_mips(command, texture.image, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                VkBufferImageCopy region{};
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
                vkCmdCopyBufferToImage(command, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                // the staging buffer outlives the command buffer, because the copy reads it when the queue runs (§6.3)
                slot.retired.push_back(std::move(staging));
                // every level is blitted from the one above, which is the chain the sampler reads (§6.4, AD-26)
                for (std::uint32_t level = 1; level < levels; level++) {
                    // the source level moves to the read layout, which the blit samples (§6.4)
                    transition_mips(command, texture.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                    VkImageBlit blit{};
                    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.srcSubresource.mipLevel = level - 1;
                    blit.srcSubresource.layerCount = 1;
                    blit.srcOffsets[1] = {static_cast<std::int32_t>(std::max<std::size_t>(1u, width >> (level - 1u))), static_cast<std::int32_t>(std::max<std::size_t>(1u, height >> (level - 1u))), 1};
                    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    blit.dstSubresource.mipLevel = level;
                    blit.dstSubresource.layerCount = 1;
                    blit.dstOffsets[1] = {static_cast<std::int32_t>(std::max<std::size_t>(1u, width >> level)), static_cast<std::int32_t>(std::max<std::size_t>(1u, height >> level)), 1};
                    // the destination level is already in the transfer layout, which the blit writes (§6.4)
                    vkCmdBlitImage(command, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                }
                // the source levels move to the sampled layout, which the draws read for the whole chain (§6.4)
                if (levels > 1)
                    transition_mips(command, texture.image, 0, levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                // the last level was written by the last blit, so its own barrier finishes the chain (§6.4)
                transition_mips(command, texture.image, levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                // the store hands back the coldest textures it drops, which the slot retires rather than destroys (AD-06)
                std::vector<Image> dropped = m_textures.store(upload.id, std::move(texture));
                for (Image& image : dropped) {
                    // the dropped image is kept until the slot that recorded this frame is recycled (AD-06)
                    slot.retired_images.push_back(std::move(image));
                }
            }
        }

        bool AlborRenderSurface::fill_vertices(RetainedVertices& target, const BatchVertex* vertices, std::size_t count, VkBufferUsageFlags usage) {
            // a frame with no vertex has nothing the stage could index, so no buffer is touched (§6.2)
            if (vertices == nullptr || count == 0)
                return false;
            // a buffer smaller than the batch grows, which is the only allocation a steady frame avoids (AD-06)
            if (!target.buffer.valid() || target.capacity < count) {
                // the buffer doubles, so a stream of growing text does not allocate on every larger frame (AD-06)
                const std::size_t capacity = std::max(count, target.capacity * 2u);
                Buffer buffer;
                if (!create_host_buffer(static_cast<VkDeviceSize>(capacity * sizeof(BatchVertex)), usage, buffer))
                    return false;
                target.buffer = std::move(buffer);
                target.capacity = capacity;
                note_batch_buffer_allocation();
            }
            // the frame's vertices overwrite the buffer, which a steady frame rewrites in place (§6.3)
            std::memcpy(target.buffer.mapped, vertices, count * sizeof(BatchVertex));
            return true;
        }

        bool AlborRenderSurface::fill_indices(FrameSlot& slot, const std::vector<std::uint32_t>& indices) {
            // a frame with no index has nothing to draw, so no buffer is touched (§6.2)
            if (indices.empty())
                return false;
            // a buffer smaller than the batch grows, which is the only allocation a steady frame avoids (AD-06)
            if (!slot.indices.buffer.valid() || slot.indices.capacity < indices.size()) {
                // the buffer doubles, so a stream of growing text does not allocate on every larger frame (AD-06)
                const std::size_t capacity = std::max(indices.size(), slot.indices.capacity * 2u);
                Buffer buffer;
                if (!create_host_buffer(static_cast<VkDeviceSize>(capacity * sizeof(std::uint32_t)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, buffer))
                    return false;
                slot.indices.buffer = std::move(buffer);
                slot.indices.capacity = capacity;
                note_batch_buffer_allocation();
            }
            // the frame's indices overwrite the buffer, which a steady frame rewrites in place (§6.3)
            std::memcpy(slot.indices.buffer.mapped, indices.data(), indices.size() * sizeof(std::uint32_t));
            return true;
        }

        void AlborRenderSurface::note_batch_buffer_allocation() {
            // a surface built without a counter still draws, which the portable shell does (§6.2)
            if (m_batch_buffer_allocations != nullptr)
                m_batch_buffer_allocations->fetch_add(1u, std::memory_order_relaxed);
        }

        std::uint32_t AlborRenderSurface::write_uniforms(FrameSlot& slot, const void* block, std::size_t size) {
            // the ring grows when a scene carries more draws than it was sized for, which keeps every draw (§6.3)
            DeviceState& state = device_state();
            if (slot.uniform_cursor + state.uniform_stride > slot.uniforms.size) {
                // the ring doubles, and the frame's descriptor sets are reallocated against the new buffer (§6.3)
                const VkDeviceSize capacity = std::max<VkDeviceSize>(state.uniform_stride * DESCRIPTOR_CAPACITY, slot.uniforms.size * 2u);
                Buffer buffer;
                if (!create_host_buffer(capacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer))
                    return std::numeric_limits<std::uint32_t>::max();
                // the buffer the frame already recorded stays alive for its recorded draws (§6.3)
                if (slot.uniforms.valid())
                    slot.retired.push_back(std::move(slot.uniforms));
                slot.uniforms = std::move(buffer);
                slot.descriptors.clear();
                note_batch_buffer_allocation();
            }
            const VkDeviceSize offset = slot.uniform_cursor;
            std::memcpy(static_cast<std::uint8_t*>(slot.uniforms.mapped) + offset, block, size);
            slot.uniform_cursor += state.uniform_stride;
            return static_cast<std::uint32_t>(offset);
        }

        std::uint32_t AlborRenderSurface::fill_quad_vertices(FrameSlot& slot, const BatchVertex* quad, std::size_t count) {
            // a frame with no vertex has nothing the stage could index, so no buffer is touched (§6.2)
            if (quad == nullptr || count == 0)
                return std::numeric_limits<std::uint32_t>::max();
            // the range this quad takes is reserved after every quad the frame already recorded (§6.3)
            const VkDeviceSize alignment = device_state().properties.limits.minStorageBufferOffsetAlignment;
            const std::size_t alignment_vertices = std::max<std::size_t>(1u, static_cast<std::size_t>(alignment / sizeof(BatchVertex)));
            const std::size_t begin = (slot.quad_cursor + alignment_vertices - 1u) / alignment_vertices * alignment_vertices;
            const std::size_t needed = begin + count;
            // a buffer smaller than the frame's quads grows, which copies the ranges already recorded (§6.3, AD-06)
            if (!slot.quad_vertices.buffer.valid() || slot.quad_vertices.capacity < needed) {
                // the buffer doubles, so a frame with many layers does not allocate on every composite (§6.3)
                const std::size_t capacity = std::max(needed, slot.quad_vertices.capacity * 2u);
                Buffer buffer;
                if (!create_host_buffer(static_cast<VkDeviceSize>(capacity * sizeof(BatchVertex)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, buffer))
                    return std::numeric_limits<std::uint32_t>::max();
                // the buffer the frame already recorded stays alive for its deferred draws, which still read it (§6.3)
                if (slot.quad_vertices.buffer.valid())
                    slot.retired.push_back(std::move(slot.quad_vertices.buffer));
                slot.quad_vertices.buffer = std::move(buffer);
                slot.quad_vertices.capacity = capacity;
                // the descriptors written against the retired buffer are rebuilt against the new one (§6.3)
                slot.descriptors.clear();
                note_batch_buffer_allocation();
            }
            // the quad lands at the reserved range, which its own descriptor names (§6.3)
            std::memcpy(static_cast<std::uint8_t*>(slot.quad_vertices.buffer.mapped) + begin * sizeof(BatchVertex), quad, count * sizeof(BatchVertex));
            slot.quad_cursor = needed;
            return static_cast<std::uint32_t>(begin * sizeof(BatchVertex));
        }

        VkDescriptorSet AlborRenderSurface::descriptor_for(FrameSlot& slot, VkBuffer vertices, VkDeviceSize vertex_offset, VkDeviceSize vertex_range, VkImageView paint, VkImageView blur) {
            DeviceState& state = device_state();
            // a set the frame already wrote for this exact key is rebound rather than allocated again (§6.3)
            const DescriptorKey key{.vertices = vertices, .offset = vertex_offset, .paint = paint, .blur = blur};
            for (const DescriptorEntry& entry : slot.descriptors) {
                if (entry.key.vertices == key.vertices && entry.key.offset == key.offset && entry.key.paint == key.paint && entry.key.blur == key.blur)
                    return entry.set;
            }
            // the set is allocated from the slot pool, which a reset recycles (§6.3)
            VkDescriptorSetAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocation.descriptorPool = slot.descriptor_pool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &state.set_layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(state.device, &allocation, &set) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            // the uniform range names the whole ring, which the dynamic offset selects a block from (§6.3)
            VkDescriptorBufferInfo uniform{};
            uniform.buffer = slot.uniforms.buffer;
            uniform.offset = 0;
            uniform.range = sizeof(DrawUniforms);
            VkDescriptorBufferInfo vertices_info{};
            vertices_info.buffer = vertices;
            vertices_info.offset = vertex_offset;
            vertices_info.range = vertex_range;
            VkDescriptorImageInfo paint_info{};
            paint_info.imageView = paint;
            paint_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorImageInfo blur_info{};
            blur_info.imageView = blur;
            blur_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorImageInfo sampler_info{};
            sampler_info.sampler = state.sampler;
            VkWriteDescriptorSet writes[4]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = set;
            writes[0].dstBinding = UNIFORM_BINDING;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            writes[0].pBufferInfo = &uniform;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = set;
            writes[1].dstBinding = PAINT_BINDING;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[1].pImageInfo = &paint_info;
            // the blur slot reads the source the blur pipeline samples, which the composite leaves white (§6.2)
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = set;
            writes[2].dstBinding = BLUR_BINDING;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[2].pImageInfo = &blur_info;
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = set;
            writes[3].dstBinding = SAMPLER_BINDING;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[3].pImageInfo = &sampler_info;
            VkWriteDescriptorSet vertex_write{};
            vertex_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vertex_write.dstSet = set;
            vertex_write.dstBinding = VERTEX_BINDING;
            vertex_write.descriptorCount = 1;
            vertex_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vertex_write.pBufferInfo = &vertices_info;
            VkWriteDescriptorSet all[5] = {writes[0], writes[1], writes[2], writes[3], vertex_write};
            vkUpdateDescriptorSets(state.device, 5, all, 0, nullptr);
            // the set joins the frame cache, so the next draw of the same key reuses it (§6.3)
            slot.descriptors.push_back(DescriptorEntry{.key = key, .set = set});
            return set;
        }

        std::uint32_t AlborRenderSurface::acquire_scratch(FrameSlot& slot, std::uint32_t width, std::uint32_t height) {
            // a target the frame already finished with is reused when its extent matches (§6.2, AD-06)
            for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(slot.scratch.size()); index++) {
                ScratchTarget& target = slot.scratch[index];
                if (target.in_use || !target.image.valid())
                    continue;
                if (target.image.width != width || target.image.height != height)
                    continue;
                target.in_use = true;
                return index;
            }
            // a slot at its ceiling recycles the oldest target, which a growing cast would hold (§6.2, AD-06)
            if (slot.scratch.size() >= MAXIMUM_SCRATCH_TARGETS) {
                ScratchTarget& coldest = slot.scratch.front();
                if (coldest.in_use)
                    return std::numeric_limits<std::uint32_t>::max();
                coldest.image = Image{};
                if (create_image(width, height, 1, TARGET_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED, coldest.image) != VK_SUCCESS)
                    return std::numeric_limits<std::uint32_t>::max();
                coldest.in_use = true;
                return 0;
            }
            // a new target joins the slot, which the next frames of the slot reuse (§6.2, AD-06)
            ScratchTarget target;
            if (create_image(width, height, 1, TARGET_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED, target.image) != VK_SUCCESS)
                return std::numeric_limits<std::uint32_t>::max();
            target.in_use = true;
            slot.scratch.push_back(std::move(target));
            return static_cast<std::uint32_t>(slot.scratch.size() - 1u);
        }

        void AlborRenderSurface::draw_batch_draw(FrameSlot& slot, Encoder& encoder, const BatchDraw& draw, Image& target) {
            // the draw binds its composition pipeline, and the three exact modes use theirs (§6.2)
            const std::size_t composition = static_cast<std::size_t>(draw.composition);
            VkPipeline pipeline = device_state().pipelines[composition];
            if (pipeline == VK_NULL_HANDLE)
                return;
            // the packed colour unpacks into premultiplied channels, which the shader multiplies through
            DrawUniforms uniforms{};
            const float alpha = static_cast<float>((draw.color >> 24) & 0xFFu) / 255.0f;
            uniforms.color[0] = static_cast<float>(draw.color & 0xFFu) / 255.0f;
            uniforms.color[1] = static_cast<float>((draw.color >> 8) & 0xFFu) / 255.0f;
            uniforms.color[2] = static_cast<float>((draw.color >> 16) & 0xFFu) / 255.0f;
            uniforms.color[3] = alpha;
            // the inverse clip matrix lands row wise, which maps a fragment onto the clip centre (§6.2)
            uniforms.inverse0[0] = draw.scissor.inverse.scale_x;
            uniforms.inverse0[1] = draw.scissor.inverse.skew_x;
            uniforms.inverse0[2] = draw.scissor.inverse.translate_x;
            uniforms.inverse1[0] = draw.scissor.inverse.skew_y;
            uniforms.inverse1[1] = draw.scissor.inverse.scale_y;
            uniforms.inverse1[2] = draw.scissor.inverse.translate_y;
            // the half extents and the feather scales turn the masked distance into a one pixel ramp (§6.2)
            uniforms.extent_scale[0] = draw.scissor.extent_x;
            uniforms.extent_scale[1] = draw.scissor.extent_y;
            uniforms.extent_scale[2] = draw.scissor.scale_x;
            uniforms.extent_scale[3] = draw.scissor.scale_y;
            // the corner radius rounds the mask, which zero leaves the clip rectangular (§6.2)
            uniforms.extra[0] = draw.scissor.radius;
            // the target extent projects a device position into clip space (§6.3)
            uniforms.target_size[0] = static_cast<float>(target.width);
            uniforms.target_size[1] = static_cast<float>(target.height);
            uniforms.ramp = draw.ramp;
            uniforms.kind = draw.coverage == CoverageKind::Folded ? 1 : 0;
            uniforms.scissor_enabled = draw.scissor.enabled ? 1 : 0;
            // a draw with a texture samples it, and a solid one samples the white texel (§6.3, §6.4)
            uniforms.has_texture = draw.texture == 0 ? 0 : 1;
            // the shader reshapes its alpha for the modes that replace the destination (§6.2)
            uniforms.blend = static_cast<int>(draw.composition);
            // the shader adds the first vertex of the draw, which the indexed draw leaves out of its index (§6.3)
            uniforms.base_vertex = static_cast<std::int32_t>(draw.base_vertex);
            const std::uint32_t offset = write_uniforms(slot, &uniforms, sizeof(uniforms));
            if (offset == std::numeric_limits<std::uint32_t>::max())
                return;
            // the draw binds its own texture, or the white texel a solid draw samples (§6.3, §6.4)
            VkImageView paint = device_state().white.view;
            if (draw.texture != 0) {
                // the upload the frame carried became the texture the cache holds (AD-06)
                const Image* found = m_textures.peek(draw.texture);
                if (found != nullptr && found->valid())
                    paint = found->view;
            }
            VkDescriptorSet set = descriptor_for(slot, slot.vertices.buffer.buffer, 0, static_cast<VkDeviceSize>(slot.vertices.capacity) * sizeof(BatchVertex), paint, device_state().white.view);
            if (set == VK_NULL_HANDLE)
                return;
            vkCmdBindPipeline(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, device_state().pipeline_layout, 0, 1, &set, 1, &offset);
            vkCmdBindIndexBuffer(encoder.command, slot.indices.buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            // the draw consumes its own index range, which the builder recorded, based at its own first vertex (§6.3)
            vkCmdDrawIndexed(encoder.command, draw.index_count, 1, draw.index_begin, 0, 0);
        }

        void AlborRenderSurface::composite_layer(FrameSlot& slot, Encoder& encoder, float opacity, Image& offscreen, Image& parent) {
            DeviceState& state = device_state();
            // a refused offscreen target draws the group into its parent instead, because no offscreen exists (§6.2)
            if (!offscreen.valid())
                return;
            // the quad spans the whole parent, because a layer covers the frame it was recorded for (§6.2)
            const float width = static_cast<float>(parent.width);
            const float height = static_cast<float>(parent.height);
            BatchVertex quad[6];
            const float corners[6][2] = {{0.0f, 0.0f}, {width, 0.0f}, {0.0f, height}, {0.0f, height}, {width, 0.0f}, {width, height}};
            for (std::size_t index = 0; index < 6; index++) {
                // the corner lands where the parent it covers begins (§6.2)
                quad[index].x = corners[index][0];
                quad[index].y = corners[index][1];
                quad[index].u = (corners[index][0] == width) ? 1.0f : 0.0f;
                quad[index].v = (corners[index][1] == height) ? 1.0f : 0.0f;
            }
            // the group dims once, which a premultiplied white at the opacity multiplies into every texel (§6.2)
            DrawUniforms uniforms{};
            uniforms.color[0] = opacity;
            uniforms.color[1] = opacity;
            uniforms.color[2] = opacity;
            uniforms.color[3] = opacity;
            uniforms.target_size[0] = width;
            uniforms.target_size[1] = height;
            uniforms.ramp = 1.0f;
            uniforms.kind = 0;
            uniforms.has_texture = 1;
            // the layer composites source over the parent, which is the one blend a group lands with (§6.2)
            draw_quad(slot, encoder, quad, uniforms, offscreen.view, state.pipelines[0]);
        }

        void AlborRenderSurface::composite_shadow(FrameSlot& slot, Encoder& encoder, const BatchShadow& shadow, Image& target) {
            DeviceState& state = device_state();

            // the offscreen target covers the caster plus the reach, clipped to the frame it lands on (§6.2)
            const float reach = std::max(shadow.reach, 1.0f);
            const float left = std::max(shadow.rect.x - reach, 0.0f);
            const float top = std::max(shadow.rect.y - reach, 0.0f);
            const float right = std::min(shadow.rect.x + shadow.rect.width + reach, static_cast<float>(target.width));
            const float bottom = std::min(shadow.rect.y + shadow.rect.height + reach, static_cast<float>(target.height));
            // a cast entirely outside the frame reaches no pixel, so its offscreen passes stay unallocated (§6.2)
            if (right <= left || bottom <= top)
                return;
            // the two offscreen dimensions never exceed the frame, which bounds the allocation a blur can request (§6.2)
            const float width = right - left;
            const float height = bottom - top;
            const std::uint32_t pixels_width = static_cast<std::uint32_t>(width);
            const std::uint32_t pixels_height = static_cast<std::uint32_t>(height);
            if (pixels_width == 0 || pixels_height == 0)
                return;
            // two offscreen targets the slot already carries ping pong the silhouette through the blur (§6.2, AD-06)
            const std::uint32_t bucket_width = bucket_extent(pixels_width);
            const std::uint32_t bucket_height = bucket_extent(pixels_height);
            const std::uint32_t silhouette_index = acquire_scratch(slot, bucket_width, bucket_height);
            const std::uint32_t scratch_index = acquire_scratch(slot, bucket_width, bucket_height);
            if (silhouette_index == std::numeric_limits<std::uint32_t>::max() || scratch_index == std::numeric_limits<std::uint32_t>::max())
                return;
            // the references are taken once both targets exist, so no later acquire invalidates them (§6.2)
            Image& silhouette = slot.scratch[silhouette_index].image;
            Image& scratch = slot.scratch[scratch_index].image;
            // a bucketed target is larger than the cast, so the composite reads its corner of it (§6.2)
            const float u_max = width / static_cast<float>(silhouette.width);
            const float v_max = height / static_cast<float>(silhouette.height);
            // the silhouette carries the cast colour, which the blur then spreads (§6.2)
            ShadowUniforms uniforms{};
            uniforms.color[0] = static_cast<float>(shadow.color & 0xFFu) / 255.0f;
            uniforms.color[1] = static_cast<float>((shadow.color >> 8) & 0xFFu) / 255.0f;
            uniforms.color[2] = static_cast<float>((shadow.color >> 16) & 0xFFu) / 255.0f;
            uniforms.color[3] = static_cast<float>((shadow.color >> 24) & 0xFFu) / 255.0f;
            // the silhouette is evaluated in the caster's own space, so the inverse maps a target pixel into it (§6.2)
            const Transform inverse = invert_transform(shadow.transform);
            uniforms.inverse0[0] = inverse.scale_x;
            uniforms.inverse0[1] = inverse.skew_x;
            uniforms.inverse0[2] = inverse.translate_x;
            // the w lane is unused, because the shader turns the local gradient into a device length (§6.2)
            uniforms.inverse0[3] = 0.0f;
            uniforms.inverse1[0] = inverse.skew_y;
            uniforms.inverse1[1] = inverse.scale_y;
            uniforms.inverse1[2] = inverse.translate_y;
            // the caster's half extent is held in its own space, where the rounded shape is measured (§6.2)
            uniforms.shape[0] = shadow.extent.width * 0.5f;
            uniforms.shape[1] = shadow.extent.height * 0.5f;
            // the offscreen target starts at the reach corner, which a target pixel is offset by (§6.2)
            uniforms.shape[2] = left;
            uniforms.shape[3] = top;
            uniforms.params[0] = shadow.radius;
            // the silhouette pass fills the cast alone, which no blend mixes with the clear (§6.2)
            begin_rendering(encoder, silhouette, true);
            const std::uint32_t offset = write_uniforms(slot, &uniforms, sizeof(uniforms));
            if (offset == std::numeric_limits<std::uint32_t>::max())
                return;
            VkDescriptorSet set = descriptor_for(slot, slot.vertices.buffer.valid() ? slot.vertices.buffer.buffer : state.dummy_vertices.buffer, 0, sizeof(BatchVertex), state.white.view, state.white.view);
            if (set == VK_NULL_HANDLE)
                return;
            vkCmdBindPipeline(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, state.shadow_shape_pipeline);
            vkCmdBindDescriptorSets(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline_layout, 0, 1, &set, 1, &offset);
            vkCmdDraw(encoder.command, 4, 1, 0, 0);
            // a sigma of zero needs no blur, which a hard cast carries (§6.2)
            const float sigma = shadow.sigma;
            if (sigma > 0.0f) {
                // the kernel carries a spread of 1.69 taps, which the sigma turns into a texel step (§6.2)
                const float step = sigma / 2.5f;
                // the horizontal pass spreads the silhouette across the scratch target (§6.2)
                blur_pass(slot, encoder, silhouette, scratch, uniforms, 1.0f, 0.0f, step, static_cast<float>(silhouette.width), static_cast<float>(silhouette.height));
                // the vertical pass spreads it back, which finishes the separable gaussian (§6.2)
                blur_pass(slot, encoder, scratch, silhouette, uniforms, 0.0f, 1.0f, step, static_cast<float>(silhouette.width), static_cast<float>(silhouette.height));
            }
            // the cast composites over what is under it, which the fill pipeline blends source over (§6.2)
            sample_target(encoder, silhouette);
            begin_rendering(encoder, target, false);
            BatchVertex quad[6];
            const float corners[6][2] = {{left, top}, {left + width, top}, {left, top + height}, {left, top + height}, {left + width, top}, {left + width, top + height}};
            for (std::size_t index = 0; index < 6; index++) {
                // the corner lands where the offscreen target sits on the frame (§6.2)
                quad[index].x = corners[index][0];
                quad[index].y = corners[index][1];
                quad[index].u = (index == 1 || index == 4 || index == 5) ? u_max : 0.0f;
                quad[index].v = (index == 2 || index == 3 || index == 5) ? v_max : 0.0f;
            }
            // the quad is drawn as two triangles over the four corners the target spans (§6.2)
            DrawUniforms draw{};
            draw.color[0] = 1.0f;
            draw.color[1] = 1.0f;
            draw.color[2] = 1.0f;
            draw.color[3] = 1.0f;
            draw.target_size[0] = static_cast<float>(target.width);
            draw.target_size[1] = static_cast<float>(target.height);
            // the cast carries the scissor of the moment it was recorded, which the mask applies (§6.2)
            draw.inverse0[0] = shadow.scissor.inverse.scale_x;
            draw.inverse0[1] = shadow.scissor.inverse.skew_x;
            draw.inverse0[2] = shadow.scissor.inverse.translate_x;
            draw.inverse1[0] = shadow.scissor.inverse.skew_y;
            draw.inverse1[1] = shadow.scissor.inverse.scale_y;
            draw.inverse1[2] = shadow.scissor.inverse.translate_y;
            draw.extent_scale[0] = shadow.scissor.extent_x;
            draw.extent_scale[1] = shadow.scissor.extent_y;
            draw.extent_scale[2] = shadow.scissor.scale_x;
            draw.extent_scale[3] = shadow.scissor.scale_y;
            draw.extra[0] = shadow.scissor.radius;
            draw.ramp = 1.0f;
            draw.kind = 0;
            draw.scissor_enabled = shadow.scissor.enabled ? 1 : 0;
            draw.has_texture = 1;
            // the cast composites into the target it covers, which its own pass binds and loads (§6.2)
            draw_quad(slot, encoder, quad, draw, silhouette.view, state.pipelines[0]);
        }

        void AlborRenderSurface::blur_pass(FrameSlot& slot, Encoder& encoder, Image& source, Image& target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height) {
            DeviceState& state = device_state();
            // the direction and the step ride in the params lane, which the shader reads (§6.2)
            uniforms.params[1] = dx;
            uniforms.params[2] = dy;
            uniforms.params[3] = step / (dx != 0.0f ? width : height);
            // the pass reads the source and writes the target, which the two directions swap (§6.2)
            sample_target(encoder, source);
            begin_rendering(encoder, target, true);
            const std::uint32_t offset = write_uniforms(slot, &uniforms, sizeof(uniforms));
            if (offset == std::numeric_limits<std::uint32_t>::max())
                return;
            VkDescriptorSet set = descriptor_for(slot, slot.vertices.buffer.valid() ? slot.vertices.buffer.buffer : state.dummy_vertices.buffer, 0, sizeof(BatchVertex), state.white.view, source.view);
            if (set == VK_NULL_HANDLE)
                return;
            vkCmdBindPipeline(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, state.shadow_blur_pipeline);
            vkCmdBindDescriptorSets(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline_layout, 0, 1, &set, 1, &offset);
            vkCmdDraw(encoder.command, 4, 1, 0, 0);
        }

        void AlborRenderSurface::draw_quad(FrameSlot& slot, Encoder& encoder, const BatchVertex* quad, const DrawUniforms& uniforms, VkImageView texture, VkPipeline pipeline) {
            DeviceState& state = device_state();
            // the composite quad is a six vertex list, which the frame's quad range takes (§6.2)
            const std::uint32_t vertex_offset = fill_quad_vertices(slot, quad, 6);
            if (vertex_offset == std::numeric_limits<std::uint32_t>::max())
                return;
            const std::uint32_t offset = write_uniforms(slot, &uniforms, sizeof(uniforms));
            if (offset == std::numeric_limits<std::uint32_t>::max())
                return;
            VkDescriptorSet set = descriptor_for(slot, slot.quad_vertices.buffer.buffer, vertex_offset, static_cast<VkDeviceSize>(slot.quad_vertices.capacity) * sizeof(BatchVertex) - vertex_offset, texture, state.white.view);
            if (set == VK_NULL_HANDLE)
                return;
            vkCmdBindPipeline(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(encoder.command, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline_layout, 0, 1, &set, 1, &offset);
            vkCmdDraw(encoder.command, 6, 1, 0, 0);
        }

        void AlborRenderSurface::encode(FrameSlot& slot) {
            // the retained vertex and index buffers take the batch, which a steady frame rewrites (§6.3, AD-06)
            const bool has_vertices = fill_vertices(slot.vertices, m_batch.vertices.data(), m_batch.vertices.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            const bool has_indices = fill_indices(slot, m_batch.indices);
            // the frame target is the one the begin allocated, which the clear and the draws share (§6.3)
            Encoder encoder{.command = slot.command};
            // a full frame clears the target, while a partial frame keeps the pixels outside its damage (§6.1)
            begin_rendering(encoder, m_target, !m_damage.has_value());
            // one open layer paints into its offscreen target and composites when its range ends (§6.2)
            struct OpenLayer
            {
                // the index of the offscreen target the group paints into, which the slot owns (§6.2)
                std::uint32_t target = 0;

                // the first draw after the group, which its composite waits for
                std::size_t draw_end = 0;

                // the opacity the group composites at
                float opacity = 1.0f;
            };
            std::vector<OpenLayer> open;
            // the capacity reserves every layer, so an open target keeps its address across the frame (§6.2)
            open.reserve(m_batch.layers.size());
            // the target the draw paints, which the innermost open layer or the frame is (§6.2)
            Image* target = &m_target;
            // the next layer marker to open, which the push order keeps sorted by its first draw (§6.2)
            std::size_t next_layer = 0;
            // every draw runs inside the layers whose offscreen passes are open at that point (§6.2)
            for (std::size_t draw_index = 0; draw_index <= m_batch.draws.size(); draw_index++) {
                // every layer that ends here composites into its parent before the next draw (§6.2)
                while (!open.empty() && open.back().draw_end == draw_index) {
                    // the target is read before the stack pops, and the parent is the enclosing layer or the frame (§6.2)
                    OpenLayer layer = open.back();
                    open.pop_back();
                    Image& parent = open.empty() ? m_target : slot.scratch[open.back().target].image;
                    // the offscreen target is sampled, which the parent composite reads (§6.2)
                    Image& offscreen = slot.scratch[layer.target].image;
                    sample_target(encoder, offscreen);
                    // the parent is bound before the composite draws into it, which the offscreen pass just left (§6.2)
                    begin_rendering(encoder, parent, false);
                    composite_layer(slot, encoder, layer.opacity, offscreen, parent);
                    target = &parent;
                }
                // the last index closes the layers that reached the end of the frame (§6.2)
                if (draw_index == m_batch.draws.size())
                    break;
                // every layer that begins here opens its offscreen pass before its first draw (§6.2)
                while (next_layer < m_batch.layers.size() && m_batch.layers[next_layer].draw_begin == draw_index) {
                    const BatchLayer& layer = m_batch.layers[next_layer++];
                    // an empty group has nothing to composite, so it opens no pass at all (§6.2)
                    if (layer.draw_end <= layer.draw_begin)
                        continue;
                    // the offscreen is the slot's own target, so a steady frame allocates nothing for it (§6.2, AD-06)
                    const std::uint32_t scratch = acquire_scratch(slot, m_target.width, m_target.height);
                    if (scratch == std::numeric_limits<std::uint32_t>::max())
                        continue;
                    open.push_back(OpenLayer{.target = scratch, .draw_end = layer.draw_end, .opacity = layer.opacity});
                    target = &slot.scratch[scratch].image;
                    begin_rendering(encoder, *target, true);
                }
                const BatchDraw& draw = m_batch.draws[draw_index];
                // a draw that casts a shadow composites the cast before it paints, which closes no pass on this api (§6.2)
                if (draw.shadow >= 0) {
                    const std::size_t shadow = static_cast<std::size_t>(draw.shadow);
                    if (shadow < m_batch.shadows.size())
                        composite_shadow(slot, encoder, m_batch.shadows[shadow], *target);
                    // the caller restores its target, which a failed cast must not leave on the silhouette (§6.2)
                    begin_rendering(encoder, *target, false);
                    continue;
                }
                // a draw with no geometry has nothing to encode, which a cast only frame leaves (§6.2)
                if (has_vertices && has_indices && draw.index_count > 0)
                    draw_batch_draw(slot, encoder, draw, *target);
            }
            // the frame closes on the frame target, which the present copies (§6.3)
            end_rendering(encoder);
        }
    } // namespace

    bool backend_device_available() {
        // readiness is the device and its pipelines, which the driver has not removed since (§6.2)
        return ensure_device_state() && !device_state().lost.load();
    }

    std::uint64_t vulkan_presented_frames() {
        // the count is the one the present path rose, which an offscreen frame never reaches (§6.3)
        return submitted_frames().load();
    }

    std::uint64_t vulkan_device_allocations() {
        // the count is what the two allocation paths raised, which a steady frame must not grow (§6.3, AD-06)
        return device_allocations().load();
    }

    std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics, std::shared_ptr<std::atomic<std::uint64_t>> batch_buffer_allocations) {
        // a host without the shared device state builds no surface, which readiness reports (§6.2)
        if (!ensure_device_state())
            return nullptr;
        // a surface that could not allocate its target or attach its window is refused rather than handed out (§6.2)
        std::unique_ptr<AlborRenderSurface> surface = std::make_unique<AlborRenderSurface>(handle, metrics, std::move(batch_buffer_allocations));
        if (!surface->valid())
            return nullptr;
        return surface;
    }
} // namespace albor::render
