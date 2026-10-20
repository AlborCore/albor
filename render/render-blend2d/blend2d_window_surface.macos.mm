#include "render/render-blend2d/blend2d_window_surface.h"
#include "render/render-blend2d/blend2d_render_surface.h"

#import <Metal/Metal.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "window/window_metrics.h"

namespace albor::blend2d
{
    namespace
    {
        // the device, which is the one pool the drawables are drawn from, so it is held for the process (§6.3)
        [[nodiscard]] id<MTLDevice> process_device() {
            static id<MTLDevice> device = nil;
            static std::once_flag guarded;
            std::call_once(guarded, [] { device = MTLCreateSystemDefaultDevice(); });
            return device;
        }

        // the queue, which is submitted to once per frame, so it is held for the process too (§6.3)
        [[nodiscard]] id<MTLCommandQueue> process_queue() {
            static id<MTLCommandQueue> queue = nil;
            static std::once_flag guarded;
            std::call_once(guarded, [] {
                if (process_device() != nil)
                    queue = [process_device() newCommandQueue];
            });
            return queue;
        }

        // the layer is sized from the metrics, since the layer speaks points and the drawable physical pixels (AD-27)
        void apply_metrics(CAMetalLayer* layer, const WindowMetrics& metrics) {
            // a layer with no device cannot size a drawable, so the resize is refused (§6.3)
            if (layer == nil || layer.device == nil)
                return;
            // the layer frame is in points at the content view, which is the frame the drawable crops to (AD-27)
            layer.frame = CGRectMake(0.0, 0.0, metrics.logical_size.width, metrics.logical_size.height);
            // the contents scale keeps a backing scale change from stretching the drawable (AD-27)
            layer.contentsScale = metrics.scale_factor;
            // the drawable is the physical extent the cpu raster matches byte for byte (§6.3, AD-27)
            layer.drawableSize = CGSizeMake(metrics.pixel_size.width, metrics.pixel_size.height);
        }

        // the cpu raster and the native layer a windowed blend2d frame presents through (§5.3, §6.3, AD-16)
        class WindowSurface final : public RenderSurface
        {
        public:
            WindowSurface(CAMetalLayer* layer, std::unique_ptr<Blend2DRenderSurface> raster, WindowMetrics metrics) :
                m_layer(layer), m_raster(std::move(raster)), m_metrics(std::move(metrics)) {
                // the layer draws from the shared device pool, which keeps two windows on one queue (§6.3)
                m_layer.device = process_device();
                // the drawable order is bgra, which is the byte order the cpu raster already holds (§6.3, AD-26)
                m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
                // the copier needs the drawable plain, since a render target only one refuses the blit (§6.3)
                m_layer.framebufferOnly = NO;
                // the first metrics drive the layer frame, so a later resize keeps the same code path (§6.2)
                apply_metrics(m_layer, m_metrics);
            }

            WindowSurface(const WindowSurface&) = delete;
            WindowSurface& operator=(const WindowSurface&) = delete;
            WindowSurface(WindowSurface&&) = delete;
            WindowSurface& operator=(WindowSurface&&) = delete;

            ~WindowSurface() override = default;

            // override: report whether the layer and the raster both survived their setup
            [[nodiscard]] bool valid() const override {
                // the surface is presentable exactly while the layer carries a device and the raster draws (§6.2)
                return m_layer != nil && m_layer.device != nil && m_raster != nullptr && m_raster->valid();
            }

            // override: hand out the metrics the layer is sized against
            [[nodiscard]] const WindowMetrics& metrics() const override {
                // the metrics mirror the last resize, which the executor compares a scene against (§6.2)
                return m_metrics;
            }

            // override: resize the layer and the raster together, which a backing scale change drives (AD-27)
            void resize(const WindowMetrics& metrics) override {
                // metrics with no area cannot be presented, so the previous size is kept (§6.2)
                if (!valid() || !metrics.valid())
                    return;
                // the metrics move first, because the executor reads them for the next scene (§6.2)
                m_metrics = metrics;
                // the layer is resised at its own units, and the raster carries exactly the pixels it needs (§6.3, AD-27)
                apply_metrics(m_layer, m_metrics);
                m_raster->resize(metrics);
            }

            // override: begin a frame and hand out the context that draws it
            RenderContext& begin_frame() override {
                // the frame is begun, which is what balances the end that presents it (§6.1)
                m_began = true;
                // the raster draws into its own cpu image, which is what the present afterwards blits (§6.3)
                return m_raster->begin_frame();
            }

