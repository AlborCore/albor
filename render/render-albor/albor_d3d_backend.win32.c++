#include "render/render-albor/albor_backend_support.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "render/render-albor/albor_batch.h"
#include "render/render-albor/albor_glyph_cache.h"
#include "render/render-albor/albor_render_context.h"
#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::render
{
    namespace
    {
        // com pointers release their interface on every exit, so no early return leaves a device behind (§6.3)
        using Microsoft::WRL::ComPtr;

        // the device wide state every direct3d surface draws through, which outlives them (§6.3)
        struct DeviceState
        {
            // the direct3d device the process draws with, which is null until the backend builds it
            ComPtr<ID3D11Device> device;

            // the immediate context every frame is recorded on
            ComPtr<ID3D11DeviceContext> context;

            // the dxgi factory every composition swapchain is created from
            ComPtr<IDXGIFactory2> factory;

            // the composition device every window visual hangs from
            ComPtr<IDCompositionDevice> composition;

            // whether the shared state finished building, which every later call reads (§6.3)
            bool built = false;

            // whether the build was already tried, so a failed host never retries it (§6.3)
            bool attempted = false;
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

        // build the direct3d device and its output merger, which is the next implementation step (§6.3)
        bool ensure_device_state() {
            // the first build is serialized, because two surfaces may race for it (§10.5)
            const std::lock_guard<std::mutex> guard(device_mutex());
            // a state that already ran is handed out as it is, which a failed host never retries (§6.3)
            if (device_state().attempted)
                return device_state().built;
            // the attempt is recorded before the build runs, so a failure stays final (§6.3)
            device_state().attempted = true;
            // the direct3d device, the dxgi factory and the pipelines land here (§6.3)
            return false;
        }

        // the surface the backend presents into, which owns one colour target per frame (§6.2, §6.3)
        class AlborRenderSurface final : public RenderSurface
        {
        public:
            // build a windowed surface over the native handle, or an offscreen one when it is empty (§6.2)
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
            // the implementation map begins with the colour target, which a resize replaces (§6.2, §6.3)
            void allocate();

            // copy every image the frame carries into the texture cache, which a later frame reuses (AD-06)
            void upload_textures();

            // encode every recorded draw of the batch into the frame (§6.3)
            void encode();

            // blur a cast offscreen and composite it under the shape, which runs four passes (§6.2)
            void composite_shadow(const BatchShadow& shadow);

            // composite a layer onto the target beneath it at the opacity its group names (§6.2)
            void composite_layer(const BatchLayer& layer);

            // copy the finished frame into the swapchain and present it (§6.3)
            void present();

            // the native window the surface presents into, which is empty for an offscreen surface
            NativeWindowHandle m_handle;

            // the metrics the surface presents at (§6.1, D9.1)
            WindowMetrics m_metrics;

            // the colour target every frame draws into, which the read back and the present both read (§6.3)
            ComPtr<ID3D11Texture2D> m_target;

            // the composition swapchain the window presents through (§5.3, §6.3)
            ComPtr<IDXGISwapChain1> m_swap_chain;

            // the composition target that binds the visual tree to the window (§5.3)
            ComPtr<IDCompositionTarget> m_composition_target;

            // the visual that carries the swapchain into the composition tree (§5.3)
            ComPtr<IDCompositionVisual> m_composition_visual;

            // the immutable batch the frame builds, which the encoder consumes once (§6.3)
            FrameBatch m_batch;

            // the glyph paths this surface already walked, which the context borrows so a glyph walks once (§6.4)
            GlyphPathCache m_glyph_paths;

            // the context that records the frame into the batch, which is rebuilt with it (§6.2)
            std::unique_ptr<AlborRenderContext> m_context;

            // how many frames this surface has drawn, which the eviction ages the cache by (AD-06)
            std::uint64_t m_frame_counter = 0;

            // how many frames finished on the current target, which a resize resets (§6.3)
            std::size_t m_finished = 0;

            // the metrics a resize asked for while a frame was in flight, which the end applies (§6.2)
            WindowMetrics m_pending_metrics;

            // whether a resize is waiting for the frame in flight to finish (§6.2)
            bool m_resize_pending = false;

            // whether a frame began and has not ended yet (§6.1)
            bool m_begun = false;
        };

        AlborRenderSurface::AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics) :
            m_handle(handle), m_metrics(metrics) {
            // the swapchain and the composition tree attach here once the device state is built (§5.3, §6.3)
        }

        AlborRenderSurface::~AlborRenderSurface() = default;

        bool AlborRenderSurface::valid() const {
            // the device and the colour target are both required before a frame can draw (§6.2)
            return m_target != nullptr && device_state().built;
        }

        const WindowMetrics& AlborRenderSurface::metrics() const {
            // the metrics are what the executor compares the scene against (§6.1, D9.1)
            return m_metrics;
        }

        void AlborRenderSurface::resize(const WindowMetrics& metrics) {
            // the swapchain and the colour target move together once the device state is built (§6.2)
            (void)metrics;
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
            m_context = std::make_unique<AlborRenderContext>(m_batch, Rect::from_origin_size(Point{0.0f, 0.0f}, m_metrics.logical_size), m_metrics.scale_factor, &m_glyph_paths);
            // the frame counter advances once per frame, which the glyph paths age against (AD-06)
            m_frame_counter++;
            // the glyph paths age against the same counter, which drops the ones no draw reached (§6.4)
            m_glyph_paths.begin_frame(m_frame_counter);
            // the frame is now in flight, which the matching end presents (§6.1)
            m_begun = true;
            return *m_context;
        }

        void AlborRenderSurface::end_frame() {
            // an end without a begun frame is a no operation rather than an error (§6.1)
            if (!m_begun)
                return;
            // the frame is no longer in flight, which the encoder and present will balance (§6.1)
            m_begun = false;
        }

        std::vector<std::uint8_t> AlborRenderSurface::read_pixels() const {
            // a surface that cannot read back reports nothing (§6.3)
            return {};
        }
    } // namespace

    bool backend_device_available() {
        // readiness is whether the direct3d device and its pipelines all came up (§6.2)
        return ensure_device_state();
    }

    std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a host without the shared device state builds no surface, which readiness reports (§6.2)
        return ensure_device_state() ? std::make_unique<AlborRenderSurface>(handle, metrics) : nullptr;
    }
} // namespace albor::render
