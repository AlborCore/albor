#include "render/render-albor/albor_backend_support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "core/geometry.h"
#include "render/render-albor/albor_batch.h"
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
        static_assert(sizeof(DrawUniforms) == 96, "the draw uniforms must match the msl layout");

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

            // the one pixel white texture a solid draw samples, which keeps one shader path for every draw
            id<MTLTexture> white = nil;

            // the sampler every draw reads its texture through
            id<MTLSamplerState> sampler = nil;

            // whether the shared state finished building, which every later call reads (§6.3)
            bool built = false;

            // whether the build was already tried, so a failed host never recompiles the shader (§6.3)
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

        // build the shared device state, which every surface draws through
        void build_device_state(DeviceState& state) {
            // the default device is the one the process presents with, which a headless host still has (AD-16)
            state.device = MTLCreateSystemDefaultDevice();
            // messaging a nil device answers nil, so every step below degrades instead of branching (§6.2)
            state.queue = [state.device newCommandQueue];
            // the source compiles once per process, which is the only shader work a run ever does (AD-16)
            NSString* source = [[NSString alloc] initWithBytes:shader_source().data() length:shader_source().size() encoding:NSUTF8StringEncoding];
            state.library = [state.device newLibraryWithSource:source options:nil error:nil];
            // premultiplied source over the destination is the one blend the hal composites with (§6.3)
            state.pipeline = build_pipeline(state.device, state.library, MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha);
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
            sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
            state.sampler = [state.device newSamplerStateWithDescriptor:sampler_descriptor];
            // the state is complete only when every piece the draw path binds came up (§6.3)
            state.built = state.device != nil && state.queue != nil && state.library != nil && state.pipeline != nil && state.white != nil && state.sampler != nil;
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

            // encode every recorded draw of the batch into the frame (§6.3)
            void encode(id<MTLRenderCommandEncoder> encoder) const;

            // the layer the surface presents into, which is nil for an offscreen surface (§6.2)
            CAMetalLayer* __strong m_layer = nil;

            // the metrics the surface presents at (§6.1, D9.1)
            WindowMetrics m_metrics;

            // the colour target every frame draws into, which the read back and the present both read (§6.3)
            id<MTLTexture> m_target = nil;

            // the immutable batch the frame builds, which the encoder consumes once (§6.3)
            FrameBatch m_batch;

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
            // the context records logical points onto the physical target, so the device scale is handed to it (AD-27)
            m_context = std::make_unique<AlborRenderContext>(m_batch, Rect::from_origin_size(Point{0.0f, 0.0f}, m_metrics.logical_size), m_metrics.scale_factor);
            // the frame is now in flight, which the matching end presents (§6.1)
            m_begun = true;
            return *m_context;
        }

        void AlborRenderSurface::encode(id<MTLRenderCommandEncoder> encoder) const {
            // the encoder binds the pipeline and the sampler once, which every draw of the frame shares (§6.3)
            [encoder setRenderPipelineState:device_state().pipeline];
            [encoder setFragmentSamplerState:device_state().sampler atIndex:0];
            // the vertices and indices are uploaded once per frame into two shared buffers (§6.3)
            if (m_batch.vertices.empty() || m_batch.indices.empty())
                return;
            id<MTLBuffer> vertices = [device_state().device newBufferWithBytes:m_batch.vertices.data() length:m_batch.vertices.size() * sizeof(BatchVertex) options:MTLResourceStorageModeShared];
            id<MTLBuffer> indices = [device_state().device newBufferWithBytes:m_batch.indices.data() length:m_batch.indices.size() * sizeof(std::uint32_t) options:MTLResourceStorageModeShared];
            [encoder setVertexBuffer:vertices offset:0 atIndex:0];
            // every recorded draw binds its own uniforms and its own texture, then draws its index range (§6.3)
            for (const BatchDraw& draw : m_batch.draws) {
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
                // the target extent projects a device position into clip space (§6.3)
                uniforms.target_size[0] = static_cast<float>(m_target.width);
                uniforms.target_size[1] = static_cast<float>(m_target.height);
                uniforms.ramp = draw.ramp;
                uniforms.kind = draw.coverage == CoverageKind::Folded ? 1 : 0;
                uniforms.scissor_enabled = draw.scissor.enabled ? 1 : 0;
                // an image upload lands with the parity phase, so every draw samples the white texel for now (§6.4, #106)
                uniforms.has_texture = 0;
                [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
                [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
                // a solid draw samples the white texel, which keeps one shader path for every draw (§6.3)
                [encoder setFragmentTexture:device_state().white atIndex:0];
                // the draw consumes its own index range, which the builder recorded (§6.3)
                [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:draw.index_count indexType:MTLIndexTypeUInt32 indexBuffer:indices indexBufferOffset:draw.index_begin * sizeof(std::uint32_t)];
            }
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
                // the encoder replays every recorded draw into the target (§6.3)
                id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
                encode(encoder);
                [encoder endEncoding];
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
