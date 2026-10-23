#include "render/render-albor/albor_backend_support.h"
#include "render/render-albor/albor_d3d_diagnostics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "render/aged_cache.h"
#include "render/image_resource.h"
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
// the sdk's uuid macro expands at every use in this unit, so the suppression spans the unit (§5.3)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"

        // com pointers release their interface on every exit, so no early return leaves a device behind (§6.3)
        using Microsoft::WRL::ComPtr;

        // the compositor hands out bgra, which the read back swaps to the metal rgba order (AD-26)
        constexpr DXGI_FORMAT TARGET_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

        // how many composition modes the pipeline table carries, which indexes the array the draw binds (§6.2)
        constexpr std::size_t COMPOSITION_MODES = 11;

        // the shader names modes by index, which must follow the hal order it shapes its alpha for (§6.2)
        static_assert(static_cast<int>(CompositionMode::SourceIn) == 1 && static_cast<int>(CompositionMode::SourceOut) == 2 && static_cast<int>(CompositionMode::DestinationIn) == 5 && static_cast<int>(CompositionMode::DestinationAtop) == 7 && static_cast<int>(CompositionMode::Copy) == 9, "the shader composition constants must follow the hal order");

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

        // the structured vertex stride is the one the shader indexes, so the two layouts cannot drift (§6.3)
        static_assert(sizeof(BatchVertex) == 24, "the batch vertex must stay the stride the shader indexes");

        // the two blend factors one composition mode applies, which the fixed function blender evaluates (§6.2)
        struct BlendFactors
        {
            // the factor the source colour is multiplied by
            D3D11_BLEND source = D3D11_BLEND_ONE;

            // the factor the destination colour is multiplied by
            D3D11_BLEND destination = D3D11_BLEND_INV_SRC_ALPHA;
        };

        // map a composition mode onto the porter duff factors premultiplied colour leaves, which one blender covers (§6.2)
        BlendFactors blend_factors(CompositionMode mode) {
            switch (mode) {
            case CompositionMode::SourceIn:
                return {.source = D3D11_BLEND_DEST_ALPHA, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::SourceOut:
                return {.source = D3D11_BLEND_INV_DEST_ALPHA, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::Atop:
                return {.source = D3D11_BLEND_DEST_ALPHA, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::DestinationOver:
                return {.source = D3D11_BLEND_INV_DEST_ALPHA, .destination = D3D11_BLEND_ONE};
            case CompositionMode::DestinationIn:
                return {.source = D3D11_BLEND_ZERO, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::DestinationOut:
                return {.source = D3D11_BLEND_ZERO, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::DestinationAtop:
                return {.source = D3D11_BLEND_INV_DEST_ALPHA, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::Lighter:
                return {.source = D3D11_BLEND_ONE, .destination = D3D11_BLEND_ONE};
            case CompositionMode::Copy:
                return {.source = D3D11_BLEND_ONE, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::Xor:
                return {.source = D3D11_BLEND_INV_DEST_ALPHA, .destination = D3D11_BLEND_INV_SRC_ALPHA};
            case CompositionMode::SourceOver:
                break;
            }
            return {.source = D3D11_BLEND_ONE, .destination = D3D11_BLEND_INV_SRC_ALPHA};
        }

        // the six compiled entry points the committed container carries, in the order the build stitches them (§6.3)
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

        // read one little endian length the container prefix carries (§6.3)
        [[nodiscard]] std::uint32_t read_length(const std::byte* bytes) {
            const auto* octets = reinterpret_cast<const std::uint8_t*>(bytes);
            return static_cast<std::uint32_t>(octets[0]) | (static_cast<std::uint32_t>(octets[1]) << 8u) | (static_cast<std::uint32_t>(octets[2]) << 16u) | (static_cast<std::uint32_t>(octets[3]) << 24u);
        }

        // parse the committed container, which a corrupt or truncated artifact reports as empty (§6.3)
        ShaderData parse_shader_data(std::span<const std::byte> container) {
            // the magic names the container format, so an artifact from another build is refused here (§6.3)
            static constexpr char MAGIC[8] = {'A', 'L', 'B', 'D', 'X', 'B', 'C', '1'};
            if (container.size() <= 8 || std::memcmp(container.data(), MAGIC, sizeof(MAGIC)) != 0)
                return {};
            std::span<const std::byte> sections[6];
            std::size_t offset = 8;
            for (std::span<const std::byte>& section : sections) {
                // a section whose length prefix or blob runs past the artifact is a truncated container (§6.3)
                if (offset + 4 > container.size())
                    return {};
                const std::uint32_t length = read_length(container.data() + offset);
                offset += 4;
                if (length == 0 || offset + length > container.size())
                    return {};
                section = container.subspan(offset, length);
                offset += length;
            }
            return ShaderData{.vertex = sections[0], .fragment = sections[1], .fragment_exact = sections[2], .shadow_vertex = sections[3], .shadow_shape = sections[4], .shadow_blur = sections[5]};
        }

        // one colour target a frame renders into, which a layer allocates beside the frame target (§6.2)
        struct RenderTarget
        {
            // the texture the target and the read back share
            ComPtr<ID3D11Texture2D> texture;

            // the view the pass binds
            ComPtr<ID3D11RenderTargetView> view;

            // the view the composite samples, which only an offscreen target needs
            ComPtr<ID3D11ShaderResourceView> shader_view;

            // the physical extent the viewport covers
            UINT width = 0;

            // the physical extent the viewport covers
            UINT height = 0;
        };

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

            // the batch vertex stage every draw runs
            ComPtr<ID3D11VertexShader> vertex_shader;

            // the fragment every ordinary draw runs, which the kind uniform selects the coverage of (§6.3)
            ComPtr<ID3D11PixelShader> fragment_shader;

            // the fragment that writes the blend control dual source blending reads (§6.2)
            ComPtr<ID3D11PixelShader> fragment_exact;

            // the vertex stage the two offscreen passes share (§6.2)
            ComPtr<ID3D11VertexShader> shadow_vertex;

            // the fragment that rasterizes a rounded silhouette, which a cast is blurred from (§6.2)
            ComPtr<ID3D11PixelShader> shadow_shape;

            // the fragment that runs one separable gaussian pass over the silhouette (§6.2)
            ComPtr<ID3D11PixelShader> shadow_blur;

            // the blend state each composition mode binds, indexed by the mode the draw carries (§6.2)
            std::array<ComPtr<ID3D11BlendState>, COMPOSITION_MODES> blend_states{};

            // the blend states that read the second shader output, which the fixed list cannot express (§6.2)
            std::array<ComPtr<ID3D11BlendState>, COMPOSITION_MODES> exact_blend_states{};

            // the blend state the two shadow passes share, which writes the silhouette alone (§6.2)
            ComPtr<ID3D11BlendState> shadow_blend;

            // the rasterizer every pass uses, which culls nothing because the geometry core emits both windings (§6.3)
            ComPtr<ID3D11RasterizerState> rasterizer;

            // the one by one white texture a run without an image samples, so one shader path covers both (§6.3)
            ComPtr<ID3D11Texture2D> white;

            // the view the white texture binds through (§6.3)
            ComPtr<ID3D11ShaderResourceView> white_view;

            // the sampler the paint surface reads through, which the fragment stage names
            ComPtr<ID3D11SamplerState> sampler;

            // the draw uniform buffer every pass writes once per draw (§6.3)
            ComPtr<ID3D11Buffer> draw_uniforms;

            // the shadow uniform buffer the offscreen passes write (§6.2)
            ComPtr<ID3D11Buffer> shadow_uniforms;

            // whether the shared state finished building, which every later call reads without the device lock (§6.3)
            std::atomic<bool> built{false};

            // whether the build was already tried, so a failed host never retries it (§6.3)
            std::atomic<bool> attempted{false};

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

        // the lock that serializes the presentation path, because the context and its dxgi calls are process wide (§6.3)
        std::mutex& process_present_mutex() {
            // the mutex is held for the process, because the immediate context and the factory it guards are too (§6.3)
            static std::mutex mutex;
            return mutex;
        }

        // record a driver loss, which every surface reads as invalid rather than presenting a frozen frame (§6.3)
        void note_device_result(HRESULT result) {
            // only a removed or reset device ends the session, which every other failure leaves recoverable (§6.3)
            if (result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET)
                return;
            device_state().lost.store(true);
        }

        // count the frames the process submitted, which a test reads to prove a windowed frame passed the present (§6.3)
        std::atomic<std::uint64_t>& presented_frames() {
            // the counter lives for the process, because one present is one frame whichever surface submitted it (§6.3)
            static std::atomic<std::uint64_t> frames{0};
            return frames;
        }

        // one composition target per window and the visuals its surfaces bound (§5.3)
        struct WindowTarget
        {
            // the target the compositor bound to the window
            ComPtr<IDCompositionTarget> target;

            // the visuals in attach order, whose last one the window shows
            std::vector<IDCompositionVisual*> visuals;
        };

        // the targets this process bound, which a second surface for the same window reuses (§5.3)
        std::unordered_map<HWND, WindowTarget>& window_targets() {
            // the map is held for the process, because a target a surface burned dies with its refs (§6.2)
            static std::unordered_map<HWND, WindowTarget> targets;
            return targets;
        }

        // create one blend state, which the pipeline table binds per draw (§6.2)
        [[nodiscard]] ComPtr<ID3D11BlendState> create_blend_state(ID3D11Device* device, D3D11_BLEND source, D3D11_BLEND destination) {
            // the descriptor names one attachment, and the sampler never reads coverage (§6.2)
            D3D11_BLEND_DESC description{};
            description.AlphaToCoverageEnable = FALSE;
            description.IndependentBlendEnable = FALSE;
            description.RenderTarget[0].BlendEnable = TRUE;
            description.RenderTarget[0].SrcBlend = source;
            description.RenderTarget[0].DestBlend = destination;
            description.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            description.RenderTarget[0].SrcBlendAlpha = source;
            description.RenderTarget[0].DestBlendAlpha = destination;
            description.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            description.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            ComPtr<ID3D11BlendState> state;
            if (device == nullptr || device->CreateBlendState(&description, state.GetAddressOf()) != S_OK)
                return nullptr;
            return state;
        }

        // create one blend state that reads the second shader output, which dual source blending needs (§6.2)
        [[nodiscard]] ComPtr<ID3D11BlendState> create_exact_blend_state(ID3D11Device* device, D3D11_BLEND source_rgb, D3D11_BLEND destination_rgb, D3D11_BLEND source_alpha, D3D11_BLEND destination_alpha) {
            // the colour and alpha lanes take different factors, which a single source blender cannot express (§6.2)
            D3D11_BLEND_DESC description{};
            description.AlphaToCoverageEnable = FALSE;
            description.IndependentBlendEnable = FALSE;
            description.RenderTarget[0].BlendEnable = TRUE;
            description.RenderTarget[0].SrcBlend = source_rgb;
            description.RenderTarget[0].DestBlend = destination_rgb;
            description.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            description.RenderTarget[0].SrcBlendAlpha = source_alpha;
            description.RenderTarget[0].DestBlendAlpha = destination_alpha;
            description.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            description.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            ComPtr<ID3D11BlendState> state;
            if (device == nullptr || device->CreateBlendState(&description, state.GetAddressOf()) != S_OK)
                return nullptr;
            return state;
        }

        // create the offscreen target one layer or shadow pass paints into, or an empty target the device refused (§6.2)
        [[nodiscard]] RenderTarget create_render_target(ID3D11Device* device, UINT width, UINT height) {
            // an empty extent has no pixel a pass could write, so no target is allocated (§6.2)
            if (device == nullptr || width == 0 || height == 0)
                return {};
            RenderTarget target;
            target.width = width;
            target.height = height;
            // the offscreen target carries the same format as the frame, so one shader path serves both (§6.3)
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = TARGET_FORMAT;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (device->CreateTexture2D(&description, nullptr, target.texture.GetAddressOf()) != S_OK)
                return {};
            // the view the pass binds names the whole target, which the pass covers (§6.3)
            D3D11_RENDER_TARGET_VIEW_DESC view_description{};
            view_description.Format = TARGET_FORMAT;
            view_description.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            if (device->CreateRenderTargetView(target.texture.Get(), &view_description, target.view.GetAddressOf()) != S_OK)
                return {};
            // the view the composite samples reads the same texels the pass wrote (§6.2)
            D3D11_SHADER_RESOURCE_VIEW_DESC resource_description{};
            resource_description.Format = TARGET_FORMAT;
            resource_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            resource_description.Texture2D.MipLevels = 1;
            if (device->CreateShaderResourceView(target.texture.Get(), &resource_description, target.shader_view.GetAddressOf()) != S_OK)
                return {};
            return target;
        }

        // create the structured vertex view the vertex stage indexes, which one draw or a composite quad reads (§6.3)
        [[nodiscard]] ComPtr<ID3D11ShaderResourceView> create_vertex_view(ID3D11Device* device, const BatchVertex* vertices, std::size_t count) {
            // a buffer with no vertex has nothing the stage could index, so no view is created (§6.2)
            if (device == nullptr || vertices == nullptr || count == 0)
                return nullptr;
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = static_cast<UINT>(count * sizeof(BatchVertex));
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            description.StructureByteStride = static_cast<UINT>(sizeof(BatchVertex));
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = vertices;
            ComPtr<ID3D11Buffer> buffer;
            if (device->CreateBuffer(&description, &data, buffer.GetAddressOf()) != S_OK)
                return nullptr;
            // the view covers every vertex the buffer carries, which the identifier indexes (§6.3)
            D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
            view_description.Format = DXGI_FORMAT_UNKNOWN;
            view_description.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            view_description.Buffer.FirstElement = 0;
            view_description.Buffer.NumElements = static_cast<UINT>(count);
            ComPtr<ID3D11ShaderResourceView> view;
            if (device->CreateShaderResourceView(buffer.Get(), &view_description, view.GetAddressOf()) != S_OK)
                return nullptr;
            return view;
        }

        // create the index buffer one frame consumes, which every draw takes its own range of (§6.3)
        [[nodiscard]] ComPtr<ID3D11Buffer> create_index_buffer(ID3D11Device* device, const std::vector<std::uint32_t>& indices) {
            // a frame with no index has nothing to draw, so no buffer is created (§6.2)
            if (device == nullptr || indices.empty())
                return nullptr;
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = indices.data();
            ComPtr<ID3D11Buffer> buffer;
            if (device->CreateBuffer(&description, &data, buffer.GetAddressOf()) != S_OK)
                return nullptr;
            return buffer;
        }

        // write one uniform block into the dynamic buffer, which the discard map leaves the previous frame free (§6.3)
        void upload_uniforms(ID3D11Buffer* buffer, const void* block, std::size_t size) {
            ID3D11DeviceContext* context = device_state().context.Get();
            if (context == nullptr || buffer == nullptr || block == nullptr)
                return;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) != S_OK)
                return;
            std::memcpy(mapped.pData, block, size);
            context->Unmap(buffer, 0);
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
            void allocate(const WindowMetrics& extent);

            // copy every image the frame carries into the texture cache, which a later frame reuses (AD-06)
            void upload_textures();

            // encode every recorded draw of the batch into the frame (§6.3)
            void encode();

            // record one batch draw with the pipeline its composition names (§6.2)
            void draw_batch_draw(const BatchDraw& draw, const RenderTarget& target, ID3D11ShaderResourceView* vertices);

            // blur a cast offscreen and composite it under the shape, which runs three passes (§6.2)
            void composite_shadow(const BatchShadow& shadow, const RenderTarget& target);

            // run one separable gaussian pass over the silhouette (§6.2)
            void blur_pass(const RenderTarget& source, const RenderTarget& target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height);

            // composite a layer onto the target beneath it at the opacity its group names (§6.2)
            void composite_layer(float opacity, const RenderTarget& offscreen, const RenderTarget& parent);

            // draw one six vertex quad through the ordinary fragment stage (§6.2)
            void draw_quad(const BatchVertex* quad, const DrawUniforms& uniforms, ID3D11ShaderResourceView* texture, ID3D11BlendState* blend);

            // bind a target and cover it with its viewport, clearing it when the pass begins a surface (§6.2)
            void bind_target(const RenderTarget& target, bool clear);

            // copy the finished frame into the swapchain and submit it, which the caller runs under the lock (§6.3)
            void present(ID3D11Texture2D* back_buffer);

            // report validity to a caller that already holds the present mutex (§6.3)
            [[nodiscard]] bool valid_locked() const;

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

            // how many frames an unreferenced texture survives, which bounds the memory a long run holds (AD-06)
            static constexpr std::uint64_t TEXTURE_IDLE_FRAMES = 120;

            // how many textures a surface aims to hold, which one frame's own uploads may briefly exceed (AD-06)
            static constexpr std::size_t MAXIMUM_CACHED_TEXTURES = 64;

            // the textures the uploaded images became, keyed by the identifier an image carries (AD-06)
            AgedCache<std::uint64_t, ComPtr<ID3D11ShaderResourceView>> m_textures{TEXTURE_IDLE_FRAMES, MAXIMUM_CACHED_TEXTURES};

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
            std::atomic<bool> m_resize_pending{false};

            // whether a frame began and has not ended yet (§6.1)
            std::atomic<bool> m_begun{false};
        };

        // create the composition swapchain a window's visual presents, at the physical extent (§5.3, AD-16)
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
            // the submit waits for one frame at most, which bounds the pacing a hidden or occluded window can add (§6.3)
            ComPtr<IDXGISwapChain2> latency;
            if (swap_chain.As(&latency) == S_OK)
                (void)latency->SetMaximumFrameLatency(1);
            return swap_chain;
        }

        // bind the swapchain to the window through the shared composition device, reusing a target it holds (§5.3)
        bool attach_composition(HWND window, IDXGISwapChain1* swap_chain, ComPtr<IDCompositionTarget>& target_out, ComPtr<IDCompositionVisual>& visual_out) {
            // the composition device is the one the shared device answers on, whose targets are created per window (§5.3)
            DeviceState& state = device_state();
            if (swap_chain == nullptr)
                return false;
            // the composition device is process wide, so the whole attach runs inside the presentation lock (§6.3)
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            // the device is created on the first windowed attach, so an offscreen host never needs a compositor (§5.3)
            if (state.composition == nullptr) {
                ComPtr<IDXGIDevice> dxgi_device;
                if (state.device == nullptr || state.device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf())) != S_OK)
                    return false;
                if (DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(state.composition.GetAddressOf())) != S_OK)
                    return false;
            }
            auto& targets = window_targets();
            const auto found = targets.find(window);
            ComPtr<IDCompositionTarget> target;
            // a window that already holds a target hands it over, which the second surface re-roots (§5.3)
            if (found != targets.end()) {
                target = found->second.target;
            }
            else if (state.composition->CreateTargetForHwnd(window, TRUE, target.GetAddressOf()) != S_OK) {
                return false;
            }
            // the visual is the node the swapchain hangs from (§5.3)
            ComPtr<IDCompositionVisual> visual;
            if (state.composition->CreateVisual(visual.GetAddressOf()) != S_OK)
                return false;
            // the swapchain is the content the visual shows, which is the whole frame (§5.3)
            if (visual->SetContent(swap_chain) != S_OK)
                return false;
            // the visual becomes the root of the window's tree, which the last surface bound to it owns (§5.3)
            IDCompositionVisual* previous_root = found != targets.end() && !found->second.visuals.empty() ? found->second.visuals.back() : nullptr;
            if (target->SetRoot(visual.Get()) != S_OK)
                return false;
            // a refused commit leaves the window on the root it showed, rather than on a visual nothing recorded (§5.3)
            if (state.composition->Commit() != S_OK) {
                // a rollback that also fails blanks the tree, so no later commit can show a released visual (§5.3)
                if (target->SetRoot(previous_root) != S_OK || state.composition->Commit() != S_OK) {
                    (void)target->SetRoot(nullptr);
                    (void)state.composition->Commit();
                }
                return false;
            }
            // the window holds the target for every visual its surfaces bound, in the order they attached (§5.3)
            if (found != targets.end()) {
                found->second.visuals.push_back(visual.Get());
            }
            else {
                targets.emplace(window, WindowTarget{.target = target, .visuals = {visual.Get()}});
            }
            target_out = target;
            visual_out = std::move(visual);
            return true;
        }

        // drop the visual a surface held, which re-roots the window on the newest one still alive (§5.3)
        void detach_composition(HWND window, IDCompositionVisual* visual) {
            // a surface with no window bound no target, so nothing is released (§5.3)
            if (window == nullptr)
                return;
            // the target map is shared with the attach, which the same lock serializes (§6.3)
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            auto& targets = window_targets();
            const auto found = targets.find(window);
            if (found == targets.end())
                return;
            // the surface that goes away is no longer a candidate root, which the next newest replaces (§5.3)
            std::vector<IDCompositionVisual*>& visuals = found->second.visuals;
            visuals.erase(std::remove(visuals.begin(), visuals.end(), visual), visuals.end());
            if (!visuals.empty()) {
                (void)found->second.target->SetRoot(visuals.back());
                (void)device_state().composition->Commit();
                return;
            }
            // the last visual blanks the tree before the target is released, so no window keeps a dead frame (§5.3)
            (void)found->second.target->SetRoot(nullptr);
            (void)device_state().composition->Commit();
            targets.erase(found);
        }

        // a windowed surface attaches the tree once the target allocated, so the first frame presents (§5.3, AD-16)
        AlborRenderSurface::AlborRenderSurface(NativeWindowHandle handle, const WindowMetrics& metrics) :
            m_metrics(metrics) {
            // the handle that names a window is the borrowed window the composition target binds to (§5.3)
            m_window = handle.valid() ? static_cast<HWND>(const_cast<void*>(handle.object())) : nullptr;
            // the colour target is allocated at the physical extent the metrics name (§6.3, AD-27)
            allocate(m_metrics);
            // a windowed surface joins the composition device the build supplied (§5.3)
            if (m_window != nullptr && (m_swap_chain = create_window_swap_chain(m_metrics)) != nullptr) {
                // a composition tree the device refused resets the surface, so it reports invalid rather than blank (§6.2)
                if (!attach_composition(m_window, m_swap_chain.Get(), m_composition_target, m_composition_visual)) {
                    m_composition_target.Reset();
                    m_composition_visual.Reset();
                    m_swap_chain.Reset();
                }
            }
        }

        AlborRenderSurface::~AlborRenderSurface() {
            // only a surface whose tree took the window holds a visual, which the attach recorded (§5.3)
            if (m_composition_target != nullptr)
                detach_composition(m_window, m_composition_visual.Get());
        }

        bool AlborRenderSurface::valid() const {
            // a caller outside the frame path reads the target under the lock its resize also takes (§10.5, D9.1)
            const std::lock_guard<std::mutex> guard(process_present_mutex());
            return valid_locked();
        }

        bool AlborRenderSurface::valid_locked() const {
            // the device and the colour target are both required before a frame can draw (§6.2)
            if (m_target == nullptr || m_target_view == nullptr || !device_state().built || device_state().lost.load())
                return false;
            // a windowed surface is valid only when the composition tree took it (§6.2)
            if (m_window != nullptr && (m_swap_chain == nullptr || m_composition_target == nullptr || m_composition_visual == nullptr))
                return false;
            return true;
        }

        const WindowMetrics& AlborRenderSurface::metrics() const {
            // the metrics are what the executor compares the scene against (§6.1, D9.1)
            return m_metrics;
        }

        void AlborRenderSurface::allocate(const WindowMetrics& extent) {
            // the extent is the physical pixel size the device scale already applied (AD-27)
            const UINT width = std::max<UINT>(1u, static_cast<UINT>(extent.pixel_size.width));
            const UINT height = std::max<UINT>(1u, static_cast<UINT>(extent.pixel_size.height));
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
            // the target and the metrics are only ever swapped under the lock every reader holds (§10.5, D9.1)
            const std::lock_guard<std::mutex> guard(process_present_mutex());
            // a resize while a frame is in flight waits for the frame, so no frame mixes two extents (§6.2, D9.1)
            if (m_begun.load()) {
                m_pending_metrics = metrics;
                m_resize_pending.store(true);
                return;
            }
            // the previous target is kept aside, so a refused resize leaves the surface on the extent it presented (§6.3)
            ComPtr<ID3D11Texture2D> previous_target = m_target;
            ComPtr<ID3D11RenderTargetView> previous_view = m_target_view;
            const std::size_t previous_finished = m_finished;
            // the target is reallocated first, because a refused allocation leaves the swapchain untouched (§6.2)
            allocate(metrics);
            if (m_target != nullptr && m_window != nullptr && m_swap_chain != nullptr) {
                // a windowed surface resizes its buffers alongside the target, which keeps the compositor in step (§6.3)
                const HRESULT resized = m_swap_chain->ResizeBuffers(0, std::max<UINT>(1u, static_cast<UINT>(metrics.pixel_size.width)), std::max<UINT>(1u, static_cast<UINT>(metrics.pixel_size.height)), DXGI_FORMAT_UNKNOWN, 0);
                if (SUCCEEDED(resized)) {
                    // the metrics are published last, so no reader sees an extent the target does not carry (§6.3)
                    m_metrics = metrics;
                    return;
                }
                // a lost device cannot present the new extent, which the surface reports rather than hides (§6.3)
                note_device_result(resized);
            }
            else if (m_target != nullptr) {
                m_metrics = metrics;
                return;
            }
            // the new extent did not take, so the extent the window already showed is restored with its target (§6.3)
            m_target = std::move(previous_target);
            m_target_view = std::move(previous_view);
            m_finished = previous_finished;
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
                const std::lock_guard<std::mutex> lock(process_present_mutex());
                metrics = m_metrics;
                // the frame is in flight before the lock is released, which the matching end presents (§6.1)
                m_begun.store(true);
            }
            // the context records logical points onto the physical target, so the device scale is handed to it (AD-27)
            m_context = std::make_unique<AlborRenderContext>(m_batch, Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size), metrics.scale_factor, &m_glyph_paths);
            // the frame counter advances once per frame, which the textures record and the glyph paths age against (AD-06)
            m_frame_counter++;
            // the textures age against the same counter, which drops the images no draw reached (AD-06)
            m_textures.begin_frame(m_frame_counter);
            // the glyph paths age against the same counter, which drops the ones no draw reached (§6.4)
            m_glyph_paths.begin_frame(m_frame_counter);
            return *m_context;
        }

        void AlborRenderSurface::end_frame() {
            // an end without a begun frame is a no operation rather than an error (§6.1)
            if (!m_begun.load())
                return;
            bool finished = false;
            bool pending = false;
            WindowMetrics pending_metrics;
            {
                // the back buffer lives only inside this block, so a resize applied after it holds no reference (§6.3)
                ComPtr<ID3D11Texture2D> back_buffer;
                if (m_window != nullptr && m_swap_chain != nullptr) {
                    // the pacing wait is per window, so the acquisition runs outside the process lock (§6.3)
                    const HRESULT acquired = m_swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
                    if (FAILED(acquired))
                        note_device_result(acquired);
                }
                // the encode and the submit record on the process wide context, so one lock covers both (§6.3, §10.2)
                const std::lock_guard<std::mutex> lock(process_present_mutex());
                // the images the frame carries become textures once, which a later frame reuses (AD-06)
                upload_textures();
                // the encode replays every recorded draw, which the clear begins (§6.3)
                encode();
                // a windowed frame copies the target onto its back buffer, which the submit shows (§6.3)
                present(back_buffer.Get());
                // a frame that never reached a back buffer is not finished, so a read back does not answer with it (§6.3)
                finished = m_window == nullptr || back_buffer != nullptr;
                if (finished)
                    m_finished++;
                // the frame is no longer in flight, and a resize that waited for it is taken together (§6.2)
                m_begun.store(false);
                pending = m_resize_pending.exchange(false);
                pending_metrics = m_pending_metrics;
            }
            // a resize that waited for this frame applies now, so the next frame uses one extent (§6.2)
            if (pending)
                resize(pending_metrics);
        }

        void AlborRenderSurface::present(ID3D11Texture2D* back_buffer) {
            // an offscreen surface presents nothing, because its target is what a renderer reads back (§6.2)
            if (back_buffer == nullptr || m_swap_chain == nullptr)
                return;
            // the copy and the submit both reach the process wide context, so the caller holds the present mutex (§6.3)
            device_state().context->CopyResource(back_buffer, m_target.Get());
            // the submit never waits for the display, so the lock never covers a refresh (§6.3)
            const HRESULT presented = m_swap_chain->Present(0, 0);
            // only a submit that returned success is a frame the window composes, not an occluded status (§6.3)
            if (presented == S_OK)
                presented_frames().fetch_add(1);
            note_device_result(presented);
        }

        std::vector<std::uint8_t> AlborRenderSurface::read_pixels() const {
            // the read copies on the process wide context, so the lock serializes it with the frames (§6.3)
            const std::lock_guard<std::mutex> lock(process_present_mutex());
            // a read before the first finished frame reports nothing (§6.3)
            if (!valid_locked() || m_finished == 0 || m_begun.load())
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
            if (device == nullptr || device->CreateTexture2D(&description, nullptr, staging.GetAddressOf()) != S_OK)
                return {};
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

        void AlborRenderSurface::upload_textures() {
            DeviceState& state = device_state();
            ID3D11Device* device = state.device.Get();
            ID3D11DeviceContext* context = state.context.Get();
            if (device == nullptr || context == nullptr)
                return;
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
                D3D11_TEXTURE2D_DESC description{};
                description.Width = static_cast<UINT>(width);
                description.Height = static_cast<UINT>(height);
                // a full chain keeps a minified image from aliasing, which the reference also avoids (§6.4, AD-26)
                description.MipLevels = 0;
                description.ArraySize = 1;
                description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                description.SampleDesc.Count = 1;
                description.Usage = D3D11_USAGE_DEFAULT;
                description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                ComPtr<ID3D11Texture2D> texture;
                if (device->CreateTexture2D(&description, nullptr, texture.GetAddressOf()) != S_OK)
                    continue;
                // the upload copies the row packed bytes into level zero, which the mip chain is generated from (§6.4)
                context->UpdateSubresource(texture.Get(), 0, nullptr, upload.image->pixels().data(), static_cast<UINT>(upload.image->row_bytes()), 0);
                ComPtr<ID3D11ShaderResourceView> view;
                if (device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()) != S_OK)
                    continue;
                // the chain is generated once, which a later frame samples instead of reuploading (§6.4, AD-06)
                context->GenerateMips(view.Get());
                m_textures.store(upload.id, std::move(view));
            }
        }

        void AlborRenderSurface::bind_target(const RenderTarget& target, bool clear) {
            ID3D11DeviceContext* context = device_state().context.Get();
            if (context == nullptr || target.view == nullptr)
                return;
            // the view is bound for the whole pass, because the offscreen passes replace it rather than blend to it (§6.2)
            ID3D11RenderTargetView* view = target.view.Get();
            context->OMSetRenderTargets(1, &view, nullptr);
            // a target a pass begins on starts from a transparent clear, which an unpainted frame leaves (§6.1)
            if (clear) {
                const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                context->ClearRenderTargetView(view, transparent);
            }
            // the viewport covers the whole target, which the physical extent names (§6.3)
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(target.width);
            viewport.Height = static_cast<float>(target.height);
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
        }

        void AlborRenderSurface::draw_batch_draw(const BatchDraw& draw, const RenderTarget& target, ID3D11ShaderResourceView* vertices) {
            DeviceState& state = device_state();
            ID3D11DeviceContext* context = state.context.Get();
            if (context == nullptr || vertices == nullptr)
                return;
            // the draw binds its composition pipeline, and the three exact modes use theirs (§6.2)
            const std::size_t composition = static_cast<std::size_t>(draw.composition);
            const bool exact = state.exact_blend_states[composition] != nullptr;
            ID3D11BlendState* blend = (exact ? state.exact_blend_states[composition] : state.blend_states[composition]).Get();
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
            upload_uniforms(state.draw_uniforms.Get(), &uniforms, sizeof(uniforms));
            // the draw binds its own texture, or the white texel a solid draw samples (§6.3, §6.4)
            ID3D11ShaderResourceView* paint = state.white_view.Get();
            if (draw.texture != 0) {
                // the upload the frame carried became the texture the cache holds (AD-06)
                const auto found = m_textures.peek(draw.texture);
                if (found != nullptr && *found != nullptr)
                    paint = found->Get();
            }
            context->VSSetShader(state.vertex_shader.Get(), nullptr, 0);
            context->PSSetShader((exact ? state.fragment_exact : state.fragment_shader).Get(), nullptr, 0);
            context->OMSetBlendState(blend, nullptr, 0xffffffffu);
            // the uniform block is the one both stages read, which the per draw upload replaced (§6.3)
            ID3D11Buffer* uniform_buffer = state.draw_uniforms.Get();
            context->VSSetConstantBuffers(0, 1, &uniform_buffer);
            context->PSSetConstantBuffers(0, 1, &uniform_buffer);
            // the vertex stage indexes the structured buffer the batch was uploaded into (§6.3)
            context->VSSetShaderResources(0, 1, &vertices);
            // the sampler every paint texture reads through, and the texel the draw selected (§6.4)
            ID3D11SamplerState* sampler = state.sampler.Get();
            context->PSSetSamplers(0, 1, &sampler);
            context->PSSetShaderResources(0, 1, &paint);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            // the draw consumes its own index range, which the builder recorded (§6.3)
            context->DrawIndexed(draw.index_count, draw.index_begin, 0);
        }

        void AlborRenderSurface::composite_layer(float opacity, const RenderTarget& offscreen, const RenderTarget& parent) {
            DeviceState& state = device_state();
            // a refused offscreen target draws the group into its parent instead, because no offscreen exists (§6.2)
            if (offscreen.shader_view == nullptr)
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
            draw_quad(quad, uniforms, offscreen.shader_view.Get(), state.blend_states[0].Get());
        }

        void AlborRenderSurface::composite_shadow(const BatchShadow& shadow, const RenderTarget& target) {
            DeviceState& state = device_state();
            ID3D11Device* device = state.device.Get();
            ID3D11DeviceContext* context = state.context.Get();
            if (device == nullptr || context == nullptr || target.view == nullptr)
                return;
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
            const UINT pixels_width = static_cast<UINT>(width);
            const UINT pixels_height = static_cast<UINT>(height);
            if (pixels_width == 0 || pixels_height == 0)
                return;
            // two offscreen targets ping pong the silhouette through the two blur directions (§6.2)
            RenderTarget silhouette = create_render_target(device, pixels_width, pixels_height);
            RenderTarget scratch = create_render_target(device, pixels_width, pixels_height);
            // a refused offscreen target leaves the cast unpainted, because the blur has no surface to land on (§6.2)
            if (silhouette.view == nullptr || scratch.view == nullptr)
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
            bind_target(silhouette, true);
            upload_uniforms(state.shadow_uniforms.Get(), &uniforms, sizeof(uniforms));
            context->VSSetShader(state.shadow_vertex.Get(), nullptr, 0);
            context->PSSetShader(state.shadow_shape.Get(), nullptr, 0);
            context->OMSetBlendState(state.shadow_blend.Get(), nullptr, 0xffffffffu);
            ID3D11Buffer* shadow_buffer = state.shadow_uniforms.Get();
            context->PSSetConstantBuffers(0, 1, &shadow_buffer);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->Draw(4, 0);
            // a sigma of zero needs no blur, which a hard cast carries (§6.2)
            const float sigma = shadow.sigma;
            if (sigma > 0.0f) {
                // the kernel carries a spread of 1.69 taps, which the sigma turns into a texel step (§6.2)
                const float step = sigma / 2.5f;
                // the horizontal pass spreads the silhouette across the scratch target (§6.2)
                blur_pass(silhouette, scratch, uniforms, 1.0f, 0.0f, step, width, height);
                // the vertical pass spreads it back, which finishes the separable gaussian (§6.2)
                blur_pass(scratch, silhouette, uniforms, 0.0f, 1.0f, step, width, height);
            }
            // the cast composites over what is under it, which the fill pipeline blends source over (§6.2)
            bind_target(target, false);
            BatchVertex quad[6];
            const float corners[6][2] = {{left, top}, {left + width, top}, {left, top + height}, {left, top + height}, {left + width, top}, {left + width, top + height}};
            for (std::size_t index = 0; index < 6; index++) {
                // the corner lands where the offscreen target sits on the frame (§6.2)
                quad[index].x = corners[index][0];
                quad[index].y = corners[index][1];
                quad[index].u = (index == 1 || index == 4 || index == 5) ? 1.0f : 0.0f;
                quad[index].v = (index == 2 || index == 3 || index == 5) ? 1.0f : 0.0f;
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
            draw_quad(quad, draw, silhouette.shader_view.Get(), state.blend_states[0].Get());
            // the offscreen the cast sampled is released before the pass ends, so no retired target stays bound (AD-06)
            ID3D11ShaderResourceView* no_texture[2] = {nullptr, nullptr};
            context->PSSetShaderResources(0, 2, no_texture);
        }

        void AlborRenderSurface::blur_pass(const RenderTarget& source, const RenderTarget& target, ShadowUniforms uniforms, float dx, float dy, float step, float width, float height) {
            DeviceState& state = device_state();
            ID3D11DeviceContext* context = state.context.Get();
            if (context == nullptr || source.shader_view == nullptr || target.view == nullptr)
                return;
            // the direction and the step ride in the params lane, which the shader reads (§6.2)
            uniforms.params[1] = dx;
            uniforms.params[2] = dy;
            uniforms.params[3] = step / (dx != 0.0f ? width : height);
            // the pass reads the source and writes the target, which the two directions swap (§6.2)
            bind_target(target, true);
            upload_uniforms(state.shadow_uniforms.Get(), &uniforms, sizeof(uniforms));
            context->VSSetShader(state.shadow_vertex.Get(), nullptr, 0);
            context->PSSetShader(state.shadow_blur.Get(), nullptr, 0);
            context->OMSetBlendState(state.shadow_blend.Get(), nullptr, 0xffffffffu);
            ID3D11Buffer* shadow_buffer = state.shadow_uniforms.Get();
            context->PSSetConstantBuffers(0, 1, &shadow_buffer);
            ID3D11SamplerState* sampler = state.sampler.Get();
            context->PSSetSamplers(0, 1, &sampler);
            // the blur source is the second texture the fragment declares, which leaves the paint slot free (§6.2)
            ID3D11ShaderResourceView* texture = source.shader_view.Get();
            context->PSSetShaderResources(1, 1, &texture);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->Draw(4, 0);
        }

        void AlborRenderSurface::draw_quad(const BatchVertex* quad, const DrawUniforms& uniforms, ID3D11ShaderResourceView* texture, ID3D11BlendState* blend) {
            DeviceState& state = device_state();
            ID3D11Device* device = state.device.Get();
            ID3D11DeviceContext* context = state.context.Get();
            if (device == nullptr || context == nullptr || texture == nullptr)
                return;
            // the composite quad is a six vertex list, which one structured read serves (§6.2)
            ComPtr<ID3D11ShaderResourceView> vertices = create_vertex_view(device, quad, 6);
            if (vertices == nullptr)
                return;
            upload_uniforms(state.draw_uniforms.Get(), &uniforms, sizeof(uniforms));
            context->VSSetShader(state.vertex_shader.Get(), nullptr, 0);
            context->PSSetShader(state.fragment_shader.Get(), nullptr, 0);
            context->OMSetBlendState(blend, nullptr, 0xffffffffu);
            ID3D11Buffer* uniform_buffer = state.draw_uniforms.Get();
            context->VSSetConstantBuffers(0, 1, &uniform_buffer);
            context->PSSetConstantBuffers(0, 1, &uniform_buffer);
            ID3D11ShaderResourceView* vertex_resource = vertices.Get();
            context->VSSetShaderResources(0, 1, &vertex_resource);
            ID3D11SamplerState* sampler = state.sampler.Get();
            context->PSSetSamplers(0, 1, &sampler);
            context->PSSetShaderResources(0, 1, &texture);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(6, 0);
        }

        void AlborRenderSurface::encode() {
            DeviceState& state = device_state();
            ID3D11Device* device = state.device.Get();
            ID3D11DeviceContext* context = state.context.Get();
            if (device == nullptr || context == nullptr || m_target_view == nullptr)
                return;
            // the frame target is the one the begin allocated, which the clear and the draws share (§6.3)
            RenderTarget frame;
            frame.texture = m_target;
            frame.view = m_target_view;
            frame.width = std::max<UINT>(1u, static_cast<UINT>(m_metrics.pixel_size.width));
            frame.height = std::max<UINT>(1u, static_cast<UINT>(m_metrics.pixel_size.height));
            // the frame starts from a transparent clear, which an empty frame leaves (§6.1)
            bind_target(frame, true);
            context->RSSetState(state.rasterizer.Get());
            // the batch vertex buffer is one structured read both the batch draws and the composites index (§6.3)
            ComPtr<ID3D11ShaderResourceView> vertices = create_vertex_view(device, m_batch.vertices.data(), m_batch.vertices.size());
            ComPtr<ID3D11Buffer> indices = create_index_buffer(device, m_batch.indices);
            if (indices != nullptr)
                context->IASetIndexBuffer(indices.Get(), DXGI_FORMAT_R32_UINT, 0);
            // one open layer paints into its offscreen target and composites when its range ends (§6.2)
            struct OpenLayer
            {
                // the offscreen target the group paints into
                RenderTarget target;

                // the first draw after the group, which its composite waits for
                std::size_t draw_end = 0;

                // the opacity the group composites at
                float opacity = 1.0f;
            };
            std::vector<OpenLayer> open;
            // the capacity reserves every layer, so an open target keeps its address across the frame (§6.2)
            open.reserve(m_batch.layers.size());
            // the target the draw paints, which the innermost open layer or the frame is (§6.2)
            const RenderTarget* target = &frame;
            // the next layer marker to open, which the push order keeps sorted by its first draw (§6.2)
            std::size_t next_layer = 0;
            // every draw runs inside the layers whose offscreen passes are open at that point (§6.2)
            for (std::size_t draw_index = 0; draw_index <= m_batch.draws.size(); draw_index++) {
                // every layer that ends here composites into its parent before the next draw (§6.2)
                while (!open.empty() && open.back().draw_end == draw_index) {
                    // the target is read before the stack pops, and the parent is the enclosing layer or the frame (§6.2)
                    OpenLayer layer = std::move(open.back());
                    open.pop_back();
                    const RenderTarget& parent = open.empty() ? frame : open.back().target;
                    // the parent is bound before the composite draws into it, which the offscreen pass just left (§6.2)
                    bind_target(parent, false);
                    composite_layer(layer.opacity, layer.target, parent);
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
                    RenderTarget offscreen = create_render_target(device, frame.width, frame.height);
                    // a refused target draws the group into its parent instead, because no offscreen exists (§6.2)
                    if (offscreen.view != nullptr) {
                        open.push_back(OpenLayer{.target = std::move(offscreen), .draw_end = layer.draw_end, .opacity = layer.opacity});
                        target = &open.back().target;
                        bind_target(*target, true);
                    }
                }
                const BatchDraw& draw = m_batch.draws[draw_index];
                // a draw that casts a shadow composites the cast before it paints, which closes no pass on this api (§6.2)
                if (draw.shadow >= 0) {
                    const std::size_t shadow = static_cast<std::size_t>(draw.shadow);
                    if (shadow < m_batch.shadows.size())
                        composite_shadow(m_batch.shadows[shadow], *target);
                    bind_target(*target, false);
                    continue;
                }
                // a draw with no geometry has nothing to encode, which a cast only frame leaves (§6.2)
                if (vertices != nullptr && indices != nullptr && draw.index_count > 0)
                    draw_batch_draw(draw, *target, vertices.Get());
            }
            // the frame closes on the frame target, which the present copies (§6.3)
            bind_target(frame, false);
            // the frame leaves no surface texture bound, so a popped layer does not outlive the pass that made it (§6.3)
            ID3D11ShaderResourceView* no_textures[2] = {nullptr, nullptr};
            context->PSSetShaderResources(0, 2, no_textures);
            context->VSSetShaderResources(0, 1, no_textures);
        }

        // build the device, its compiled stages and the pipeline table once, which every surface shares (§6.3)
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
            // the committed container carries every stage, which a corrupt artifact answers as empty (§6.3)
            const ShaderData shaders = parse_shader_data(d3d_shader_data());
            if (shaders.vertex.empty() || shaders.fragment.empty() || shaders.fragment_exact.empty() || shaders.shadow_vertex.empty() || shaders.shadow_shape.empty() || shaders.shadow_blur.empty()) {
                // the reason is named, so a missing or corrupt artifact is not reported as a missing adapter (§6.3)
                std::fprintf(stderr, "albor-render-albor: the direct3d shader container could not be used\n");
                return false;
            }
            // the compiled stages join the device, which every pipeline binds (§6.3)
            if (state.device->CreateVertexShader(shaders.vertex.data(), shaders.vertex.size(), nullptr, state.vertex_shader.GetAddressOf()) != S_OK)
                return false;
            if (state.device->CreatePixelShader(shaders.fragment.data(), shaders.fragment.size(), nullptr, state.fragment_shader.GetAddressOf()) != S_OK)
                return false;
            if (state.device->CreatePixelShader(shaders.fragment_exact.data(), shaders.fragment_exact.size(), nullptr, state.fragment_exact.GetAddressOf()) != S_OK)
                return false;
            if (state.device->CreateVertexShader(shaders.shadow_vertex.data(), shaders.shadow_vertex.size(), nullptr, state.shadow_vertex.GetAddressOf()) != S_OK)
                return false;
            if (state.device->CreatePixelShader(shaders.shadow_shape.data(), shaders.shadow_shape.size(), nullptr, state.shadow_shape.GetAddressOf()) != S_OK)
                return false;
            if (state.device->CreatePixelShader(shaders.shadow_blur.data(), shaders.shadow_blur.size(), nullptr, state.shadow_blur.GetAddressOf()) != S_OK)
                return false;
            // every composition mode gets its blend state, which the draw record selects at encode time (§6.2)
            for (std::size_t index = 0; index < COMPOSITION_MODES; index++) {
                // the mode the index names decided the two factors the blender evaluates (§6.2)
                const BlendFactors factors = blend_factors(static_cast<CompositionMode>(index));
                state.blend_states[index] = create_blend_state(state.device.Get(), factors.source, factors.destination);
            }
            // the three modes whose result depends on destination alpha use the dual source ones (§6.2)
            state.exact_blend_states[static_cast<std::size_t>(CompositionMode::SourceIn)] = create_exact_blend_state(state.device.Get(), D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_SRC1_ALPHA, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_SRC1_ALPHA);
            state.exact_blend_states[static_cast<std::size_t>(CompositionMode::Copy)] = create_exact_blend_state(state.device.Get(), D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC1_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC1_ALPHA);
            state.exact_blend_states[static_cast<std::size_t>(CompositionMode::DestinationAtop)] = create_exact_blend_state(state.device.Get(), D3D11_BLEND_INV_DEST_ALPHA, D3D11_BLEND_INV_SRC1_COLOR, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC1_ALPHA);
            // the silhouette passes write the cast alone, so the one over zero blend leaves the clear behind (§6.2)
            state.shadow_blend = create_blend_state(state.device.Get(), D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
            // the rasterizer draws both windings, because the geometry core emits no fixed order (§6.3)
            D3D11_RASTERIZER_DESC rasterizer_description{};
            rasterizer_description.FillMode = D3D11_FILL_SOLID;
            rasterizer_description.CullMode = D3D11_CULL_NONE;
            rasterizer_description.FrontCounterClockwise = FALSE;
            rasterizer_description.DepthClipEnable = TRUE;
            rasterizer_description.ScissorEnable = FALSE;
            rasterizer_description.MultisampleEnable = FALSE;
            rasterizer_description.AntialiasedLineEnable = FALSE;
            if (state.device->CreateRasterizerState(&rasterizer_description, state.rasterizer.GetAddressOf()) != S_OK)
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
            if (state.device->CreateShaderResourceView(state.white.Get(), nullptr, state.white_view.GetAddressOf()) != S_OK)
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
            // the draw uniforms are rewritten once per draw, which the discard map allows without a stall (§6.3)
            D3D11_BUFFER_DESC uniform_description{};
            uniform_description.ByteWidth = static_cast<UINT>(sizeof(DrawUniforms));
            uniform_description.Usage = D3D11_USAGE_DYNAMIC;
            uniform_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            uniform_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (state.device->CreateBuffer(&uniform_description, nullptr, state.draw_uniforms.GetAddressOf()) != S_OK)
                return false;
            // the shadow uniforms are rewritten once per pass, which the same dynamic path serves (§6.2)
            uniform_description.ByteWidth = static_cast<UINT>(sizeof(ShadowUniforms));
            if (state.device->CreateBuffer(&uniform_description, nullptr, state.shadow_uniforms.GetAddressOf()) != S_OK)
                return false;
            // readiness is whether every stage and every pipeline slot came up, which the draw path binds (§6.2)
            const bool composed = std::all_of(state.blend_states.begin(), state.blend_states.end(), [](const ComPtr<ID3D11BlendState>& blend) { return blend != nullptr; });
            // the exact states are required, because the three modes would otherwise silently fall back (§6.2)
            const bool exact = state.exact_blend_states[static_cast<std::size_t>(CompositionMode::SourceIn)] != nullptr && state.exact_blend_states[static_cast<std::size_t>(CompositionMode::Copy)] != nullptr && state.exact_blend_states[static_cast<std::size_t>(CompositionMode::DestinationAtop)] != nullptr;
            state.built = state.device != nullptr && state.context != nullptr && state.factory != nullptr && state.vertex_shader != nullptr && state.fragment_shader != nullptr && state.fragment_exact != nullptr && state.shadow_vertex != nullptr && state.shadow_shape != nullptr && state.shadow_blur != nullptr && composed && exact && state.shadow_blend != nullptr && state.rasterizer != nullptr && state.white != nullptr && state.white_view != nullptr && state.sampler != nullptr && state.draw_uniforms != nullptr && state.shadow_uniforms != nullptr;
            return state.built;
        }
    } // namespace

    bool backend_device_available() {
        // readiness is the device and its pipelines, which the driver has not removed since (§6.2)
        return ensure_device_state() && !device_state().lost.load();
    }

    std::uint64_t d3d_presented_frames() {
        // the count is the one the present path rose, which an offscreen frame never reaches (§6.3)
        return presented_frames().load();
    }

    std::unique_ptr<RenderSurface> backend_create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) {
        // a host without the shared device state builds no surface, which readiness reports (§6.2)
        if (!ensure_device_state())
            return nullptr;
        // a surface that could not allocate its target or attach its tree is refused rather than handed out (§6.2)
        std::unique_ptr<AlborRenderSurface> surface = std::make_unique<AlborRenderSurface>(handle, metrics);
        if (!surface->valid())
            return nullptr;
        return surface;
    }
// the diagnostic state returns to what this translation unit had before the suppression (§5.3)
#pragma clang diagnostic pop
} // namespace albor::render
