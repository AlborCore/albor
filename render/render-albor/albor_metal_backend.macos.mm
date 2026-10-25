#include "render/render-albor/albor_backend_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "core/geometry.h"
#include "render/aged_cache.h"
#include "render/frame_profile.h"
#include "render/render-albor/albor_batch.h"
#include "render/render-albor/albor_geometry.h"
#include "render/render-albor/albor_glyph_cache.h"
#include "render/render-albor/albor_render_context.h"
#include "render/render-albor/albor_shaders.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::render
{
    namespace
    {
        // the pixel format every target uses, which reads back as canonical rgba8 bytes (§6.3, AD-26)
        constexpr MTLPixelFormat TARGET_FORMAT = MTLPixelFormatRGBA8Unorm;

        // one draw state, which both shader stages read as a packed buffer, aligned as msl pads it (§6.3)
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
            int kind;

            // whether the draw is cut by a scissor at all
            int scissor_enabled;

            // whether the draw samples a texture rather than a flat colour
            int has_texture;

            // the composition the draw blends with, which the shader reads its residual alpha from
            int blend;
        };

        // the msl struct pads to a sixteen byte boundary, so the host side must carry the same size
        static_assert(sizeof(DrawUniforms) == 112, "the draw uniforms must match the msl layout");

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

        // the shadow struct pads to a sixteen byte boundary, which the msl side mirrors
        static_assert(sizeof(ShadowUniforms) == 80, "the shadow uniforms must match the msl layout");

        // the two blend factors one composition mode applies, which the fixed function blender evaluates (§6.2)
        struct BlendFactors
        {
            // the factor the source colour is multiplied by
            MTLBlendFactor source = MTLBlendFactorOne;

            // the factor the destination colour is multiplied by
            MTLBlendFactor destination = MTLBlendFactorOneMinusSourceAlpha;
        };

        // map a composition mode onto the porter duff factors premultiplied colour leaves, which one blender covers (§6.2)
        BlendFactors blend_factors(CompositionMode mode) {
            switch (mode) {
            case CompositionMode::SourceIn:
                return {.source = MTLBlendFactorDestinationAlpha, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::SourceOut:
                return {.source = MTLBlendFactorOneMinusDestinationAlpha, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::Atop:
                return {.source = MTLBlendFactorDestinationAlpha, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::DestinationOver:
                return {.source = MTLBlendFactorOneMinusDestinationAlpha, .destination = MTLBlendFactorOne};
            case CompositionMode::DestinationIn:
                return {.source = MTLBlendFactorZero, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::DestinationOut:
                return {.source = MTLBlendFactorZero, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::DestinationAtop:
                return {.source = MTLBlendFactorOneMinusDestinationAlpha, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::Lighter:
                return {.source = MTLBlendFactorOne, .destination = MTLBlendFactorOne};
            case CompositionMode::Copy:
                return {.source = MTLBlendFactorOne, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::Xor:
                return {.source = MTLBlendFactorOneMinusDestinationAlpha, .destination = MTLBlendFactorOneMinusSourceAlpha};
            case CompositionMode::SourceOver:
                break;
            }
            return {.source = MTLBlendFactorOne, .destination = MTLBlendFactorOneMinusSourceAlpha};
        }

        // how many composition modes the pipeline table carries, which indexes the array the draw binds (§6.2)
        constexpr std::size_t COMPOSITION_MODES = 11;

        // the shader names modes by index, which must follow the hal order it shapes its alpha for (§6.2)
        static_assert(static_cast<int>(CompositionMode::SourceIn) == 1 && static_cast<int>(CompositionMode::SourceOut) == 2 && static_cast<int>(CompositionMode::DestinationIn) == 5 && static_cast<int>(CompositionMode::DestinationAtop) == 7 && static_cast<int>(CompositionMode::Copy) == 9, "the shader composition constants must follow the hal order");

        // the device wide state every surface draws through, which outlives them (§6.3)
        struct DeviceState
        {
            // the metal device the process draws with, which is nil when none came up
            id<MTLDevice> device = nil;

            // the queue every frame is submitted on
            id<MTLCommandQueue> queue = nil;

            // the shader library the two pipelines build from
            id<MTLLibrary> library = nil;

            // the one pipeline every ordinary draw binds, which the kind uniform selects the coverage of (§6.3)
            id<MTLRenderPipelineState> pipeline = nil;

            // the pipeline each composition mode binds, indexed by the mode the draw carries (§6.2)
            std::array<id<MTLRenderPipelineState>, COMPOSITION_MODES> pipelines{};

            // the pipelines that read the destination, which the modes a fixed blender cannot express bind (§6.2)
            std::array<id<MTLRenderPipelineState>, COMPOSITION_MODES> exact_pipelines{};

            // the pipeline that rasterizes a rounded silhouette, which a cast is blurred from (§6.2)
            id<MTLRenderPipelineState> shadow_shape = nil;

            // the pipeline that runs one separable gaussian pass over the silhouette (§6.2)
            id<MTLRenderPipelineState> shadow_blur = nil;

            // the one pixel white texture a solid draw samples, which keeps one shader path for every draw
            id<MTLTexture> white = nil;

            // the sampler every draw reads its texture through
            id<MTLSamplerState> sampler = nil;

            // whether the shared state finished building, which every later call reads (§6.3)
            bool built = false;

            // whether the build was already tried, so a failed host never retries it (§6.3)
            bool attempted = false;
        };

        // the process wide device state, which the first surface builds and every later one reuses (§6.3)
        DeviceState& device_state() {
            // the state is never destroyed, because the driver traps in the dealloc of a live queue (defect record)
            static DeviceState* state = new DeviceState();
            return *state;
        }

        // the lock that guards the first build of the shared state across threads (§10.5)
        std::mutex& device_mutex() {
            // one mutex guards the lazy build, which no later access needs again
            static std::mutex mutex;
            return mutex;
        }

        // build a pipeline over the shared vertex and fragment functions, which blend premultiplied source over
        id<MTLRenderPipelineState> build_pipeline(id<MTLDevice> device, id<MTLLibrary> library, MTLBlendFactor source, MTLBlendFactor destination) {
            // the descriptor names the vertex layout and the blend the pipeline applies (§6.3)
            MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            descriptor.vertexFunction = [library newFunctionWithName:@"albor_vertex"];
            descriptor.fragmentFunction = [library newFunctionWithName:@"albor_fragment"];
            // the colour attachment blends premultiplied source over the destination (§6.3)
            MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[0];
            attachment.pixelFormat = TARGET_FORMAT;
            attachment.blendingEnabled = YES;
            attachment.sourceRGBBlendFactor = source;
            attachment.destinationRGBBlendFactor = destination;
            attachment.rgbBlendOperation = MTLBlendOperationAdd;
            attachment.sourceAlphaBlendFactor = source;
            attachment.destinationAlphaBlendFactor = destination;
            attachment.alphaBlendOperation = MTLBlendOperationAdd;
            // a pipeline the driver refuses leaves the draw slot empty, which readiness reports (§6.2)
            NSError* error = nil;
            id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
            return state;
        }

        // build a pipeline that reads the destination through dual source blending, which a translucent mode needs (§6.2)
        id<MTLRenderPipelineState> build_exact_pipeline(id<MTLDevice> device, id<MTLLibrary> library, MTLBlendFactor source_rgb, MTLBlendFactor destination_rgb, MTLBlendFactor source_alpha, MTLBlendFactor destination_alpha) {
            // the descriptor names the shared vertex stage and the fragment that writes the blend control (§6.2)
            MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            descriptor.vertexFunction = [library newFunctionWithName:@"albor_vertex"];
            descriptor.fragmentFunction = [library newFunctionWithName:@"albor_fragment_exact"];
            // the destination factor reads the control attribute, which leaves coverage out of the source term (§6.2)
            MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[0];
            attachment.pixelFormat = TARGET_FORMAT;
            attachment.blendingEnabled = YES;
            attachment.sourceRGBBlendFactor = source_rgb;
            attachment.destinationRGBBlendFactor = destination_rgb;
            attachment.rgbBlendOperation = MTLBlendOperationAdd;
            attachment.sourceAlphaBlendFactor = source_alpha;
            attachment.destinationAlphaBlendFactor = destination_alpha;
            attachment.alphaBlendOperation = MTLBlendOperationAdd;
            // a pipeline the driver refuses leaves the draw slot empty, which readiness reports (§6.2)
            NSError* error = nil;
            return [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        }

        // build one offscreen pipeline, which the shadow passes share the vertex stage of
        id<MTLRenderPipelineState> build_shadow_pipeline(id<MTLDevice> device, id<MTLLibrary> library, NSString* vertex, NSString* fragment, MTLBlendFactor source, MTLBlendFactor destination) {
            // the descriptor names the two functions and the target format of the offscreen pass (§6.2)
            MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
            descriptor.vertexFunction = [library newFunctionWithName:vertex];
            descriptor.fragmentFunction = [library newFunctionWithName:fragment];
            // the offscreen target carries the same format as the frame, so one texture serves both (§6.3)
            MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[0];
            attachment.pixelFormat = TARGET_FORMAT;
            attachment.blendingEnabled = YES;
            attachment.sourceRGBBlendFactor = source;
            attachment.destinationRGBBlendFactor = destination;
            attachment.rgbBlendOperation = MTLBlendOperationAdd;
            attachment.sourceAlphaBlendFactor = source;
            attachment.destinationAlphaBlendFactor = destination;
            attachment.alphaBlendOperation = MTLBlendOperationAdd;
            // a pipeline the driver refuses leaves the shadow pass empty, which readiness reports (§6.2)
            NSError* error = nil;
            return [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        }

        // build the shared device state, which every surface draws through
        void build_device_state(DeviceState& state) {
            // the default device is the one the process presents with, which a headless host still has (AD-16)
            state.device = MTLCreateSystemDefaultDevice();
            // messaging a nil device answers nil, so every step below degrades instead of branching (§6.2)
            state.queue = [state.device newCommandQueue];
            // the library ships with the framework, so a run loads the bytes rather than invoking a compiler (AD-16)
            const std::span<const std::byte> data = metal_library_data();
            // a buffer with no bytes came from a build that failed to embed the library, which is refused below (§6.3)
            dispatch_data_t library_data = data.empty() ? nullptr : dispatch_data_create(data.data(), data.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError* library_error = nil;
            state.library = library_data != nullptr ? [state.device newLibraryWithData:library_data error:&library_error] : nil;
            // a library metal cannot use is reported, because every pipeline below could only assert inside metal (§6.2)
            const bool usable = state.library != nil && [state.library newFunctionWithName:@"albor_vertex"] != nil && [state.library newFunctionWithName:@"albor_fragment"] != nil && [state.library newFunctionWithName:@"shadow_vertex"] != nil && [state.library newFunctionWithName:@"shadow_shape"] != nil && [state.library newFunctionWithName:@"shadow_blur"] != nil;
            // the loader names its own failure when it has one, which is the most specific reason there is (§6.3)
            const char* const loaded = library_error != nil ? library_error.localizedDescription.UTF8String : nullptr;
            // an empty embed comes next, because a library it never carried cannot miss an entry point (§6.3)
            const char* const reason = loaded != nullptr ? loaded : (data.empty() ? "the embedded metal library is empty" : "a shader entry point is missing");
            // a library metal cannot use is reported, because no pipeline can be built from it (§6.2)
            if (!usable)
                std::fprintf(stderr, "albor-render-albor: the metal library could not be used: %s\n", reason);
            // no pipeline can be built from a library metal cannot use, because the descriptor would assert (§6.2)
            if (usable) {
                // every composition mode gets its pipeline, which the draw record selects at encode time (§6.2)
                for (std::size_t index = 0; index < COMPOSITION_MODES; index++) {
                    // the mode the index names decided the two factors the blender evaluates (§6.2)
                    const BlendFactors factors = blend_factors(static_cast<CompositionMode>(index));
                    state.pipelines[index] = build_pipeline(state.device, state.library, factors.source, factors.destination);
                }
                // premultiplied source over the destination is the pipeline a shadow composite resumes with (§6.3)
                state.pipeline = state.pipelines[0];
                // the three modes whose result depends on destination alpha use the exact ones (§6.2)
                state.exact_pipelines[static_cast<std::size_t>(CompositionMode::SourceIn)] = build_exact_pipeline(state.device, state.library, MTLBlendFactorDestinationAlpha, MTLBlendFactorOneMinusSource1Alpha, MTLBlendFactorDestinationAlpha, MTLBlendFactorOneMinusSource1Alpha);
                // a copy keeps the source alpha while the control leaves the uncovered destination to the blender (§6.2)
                state.exact_pipelines[static_cast<std::size_t>(CompositionMode::Copy)] = build_exact_pipeline(state.device, state.library, MTLBlendFactorOne, MTLBlendFactorOneMinusSource1Alpha, MTLBlendFactorOne, MTLBlendFactorOneMinusSource1Alpha);
                // destination atop dims the destination by its alpha and keeps the source alpha (§6.2)
                state.exact_pipelines[static_cast<std::size_t>(CompositionMode::DestinationAtop)] = build_exact_pipeline(state.device, state.library, MTLBlendFactorOneMinusDestinationAlpha, MTLBlendFactorOneMinusSource1Color, MTLBlendFactorOne, MTLBlendFactorOneMinusSource1Alpha);
                // the silhouette pass writes the cast alone, which no blend mixes with the clear (§6.2)
                state.shadow_shape = build_shadow_pipeline(state.device, state.library, @"shadow_vertex", @"shadow_shape", MTLBlendFactorOne, MTLBlendFactorZero);
                // the blur pass replaces its cleared target, which the one over zero blend and the ping pong give (§6.2)
                state.shadow_blur = build_shadow_pipeline(state.device, state.library, @"shadow_vertex", @"shadow_blur", MTLBlendFactorOne, MTLBlendFactorZero);
            }
            // a solid draw samples one white texel, which keeps a single shader path for every draw (§6.3)
            MTLTextureDescriptor* white_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:TARGET_FORMAT width:1 height:1 mipmapped:NO];
            white_descriptor.usage = MTLTextureUsageShaderRead;
            // the storage stays at the platform default, because a mac texture refuses shared storage
            state.white = [state.device newTextureWithDescriptor:white_descriptor];
            const std::uint8_t white[4] = {255, 255, 255, 255};
            [state.white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
            // the sampler samples linearly, which an image scaled to its destination needs (§6.4)
            MTLSamplerDescriptor* sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
            sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
            sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
            sampler_descriptor.mipFilter = MTLSamplerMipFilterLinear;
            sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
            state.sampler = [state.device newSamplerStateWithDescriptor:sampler_descriptor];
            // the state is complete only when every piece the draw path binds came up (§6.3)
            const bool composed = std::all_of(state.pipelines.begin(), state.pipelines.end(), [](id<MTLRenderPipelineState> pipeline) { return pipeline != nil; });
            // the exact pipelines are required, because the three modes would otherwise silently fall back (§6.2)
            const bool exact = state.exact_pipelines[static_cast<std::size_t>(CompositionMode::SourceIn)] != nil && state.exact_pipelines[static_cast<std::size_t>(CompositionMode::Copy)] != nil && state.exact_pipelines[static_cast<std::size_t>(CompositionMode::DestinationAtop)] != nil;
            state.built = state.device != nil && state.queue != nil && state.library != nil && composed && exact && state.shadow_shape != nil && state.shadow_blur != nil && state.white != nil && state.sampler != nil;
        }

        // the surface the backend presents into, which owns one colour target per frame (§6.2, §6.3)
        class AlborRenderSurface final : public RenderSurface
        {
        public:
            // build an offscreen surface, or a windowed one when the handle names a layer (§6.2)
            AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics);
            ~AlborRenderSurface() override;

            AlborRenderSurface(const AlborRenderSurface&) = delete;
            AlborRenderSurface& operator=(const AlborRenderSurface&) = delete;
            AlborRenderSurface(AlborRenderSurface&&) = delete;
            AlborRenderSurface& operator=(AlborRenderSurface&&) = delete;

            [[nodiscard]] bool valid() const override;
            [[nodiscard]] const WindowMetrics& metrics() const override;
            void resize(const WindowMetrics& metrics) override;
            RenderContext& begin_frame() override;
            void end_frame() override;
            [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override;

        private:
            // allocate the colour target the frame draws into, which a resize replaces (§6.2)
            void allocate();

            // copy every image the frame carries into the texture cache, which a later frame reuses (AD-06)
            void upload_textures(id<MTLCommandBuffer> command);

            // encode every recorded draw of the batch into the frame (§6.3)
            void encode(id<MTLCommandBuffer> command, MTLRenderPassDescriptor* pass);

            // blur a cast offscreen and composite it under the shape, which runs four passes (§6.2)
            void composite_shadow(id<MTLCommandBuffer> command, const BatchShadow& shadow, id<MTLTexture> target) const;

            // composite a layer onto the target beneath it at the opacity its group names (§6.2)
            void composite_layer(id<MTLCommandBuffer> command, id<MTLTexture> layer, id<MTLTexture> target, float opacity) const;

            // the layer the surface presents into, which is nil for an offscreen surface (§6.2)
            CAMetalLayer* __strong m_layer = nil;

            // the metrics the surface presents at (§6.1, D9.1)
            WindowMetrics m_metrics;

            // the colour target every frame draws into, which the read back and the present both read (§6.3)
            id<MTLTexture> m_target = nil;

            // the immutable batch the frame builds, which the encoder consumes once (§6.3)
            FrameBatch m_batch;

            // one retained pair of batch buffers, which a frame fills and the gpu reads (§6.3, AD-06)
            struct BatchSlot
            {
                // the vertices the frame copied, which a later frame in this slot reuses (AD-06)
                id<MTLBuffer> __strong vertices = nil;

                // the indices the frame copied, which a later frame in this slot reuses (AD-06)
                id<MTLBuffer> __strong indices = nil;

                // the command that last read this slot, which its next use waits on (§6.3)
                id<MTLCommandBuffer> __strong command = nil;
            };

            // how many slots the surface cycles, which covers the drawables the layer keeps in flight (§6.3)
            static constexpr std::size_t BATCH_SLOT_COUNT = 3;

            // the retained batch buffers, which a frame writes and the gpu reads three frames later (§6.3, AD-06)
            std::array<BatchSlot, BATCH_SLOT_COUNT> m_batch_slots;

            // the slot the next frame writes, which advances once per frame (§6.3)
            std::size_t m_batch_slot = 0;

            // how many frames an unreferenced texture survives, which bounds the memory a long run holds (AD-06)
            static constexpr std::uint64_t TEXTURE_IDLE_FRAMES = 120;

            // how many textures a surface aims to hold, which one frame's own uploads may briefly exceed (AD-06)
            static constexpr std::size_t MAXIMUM_CACHED_TEXTURES = 64;

            // the textures the uploaded images became, keyed by the identifier an image carries (AD-06)
            AgedCache<std::uint64_t, id<MTLTexture>> m_textures{TEXTURE_IDLE_FRAMES, MAXIMUM_CACHED_TEXTURES};

            // the glyph paths this surface already walked, which the context borrows so a glyph walks once (§6.4, AD-06)
            GlyphPathCache m_glyph_paths;

            // the expanded glyphs this surface keeps, which the context borrows so a glyph sweeps once (§6.4, AD-06)
            GlyphMeshCache m_glyph_meshes;

            // how many frames this surface has drawn, which the eviction ages the cache by (AD-06)
            std::uint64_t m_frame_counter = 0;

            // the context that records the frame into the batch, which is rebuilt with it (§6.2)
            std::unique_ptr<AlborRenderContext> m_context;

            // how many frames finished on the current target, which a resize resets (§4.3)
            std::size_t m_finished = 0;

            // the metrics a resize asked for while a frame was in flight, which the end applies (§6.2)
            WindowMetrics m_pending_metrics;

            // whether a resize is waiting for the frame in flight to finish (§6.2)
            bool m_resize_pending = false;

            // whether a frame began and has not ended yet (§6.1)
            bool m_begun = false;
        };

        AlborRenderSurface::AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics) :
            m_layer(handle.valid() ? (__bridge CAMetalLayer*)handle.object() : nil), m_metrics(metrics) {
            // a windowed surface presents through the layer, which is configured for the target format (§6.3)
            if (m_layer != nil) {
                m_layer.device = device_state().device;
                m_layer.pixelFormat = TARGET_FORMAT;
                m_layer.framebufferOnly = NO;
            }
            // the colour target is allocated at the physical extent the metrics name (§6.3, AD-27)
            allocate();
        }

        AlborRenderSurface::~AlborRenderSurface() = default;

        bool AlborRenderSurface::valid() const {
            // a surface draws only once its target allocated and the shared state built (§6.2)
            return m_target != nil && device_state().built;
        }

        const WindowMetrics& AlborRenderSurface::metrics() const {
            // the metrics are what the executor compares the scene against (§6.1, D9.1)
            return m_metrics;
        }

        void AlborRenderSurface::allocate() {
            // a target is allocated at the physical pixel extent, which the device scale already applied (AD-27)
            const NSUInteger width = std::max<NSUInteger>(1u, static_cast<NSUInteger>(m_metrics.pixel_size.width));
            const NSUInteger height = std::max<NSUInteger>(1u, static_cast<NSUInteger>(m_metrics.pixel_size.height));
            // the descriptor names a render target the read back can copy from (§6.3)
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:TARGET_FORMAT width:width height:height mipmapped:NO];
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModePrivate;
            m_target = [device_state().device newTextureWithDescriptor:descriptor];
            // a windowed surface resizes its layer alongside the target, which keeps the drawable in step (§6.2)
            if (m_layer != nil) {
                // the layer carries the density it presents at, which the drawable size and the scale agree on (AD-27)
                m_layer.contentsScale = static_cast<CGFloat>(m_metrics.scale_factor);
                m_layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
            }
            // the new target carries no finished frame yet, so a read back waits for one (§6.3)
            m_finished = 0;
        }

        void AlborRenderSurface::resize(const WindowMetrics& metrics) {
            // metrics with no area cannot be presented, so the previous target is kept (§6.2)
            if (!metrics.valid())
                return;
            // a resize while a frame is in flight waits for the frame, so no frame mixes two extents (§6.2, D9.1)
            if (m_begun) {
                m_pending_metrics = metrics;
                m_resize_pending = true;
                return;
            }
            // the extent is replaced on the thread that owns the surface (§5.3, D9.1)
            m_metrics = metrics;
            allocate();
        }

        RenderContext& AlborRenderSurface::begin_frame() {
            // a frame starts from an empty batch, which the context records the scene into (§6.3)
            m_batch.vertices.clear();
            m_batch.indices.clear();
            m_batch.draws.clear();
            m_batch.layers.clear();
            m_batch.uploads.clear();
            m_batch.shadows.clear();
            // the context records logical points onto the physical target, so the device scale is handed to it (AD-27)
            m_context = std::make_unique<AlborRenderContext>(m_batch, Rect::from_origin_size(Point{0.0f, 0.0f}, m_metrics.logical_size), m_metrics.scale_factor, &m_glyph_paths, &m_glyph_meshes);
            // the frame counter advances once per frame, which the textures record and the glyph paths age against (AD-06)
            m_frame_counter++;
            // the textures age against the same counter, which drops the images no draw reached (AD-06)
            m_textures.begin_frame(m_frame_counter);
            // the glyph paths age against the same counter, which drops the ones no draw reached (§6.4, AD-06)
            m_glyph_paths.begin_frame(m_frame_counter);
            // the glyph meshes age against the same counter too, which drops the ones no draw reached (§6.4, AD-06)
            m_glyph_meshes.begin_frame(m_frame_counter);
            // the frame is now in flight, which the matching end presents (§6.1)
            m_begun = true;
            return *m_context;
        }

        void blur_pass(id<MTLCommandBuffer> command, id<MTLTexture> source, id<MTLTexture> target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height);

        void AlborRenderSurface::composite_shadow(id<MTLCommandBuffer> command, const BatchShadow& shadow, id<MTLTexture> target) const {
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
            // two offscreen targets ping pong the silhouette through the two blur directions (§6.2)
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:TARGET_FORMAT width:static_cast<NSUInteger>(width) height:static_cast<NSUInteger>(height) mipmapped:NO];
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModePrivate;
            id<MTLTexture> silhouette = [device_state().device newTextureWithDescriptor:descriptor];
            id<MTLTexture> scratch = [device_state().device newTextureWithDescriptor:descriptor];
            // a refused offscreen target leaves the cast unpainted, because the blur has no surface to land on (§6.2)
            if (silhouette == nil || scratch == nil)
                return;
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
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = silhouette;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
            id<MTLRenderCommandEncoder> shape = [command renderCommandEncoderWithDescriptor:pass];
            [shape setRenderPipelineState:device_state().shadow_shape];
            [shape setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [shape drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [shape endEncoding];
            // a sigma of zero needs no blur, which a hard cast carries (§6.2)
            const float sigma = shadow.sigma;
            if (sigma > 0.0f) {
                // the kernel carries a spread of 1.69 taps, which the sigma turns into a texel step (§6.2)
                const float step = sigma / 2.5f;
                // the horizontal pass spreads the silhouette across the scratch target (§6.2)
                blur_pass(command, silhouette, scratch, uniforms, 1.0f, 0.0f, step, width, height);
                // the vertical pass spreads it back, which finishes the separable gaussian (§6.2)
                blur_pass(command, scratch, silhouette, uniforms, 0.0f, 1.0f, step, width, height);
            }
            // the cast composites over what is under it, which the fill pipeline blends source over (§6.2)
            BatchVertex quad[4];
            const float corners[4][2] = {{left, top}, {left + width, top}, {left, top + height}, {left + width, top + height}};
            for (int index = 0; index < 4; index++) {
                // the corner lands where the offscreen target sits on the frame (§6.2)
                quad[index].x = corners[index][0];
                quad[index].y = corners[index][1];
                quad[index].u = (index == 1 || index == 3) ? 1.0f : 0.0f;
                quad[index].v = (index >= 2) ? 1.0f : 0.0f;
            }
            // the quad is drawn as one strip over the four corners the target spans (§6.2)
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
            MTLRenderPassDescriptor* composite = [MTLRenderPassDescriptor renderPassDescriptor];
            composite.colorAttachments[0].texture = target;
            composite.colorAttachments[0].loadAction = MTLLoadActionLoad;
            composite.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> cast = [command renderCommandEncoderWithDescriptor:composite];
            [cast setRenderPipelineState:device_state().pipeline];
            [cast setFragmentSamplerState:device_state().sampler atIndex:0];
            [cast setVertexBytes:quad length:sizeof(quad) atIndex:0];
            [cast setVertexBytes:&draw length:sizeof(draw) atIndex:1];
            [cast setFragmentBytes:&draw length:sizeof(draw) atIndex:1];
            [cast setFragmentTexture:silhouette atIndex:0];
            [cast drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [cast endEncoding];
        }

        void AlborRenderSurface::composite_layer(id<MTLCommandBuffer> command, id<MTLTexture> layer, id<MTLTexture> target, float opacity) const {
            // the quad spans the whole target, because a layer covers the frame it was recorded for (§6.2)
            const float width = static_cast<float>(target.width);
            const float height = static_cast<float>(target.height);
            BatchVertex quad[4];
            const float corners[4][2] = {{0.0f, 0.0f}, {width, 0.0f}, {0.0f, height}, {width, height}};
            for (int index = 0; index < 4; index++) {
                // the corner lands where the target it covers begins (§6.2)
                quad[index].x = corners[index][0];
                quad[index].y = corners[index][1];
                quad[index].u = (index == 1 || index == 3) ? 1.0f : 0.0f;
                quad[index].v = (index >= 2) ? 1.0f : 0.0f;
            }
            // the group dims once, which a premultiplied white at the opacity multiplies into every texel (§6.2)
            DrawUniforms draw{};
            draw.color[0] = opacity;
            draw.color[1] = opacity;
            draw.color[2] = opacity;
            draw.color[3] = opacity;
            draw.target_size[0] = width;
            draw.target_size[1] = height;
            draw.ramp = 1.0f;
            draw.kind = 0;
            draw.has_texture = 1;
            // the layer composites source over the parent, which is the one blend a group lands with (§6.2)
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target;
            pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:device_state().pipeline];
            [encoder setFragmentSamplerState:device_state().sampler atIndex:0];
            [encoder setVertexBytes:quad length:sizeof(quad) atIndex:0];
            [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:1];
            [encoder setFragmentBytes:&draw length:sizeof(draw) atIndex:1];
            [encoder setFragmentTexture:layer atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [encoder endEncoding];
        }

        void blur_pass(id<MTLCommandBuffer> command, id<MTLTexture> source, id<MTLTexture> target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height) {
            // the pass reads the source and writes the target, which the two directions swap (§6.2)
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
            // the direction and the step ride in the params lane, which the shader reads (§6.2)
            uniforms.params[1] = dx;
            uniforms.params[2] = dy;
            uniforms.params[3] = step / (dx != 0.0f ? width : height);
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:device_state().shadow_blur];
            [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder setFragmentSamplerState:device_state().sampler atIndex:0];
            [encoder setFragmentTexture:source atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [encoder endEncoding];
        }

        void AlborRenderSurface::upload_textures(id<MTLCommandBuffer> command) {
            // a texture a draw references stays resident, which the eviction reads as its last use (AD-06)
            for (const BatchDraw& draw : m_batch.draws) {
                // a draw that samples no texture reaches nothing, which is the case of a solid fill (AD-06)
                if (draw.texture == 0)
                    continue;
                // the draw's texture is reached so this frame becomes its last use (AD-06)
                m_textures.touch(draw.texture);
            }
            // every image the frame carries becomes a texture the cache holds by its identifier (AD-06)
            for (const BatchUpload& upload : m_batch.uploads) {
                // a texture the cache already holds is reused, which makes a retained image constant time (AD-06)
                if (m_textures.peek(upload.id) != nullptr)
                    continue;
                // the pixels are premultiplied rgba8, which the target format already matches (§6.3, AD-26)
                MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:TARGET_FORMAT width:upload.image->width() height:upload.image->height() mipmapped:YES];
                descriptor.usage = MTLTextureUsageShaderRead;
                id<MTLTexture> texture = [device_state().device newTextureWithDescriptor:descriptor];
                // the upload copies the row packed bytes the resource carries (§6.4)
                [texture replaceRegion:MTLRegionMake2D(0, 0, upload.image->width(), upload.image->height()) mipmapLevel:0 withBytes:upload.image->pixels().data() bytesPerRow:upload.image->row_bytes()];
                // a mip chain keeps a minified image from aliasing, which the reference also avoids (§6.4, AD-26)
                if (texture.mipmapLevelCount > 1) {
                    // the chain is generated on the frame command buffer, which the frame samples after the blit (§6.4)
                    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                    [blit generateMipmapsForTexture:texture];
                    [blit endEncoding];
                }
                // the texture joins the cache under the identifier its image carries, aged to this frame (AD-06)
                m_textures.store(upload.id, texture);
            }
        }

        void AlborRenderSurface::encode(id<MTLCommandBuffer> command, MTLRenderPassDescriptor* pass) {
            // the frame opens one encoder, which a layer or shadow pass has to close and reopen (§6.2)
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:device_state().pipeline];
            [encoder setFragmentSamplerState:device_state().sampler atIndex:0];
            // a frame with no geometry still composites its shadows, which the draw loop reaches without buffers (§6.2)
            id<MTLBuffer> vertices = nil;
            id<MTLBuffer> indices = nil;
            if (!m_batch.vertices.empty() && !m_batch.indices.empty()) {
                // the slot this frame fills was read three frames ago, so the gpu is done with it before the copy (§6.3)
                BatchSlot& slot = m_batch_slots[m_batch_slot];
                if (slot.command != nil)
                    [slot.command waitUntilCompleted];
                // a slot grows to the batch size once, so a later frame copies into it instead of allocating (AD-06)
                const NSUInteger vertex_bytes = static_cast<NSUInteger>(m_batch.vertices.size() * sizeof(BatchVertex));
                const NSUInteger index_bytes = static_cast<NSUInteger>(m_batch.indices.size() * sizeof(std::uint32_t));
                if (slot.vertices == nil || slot.vertices.length < vertex_bytes)
                    slot.vertices = [device_state().device newBufferWithLength:vertex_bytes options:MTLResourceStorageModeShared];
                if (slot.indices == nil || slot.indices.length < index_bytes)
                    slot.indices = [device_state().device newBufferWithLength:index_bytes options:MTLResourceStorageModeShared];
                // a slot the device refused carries no geometry, which the frame draws without rather than nil (§6.3)
                if (slot.vertices != nil && slot.indices != nil) {
                    std::memcpy(slot.vertices.contents, m_batch.vertices.data(), vertex_bytes);
                    std::memcpy(slot.indices.contents, m_batch.indices.data(), index_bytes);
                    // the command keeps the slot it reads, so the next frame in this slot can wait on it (§6.3)
                    slot.command = command;
                    vertices = slot.vertices;
                    indices = slot.indices;
                    [encoder setVertexBuffer:vertices offset:0 atIndex:0];
                }
            }
            // the slot advances every frame, so the next present writes the one the gpu finished last (§6.3)
            m_batch_slot = (m_batch_slot + 1) % BATCH_SLOT_COUNT;
            // the target the open encoder writes into, which an open layer replaces (§6.2)
            id<MTLTexture> target = m_target;
            // one open layer records the target it paints and the draw it ends at (§6.2)
            struct OpenLayer
            {
                // the offscreen texture the group paints into
                id<MTLTexture> texture;

                // the first draw after the group, which its composite waits for
                std::size_t draw_end = 0;

                // the opacity the group composites at
                float opacity = 1.0f;
            };
            std::vector<OpenLayer> open;
            // the next layer marker to open, which the push order keeps sorted by its first draw (§6.2)
            std::size_t next_layer = 0;
            // reopen a target after a composite, which loads what the target already carries (§6.2)
            auto reopen = [&](id<MTLTexture> into, bool clear) -> id<MTLRenderCommandEncoder> {
                MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
                descriptor.colorAttachments[0].texture = into;
                descriptor.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
                descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
                descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
                id<MTLRenderCommandEncoder> fresh = [command renderCommandEncoderWithDescriptor:descriptor];
                [fresh setRenderPipelineState:device_state().pipeline];
                [fresh setFragmentSamplerState:device_state().sampler atIndex:0];
                // a frame with no geometry binds no vertex buffer, because no draw reads one (§6.2)
                if (vertices != nil)
                    [fresh setVertexBuffer:vertices offset:0 atIndex:0];
                return fresh;
            };
            // every draw runs inside the layers whose offscreen passes are open at that point (§6.2)
            for (std::size_t draw_index = 0; draw_index <= m_batch.draws.size(); draw_index++) {
                // every layer that ends here composites into its parent before the next draw (§6.2)
                while (!open.empty() && open.back().draw_end == draw_index) {
                    [encoder endEncoding];
                    // the texture and opacity are read before the stack pops (§6.2)
                    const id<MTLTexture> texture = open.back().texture;
                    const float opacity = open.back().opacity;
                    open.pop_back();
                    // the parent is the enclosing layer, or the frame target when none remains (§6.2)
                    target = open.empty() ? m_target : open.back().texture;
                    composite_layer(command, texture, target, opacity);
                    encoder = reopen(target, false);
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
                    [encoder endEncoding];
                    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:TARGET_FORMAT width:m_target.width height:m_target.height mipmapped:NO];
                    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                    descriptor.storageMode = MTLStorageModePrivate;
                    id<MTLTexture> texture = [device_state().device newTextureWithDescriptor:descriptor];
                    // a refused target draws the group into its parent instead, because no offscreen exists (§6.2)
                    if (texture != nil) {
                        encoder = reopen(texture, true);
                        target = texture;
                        open.push_back(OpenLayer{.texture = texture, .draw_end = layer.draw_end, .opacity = layer.opacity});
                    }
                }
                const BatchDraw& draw = m_batch.draws[draw_index];
                // a draw that casts a shadow closes the pass, blurs the cast and reopens it (§6.2)
                if (draw.shadow >= 0) {
                    [encoder endEncoding];
                    composite_shadow(command, m_batch.shadows[static_cast<std::size_t>(draw.shadow)], target);
                    // the pass resumes over what it already painted, which the load keeps (§6.2)
                    encoder = reopen(target, false);
                    continue;
                }
                // a draw with no geometry has nothing to encode, which a cast only frame leaves (§6.2)
                if (vertices != nil && indices != nil && draw.index_count > 0) {
                    // the draw binds its composition pipeline, and the three exact modes use theirs (§6.2)
                    const std::size_t composition = static_cast<std::size_t>(draw.composition);
                    const id<MTLRenderPipelineState> exact = device_state().exact_pipelines[composition];
                    [encoder setRenderPipelineState:exact != nil ? exact : device_state().pipelines[composition]];
                    DrawUniforms uniforms{};
                    // the packed colour unpacks into premultiplied channels, which the shader multiplies through
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
                    uniforms.target_size[0] = static_cast<float>(m_target.width);
                    uniforms.target_size[1] = static_cast<float>(m_target.height);
                    uniforms.ramp = draw.ramp;
                    uniforms.kind = draw.coverage == CoverageKind::Folded ? 1 : 0;
                    uniforms.scissor_enabled = draw.scissor.enabled ? 1 : 0;
                    // a draw with a texture samples it, and a solid one samples the white texel (§6.3, §6.4)
                    uniforms.has_texture = draw.texture == 0 ? 0 : 1;
                    // the shader reshapes its alpha for the modes that replace the destination (§6.2)
                    uniforms.blend = static_cast<int>(draw.composition);
                    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
                    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
                    // the draw binds its own texture, or the white texel a solid draw samples (§6.3, §6.4)
                    id<MTLTexture> texture = device_state().white;
                    if (draw.texture != 0) {
                        // the upload the frame carried became the texture the cache holds (AD-06)
                        const auto found = m_textures.peek(draw.texture);
                        if (found != nullptr)
                            texture = *found;
                    }
                    [encoder setFragmentTexture:texture atIndex:0];
                    // the draw consumes its own index range, based at its own first vertex (§6.3)
                    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:draw.index_count indexType:MTLIndexTypeUInt32 indexBuffer:indices indexBufferOffset:draw.index_begin * sizeof(std::uint32_t) instanceCount:1 baseVertex:static_cast<NSInteger>(draw.base_vertex) baseInstance:0];
                }
            }
            // the frame closes its encoder once every draw is recorded (§6.3)
            [encoder endEncoding];
        }

        void AlborRenderSurface::end_frame() {
            // an end without a begun frame is a no operation rather than an error (§6.1)
            if (!m_begun)
                return;
            // the present is what the frame spends after the scene is recorded, which the profile reports (§6.3)
            const FrameProfileScope present_scope;
            // the batch is complete once the scene is recorded, so its size is what the profile reports (§6.3)
            profile_batch(m_batch.vertices.size(), m_batch.indices.size() / 3u, m_batch.draws.size(), m_batch.shadows.size(), m_glyph_meshes.count());
            // the frame runs inside a pool, because a render thread has none of its own (§6.3, AD-18)
            @autoreleasepool {
                // the frame is one command buffer, which the queue submits once (§6.3)
                id<MTLCommandBuffer> command = [device_state().queue commandBuffer];
                // a windowed surface presents the drawable the layer hands out for this frame (§6.3)
                const FrameProfileScope wait_scope;
                id<CAMetalDrawable> drawable = m_layer != nil ? [m_layer nextDrawable] : nil;
                // the wait is the compositor pacing, which a slow frame shows before the encode (§6.3)
                profile_drawable_wait(wait_scope.elapsed_ms());
                // the pass clears the target first, which an empty frame leaves transparent (§6.1)
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = m_target;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
                // the images the frame carries become textures once, which a later frame reuses (AD-06)
                const FrameProfileScope upload_scope;
                upload_textures(command);
                profile_upload(upload_scope.elapsed_ms());
                // the encode replays every recorded draw, closing the pass around each shadow (§6.3)
                const FrameProfileScope encode_scope;
                encode(command, pass);
                profile_encode(encode_scope.elapsed_ms());
                // the blit and the commit finish the present, which the profile splits from the encode (§6.3)
                const FrameProfileScope commit_scope;
                // a windowed frame blits the target onto the drawable, which keeps one pixel path for both kinds (§6.3)
                if (drawable != nil) {
                    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                    [blit copyFromTexture:m_target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m_target.width, m_target.height, 1) toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                    [blit endEncoding];
                    [command presentDrawable:drawable];
                }
                // the buffer is committed, which is what makes the frame exactly once (§6.1)
                [command commit];
                profile_commit(commit_scope.elapsed_ms());
                // the frame counts once it is submitted, which a read back then waits on (§4.3)
                m_finished++;
            }
            // the recorded present includes the wait, the encode and the blit, which the profile reports (§6.3)
            profile_present(present_scope.elapsed_ms());
            // the frame is no longer in flight, so a read back accepts one (§6.1)
            m_begun = false;
            // a resize that waited for this frame applies now, so the next frame uses one extent (§6.2)
            if (m_resize_pending) {
                // the resize runs inside a pool too, because it allocates descriptors on a thread that owns none (§6.3)
                @autoreleasepool {
                    m_resize_pending = false;
                    resize(m_pending_metrics);
                }
            }
        }

        std::vector<std::uint8_t> AlborRenderSurface::read_pixels() const {
            // a read before the first finished frame reports nothing (§6.3)
            if (m_target == nil || m_finished == 0 || m_begun)
                return {};
            // the copy runs inside a pool, because a render thread has none of its own (§6.3, AD-18)
            @autoreleasepool {
                // the target is copied into a shared buffer, which the host can read once the copy completes (§6.3)
                const std::size_t width = static_cast<std::size_t>(m_target.width);
                const std::size_t height = static_cast<std::size_t>(m_target.height);
                const std::size_t row_bytes = width * 4;
                id<MTLBuffer> readback = [device_state().device newBufferWithLength:row_bytes * height options:MTLResourceStorageModeShared];
                // the copy needs a command buffer of its own, because the frame already completed (§6.3)
                id<MTLCommandBuffer> command = [device_state().queue commandBuffer];
                id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                [blit copyFromTexture:m_target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1) toBuffer:readback destinationOffset:0 destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * height];
                [blit endEncoding];
                [command commit];
                [command waitUntilCompleted];
                // the bytes are already canonical premultiplied rgba8, which the target format guarantees (§6.3, AD-26)
                std::vector<std::uint8_t> pixels(row_bytes * height);
                std::memcpy(pixels.data(), readback.contents, pixels.size());
                return pixels;
            }
        }

        // the one device the process draws through, which the lazy build fills on first use
        bool ensure_device_state() {
            // the lock serializes the first build, because two surfaces may race for it (§10.5)
            const std::lock_guard<std::mutex> guard(device_mutex());
            // a state that already ran is handed out as it is, which a failed host never retries (§6.3)
            if (device_state().attempted)
                return device_state().built;
            // the build runs once, and its result is what readiness reports (§6.2)
            device_state().attempted = true;
            build_device_state(device_state());
            return device_state().built;
        }
    } // namespace

    bool backend_device_available() {
        // readiness is whether the device, the library and the pipelines all came up (§6.2)
        return ensure_device_state();
    }

    std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a surface without the shared state cannot draw, so none is built, and a null handle stays offscreen (§6.2)
        return ensure_device_state() ? std::make_unique<AlborRenderSurface>(handle, metrics) : nullptr;
    }
} // namespace albor::render
