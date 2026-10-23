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
        // the exchange macros the windows sdk expands carry the language extension tokens clang flags on every use
        // the com pointers are ours and the uuid expansion is the sdk's, so the diagnostic is silenced from here on
        #pragma clang diagnostic ignored "-Wlanguage-extension-token"

        // com pointers release their interface on every exit, so no early return leaves a device behind (§6.3)
        using Microsoft::WRL::ComPtr;

        // the format every colour target presents and reads back at, which the compositor hands out bgra
        // the metal target stays rgba8, so the read back of this platform swaps the channels back to that order
        constexpr DXGI_FORMAT TARGET_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

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

            // the white one by one texture a run without an image draws from, which the milestone 2 sampler serves
            ComPtr<ID3D11Texture2D> white;

            // the sampler the paint surface reads through, which the fragment stage names
            ComPtr<ID3D11SamplerState> sampler;

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

        // the lock that serializes the presentation path, because the context and its dxgi calls are process wide (§6.3)
        std::mutex& process_present_mutex() {
            // the mutex is held for the process, because the immediate context and the factory it guards are too (§6.3)
            static std::mutex mutex;
            return mutex;
        }

        // the pipeline table build lands with the shader artifact of milestone 2, whose slots it fills (§6.3)
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

            // the borrowed window the composition target binds, which the surface never owns (§5.3)
            HWND m_window = nullptr;

            // the metrics the surface presents at (§6.1, D9.1)
            WindowMetrics m_metrics;

            // the colour target every frame draws into, which the read back and the present both read (§6.3)
            ComPtr<ID3D11Texture2D> m_target;

            // the render target view the pass clears and binds, which the target allocates alongside (§6.3)
            ComPtr<ID3D11RenderTargetView> m_target_view;

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

        // create the composition swapchain a window's visual presents, at the physical extent the raster matches (§5.3, AD-16)
        [[nodiscard]] ComPtr<IDXGISwapChain1> create_window_swap_chain(const WindowMetrics& metrics) {
            // the state the process reuses carries the device and the factory the swapchain hangs from (§6.3)
            DeviceState& state = device_state();
            if (state.device == nullptr || state.factory == nullptr)
                return nullptr;
            // the description names the physical extent and the display order the frames are copied into (§6.3, AD-27)
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = std::max<UINT>(1u, static_cast<UINT>(metrics.pixel_size.width));
            description.Height = std::max<UINT>(1u, static_cast<UINT>(metrics.pixel_size.height));
            description.Format = TARGET_FORMAT;
            description.Stereo = FALSE;
            description.SampleDesc.Count = 1;
            description.SampleDesc.Quality = 0;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            // two buffers keep one presented while the next frame is copied into the other (§6.3)
            description.BufferCount = 2;
            // a composition swapchain accepts the stretch scaling, which maps the buffer onto the visual (§5.3)
            description.Scaling = DXGI_SCALING_STRETCH;
            // the flip model is the only presentation model a composition swapchain accepts (§5.3)
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            // the window owns its pixels, so the frame alpha is composited by the scene rather than the desktop (§6.3)
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            // the swapchain attach shares the dxgi path with the presents, so it runs inside the same lock (§6.3)
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            ComPtr<IDXGISwapChain1> swap_chain;
            if (state.factory->CreateSwapChainForComposition(state.device.Get(), &description, nullptr, swap_chain.GetAddressOf()) != S_OK)
                return nullptr;
            return swap_chain;
        }

        // bind the swapchain to the window through the shared composition device, which is the presentation path
        // the target and the visual move into the caller's pointers, because a committed tree dies with its refs
        bool attach_composition(HWND window, IDXGISwapChain1* swap_chain, IDCompositionTarget** target_out, IDCompositionVisual** visual_out) {
            // the composition device is the one the build supplied, whose targets are created per window on it (§5.3)
            DeviceState& state = device_state();
            if (state.composition == nullptr || swap_chain == nullptr || target_out == nullptr || visual_out == nullptr)
                return false;
            ComPtr<IDCompositionTarget> target;
            // the target binds the visual tree to the borrowed window (§5.3)
            if (state.composition->CreateTargetForHwnd(window, TRUE, target.GetAddressOf()) != S_OK)
                return false;
            ComPtr<IDCompositionVisual> visual;
            // the visual is the node the swapchain hangs from (§5.3)
            if (state.composition->CreateVisual(visual.GetAddressOf()) != S_OK)
                return false;
            // the swapchain is the content the visual shows, which is the whole frame (§5.3)
            if (visual->SetContent(swap_chain) != S_OK)
                return false;
            // the visual becomes the root of the window's tree, and the commit is what the compositor reads (§5.3)
            if (target->SetRoot(visual.Get()) != S_OK || state.composition->Commit() != S_OK)
                return false;
            // the caller keeps the refs alive for exactly the lifetime the tree must hold (§6.2)
            *target_out = target.Detach();
            *visual_out = visual.Detach();
            return true;
        }

        // a windowed surface attaches the tree once the target allocated, so the first frame presents (§5.3, AD-16)
        AlborRenderSurface::AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics) :
            m_handle(handle), m_metrics(metrics) {
            // the handle that names a window is the borrowed window the composition target binds to (§5.3)
            m_window = handle.valid() ? static_cast<HWND>(const_cast<void*>(handle.object())) : nullptr;
            // the colour target is allocated at the physical extent the metrics name (§6.3, AD-27)
            allocate();
            // a windowed surface joins the composition device the build supplied (§5.3)
            if (m_window != nullptr) {
                m_swap_chain = create_window_swap_chain(m_metrics);
                if (m_swap_chain != nullptr)
                    (void)attach_composition(m_window, m_swap_chain.Get(), m_composition_target.GetAddressOf(), m_composition_visual.GetAddressOf());
            }
        }

        AlborRenderSurface::~AlborRenderSurface() = default;

        bool AlborRenderSurface::valid() const {
            // the device and the colour target are both required before a frame can draw (§6.2)
            if (m_target == nullptr || m_target_view == nullptr || !device_state().built)
                return false;
            // a windowed surface also needs the swapchain it presents through (§5.3)
            if (m_window != nullptr && m_swap_chain == nullptr)
                return false;
            return true;
        }

        const WindowMetrics& AlborRenderSurface::metrics() const {
            // the metrics are what the executor compares the scene against (§6.1, D9.1)
            return m_metrics;
        }

        void AlborRenderSurface::allocate() {
            // the extent is the physical pixel size the device scale already applied (AD-27)
            const UINT width = std::max<UINT>(1u, static_cast<UINT>(m_metrics.pixel_size.width));
            const UINT height = std::max<UINT>(1u, static_cast<UINT>(m_metrics.pixel_size.height));
            ID3D11Device* device = device_state().device.Get();
            // a target is allocated at the physical pixel extent, and the read back copies out of it (§6.3)
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = TARGET_FORMAT;
            description.SampleDesc.Count = 1;
            description.SampleDesc.Quality = 0;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            description.CPUAccessFlags = 0;
            description.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> target;
            // a target the device refused leaves the surface refused rather than half built (§6.2)
            if (device == nullptr || device->CreateTexture2D(&description, nullptr, target.GetAddressOf()) != S_OK) {
                m_target.Reset();
                m_target_view.Reset();
                m_finished = 0;
                return;
            }
            // the view the pass clears names the whole target, which the load covers (§6.3)
            D3D11_RENDER_TARGET_VIEW_DESC view_description{};
            view_description.Format = TARGET_FORMAT;
            view_description.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11RenderTargetView> view;
            if (device->CreateRenderTargetView(target.Get(), &view_description, view.GetAddressOf()) != S_OK) {
                m_target.Reset();
                m_target_view.Reset();
                m_finished = 0;
                return;
            }
            // the buffers took the extent, so the previous target is replaced only now (§6.2)
            m_target = std::move(target);
            m_target_view = std::move(view);
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
            // a windowed surface resizes its buffers alongside the target, which keeps the compositor in step (§6.3)
            if (m_window != nullptr && m_swap_chain != nullptr) {
                const std::lock_guard<std::mutex> lock(process_present_mutex());
                (void)m_swap_chain->ResizeBuffers(0, std::max<UINT>(1u, static_cast<UINT>(metrics.pixel_size.width)), std::max<UINT>(1u, static_cast<UINT>(metrics.pixel_size.height)), DXGI_FORMAT_UNKNOWN, 0);
            }
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
            // the pass starts from a cleared target, which an empty frame leaves transparent (§6.1)
            if (ID3D11DeviceContext* context = device_state().context.Get(); context != nullptr && m_target_view != nullptr) {
                const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                context->ClearRenderTargetView(m_target_view.Get(), transparent);
            }
            // the frame is now in flight, which the matching end presents (§6.1)
            m_begun = true;
            return *m_context;
        }

        void AlborRenderSurface::end_frame() {
            // an end without a begun frame is a no operation rather than an error (§6.1)
            if (!m_begun)
                return;
            // the frame is no longer in flight, so a read back accepts one (§6.1)
            m_begun = false;
            present();
            // the frame counts once it is submitted, which a read back then waits on (§4.3)
            m_finished++;
            // a resize that waited for this frame applies now, so the next frame uses one extent (§6.2)
            if (m_resize_pending) {
                m_resize_pending = false;
                resize(m_pending_metrics);
            }
        }

        void AlborRenderSurface::present() {
            // an offscreen surface presents nothing, because its target is what a renderer reads back (§6.2)
            if (m_window == nullptr || m_swap_chain == nullptr)
                return;
            // the back buffer is the drawable the frame lands in, which may be vacant under pressure (§6.3)
            ComPtr<ID3D11Texture2D> back_buffer;
            if (m_swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())) != S_OK)
                return;
            // the copy and the present share the dxgi path, so both run inside the same lock (§6.3)
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            device_state().context->CopyResource(back_buffer.Get(), m_target.Get());
            (void)m_swap_chain->Present(1, 0);
        }

        std::vector<std::uint8_t> AlborRenderSurface::read_pixels() const {
            // a read before the first finished frame reports nothing (§6.3)
            if (!valid() || m_finished == 0 || m_begun)
                return {};
            const UINT width = std::max<UINT>(1u, static_cast<UINT>(m_metrics.pixel_size.width));
            const UINT height = std::max<UINT>(1u, static_cast<UINT>(m_metrics.pixel_size.height));
            ID3D11Device* device = device_state().device.Get();
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = TARGET_FORMAT;
            description.SampleDesc.Count = 1;
            description.SampleDesc.Quality = 0;
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            description.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> staging;
            if (device->CreateTexture2D(&description, nullptr, staging.GetAddressOf()) != S_OK)
                return {};
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            device_state().context->CopyResource(staging.Get(), m_target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (device_state().context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped) != S_OK)
                return {};
            // the compositor hands the bytes bgra and the metal target stays rgba, so the read swaps the channels (AD-26)
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * 4u * static_cast<std::size_t>(height));
            for (UINT y = 0; y < height; y++) {
                const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
                auto* out = pixels.data() + static_cast<std::size_t>(y) * 4u * static_cast<std::size_t>(width);
                for (UINT x = 0; x < width; x++) {
                    out[x * 4u + 0u] = row[x * 4u + 2u];
                    out[x * 4u + 1u] = row[x * 4u + 1u];
                    out[x * 4u + 2u] = row[x * 4u + 0u];
                    out[x * 4u + 3u] = row[x * 4u + 3u];
                }
            }
            device_state().context->Unmap(staging.Get(), 0);
            return pixels;
        }

        // the pipeline table build lands with the shader artifact of milestone 2, whose slots it fills (§6.3)
        bool build_device_state(DeviceState& state) {
            // the bgra support carries the display byte order the presentation copies through (§6.3)
            const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            // the hardware adapter is preferred, because the copy into the window is gpu work (§6.3)
            if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, state.device.GetAddressOf(), nullptr, nullptr) != S_OK) {
                // a host with no hardware adapter still has warp, which is the software device this platform ships (§6.3)
                state.device.Reset();
                if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, state.device.GetAddressOf(), nullptr, nullptr) != S_OK)
                    return false;
            }
            // the immediate context is the queue the device itself submits on (§6.3)
            state.device->GetImmediateContext(state.context.GetAddressOf());
            // the device names the adapter the window buffers present on, whose parent is the factory (§6.3)
            ComPtr<IDXGIDevice> dxgi_device;
            if (state.device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())) != S_OK)
                return false;
            ComPtr<IDXGIAdapter> adapter;
            if (dxgi_device->GetAdapter(adapter.GetAddressOf()) != S_OK)
                return false;
            if (adapter->GetParent(IID_PPV_ARGS(state.factory.GetAddressOf())) != S_OK)
                return false;
            // the composition device is the one the shared device answers on, whose targets are per window (§5.3)
            if (DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(state.composition.GetAddressOf())) != S_OK)
                return false;
            // the white one by one texture every imageless draw samples through the sampler (§6.3)
            D3D11_TEXTURE2D_DESC white_description{};
            white_description.Width = 1;
            white_description.Height = 1;
            white_description.MipLevels = 1;
            white_description.ArraySize = 1;
            white_description.Format = TARGET_FORMAT;
            white_description.SampleDesc.Count = 1;
            white_description.Usage = D3D11_USAGE_IMMUTABLE;
            white_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA white_data{};
            // the canonical byte is white premultiplied opaque, which nothing draws over (§6.3)
            static const std::uint8_t WHITE_PIXEL[4] = {255u, 255u, 255u, 255u};
            white_data.pSysMem = WHITE_PIXEL;
            white_data.SysMemPitch = 4;
            if (state.device->CreateTexture2D(&white_description, &white_data, state.white.GetAddressOf()) != S_OK)
                return false;
            D3D11_SAMPLER_DESC sampler_description{};
            sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_description.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampler_description.MaxAnisotropy = 1;
            sampler_description.MinLOD = 0.0f;
            sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
            if (state.device->CreateSamplerState(&sampler_description, state.sampler.GetAddressOf()) != S_OK)
                return false;
            // readiness is whether the device, the factory, the composition device and the shared objects all came up
            state.built = state.device != nullptr && state.context != nullptr && state.factory != nullptr && state.composition != nullptr && state.white != nullptr && state.sampler != nullptr;
            return state.built;
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