            // override: end the frame by flushing the raster and presenting it on the layer (§6.1, §6.3)
            void end_frame() override {
                // an unbalanced end would present a frame that never began, so it is ignored (§6.1)
                if (!m_began)
                    return;
                // the frame is marked finished before the present, so a losing drawable skips it (§6.1, §6.3)
                m_began = false;
                // the raster is flushed first, because the blit copies pixels that must be complete (§6.3)
                m_raster->end_frame();
                present();
            }

            // override: read the last finished frame back
            [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override {
                // the read is of the raster, which carries the same picture the layer was given (§6.3)
                if (m_raster == nullptr)
                    return {};
                return m_raster->read_pixels();
            }

        private:
            // copy the finished raster into the drawable, which the layer presents (§6.3)
            void present() {
                // the extent is the physical size the metrics shaped the raster into (§6.3, AD-27)
                const std::size_t width = static_cast<std::size_t>(m_metrics.pixel_size.width);
                const std::size_t height = static_cast<std::size_t>(m_metrics.pixel_size.height);
                const std::size_t row = width * 4u;
                // a host without a device has no drawable to land on, so the present refuses (§6.3)
                id<MTLDevice> device = process_device();
                if (device == nil)
                    return;
                // the staging buffer lives in shared memory, which the copier reads from directly (§6.3)
                const std::size_t bytes = row * height;
                id<MTLBuffer> stage = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
                if (stage == nil || stage.contents == nullptr)
                    return;
                // the native raster is already premultiplied bgra, so it lands without a read back or a swap (§6.3, AD-26)
                if (!m_raster->copy_native_pixels(stage.contents, row))
                    return;
                // the buffer is exercised once it lands, so the copier sees the flushed contents (§6.3)
                [stage didModifyRange:NSMakeRange(0, bytes)];
                // the drawable is the back buffer, which may be vacant when the queue is under pressure (§6.3)
                id<CAMetalDrawable> drawable = [m_layer nextDrawable];
                if (drawable == nil || drawable.texture == nil)
                    return;
                // the copier writes one full frame, with the source rows tightly packed (§6.3)
                id<MTLCommandBuffer> buffer = [process_queue() commandBuffer];
                id<MTLBlitCommandEncoder> blit = buffer == nil ? nil : [buffer blitCommandEncoder];
                if (blit == nil)
                    return;
                [blit copyFromBuffer:stage sourceOffset:0 sourceBytesPerRow:static_cast<NSUInteger>(row) sourceBytesPerImage:static_cast<NSUInteger>(bytes) sourceSize:MTLSizeMake(width, height, 1) toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                [blit endEncoding];
                // the layer presents the drawable when the copier lands, which is the whole presentation (§6.3)
                [buffer presentDrawable:drawable];
                [buffer commit];
            }

            // the native layer the window attached, which is borrowed and never owned (§5.3)
            CAMetalLayer* m_layer = nil;

            // the cpu raster the frame is drawn and presented through (§6.3)
            std::unique_ptr<Blend2DRenderSurface> m_raster;

            // the metrics the layer is sized against (§6.2)
            WindowMetrics m_metrics;

            // whether a frame is currently begun, which the end balances (§6.1)
            bool m_began = false;
        };

        // build the layer-backed surface, refusing the handle when it names no metal layer (§5.3, AD-16)
        [[nodiscard]] std::unique_ptr<RenderSurface> create_metal_window_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
            // metrics with no area cannot be presented, so the surface is refused rather than half built (§6.2)
            if (!metrics.valid())
                return nullptr;
            // the opaque handle carries the metal layer, which only this translation unit may name (§3.5, AD-05)
            CAMetalLayer* layer = (__bridge CAMetalLayer*)const_cast<void*>(handle.object());
            // a foreign layer is a refusal, which the composition root reports (§5.3, AD-16)
            if (layer == nil || ![layer isKindOfClass:[CAMetalLayer class]])
                return nullptr;
            // a host without a metal device has no drawables, so the surface is refused (§6.3)
            if (process_device() == nil || process_queue() == nil)
                return nullptr;
            // the raster is the frame source, which the layer-backed surface presents and reads (§6.3)
            std::unique_ptr<Blend2DRenderSurface> raster = Blend2DRenderSurface::create(metrics);
            if (raster == nullptr)
                return nullptr;
            // the surface is built through its private constructor, which owns the raster (§5.3)
            return std::unique_ptr<RenderSurface>(new WindowSurface(layer, std::move(raster), metrics));
        }

        // the installer that replaces the plain raster with the real presentation at load (§5.3, AD-16)
        struct WindowSurfaceAutoInstall
        {
            WindowSurfaceAutoInstall() {
                set_window_surface_factory([](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> { return create_metal_window_surface(handle, metrics); });
            }
        };

        // the installer is never referenced, because the program load runs it before any factory is read (§5.3)
        const WindowSurfaceAutoInstall g_window_surface_auto_install;
    } // namespace
} // namespace albor::blend2d
