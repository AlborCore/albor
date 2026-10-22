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
    float4 extra;
    float2 targetSize;
    float ramp;
    int kind;
    int scissorEnabled;
    int hasTexture;
    int blend;
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
    float2 point = float2(x, y);
    float radius = uniforms.extra.x;
    float scale = min(uniforms.extentScale.z, uniforms.extentScale.w);
    if (radius > 0.0) {
        float2 halfExtent = uniforms.extentScale.xy;
        float2 corner = abs(point) - (halfExtent - float2(radius));
        float distance = length(max(corner, float2(0.0))) + min(max(corner.x, corner.y), 0.0) - radius;
        return clamp(0.5 - distance * scale, 0.0, 1.0);
    }
    float2 distance = abs(point) - uniforms.extentScale.xy;
    float2 ramp = float2(0.5) - distance * uniforms.extentScale.zw;
    return clamp(ramp.x, 0.0, 1.0) * clamp(ramp.y, 0.0, 1.0);
}

// the composition modes that replace the destination, which the fragment reshapes its alpha for
constant int COMPOSITION_SOURCE_IN = 1;
constant int COMPOSITION_SOURCE_OUT = 2;
constant int COMPOSITION_DESTINATION_IN = 5;
constant int COMPOSITION_DESTINATION_ATOP = 7;
constant int COMPOSITION_COPY = 9;

// shade one fragment, which every pipeline shares and the kind selects between
fragment float4 albor_fragment(Varyings in [[stage_in]], constant DrawUniforms& uniforms [[buffer(1)]], texture2d<float> paint [[texture(0)]], sampler paintSampler [[sampler(0)]]) {
    float4 base = uniforms.color;
    if (uniforms.hasTexture != 0) {
        base = paint.sample(paintSampler, in.uv) * uniforms.color;
    }
    float scale = coverage(in, uniforms) * scissor_mask(in.position.xy, uniforms);
    float4 result = base * scale;
    // source in and source out keep the destination where the group does not cover, which the coverage alpha leaves
    if (uniforms.blend == COMPOSITION_SOURCE_IN || uniforms.blend == COMPOSITION_SOURCE_OUT) {
        result.a = scale;
    }
    // destination in and atop keep the uncovered destination, which the residual alpha leaves for the blender
    else if (uniforms.blend == COMPOSITION_DESTINATION_IN || uniforms.blend == COMPOSITION_DESTINATION_ATOP) {
        result.a = scale * (1.0 - base.a);
    }
    // a copy writes the painted surface alone, which leaves the uncovered part to the destination
    else if (uniforms.blend == COMPOSITION_COPY) {
        result.a = scale;
    }
    return result;
}

// the two outputs dual source blending reads, which leaves the destination available to the blender
struct ExactFragment
{
    float4 color [[color(0), index(0)]];
    float4 control [[color(0), index(1)]];
};

// shade one fragment of a mode that needs the destination, which the fixed factors alone cannot express
fragment ExactFragment albor_fragment_exact(Varyings in [[stage_in]], constant DrawUniforms& uniforms [[buffer(1)]], texture2d<float> paint [[texture(0)]], sampler paintSampler [[sampler(0)]]) {
    float4 source = uniforms.color;
    if (uniforms.hasTexture != 0) {
        source = paint.sample(paintSampler, in.uv) * uniforms.color;
    }
    float scale = coverage(in, uniforms) * scissor_mask(in.position.xy, uniforms);
    ExactFragment out;
    out.color = source * scale;
    // destination atop dims the destination by the source alpha, which the blend control leaves uncovered
    if (uniforms.blend == COMPOSITION_DESTINATION_ATOP) {
        float control = scale * (1.0 - source.a);
        out.control = float4(control, control, control, scale);
    } else {
        out.control = float4(scale);
    }
    return out;
}

// the per shadow state, which the silhouette, the blur and the composite read
struct ShadowUniforms
{
    float4 color;
    float4 shape;
    float4 params;
    float4 inverse0;
    float4 inverse1;
};

// map a full target quad into clip space, which the offscreen passes cover
vertex Varyings shadow_vertex(uint vertexId [[vertex_id]]) {
    float2 corners[4] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0)};
    float2 corner = corners[vertexId];
    Varyings out;
    out.position = float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    out.lateral = 1.0;
    out.longitudinal = 1.0;
    out.uv = corner;
    return out;
}

// rasterize the rounded silhouette a cast is blurred from, which carries the shadow colour already
fragment float4 shadow_shape(Varyings in [[stage_in]], constant ShadowUniforms& uniforms [[buffer(1)]]) {
    float2 origin = uniforms.shape.zw;
    float3 raster = float3(in.position.xy + origin, 1.0);
    float2 local = float2(dot(uniforms.inverse0.xyz, raster), dot(uniforms.inverse1.xyz, raster));
    float2 halfExtent = uniforms.shape.xy;
    float radius = min(uniforms.params.x, min(halfExtent.x, halfExtent.y));
    float2 point = local - halfExtent;
    float2 corner = abs(point) - (halfExtent - float2(radius));
    float2 outward = max(corner, float2(0.0));
    float2 axis = float2(point.x < 0.0 ? -1.0 : 1.0, point.y < 0.0 ? -1.0 : 1.0);
    float2 normal = corner.x > corner.y ? float2(axis.x, 0.0) : float2(0.0, axis.y);
    if (outward.x > 0.0 || outward.y > 0.0)
        normal = normalize(outward * axis);
    float distance = length(outward) + min(max(corner.x, corner.y), 0.0) - radius;
    float2 deviceNormal = float2(uniforms.inverse0.x * normal.x + uniforms.inverse1.x * normal.y, uniforms.inverse0.y * normal.x + uniforms.inverse1.y * normal.y);
    return uniforms.color * clamp(0.5 - distance * length(deviceNormal), 0.0, 1.0);
}

// run one separable gaussian pass over the silhouette, which the two directions share
fragment float4 shadow_blur(Varyings in [[stage_in]], constant ShadowUniforms& uniforms [[buffer(1)]], texture2d<float> source [[texture(0)]], sampler paintSampler [[sampler(0)]]) {
    float2 step = uniforms.params.yz * uniforms.params.w;
    float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
    float4 sum = source.sample(paintSampler, in.uv) * weights[0];
    for (int tap = 1; tap < 5; tap++) {
        sum += source.sample(paintSampler, in.uv + step * float(tap)) * weights[tap];
        sum += source.sample(paintSampler, in.uv - step * float(tap)) * weights[tap];
    }
    return sum;
}
