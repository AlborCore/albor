// the dawn context of one window: a metal surface, one instanced line pipeline, and one draw call per frame
// every dawn and webgpu type stays inside this file, so no webgpu header reaches a public header (AD-06)

#include "prototypes/a1_dawn/dawn_renderer.h"

#import "prototypes/a2/native_surface.macos.h"

#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <webgpu/webgpu_cpp.h>

namespace albor::a1_dawn
{

    namespace
    {

        // the clock this slice measures with
        using Clock = std::chrono::steady_clock;

        // read the clock as milliseconds since the steady clock epoch
        double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

        // one quad per segment, expanded in the vertex shader from the per-instance endpoints, which is what
        // makes a million segments one draw call instead of two hundred and twenty
        constexpr const char* LINE_SHADER = R"(
struct Uniforms {
    scale : vec2<f32>,
    offset : vec2<f32>,
    half_width : f32,
    aspect_pad : vec3<f32>,
};

@group(0) @binding(0) var<uniform> u : Uniforms;

struct VSOut {
    @builtin(position) position : vec4<f32>,
};

@vertex
fn vs(@location(0) start : vec2<f32>,
      @location(1) end : vec2<f32>,
      @builtin(vertex_index) vi : u32) -> VSOut {
    let a = u.offset + u.scale * start;
    let b = u.offset + u.scale * end;
    let d = b - a;
    let len = max(length(d), 1e-6);
    let n = vec2<f32>(-d.y, d.x) / len * u.half_width;

    // six vertices make two triangles out of the four corners of the quad
    let p0 = a + n;
    let p1 = a - n;
    let p2 = b - n;
    let p3 = b + n;

    var corner = p0;
    if (vi == 1u) { corner = p1; }
    if (vi == 2u) { corner = p2; }
    if (vi == 3u) { corner = p0; }
    if (vi == 4u) { corner = p2; }
    if (vi == 5u) { corner = p3; }

    var out : VSOut;
    out.position = vec4<f32>(corner, 0.0, 1.0);
    return out;
}

@fragment
fn fs() -> @location(0) vec4<f32> {
    return vec4<f32>(0.92, 0.95, 1.0, 1.0);
}
)";

    } // namespace

    struct DawnContext::State
    {
        // the dawn objects, which are the only place a webgpu type is allowed to appear
        wgpu::Instance instance;
        wgpu::Adapter adapter;
        wgpu::Device device;
        wgpu::Surface surface;
        wgpu::Queue queue;
        wgpu::ShaderModule shader;
        wgpu::RenderPipeline pipeline;
        wgpu::BindGroupLayout bind_layout;
        wgpu::PipelineLayout pipeline_layout;
        wgpu::BindGroup bind_group;
        wgpu::Buffer uniform_buffer;
        wgpu::Buffer vertex_buffer;
        wgpu::TextureFormat surface_format = wgpu::TextureFormat::BGRA8Unorm;

        // the surface size and the largest instance count the vertex buffer was sized for
        std::size_t surface_width = 0;
        std::size_t surface_height = 0;
        std::size_t capacity = 0;

        // the texture the current frame draws into, which the surface hands out once per frame
        wgpu::SurfaceTexture surface_texture;

        // what the current frame has cost so far
        std::size_t frames_presented = 0;
        std::size_t draw_calls = 0;
        std::uint64_t frame_index = 0;
        double hold_ms = 0.0;
        double last_submit = 0.0;
        bool has_last_submit = false;
        FrameTiming timing{};
    };

    DawnContext::DawnContext() :
        m_state(new State()) {}

    DawnContext::~DawnContext() {
        // drain the device before the objects it owns go away, because the wire client needs its tick
        if (m_state->device) {
            m_state->device.Tick();
        }

        // release the renderer state
        delete m_state;
        m_state = nullptr;
    }

    bool DawnContext::create(albor::a2::PlatformWindow& window) {
        // resolve the native surface, which owns the metal layer the swap chain presents into
        albor::a2::NativeSurface* native = window.native_surface();
        if (native == nullptr || native->layer == nil) {
            return false;
        }

        // bring up an instance and ask it for an adapter, which in this dawn is a callback rather than a call
        wgpu::InstanceDescriptor instance_desc{};
        m_state->instance = wgpu::CreateInstance(&instance_desc);

        std::atomic<bool> adapter_done{false};
        std::atomic<bool> adapter_ok{false};
        wgpu::RequestAdapterOptions adapter_options{};
        wgpu::Adapter requested;
        wgpu::Future adapter_future = m_state->instance.RequestAdapter(&adapter_options, wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            static_cast<void>(message);
            requested = std::move(adapter);
            adapter_ok.store(status == wgpu::RequestAdapterStatus::Success, std::memory_order_release);
            adapter_done.store(true, std::memory_order_release);
        });

        // timed waits need an instance feature this slice does not enable, so the callback is delivered by
        // processing events until it fires
        static_cast<void>(adapter_future);
        while (!adapter_done.load(std::memory_order_acquire)) {
            m_state->instance.ProcessEvents();
            std::this_thread::yield();
        }
        if (!adapter_ok.load(std::memory_order_acquire)) {
            return false;
        }
        m_state->adapter = requested;

        // ask the adapter for a device, which this dawn hands back synchronously
        wgpu::DeviceDescriptor device_desc{};
        m_state->device = m_state->adapter.CreateDevice(&device_desc);
        if (!m_state->device) {
            return false;
        }
        m_state->queue = m_state->device.GetQueue();

        // create the surface from the metal layer, which is the macOS presentation path of AD-16
        wgpu::SurfaceSourceMetalLayer source{};
        source.layer = static_cast<void*>(native->layer);
        wgpu::SurfaceDescriptor surface_desc{};
        surface_desc.nextInChain = &source;
        m_state->surface = m_state->instance.CreateSurface(&surface_desc);
        if (!m_state->surface) {
            return false;
        }

        // this dawn exposes no preferred canvas format on the adapter, so ask the surface what it supports
        // and take the first format it reports, which is the documented fallback
        wgpu::SurfaceCapabilities capabilities{};
        m_state->surface.GetCapabilities(m_state->adapter, &capabilities);
        if (capabilities.formatCount == 0) {
            return false;
        }
        m_state->surface_format = capabilities.formats[0];

        // read the window metrics, which are authoritative for the surface size (§9.1)
        const albor::a2::Metrics metrics = window.metrics();
        m_state->surface_width = metrics.pixel_width;
        m_state->surface_height = metrics.pixel_height;

        // configure the surface for the size the window reports
        wgpu::SurfaceConfiguration config{};
        config.device = m_state->device;
        config.format = m_state->surface_format;
        config.usage = wgpu::TextureUsage::RenderAttachment;
        config.width = static_cast<std::uint32_t>(m_state->surface_width);
        config.height = static_cast<std::uint32_t>(m_state->surface_height);
        m_state->surface.Configure(&config);

        // compile the line shader, which is the whole point of the prototype: a million quads in one call
        wgpu::ShaderSourceWGSL shader_source{};
        shader_source.code = LINE_SHADER;
        wgpu::ShaderModuleDescriptor shader_desc{};
        shader_desc.nextInChain = &shader_source;
        m_state->shader = m_state->device.CreateShaderModule(&shader_desc);

        // the pipeline reads one uniform for the view transform and nothing else
        wgpu::BindGroupLayoutEntry uniform_entry{};
        uniform_entry.binding = 0;
        uniform_entry.visibility = wgpu::ShaderStage::Vertex;
        uniform_entry.buffer.type = wgpu::BufferBindingType::Uniform;
        wgpu::BindGroupLayoutDescriptor bind_layout_desc{};
        bind_layout_desc.entries = &uniform_entry;
        bind_layout_desc.entryCount = 1;
        m_state->bind_layout = m_state->device.CreateBindGroupLayout(&bind_layout_desc);

        wgpu::PipelineLayoutDescriptor pipeline_layout_desc{};
        pipeline_layout_desc.bindGroupLayouts = &m_state->bind_layout;
        pipeline_layout_desc.bindGroupLayoutCount = 1;
        m_state->pipeline_layout = m_state->device.CreatePipelineLayout(&pipeline_layout_desc);

        // per-instance vertex data: two vec2 endpoints, sixteen bytes with no padding
        wgpu::VertexAttribute attribute0{};
        attribute0.format = wgpu::VertexFormat::Float32x2;
        attribute0.offset = 0;
        attribute0.shaderLocation = 0;
        wgpu::VertexAttribute attribute1{};
        attribute1.format = wgpu::VertexFormat::Float32x2;
        attribute1.offset = 8;
        attribute1.shaderLocation = 1;

        // both attributes advance per instance, which is what makes one call carry every segment
        wgpu::VertexStepMode step_mode = wgpu::VertexStepMode::Instance;
        wgpu::VertexBufferLayout buffer_layout{};
        buffer_layout.arrayStride = sizeof(float) * 4U;
        buffer_layout.stepMode = step_mode;
        buffer_layout.attributes = &attribute0;
        buffer_layout.attributeCount = 2;

        wgpu::VertexState vertex_state{};
        vertex_state.module = m_state->shader;
        vertex_state.entryPoint = "vs";
        vertex_state.buffers = &buffer_layout;
        vertex_state.bufferCount = 1;

        wgpu::ColorTargetState color_target{};
        color_target.format = m_state->surface_format;
        wgpu::FragmentState fragment_state{};
        fragment_state.module = m_state->shader;
        fragment_state.entryPoint = "fs";
        fragment_state.targets = &color_target;
        fragment_state.targetCount = 1;

        wgpu::PrimitiveState primitive_state{};
        primitive_state.topology = wgpu::PrimitiveTopology::TriangleList;
        primitive_state.cullMode = wgpu::CullMode::None;

        wgpu::RenderPipelineDescriptor pipeline_desc{};
        pipeline_desc.layout = m_state->pipeline_layout;
        pipeline_desc.vertex = vertex_state;
        pipeline_desc.fragment = &fragment_state;
        pipeline_desc.primitive = primitive_state;
        m_state->pipeline = m_state->device.CreateRenderPipeline(&pipeline_desc);

        // the uniform buffer holds the view transform and the half width of a cosmetic line
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
        bind_desc.entries = &bind_entry;
        bind_desc.entryCount = 1;
        bind_desc.layout = m_state->bind_layout;
        m_state->bind_group = m_state->device.CreateBindGroup(&bind_desc);
        return true;
    }

    bool DawnContext::reserve_instances(std::size_t instances) {
        // a buffer needs a capacity
        if (instances == 0) {
            return false;
        }

        // the buffer is sized once for the largest batch, so an append never reallocates it
        wgpu::BufferDescriptor desc{};
        desc.size = instances * sizeof(float) * 4U;
        desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        m_state->vertex_buffer = m_state->device.CreateBuffer(&desc);
        if (!m_state->vertex_buffer) {
            return false;
        }
        m_state->capacity = instances;
        return true;
    }

    bool DawnContext::upload(const ResourceSet& set, std::size_t already_written) {
        // nothing to upload means nothing to do
        if (set.lines.empty() || !m_state->vertex_buffer) {
            return false;
        }

        // never write past the capacity the buffer was reserved with
        if (set.lines.size() > m_state->capacity) {
            return false;
        }

        // the generator is prefix deterministic, so an append only has to write the tail it added
        const std::size_t first = std::min(already_written, set.lines.size());
        const std::size_t count = set.lines.size() - first;
        if (count == 0) {
            return true;
        }

        // write the new tail straight into the vertex buffer, which is what a vertex buffer update costs here
        m_state->queue.WriteBuffer(m_state->vertex_buffer, static_cast<std::uint64_t>(first) * sizeof(Segment), set.lines.data() + first, count * sizeof(Segment));
        return true;
    }

    bool DawnContext::begin_frame() {
        // a frame needs a configured surface
        if (!m_state->surface) {
            return false;
        }

        // reset the frame's measurements
        m_state->timing = FrameTiming{};
        m_state->timing.index = m_state->frame_index;

        // acquiring the texture is the swap chain's wait, which is the cost a1.3 called t_acquire
        const double acquire_start = now_ms();
        m_state->surface.GetCurrentTexture(&m_state->surface_texture);
        m_state->timing.acquire_ms = now_ms() - acquire_start;
        return static_cast<bool>(m_state->surface_texture.texture);
    }

    void DawnContext::draw(const ViewTransform& view, std::size_t instances) {
        // nothing to draw into means the frame never started
        if (!m_state->surface_texture.texture || instances == 0) {
            return;
        }

        // start the recording clock, which is what the record cost is measured against
        const double record_start = now_ms();

        // write the view transform into the uniform buffer, which is the only per-frame cpu work
        struct Uniforms
        {
            float scale[2];
            float offset[2];
            float half_width;
            float pad[3];
        };
        Uniforms uniforms{};
        // the shader works in pixel space, so flip y once here rather than in every vertex
        uniforms.scale[0] = view.scale;
        uniforms.scale[1] = -view.scale;
        uniforms.offset[0] = view.offset_x;
        uniforms.offset[1] = static_cast<float>(m_state->surface_height) - view.offset_y;
        uniforms.half_width = 0.5F;
        m_state->queue.WriteBuffer(m_state->uniform_buffer, 0, &uniforms, sizeof(uniforms));

        // encode one render pass that clears and then draws every segment as one instanced call
        wgpu::RenderPassColorAttachment color{};
        color.view = m_state->surface_texture.texture.CreateView();
        color.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;

        wgpu::RenderPassDescriptor pass{};
        pass.colorAttachments = &color;
        pass.colorAttachmentCount = 1;

        wgpu::CommandEncoder encoder = m_state->device.CreateCommandEncoder();
        wgpu::RenderPassEncoder encoder_pass = encoder.BeginRenderPass(&pass);
        encoder_pass.SetPipeline(m_state->pipeline);
        encoder_pass.SetBindGroup(0, m_state->bind_group, 0, nullptr);
        if (instances > 0) {
            encoder_pass.SetVertexBuffer(0, m_state->vertex_buffer, 0, sizeof(float) * 4U * instances);
        }

        // six vertices per instance make the quad, and every instance is one segment
        encoder_pass.Draw(6, static_cast<std::uint32_t>(instances), 0, 0);
        encoder_pass.End();
        m_state->draw_calls++;

        // hand the work to the queue, which is the last cpu side cost of the frame
        wgpu::CommandBuffer commands = encoder.Finish();
        m_state->queue.Submit(1, &commands);
        m_state->timing.record_ms = now_ms() - record_start;
    }

    void DawnContext::end_frame() {
        // a frame that never started has nothing to present
        if (!m_state->surface_texture.texture) {
            return;
        }

        // start the present clock
        const double present_start = now_ms();

        // present the frame, which is the swap chain's own commit
        m_state->surface.Present();
        m_state->timing.present_ms = now_ms() - present_start;

        // apply the injected hold, which is how w3 models a gpu presentation stall
        if (m_state->hold_ms > 0.0) {
            const double hold_start = now_ms();
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(m_state->hold_ms));
            m_state->timing.hold_ms += now_ms() - hold_start;
        }

        // record the interval since the previous present, which is the cadence this window reached
        const double now = now_ms();
        m_state->timing.interval_ms = m_state->has_last_submit ? now - m_state->last_submit : 0.0;
        m_state->last_submit = now;
        m_state->has_last_submit = true;
        m_state->frames_presented++;
        m_state->frame_index++;
    }

    void DawnContext::set_presentation_hold_ms(double hold_ms) { m_state->hold_ms = hold_ms; }

    std::size_t DawnContext::frames_presented() const { return m_state->frames_presented; }

    const FrameTiming& DawnContext::last_timing() const { return m_state->timing; }

    std::size_t DawnContext::draw_calls() const { return m_state->draw_calls; }

    const char* DawnContext::backend() const { return "dawn-webgpu-metal"; }

} // namespace albor::a1_dawn
