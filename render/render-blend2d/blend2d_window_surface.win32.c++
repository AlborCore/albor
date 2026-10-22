#include "render/render-blend2d/blend2d_window_surface.h"
#include "render/render-blend2d/blend2d_render_surface.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

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
        // com pointers release their object on every exit, so no early return leaves a surface behind (§5.3)
        using Microsoft::WRL::ComPtr;

        // the device, which is the one pool the surfaces are copied through, so it is held for the process (§6.3)
        [[nodiscard]] ID3D11Device* process_device() {
            // the device is built once, because every window presents through the same pool (§6.3)
            static const ComPtr<ID3D11Device> device = [] {
                // the bgra support carries the display byte order the presentation copies through (§6.3)
                const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
                // the hardware adapter is preferred, because the copy into the window is gpu work (§6.3)
                ComPtr<ID3D11Device> built;
                if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, built.GetAddressOf(), nullptr, nullptr) == S_OK)
                    return built;
                // a failed hardware attempt leaves no device, so the fallback starts from a clean slot (§6.3)
                built.Reset();
                // a host with no hardware adapter still has warp, which is the software device this platform ships (§6.3)
                (void)D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, built.GetAddressOf(), nullptr, nullptr);
                // a device that did not come up is left null, which the callers refuse on (§6.3)
                return built;
            }();
            return device.Get();
        }

        // the queue, which is the one context a copy is submitted to, so it is held for the process too (§6.3)
        [[nodiscard]] ID3D11DeviceContext* process_context() {
            // the context is created once with the device, because one copy runs per frame (§6.3)
            static const ComPtr<ID3D11DeviceContext> context = [] {
                // a missing device hands out no context, which the surfaces refuse on (§6.3)
                ID3D11Device* device = process_device();
                if (device == nullptr)
                    return ComPtr<ID3D11DeviceContext>{};
                // the immediate context is the queue the device itself submits on (§6.3)
                ComPtr<ID3D11DeviceContext> built;
                device->GetImmediateContext(built.GetAddressOf());
                return built;
            }();
            return context.Get();
        }

        // the lock that serializes the presentation path, because the context and its dxgi calls are process wide (§6.3)
        [[nodiscard]] std::mutex& process_present_mutex() {
            // the mutex is held for the process, because the context and the factory it guards are too (§6.3)
            static std::mutex mutex;
            return mutex;
        }

        // the factory, which is the one source the window swapchains attach through, so it is held for the process (§6.3)
        [[nodiscard]] IDXGIFactory2* process_factory() {
            // the factory is resolved once, because every swapchain of the process comes out of it (§6.3)
            static const ComPtr<IDXGIFactory2> factory = [] {
                // a missing device names no adapter, so no factory comes out of it (§6.3)
                ID3D11Device* device = process_device();
                if (device == nullptr)
                    return ComPtr<IDXGIFactory2>{};
                // the device names the adapter the window buffers present on (§6.3)
                ComPtr<IDXGIDevice> dxgi_device;
                if (device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())) != S_OK)
                    return ComPtr<IDXGIFactory2>{};
                // the adapter is the parent of the dxgi device, which is what the factory hangs from (§6.3)
                ComPtr<IDXGIAdapter> adapter;
                if (dxgi_device->GetAdapter(adapter.GetAddressOf()) != S_OK)
                    return ComPtr<IDXGIFactory2>{};
                // the factory is the adapter's parent, which is the object a swapchain is created from (§6.3)
                ComPtr<IDXGIFactory2> built;
                if (adapter->GetParent(IID_PPV_ARGS(built.GetAddressOf())) != S_OK)
                    return ComPtr<IDXGIFactory2>{};
                return built;
            }();
            return factory.Get();
        }

        // create the swapchain a composition visual presents, at the physical extent the raster matches (§5.3, AD-16)
        [[nodiscard]] ComPtr<IDXGISwapChain1> create_swap_chain(const WindowMetrics& metrics) {
            // a host without a factory has no swapchain, which the surface refuses on (§6.3)
            IDXGIFactory2* factory = process_factory();
            if (factory == nullptr)
                return nullptr;
            // the description names the physical extent and the display order the frames are copied into (§6.3, AD-27)
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = static_cast<UINT>(metrics.pixel_size.width);
            description.Height = static_cast<UINT>(metrics.pixel_size.height);
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
            // the process device creates the buffers, which the visual then composes into the window (§5.3, §6.3)
            ComPtr<IDXGISwapChain1> swap_chain;
            // the factory is shared with the presents, so the attach runs on the same serialized path (§6.3)
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            if (factory->CreateSwapChainForComposition(process_device(), &description, nullptr, swap_chain.GetAddressOf()) != S_OK)
                return nullptr;
            return swap_chain;
        }

        // create the cpu writable texture a finished frame is copied from, in the display byte order (§6.3)
        [[nodiscard]] ComPtr<ID3D11Texture2D> create_upload(ID3D11Device* device, const WindowMetrics& metrics) {
            // the description is the physical extent the frame is uploaded at, in the display byte order (§6.3, AD-27)
            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(metrics.pixel_size.width);
            description.Height = static_cast<UINT>(metrics.pixel_size.height);
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.SampleDesc.Quality = 0;
            // the dynamic usage is what lets the cpu write the frame into the texture (§6.3)
            description.Usage = D3D11_USAGE_DYNAMIC;
            // direct3d refuses a dynamic texture bound to nothing, so it is bound to a slot the copy never reads (§6.3)
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            description.MiscFlags = 0;
            // a device that never came up has no texture to allocate on, which valid reports (§6.3)
            ComPtr<ID3D11Texture2D> upload;
            if (device == nullptr)
                return upload;
            // a texture that did not allocate leaves the surface without a source, which valid reports (§6.3)
            if (device->CreateTexture2D(&description, nullptr, upload.GetAddressOf()) != S_OK)
                upload.Reset();
            return upload;
        }

        // the cpu raster and the composition swapchain a windowed blend2d frame presents through (§5.3, §6.3, AD-16)
        class WindowSurface final : public RenderSurface
        {
        public:
            WindowSurface(HWND window, std::unique_ptr<Blend2DRenderSurface> raster, WindowMetrics metrics) :
                m_window(window), m_raster(std::move(raster)), m_metrics(std::move(metrics)) {
                // the buffers attach to the process device, which keeps two windows on one pool (§6.3)
                m_swap_chain = create_swap_chain(m_metrics);
                // the upload texture is the source every present copies from, sized by the same metrics (§6.3)
                m_upload = create_upload(process_device(), m_metrics);
                // the visual tree is bound to the window, which is how the buffers reach the screen (§5.3, AD-16)
                (void)attach_composition();
            }

            WindowSurface(const WindowSurface&) = delete;
            WindowSurface& operator=(const WindowSurface&) = delete;
            WindowSurface(WindowSurface&&) = delete;
            WindowSurface& operator=(WindowSurface&&) = delete;

            ~WindowSurface() override = default;

            // override: report whether the composition tree, the buffers and the raster all survived their setup
            [[nodiscard]] bool valid() const override {
                // the composition tree and the buffers are both required, because either alone draws nothing (§6.2)
                if (m_window == nullptr || m_swap_chain == nullptr || m_upload == nullptr)
                    return false;
                // a window whose tree never bound has nothing to compose, so it is refused as well (§5.3)
                if (m_composition == nullptr || m_target == nullptr || m_visual == nullptr)
                    return false;
                // the raster draws the frame a present copies, so a refused raster leaves nothing to show (§6.2)
                return m_raster != nullptr && m_raster->valid();
            }

            // override: hand out the metrics the swapchain is sized against
            [[nodiscard]] const WindowMetrics& metrics() const override {
                // the metrics mirror the last resize, which the executor compares a scene against (§6.2)
                return m_metrics;
            }

            // override: resize the swapchain and the raster together, which a backing scale change drives (AD-27)
            void resize(const WindowMetrics& metrics) override {
                // metrics with no area cannot be presented, so the previous size is kept (§6.2)
                if (!valid() || !metrics.valid())
                    return;
                // the buffers and the raster move together, so a refused resize keeps the last frame presentable (§6.3)
                if (!apply_metrics(metrics))
                    return;
                // the metrics move after the buffers took the extent, which the executor reads next (§6.2)
                m_metrics = metrics;
                // the raster carries the pixels of the new extent, which the next present copies (§6.3, AD-27)
                m_raster->resize(metrics);
            }

            // override: begin a frame and hand out the context that draws it
            RenderContext& begin_frame() override {
                // the frame is begun, which is what balances the end that presents it (§6.1)
                m_began = true;
                // the raster draws into its own cpu image, which is what the present afterwards copies (§6.3)
                return m_raster->begin_frame();
            }

            // override: end the frame by flushing the raster and presenting it on the window (§6.1, §6.3)
            void end_frame() override {
                // an unbalanced end would present a frame that never began, so it is ignored (§6.1)
                if (!m_began)
                    return;
                // the frame is marked finished before the present, so a losing back buffer skips it (§6.1, §6.3)
                m_began = false;
                // the raster is flushed first, because the copy reads pixels that must be complete (§6.3)
                m_raster->end_frame();
                present();
            }

            // override: read the last finished frame back
            [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override {
                // the read is of the raster, which carries the same picture the window was given (§6.3)
                if (m_raster == nullptr)
                    return {};
                return m_raster->read_pixels();
            }

        private:
            // copy the finished raster into the back buffer, which the swapchain presents (§6.3)
            void present() {
                // a host without a context or a swapchain has no buffer to land on, so the present refuses (§6.3)
                ID3D11DeviceContext* context = process_context();
                if (context == nullptr || m_swap_chain == nullptr || m_upload == nullptr)
                    return;
                // the presentation path is shared by every window, so one frame copies and presents at a time (§6.3)
                const std::lock_guard<std::mutex> lock(process_present_mutex());
                // the back buffer is the drawable the frame lands in, which may be vacant under pressure (§6.3)
                ComPtr<ID3D11Texture2D> back_buffer;
                if (m_swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())) != S_OK)
                    return;
                // the frame is mapped into the upload texture, whose rows the map reports with a pitch (§6.3)
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (context->Map(m_upload.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) != S_OK)
                    return;
                // the native raster is already premultiplied bgra, so it lands without a read back or a swap (§6.3, AD-26)
                if (!m_raster->copy_native_pixels(mapped.pData, mapped.RowPitch)) {
                    // a refused copy must still close the map, which is what makes the texture writable again (§6.3)
                    context->Unmap(m_upload.Get(), 0);
                    return;
                }
                // the upload is closed before the copy reads it, which is what publishes the rows to the device (§6.3)
                context->Unmap(m_upload.Get(), 0);
                // the copy lands the whole frame in the back buffer, which the present then shows (§6.3)
                context->CopyResource(back_buffer.Get(), m_upload.Get());
                // the present is a shared dxgi call, so it runs inside the lock with the copy that feeds it (§6.3)
                (void)m_swap_chain->Present(1, 0);
            }

            // resize the swapchain and the upload texture to the metrics, the window presentation size (§6.3, AD-27)
            [[nodiscard]] bool apply_metrics(const WindowMetrics& metrics) {
                // a missing swapchain has no buffer to resize, which leaves the surface as it was (§6.3)
                if (m_swap_chain == nullptr)
                    return false;
                // the upload is built first, because a refused resize must leave the previous source intact (§6.3)
                ComPtr<ID3D11Texture2D> upload = create_upload(process_device(), metrics);
                // a host without a device or an extent cannot supply a source, so the resize refuses (§6.3)
                if (upload == nullptr)
                    return false;
                // the buffers resize on the shared dxgi path, so a present never overlaps it (§6.3)
                const std::lock_guard<std::mutex> lock(process_present_mutex());
                if (m_swap_chain->ResizeBuffers(0, static_cast<UINT>(metrics.pixel_size.width), static_cast<UINT>(metrics.pixel_size.height), DXGI_FORMAT_UNKNOWN, 0) != S_OK)
                    return false;
                // the new source replaces the old only after the buffers took the extent (§6.3, AD-27)
                m_upload = std::move(upload);
                return true;
            }

            // bind the swapchain to the window through a composition visual, which is the presentation path (§5.3, AD-16)
            [[nodiscard]] bool attach_composition() {
                // the composition device comes from the dxgi device the buffers live on (§5.3)
                ComPtr<IDXGIDevice> dxgi_device;
                if (process_device() == nullptr || process_device()->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())) != S_OK)
                    return false;
                // the composition device is the factory of the visual tree this window owns (§5.3)
                if (DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(m_composition.GetAddressOf())) != S_OK)
                    return false;
                // the target binds the visual tree to the borrowed window (§5.3)
                if (m_composition->CreateTargetForHwnd(m_window, TRUE, m_target.GetAddressOf()) != S_OK)
                    return false;
                // the visual is the node the swapchain hangs from (§5.3)
                if (m_composition->CreateVisual(m_visual.GetAddressOf()) != S_OK)
                    return false;
                // the swapchain is the content the visual shows, which is the whole frame (§5.3)
                if (m_swap_chain == nullptr || m_visual->SetContent(m_swap_chain.Get()) != S_OK)
                    return false;
                // the visual becomes the root of the window's tree (§5.3)
                if (m_target->SetRoot(m_visual.Get()) != S_OK)
                    return false;
                // the tree is committed once, which is what the compositor reads (§5.3)
                return m_composition->Commit() == S_OK;
            }

            // the native window the swapchain attached, which is borrowed and never owned (§5.3)
            HWND m_window = nullptr;

            // the swapchain the finished frames are presented through (§6.3)
            ComPtr<IDXGISwapChain1> m_swap_chain;

            // the cpu writable texture a frame is copied from, which the map fills each present (§6.3)
            ComPtr<ID3D11Texture2D> m_upload;

            // the composition device this window's visual tree hangs from (§5.3)
            ComPtr<IDCompositionDevice> m_composition;

            // the target that binds the visual tree to the window (§5.3)
            ComPtr<IDCompositionTarget> m_target;

            // the visual whose content is the swapchain, which composes the frame (§5.3)
            ComPtr<IDCompositionVisual> m_visual;

            // the cpu raster the frame is drawn and presented through (§6.3)
            std::unique_ptr<Blend2DRenderSurface> m_raster;

            // the metrics the swapchain is sized against (§6.2)
            WindowMetrics m_metrics;

            // whether a frame is currently begun, which the end balances (§6.1)
            bool m_began = false;
        };

        // build the swapchain-backed surface, refusing the handle when it names no window (§5.3, AD-16)
        [[nodiscard]] std::unique_ptr<RenderSurface> create_win32_window_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
            // metrics with no area cannot be presented, so the surface is refused rather than half built (§6.2)
            if (!metrics.valid())
                return nullptr;
            // the opaque handle carries the window handle, which only this translation unit may name (§3.5, AD-05)
            HWND window = static_cast<HWND>(const_cast<void*>(handle.object()));
            // a foreign handle is a refusal, which the composition root reports (§5.3, AD-16)
            if (window == nullptr || !IsWindow(window))
                return nullptr;
            // a host without a device has no swapchain, so the surface is refused (§6.3)
            if (process_device() == nullptr || process_context() == nullptr || process_factory() == nullptr)
                return nullptr;
            // the raster is the frame source, which the swapchain-backed surface presents and reads (§6.3)
            std::unique_ptr<Blend2DRenderSurface> raster = Blend2DRenderSurface::create(metrics);
            if (raster == nullptr)
                return nullptr;
            // the surface is built through its private constructor, which owns the raster (§5.3)
            std::unique_ptr<WindowSurface> surface = std::make_unique<WindowSurface>(window, std::move(raster), metrics);
            // a swapchain that did not attach leaves nothing to present, so the half built surface is refused (§6.3)
            if (!surface->valid())
                return nullptr;
            return surface;
        }

        // the installer that replaces the plain raster with the real presentation at load (§5.3, AD-16)
        struct WindowSurfaceAutoInstall
        {
            WindowSurfaceAutoInstall() {
                set_window_surface_factory([](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> { return create_win32_window_surface(handle, metrics); });
            }
        };

        // the installer is never referenced, because the program load runs it before any factory is read (§5.3)
        const WindowSurfaceAutoInstall g_window_surface_auto_install;
    } // namespace
} // namespace albor::blend2d
