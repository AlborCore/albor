#include "render/render-albor/albor_backend_support.h"

#include <algorithm>
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

        // the device wide state every surface draws through, which outlives them (§6.3)
        struct DeviceState
        {
            // the metal device the process draws with, which is nil when none came up
            id<MTLDevice> device = nil;

            // the queue every frame is submitted on
            id<MTLCommandQueue> queue = nil;

            // the shader library the two pipelines build from
            id<MTLLibrary> library = nil;

            // the one pipeline every draw binds, which the kind uniform selects the coverage of (§6.3)
            id<MTLRenderPipelineState> pipeline = nil;

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
                // premultiplied source over the destination is the one blend the hal composites with (§6.3)
                state.pipeline = build_pipeline(state.device, state.library, MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha);
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
            state.built = state.device != nil && state.queue != nil && state.library != nil && state.pipeline != nil && state.shadow_shape != nil && state.shadow_blur != nil && state.white != nil && state.sampler != nil;
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
            void upload_textures();

            // encode every recorded draw of the batch into the frame (§6.3)
            void encode(id<MTLCommandBuffer> command, MTLRenderPassDescriptor* pass) const;

            // blur a cast offscreen and composite it under the shape, which runs four passes (§6.2)
            void composite_shadow(id<MTLCommandBuffer> command, const BatchShadow& shadow) const;

            // the layer the surface presents into, which is nil for an offscreen surface (§6.2)
            CAMetalLayer* __strong m_layer = nil;

            // the metrics the surface presents at (§6.1, D9.1)
            WindowMetrics m_metrics;

            // the colour target every frame draws into, which the read back and the present both read (§6.3)
            id<MTLTexture> m_target = nil;

            // the immutable batch the frame builds, which the encoder consumes once (§6.3)
            FrameBatch m_batch;

            // how many frames an unreferenced texture survives, which bounds the memory a long run holds (AD-06)
            static constexpr std::uint64_t TEXTURE_IDLE_FRAMES = 120;

            // how many textures a surface aims to hold, which one frame's own uploads may briefly exceed (AD-06)
            static constexpr std::size_t MAXIMUM_CACHED_TEXTURES = 64;

            // the textures the uploaded images became, keyed by the identifier an image carries (AD-06)
            AgedCache<std::uint64_t, id<MTLTexture>> m_textures{TEXTURE_IDLE_FRAMES, MAXIMUM_CACHED_TEXTURES};

            // the glyph paths this surface already walked, which the context borrows so a glyph walks once (§6.4, AD-06)
            GlyphPathCache m_glyph_paths;

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
            m_batch.uploads.clear();
            m_batch.shadows.clear();
            // the context records logical points onto the physical target, so the device scale is handed to it (AD-27)
            m_context = std::make_unique<AlborRenderContext>(m_batch, Rect::from_origin_size(Point{0.0f, 0.0f}, m_metrics.logical_size), m_metrics.scale_factor, &m_glyph_paths);
            // the frame counter advances once per frame, which the textures record and the glyph paths age against (AD-06)
            m_frame_counter++;
            // the textures age against the same counter, which drops the images no draw reached (AD-06)
            m_textures.begin_frame(m_frame_counter);
            // the glyph paths age against the same counter, which drops the ones no draw reached (§6.4, AD-06)
            m_glyph_paths.begin_frame(m_frame_counter);
            // the frame is now in flight, which the matching end presents (§6.1)
            m_begun = true;
            return *m_context;
        }

        void blur_pass(id<MTLCommandBuffer> command, id<MTLTexture> source, id<MTLTexture> target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height);

        void AlborRenderSurface::composite_shadow(id<MTLCommandBuffer> command, const BatchShadow& shadow) const {
            // the offscreen target covers the caster plus the reach, and the edge ramp spans half a device pixel (§6.2)
            const float reach = std::max(shadow.reach, 1.0f);
            const float left = shadow.rect.x - reach;
            const float top = shadow.rect.y - reach;
            const float width = shadow.rect.width + reach * 2.0f;
            const float height = shadow.rect.height + reach * 2.0f;
            // two offscreen targets ping pong the silhouette through the two blur directions (§6.2)
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:TARGET_FORMAT width:static_cast<NSUInteger>(width) height:static_cast<NSUInteger>(height) mipmapped:NO];
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModePrivate;
            id<MTLTexture> silhouette = [device_state().device newTextureWithDescriptor:descriptor];
            id<MTLTexture> scratch = [device_state().device newTextureWithDescriptor:descriptor];
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
            draw.target_size[0] = static_cast<float>(m_target.width);
            draw.target_size[1] = static_cast<float>(m_target.height);
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
            // the cast composites into the frame target, which its own pass binds and loads (§6.2)
            MTLRenderPassDescriptor* composite = [MTLRenderPassDescriptor renderPassDescriptor];
            composite.colorAttachments[0].texture = m_target;
            composite.colorAttachments[0].loadAction = MTLLoadActionLoad;
            composite.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> cast = [command renderCommandEncoderWithDescriptor:composite];
            [cast setRenderPipelineState:device_state().pipeline];
            [cast setFragmentSamplerState:device_state().sampler atIndex:0];
            [cast setVertexBuffer:[device_state().device newBufferWithBytes:quad length:sizeof(quad) options:MTLResourceStorageModeShared] offset:0 atIndex:0];
            [cast setVertexBytes:&draw length:sizeof(draw) atIndex:1];
            [cast setFragmentBytes:&draw length:sizeof(draw) atIndex:1];
            [cast setFragmentTexture:silhouette atIndex:0];
            [cast drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [cast endEncoding];
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

        void AlborRenderSurface::upload_textures() {
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
                    // the chain is generated on the queue, which the same frame then samples (§6.4)
                    id<MTLCommandBuffer> mips = [device_state().queue commandBuffer];
                    id<MTLBlitCommandEncoder> blit = [mips blitCommandEncoder];
                    [blit generateMipmapsForTexture:texture];
                    [blit endEncoding];
                    [mips commit];
                    [mips waitUntilCompleted];
                }
                // the texture joins the cache under the identifier its image carries, aged to this frame (AD-06)
                m_textures.store(upload.id, texture);
            }
        }

        void AlborRenderSurface::encode(id<MTLCommandBuffer> command, MTLRenderPassDescriptor* pass) const {
            // the frame opens one encoder, which a shadow pass has to close and reopen (§6.2)
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:device_state().pipeline];
            [encoder setFragmentSamplerState:device_state().sampler atIndex:0];
            // a frame with no geometry still composites its shadows, which a cast only layer carries (§6.2)
            if (m_batch.vertices.empty() || m_batch.indices.empty()) {
                [encoder endEncoding];
                // the casts composite on their own, which no vertex range would have reached (§6.2)
                for (const BatchDraw& draw : m_batch.draws) {
                    // a draw that carries a cast composites it, which the frame has no geometry for (§6.2)
                    if (draw.shadow >= 0)
                        composite_shadow(command, m_batch.shadows[static_cast<std::size_t>(draw.shadow)]);
                }
                return;
            }
            id<MTLBuffer> vertices = [device_state().device newBufferWithBytes:m_batch.vertices.data() length:m_batch.vertices.size() * sizeof(BatchVertex) options:MTLResourceStorageModeShared];
            id<MTLBuffer> indices = [device_state().device newBufferWithBytes:m_batch.indices.data() length:m_batch.indices.size() * sizeof(std::uint32_t) options:MTLResourceStorageModeShared];
            [encoder setVertexBuffer:vertices offset:0 atIndex:0];
            // every recorded draw binds its own uniforms and its own texture, then draws its index range (§6.3)
            for (const BatchDraw& draw : m_batch.draws) {
                // a draw that casts a shadow closes the frame pass, blurs the cast and reopens it (§6.2)
                if (draw.shadow >= 0) {
                    [encoder endEncoding];
                    composite_shadow(command, m_batch.shadows[static_cast<std::size_t>(draw.shadow)]);
                    // the frame pass resumes over what it already painted, which the load keeps (§6.2)
                    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
                    encoder = [command renderCommandEncoderWithDescriptor:pass];
                    [encoder setRenderPipelineState:device_state().pipeline];
                    [encoder setFragmentSamplerState:device_state().sampler atIndex:0];
                    [encoder setVertexBuffer:vertices offset:0 atIndex:0];
                    continue;
                }
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
                // the draw consumes its own index range, which the builder recorded (§6.3)
                [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:draw.index_count indexType:MTLIndexTypeUInt32 indexBuffer:indices indexBufferOffset:draw.index_begin * sizeof(std::uint32_t)];
            }
            // the frame closes its encoder once every draw is recorded (§6.3)
            [encoder endEncoding];
        }

        void AlborRenderSurface::end_frame() {
            // an end without a begun frame is a no operation rather than an error (§6.1)
            if (!m_begun)
                return;
            // the frame runs inside a pool, because a render thread has none of its own (§6.3, AD-18)
            @autoreleasepool {
                // the frame is one command buffer, which the queue submits once (§6.3)
                id<MTLCommandBuffer> command = [device_state().queue commandBuffer];
                // a windowed surface presents the drawable the layer hands out for this frame (§6.3)
                id<CAMetalDrawable> drawable = m_layer != nil ? [m_layer nextDrawable] : nil;
                // the pass clears the target first, which an empty frame leaves transparent (§6.1)
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = m_target;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
                // the images the frame carries become textures once, which a later frame reuses (AD-06)
                upload_textures();
                // the encode replays every recorded draw, closing the pass around each shadow (§6.3)
                encode(command, pass);
                // a windowed frame blits the target onto the drawable, which keeps one pixel path for both kinds (§6.3)
                if (drawable != nil) {
                    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                    [blit copyFromTexture:m_target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m_target.width, m_target.height, 1) toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                    [blit endEncoding];
                    [command presentDrawable:drawable];
                }
                // the buffer is committed, which is what makes the frame exactly once (§6.1)
                [command commit];
                // the frame counts once it is submitted, which a read back then waits on (§4.3)
                m_finished++;
            }
            // the frame is no longer in flight, so a read back accepts one (§6.1)
            m_begun = false;
            // a resize that waited for this frame applies now, so the next frame uses one extent (§6.2)
            if (m_resize_pending) {
                m_resize_pending = false;
                resize(m_pending_metrics);
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
