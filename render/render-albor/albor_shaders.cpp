#include "render/render-albor/albor_shaders.h"

namespace albor::render
{
    namespace
    {
        // the metal shading language source, which the build keeps beside the backend it shades (AD-16)
        constexpr const char* SHADER_SOURCE = R"MSL(
#include <metal_stdlib>

using namespace metal;

// one vertex of a frame batch, which the portable geometry core emits
struct BatchVertex
{
    float2 position;
    float lateral;
    float longitudinal;
    float2 uv;
};

// the per draw state, which the backend writes once per recorded draw
struct DrawUniforms
{
    float4 color;
    float4 inverse0;
    float4 inverse1;
    float4 extentScale;
    float2 targetSize;
    float ramp;
    int kind;
    int scissorEnabled;
    int hasTexture;
};

// the varying a rasterized fragment carries, which interpolates the coverage seeds
struct Varyings
{
    float4 position [[position]];
    float lateral;
    float longitudinal;
    float2 uv;
};

// map a device space position onto the clip space the rasterizer expects, with the y axis flipped
static float4 project(float2 position, float2 targetSize) {
    float2 normalized = position / targetSize;
    float2 clip = float2(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0);
    return float4(clip, 0.0, 1.0);
}

// rasterize one batch vertex into clip space, carrying the coverage seeds through
vertex Varyings albor_vertex(uint vertexId [[vertex_id]], constant BatchVertex* vertices [[buffer(0)]], constant DrawUniforms& uniforms [[buffer(1)]]) {
    BatchVertex input = vertices[vertexId];
    Varyings out;
    out.position = project(input.position, uniforms.targetSize);
    out.lateral = input.lateral;
    out.longitudinal = input.longitudinal;
    out.uv = input.uv;
    return out;
}

// return the coverage a fragment carries, which the draw kind turns into alpha
static float coverage(Varyings in, constant DrawUniforms& uniforms) {
    float longitudinal = clamp(in.longitudinal, 0.0, 1.0);
    if (uniforms.kind == 1) {
        float lateral = clamp((1.0 - abs(in.lateral * 2.0 - 1.0)) * uniforms.ramp, 0.0, 1.0);
        return lateral * longitudinal;
    }
    return clamp(in.lateral, 0.0, 1.0) * longitudinal;
}

// return the clip mask a fragment carries, which the inverse matrix maps onto the clip box centre
static float scissor_mask(float2 position, constant DrawUniforms& uniforms) {
    if (uniforms.scissorEnabled == 0) {
        return 1.0;
    }
    float x = uniforms.inverse0.x * position.x + uniforms.inverse0.y * position.y + uniforms.inverse0.z;
    float y = uniforms.inverse1.x * position.x + uniforms.inverse1.y * position.y + uniforms.inverse1.z;
    float2 distance = abs(float2(x, y)) - uniforms.extentScale.xy;
    float2 ramp = float2(0.5) - distance * uniforms.extentScale.zw;
    return clamp(ramp.x, 0.0, 1.0) * clamp(ramp.y, 0.0, 1.0);
}

// shade one fragment, which every pipeline shares and the kind selects between
fragment float4 albor_fragment(Varyings in [[stage_in]], constant DrawUniforms& uniforms [[buffer(1)]], texture2d<float> paint [[texture(0)]], sampler paintSampler [[sampler(0)]]) {
    float4 texel = paint.sample(paintSampler, in.uv);
    float4 base = uniforms.hasTexture != 0 ? texel * uniforms.color : uniforms.color;
    return base * (coverage(in, uniforms) * scissor_mask(in.position.xy, uniforms));
}
)MSL";
    } // namespace

    std::string_view shader_source() {
        // the same source is handed out on every call, which the device compiles once (AD-16)
        return SHADER_SOURCE;
    }
} // namespace albor::render
