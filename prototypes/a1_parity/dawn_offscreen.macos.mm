// the dawn half of prototype a1-parity: an offscreen renderer that expands each segment into a two triangle quad on
// the gpu, so one draw call carries every instance, and reads the target back so two renderers can be compared
//
// every dawn type stays in this file, so the prototype's own public header speaks no webgpu (AD-05, AD-06)

#include "prototypes/a1_parity/dawn_offscreen.h"

#import <Metal/Metal.h>

#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace albor::a1_parity
{

    namespace
    {

        // webgpu requires a copied row to start on this boundary, so a readback has to be assembled through a wider
        // staging buffer than the image is
        constexpr std::uint32_t kBytesPerRowAlignment = 256;

        // the stroke width the uniform carries when a frame has no resource set, which only happens in the readback
        // sanity check where nothing is drawn anyway
        constexpr float kStrokeWidthFallback = 1.0F;

        [[nodiscard]] std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }

        // the wgsl that does the whole point of this slice: one instanced draw call expands every segment into a quad
        // whose width is given in pixels, which is what makes the stroke cosmetic rather than geometric
        constexpr const char* kShaderWgsl = R"(
struct View {
    scale : f32,
    offset_x : f32,
    offset_y : f32,
    stroke_half_width : f32,
    pixel_width : f32,
    pixel_height : f32,
    segment_count : f32,
    pad : f32,
};

@group(0) @binding(0) var<uniform> view : View;

struct VsOut {
    @builtin(position) position : vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) corner : u32,
           @location(0) a : vec2<f32>,
           @location(1) b : vec2<f32>) -> VsOut {
    // map both endpoints through the view transform, so the same geometry can be panned and zoomed without a rebuild
    let p0 = a * view.scale + vec2<f32>(view.offset_x, view.offset_y);
    let p1 = b * view.scale + vec2<f32>(view.offset_x, view.offset_y);

    var dir = p1 - p0;
    let len = length(dir);
    if (len > 1e-6) { dir = dir / len; } else { dir = vec2<f32>(1.0, 0.0); }
    let nrm = vec2<f32>(-dir.y, dir.x) * view.stroke_half_width;

    // six corners make two triangles along the segment: p0 both sides, then p1 both sides
    let at_end = select(0.0, 1.0, corner >= 2u);
    let side = select(-1.0, 1.0, corner == 1u || corner == 4u || corner == 5u);
    let p = mix(p0, p1, at_end) + nrm * side;

    var out : VsOut;
    // pixel coordinates with +y downwards, which is the orientation skia reads back in, so no flip is needed anywhere
    out.position = vec4<f32>(p.x / view.pixel_width * 2.0 - 1.0, 1.0 - p.y / view.pixel_height * 2.0, 0.0, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.2509804, 0.1254902, 1.0);
}
)";

    } // namespace

    namespace
    {

        // print whatever dawn rejected inside the scope. this is best effort only: a deliberately invalid buffer was
        // put through this scope and nothing was reported, so the absence of a message is not evidence of absence
        void report_scoped_errors(wgpu::Instance instance, wgpu::Device device, const char* what) {
            std::atomic<bool> done{false};
            const auto on_error = [&](wgpu::PopErrorScopeStatus status, wgpu::ErrorType, wgpu::StringView message) {
                if (status != wgpu::PopErrorScopeStatus::Success) {
                    std::fprintf(stderr, "[dawn] %s rejected: %.*s\n", what, static_cast<int>(message.length), message.data);
                }
                done.store(true, std::memory_order_release);
            };
            device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents, on_error);
            while (!done.load(std::memory_order_acquire)) {
                instance.ProcessEvents();
                std::this_thread::yield();
            }
        }

    } // namespace

    class DawnOffscreenRenderer final : public OffscreenRenderer
    {
    public:
        DawnOffscreenRenderer() :
            m_state(new State()) {}

        ~DawnOffscreenRenderer() override {
            if (m_state->staging) {
                m_state->staging.Destroy();
            }
            delete m_state;
        }

        [[nodiscard]] Engine engine() const override { return Engine::dawn; }

        [[nodiscard]] bool create(std::size_t pixel_width, std::size_t pixel_height) override;

        void render(const RenderRequest& request) override;

        [[nodiscard]] bool read_back(PixelBuffer& out) const override;

    private:
        struct State
        {
            wgpu::Instance instance;
            wgpu::Adapter adapter;
            wgpu::Device device;
            wgpu::Queue queue;

            // the target is resolved from a multisampled colour buffer, so what is read back is antialiased the way
            // skia's cpu rasteriser is
            wgpu::Texture target;
            wgpu::TextureView target_view;
            wgpu::Texture msaa_target;
            wgpu::TextureView msaa_view;
            wgpu::ShaderModule shader;
            wgpu::RenderPipeline pipeline;
            wgpu::BindGroupLayout bind_layout;
            wgpu::BindGroup bind_group;
            wgpu::Buffer vertex_buffer;
            wgpu::Buffer uniform_buffer;

            std::size_t pixel_width = 0;
            std::size_t pixel_height = 0;

            // the per instance endpoints, resident for the life of the renderer so a repeat costs no upload
            std::vector<std::uint8_t> vertices;

            // how many frames have gone through, which is what brackets the error scope to the first one
            std::size_t frames_drawn = 0;

            // whether the readback path has already reported a rejection, so the scope is only paid for once
            bool copy_scope_done = false;

            // the readback staging buffer, which is mutable because reading back does not change the drawing
            mutable std::mutex staging_mutex;
            mutable wgpu::Buffer staging = nullptr;
            mutable std::uint32_t staging_bytes_per_row = 0;
            mutable std::uint64_t staging_size = 0;
            mutable std::vector<std::uint8_t> staging_bytes;
        };

        // the implementation state is held behind a pointer so no webgpu type reaches this file's own header
        State* m_state = nullptr;

        [[nodiscard]] bool build_pipeline();
        void upload_segments(const albor::a1_dawn::ResourceSet& resources);
    };

    bool DawnOffscreenRenderer::create(std::size_t pixel_width, std::size_t pixel_height) {
        m_state->pixel_width = pixel_width;
        m_state->pixel_height = pixel_height;

        // an offscreen target needs a device and a queue, but no window and no window server, which is the whole reason
        // this renderer exists: the windowed path cannot be run reproducibly on this platform
        wgpu::InstanceDescriptor instance_desc{};
        m_state->instance = wgpu::CreateInstance(&instance_desc);

        std::atomic<bool> adapter_ready{false};
        m_state->instance.RequestAdapter(nullptr, wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::RequestAdapterStatus, wgpu::Adapter adapter, wgpu::StringView) {
            m_state->adapter = std::move(adapter);
            adapter_ready.store(true, std::memory_order_release);
        });
        while (!adapter_ready.load(std::memory_order_acquire)) {
            m_state->instance.ProcessEvents();
            std::this_thread::yield();
        }

        wgpu::DeviceDescriptor device_desc{};
        m_state->device = m_state->adapter.CreateDevice(&device_desc);
        if (!m_state->device) {
            return false;
        }
        m_state->queue = m_state->device.GetQueue();

        // a plain rgba target with a copy source, which is the only usage the readback needs
        wgpu::TextureDescriptor target_desc{};
        target_desc.size.width = static_cast<std::uint32_t>(pixel_width);
        target_desc.size.height = static_cast<std::uint32_t>(pixel_height);
        target_desc.size.depthOrArrayLayers = 1;
        target_desc.format = wgpu::TextureFormat::RGBA8Unorm;
        target_desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        m_state->target = m_state->device.CreateTexture(&target_desc);
        m_state->target_view = m_state->target.CreateView();

        // the multisampled buffer the pass renders into, and the plain target it resolves into
        wgpu::TextureDescriptor msaa_desc = target_desc;
        msaa_desc.sampleCount = 4;
        msaa_desc.usage = wgpu::TextureUsage::RenderAttachment;
        m_state->msaa_target = m_state->device.CreateTexture(&msaa_desc);
        m_state->msaa_view = m_state->msaa_target.CreateView();

        // bracket the pipeline build, so a rejected shader or descriptor is reported instead of drawing nothing
        m_state->device.PushErrorScope(wgpu::ErrorFilter::Validation);
        const bool built = build_pipeline();
        report_scoped_errors(m_state->instance, m_state->device, "pipeline build");
        return built;
    }

    bool DawnOffscreenRenderer::build_pipeline() {
        // the uniform every instance reads, which is the view transform plus the target size the shader needs
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        wgpu::BindGroupLayoutDescriptor bind_layout_desc{};
        bind_layout_desc.entries = &entry;
        bind_layout_desc.entryCount = 1;
        m_state->bind_layout = m_state->device.CreateBindGroupLayout(&bind_layout_desc);

        wgpu::ShaderSourceWGSL source{};
        source.code = kShaderWgsl;
        wgpu::ShaderModuleDescriptor shader_desc{};
        shader_desc.nextInChain = &source;
        m_state->shader = m_state->device.CreateShaderModule(&shader_desc);

        // ask the shader what the compiler made of it, because this build's error scopes do not report and an invalid
        // pipeline makes a render pass silently discard everything it is given
        {
            std::atomic<bool> done{false};
            const auto on_info = [&](wgpu::CompilationInfoRequestStatus status, wgpu::CompilationInfo const* info) {
                if (status == wgpu::CompilationInfoRequestStatus::Success && info != nullptr) {
                    for (std::size_t i = 0; i < info->messageCount; i++) {
                        const wgpu::CompilationMessage& message = info->messages[i];
                        std::fprintf(stderr, "[dawn] shader line %llu:%llu type %u: %.*s\n", static_cast<unsigned long long>(message.lineNum), static_cast<unsigned long long>(message.linePos), static_cast<unsigned>(message.type), static_cast<int>(message.message.length), message.message.data);
                    }
                }
                done.store(true, std::memory_order_release);
            };
            m_state->shader.GetCompilationInfo(wgpu::CallbackMode::AllowProcessEvents, on_info);
            while (!done.load(std::memory_order_acquire)) {
                m_state->instance.ProcessEvents();
                std::this_thread::yield();
            }
        }

        const wgpu::ShaderModule shader = m_state->shader;

        // four floats per instance: the two endpoints of the segment, drawn as one instanced quad
        // two vec2 endpoints per instance, described as one array, because a vertex layout reads its attributes
        // through a single pointer and separately declared locals are not an array
        wgpu::VertexAttribute attributes[2]{};
        attributes[0].format = wgpu::VertexFormat::Float32x2;
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x2;
        attributes[1].offset = 8;
        attributes[1].shaderLocation = 1;

        wgpu::VertexBufferLayout buffer_layout{};
        buffer_layout.arrayStride = sizeof(float) * 4U;
        buffer_layout.stepMode = wgpu::VertexStepMode::Instance;
        buffer_layout.attributes = attributes;
        buffer_layout.attributeCount = 2;

        wgpu::VertexState vertex_state{};
        vertex_state.module = shader;
        vertex_state.entryPoint = "vs_main";
        vertex_state.buffers = &buffer_layout;
        vertex_state.bufferCount = 1;

        wgpu::ColorTargetState color_target{};
        color_target.format = wgpu::TextureFormat::RGBA8Unorm;
        wgpu::FragmentState fragment_state{};
        fragment_state.module = shader;
        fragment_state.entryPoint = "fs_main";
        fragment_state.targets = &color_target;
        fragment_state.targetCount = 1;

        wgpu::PrimitiveState primitive_state{};
        primitive_state.topology = wgpu::PrimitiveTopology::TriangleList;
        primitive_state.cullMode = wgpu::CullMode::None;

        // four samples, which is what makes the stroke edge comparable to a rasteriser that antialiases
        wgpu::MultisampleState multisample{};
        multisample.count = 4;

        wgpu::PipelineLayoutDescriptor layout_desc{};
        layout_desc.bindGroupLayouts = &m_state->bind_layout;
        layout_desc.bindGroupLayoutCount = 1;
        const wgpu::PipelineLayout layout = m_state->device.CreatePipelineLayout(&layout_desc);

        // a fully opaque stroke needs no blend at all, and blending premultiplied alphas against the clear is what
        // made the stroke come out at 73 27 23 instead of 255 64 32
        wgpu::RenderPipelineDescriptor pipeline_desc{};
        pipeline_desc.layout = layout;
        pipeline_desc.vertex = vertex_state;
        pipeline_desc.fragment = &fragment_state;
        pipeline_desc.primitive = primitive_state;
        pipeline_desc.multisample = multisample;

        // this build's error scopes are not a usable signal, so the pipeline is created through its async path and
        // whatever dawn thinks of it is printed directly: an invalid pipeline makes a render pass discard everything
        {
            std::atomic<bool> done{false};
            m_state->device.CreateRenderPipelineAsync(&pipeline_desc, wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView message) {
                if (status == wgpu::CreatePipelineAsyncStatus::Success) {
                    m_state->pipeline = std::move(pipeline);
                }
                else {
                    std::fprintf(stderr, "[dawn] the render pipeline was rejected: %.*s\n", static_cast<int>(message.length), message.data);
                }
                done.store(true, std::memory_order_release);
            });
            while (!done.load(std::memory_order_acquire)) {
                m_state->instance.ProcessEvents();
                std::this_thread::yield();
            }
        }

        // the uniform buffer and the bind group that reads it
        wgpu::BufferDescriptor uniform_desc{};
        uniform_desc.size = 32;
        uniform_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        m_state->uniform_buffer = m_state->device.CreateBuffer(&uniform_desc);

        wgpu::BindGroupEntry bind_entry{};
        bind_entry.binding = 0;
        bind_entry.buffer = m_state->uniform_buffer;
        bind_entry.offset = 0;
        bind_entry.size = wgpu::kWholeSize;
        wgpu::BindGroupDescriptor bind_desc{};
        bind_desc.layout = m_state->bind_layout;
        bind_desc.entries = &bind_entry;
        bind_desc.entryCount = 1;
        m_state->bind_group = m_state->device.CreateBindGroup(&bind_desc);

        return true;
    }

    void DawnOffscreenRenderer::upload_segments(const albor::a1_dawn::ResourceSet& resources) {
        // both endpoints of every segment, packed as two vec2 attributes of one instance
        m_state->vertices.resize(resources.lines.size() * 16);
        std::memcpy(m_state->vertices.data(), resources.lines.data(), m_state->vertices.size());

        wgpu::BufferDescriptor vertex_desc{};
        vertex_desc.size = m_state->vertices.size();
        vertex_desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        m_state->vertex_buffer = m_state->device.CreateBuffer(&vertex_desc);

        m_state->queue.WriteBuffer(m_state->vertex_buffer, 0, m_state->vertices.data(), m_state->vertices.size());
    }

    void DawnOffscreenRenderer::render(const RenderRequest& request) {
        if (m_state->pipeline == nullptr) {
            return;
        }

        // upload once, then reuse: a repeat has to cost the same as the first frame, or the comparison is unfair
        if (request.resources != nullptr && m_state->vertex_buffer == nullptr && !request.resources->lines.empty()) {
            upload_segments(*request.resources);
            if (m_state->vertex_buffer == nullptr) {
                std::fprintf(stderr, "[dawn] render: the vertex buffer could not be created\n");
                return;
            }
        }

        // the view transform and the stroke width go to the uniform, which is the only per frame upload
        float uniform[8]{};
        uniform[0] = request.view.scale;
        uniform[1] = request.view.offset_x;
        uniform[2] = request.view.offset_y;
        uniform[3] = request.resources == nullptr ? kStrokeWidthFallback * 0.5F : request.resources->stroke_width * 0.5F;
        uniform[4] = static_cast<float>(m_state->pixel_width);
        uniform[5] = static_cast<float>(m_state->pixel_height);
        uniform[6] = static_cast<float>(request.instances);
        uniform[7] = 0.0F;

        m_state->queue.WriteBuffer(m_state->uniform_buffer, 0, uniform, sizeof(uniform));

        // only the first frame is bracketed by an error scope, because a rejected draw call draws nothing and this
        // build cannot otherwise say so
        const bool first_frame = m_state->frames_drawn < 3;
        if (first_frame) {
            m_state->device.PushErrorScope(wgpu::ErrorFilter::Validation);
        }

        const wgpu::CommandEncoder encoder = m_state->device.CreateCommandEncoder();

        // the clear is the same colour skia clears to, so an empty frame compares equal across renderers
        wgpu::RenderPassColorAttachment color{};
        color.view = m_state->msaa_view;
        color.resolveTarget = m_state->target_view;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {kClearRed, kClearGreen, kClearBlue, 1.0};

        wgpu::RenderPassDescriptor pass_desc{};
        pass_desc.colorAttachments = &color;
        pass_desc.colorAttachmentCount = 1;
        const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);

        pass.SetPipeline(m_state->pipeline);
        pass.SetBindGroup(0, m_state->bind_group);

        // a frame with no geometry must not bind a vertex buffer that was never created: binding a null buffer
        // invalidates the whole render pass, which silently drops the clear as well and leaves the target untouched
        if (m_state->vertex_buffer != nullptr && request.instances > 0) {
            pass.SetVertexBuffer(0, m_state->vertex_buffer);
            // six vertices per instance is the two triangles of one quad, which is the whole instancing claim
            pass.Draw(6, static_cast<std::uint32_t>(request.instances), 0, 0);
        }
        pass.End();

        wgpu::CommandBuffer commands = encoder.Finish();
        m_state->queue.Submit(1, &commands);

        if (first_frame) {
            report_scoped_errors(m_state->instance, m_state->device, "frame");
        }
        m_state->frames_drawn++;
    }

    bool DawnOffscreenRenderer::read_back(PixelBuffer& out) const {
        const std::lock_guard<std::mutex> lock(m_state->staging_mutex);

        // webgpu wants rows aligned, so the staging rows are wider than the image and get trimmed on the way out
        m_state->staging_bytes_per_row = align_up(static_cast<std::uint32_t>(m_state->pixel_width * 4), kBytesPerRowAlignment);
        const std::uint64_t total = static_cast<std::uint64_t>(m_state->staging_bytes_per_row) * m_state->pixel_height;
        m_state->staging_size = total;

        if (!m_state->staging) {
            wgpu::BufferDescriptor staging_desc{};
            staging_desc.size = total;
            staging_desc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
            m_state->staging = m_state->device.CreateBuffer(&staging_desc);
        }

        wgpu::CommandEncoder encoder = m_state->device.CreateCommandEncoder();

        wgpu::TexelCopyTextureInfo source{};
        source.texture = m_state->target;

        // the copy layout is a member of the destination in this dawn build, and the rows are the aligned width
        wgpu::TexelCopyBufferLayout copy_layout{};
        copy_layout.offset = 0;
        copy_layout.bytesPerRow = m_state->staging_bytes_per_row;
        // left undefined on purpose: a single layer copy does not need it, and this build appears to drop the whole
        // copy when it is set

        wgpu::TexelCopyBufferInfo destination{};
        destination.layout = copy_layout;
        destination.buffer = m_state->staging;

        wgpu::Extent3D extent{};
        extent.width = static_cast<std::uint32_t>(m_state->pixel_width);
        extent.height = static_cast<std::uint32_t>(m_state->pixel_height);
        extent.depthOrArrayLayers = 1;
        encoder.CopyTextureToBuffer(&source, &destination, &extent);

        wgpu::CommandBuffer commands = encoder.Finish();
        m_state->queue.Submit(1, &commands);

        // the first copy is bracketed too, because a rejected copy is silent in this build and looks exactly like an
        // empty target
        if (!m_state->copy_scope_done) {
            m_state->device.PushErrorScope(wgpu::ErrorFilter::Validation);
            report_scoped_errors(m_state->instance, m_state->device, "texture to buffer copy");
            m_state->copy_scope_done = true;
        }

        // a map request is ordered after every submission made before it, so polling this callback is also how the
        // slice waits for the frame to finish: there is no other completion signal in this dawn build
        std::atomic<bool> mapped{false};
        std::atomic<bool> failed{false};
        const auto on_mapped = [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
            failed.store(status != wgpu::MapAsyncStatus::Success, std::memory_order_release);
            mapped.store(true, std::memory_order_release);
        };
        // the map and the range are both asked for the exact byte count: this dawn build returns a null range when the
        // whole-size sentinel is used against a buffer this large
        m_state->staging.MapAsync(wgpu::MapMode::Read, 0, total, wgpu::CallbackMode::AllowProcessEvents, on_mapped);
        while (!mapped.load(std::memory_order_acquire)) {
            m_state->instance.ProcessEvents();
            std::this_thread::yield();
        }
        if (failed.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "[dawn] readback: the map request did not succeed\n");
            return false;
        }

        // copy the staging bytes out through the copy-out api rather than a mapped pointer: this dawn build hands
        // back a null mapped range for a buffer this large even while it reports itself mapped
        m_state->staging_bytes.resize(total);
        // the status converts to bool, where true means the copy succeeded
        const wgpu::ConvertibleStatus copied = m_state->staging.ReadMappedRange(0, m_state->staging_bytes.data(), total);
        if (!copied) {
            std::fprintf(stderr, "[dawn] readback: copying the mapped range out failed\n");
            m_state->staging.Unmap();
            return false;
        }

        // trim the aligned staging rows down to tightly packed image rows
        out.allocate(m_state->pixel_width, m_state->pixel_height);
        for (std::size_t row = 0; row < m_state->pixel_height; row++) {
            std::memcpy(out.rgba.data() + row * out.stride(), m_state->staging_bytes.data() + row * m_state->staging_bytes_per_row, out.stride());
        }
        m_state->staging.Unmap();
        return true;
    }

    OffscreenRenderer* make_dawn_renderer() { return new DawnOffscreenRenderer(); }

} // namespace albor::a1_parity
