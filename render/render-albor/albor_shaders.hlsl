// the direct3d twin of albor_shaders.metal, whose uniform shapes and math follow it field for field (§6.3, AD-16)

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
    // the first vertex of the draw, which direct3d leaves out of the system vertex id (AD-05)
    int baseVertex;
};

// the varying a rasterized fragment carries, which interpolates the coverage seeds
struct Varyings
{
    float4 position : SV_Position;
    float lateral : LATERAL;
    float longitudinal : LONGITUDINAL;
    float2 uv : PAINTUV;
};

// the draw state buffer the vertex stage reads, bound to the slot the backend names per draw
StructuredBuffer<BatchVertex> vertices : register(t0);
cbuffer VertexUniformsCb : register(b0)
{
    DrawUniforms vertexUniforms;
}

// map a device space position onto the clip space the rasterizer expects, with the y axis flipped
float4 project(float2 position, float2 targetSize) {
    float2 normalized = position / targetSize;
    float2 clip = float2(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0);
    return float4(clip, 0.0, 1.0);
}

// rasterize one batch vertex into clip space, carrying the coverage seeds through
Varyings albor_vertex(uint vertexId : SV_VertexID) {
    BatchVertex input = vertices[vertexId + vertexUniforms.baseVertex];
    Varyings shaded;
    shaded.position = project(input.position, vertexUniforms.targetSize);
    shaded.lateral = input.lateral;
    shaded.longitudinal = input.longitudinal;
    shaded.uv = input.uv;
    return shaded;
}

// return the coverage a fragment carries, which the draw kind turns into alpha
float coverage(Varyings input, DrawUniforms uniforms) {
    float longitudinal = clamp(input.longitudinal, 0.0, 1.0);
    if (uniforms.kind == 1) {
        float lateral = clamp((1.0 - abs(input.lateral * 2.0 - 1.0)) * uniforms.ramp, 0.0, 1.0);
        return lateral * longitudinal;
    }
    return clamp(input.lateral, 0.0, 1.0) * longitudinal;
}

// return the clip mask a fragment carries, which the inverse matrix maps onto the clip box centre
float scissor_mask(float2 position, DrawUniforms uniforms) {
    if (uniforms.scissorEnabled == 0) {
        return 1.0;
    }
    float x = uniforms.inverse0.x * position.x + uniforms.inverse0.y * position.y + uniforms.inverse0.z;
    float y = uniforms.inverse1.x * position.x + uniforms.inverse1.y * position.y + uniforms.inverse1.z;
    float2 location = float2(x, y);
    float radius = uniforms.extra.x;
    float scale = min(uniforms.extentScale.z, uniforms.extentScale.w);
    if (radius > 0.0) {
        float2 halfExtent = uniforms.extentScale.xy;
        float2 corner = abs(location) - (halfExtent - float2(radius, radius));
        float signedDistance = length(max(corner, float2(0.0, 0.0))) + min(max(corner.x, corner.y), 0.0) - radius;
        return clamp(0.5 - signedDistance * scale, 0.0, 1.0);
    }
    float2 span = abs(location) - uniforms.extentScale.xy;
    float2 ramp = float2(0.5, 0.5) - span * uniforms.extentScale.zw;
    return clamp(ramp.x, 0.0, 1.0) * clamp(ramp.y, 0.0, 1.0);
}

// the composition modes that replace the destination, which the fragment reshapes its alpha for
static const int COMPOS_SOURCE_IN = 1;
static const int COMPOS_SOURCE_OUT = 2;
static const int COMPOS_DESTINATION_IN = 5;
static const int COMPOS_DESTINATION_ATOP = 7;
static const int COMPOS_COPY = 9;

// the paint surface and its sampler, which the fragment reads when the draw carries one, and the draw uniform block the fragment stage reads
Texture2D<float4> paint : register(t0);
SamplerState paintSampler : register(s0);
cbuffer FragmentUniformsCb : register(b0)
{
    DrawUniforms fragmentUniforms;
}

// shade one fragment, which every pipeline shares and the kind selects between
float4 albor_fragment(Varyings pixel) : SV_Target0 {
    float4 base = fragmentUniforms.color;
    if (fragmentUniforms.hasTexture != 0) {
        base = paint.Sample(paintSampler, pixel.uv) * fragmentUniforms.color;
    }
    float scale = coverage(pixel, fragmentUniforms) * scissor_mask(pixel.position.xy, fragmentUniforms);
    float4 result = base * scale;
    // source in and source out keep the destination where the group does not cover, which the coverage alpha leaves
    if (fragmentUniforms.blend == COMPOS_SOURCE_IN || fragmentUniforms.blend == COMPOS_SOURCE_OUT) {
        result.a = scale;
    }
    // destination in and atop keep the uncovered destination, which the residual alpha leaves for the blender
    else if (fragmentUniforms.blend == COMPOS_DESTINATION_IN || fragmentUniforms.blend == COMPOS_DESTINATION_ATOP) {
        result.a = scale * (1.0 - base.a);
    }
    // a copy writes the painted surface alone, which leaves the uncovered part to the destination
    else if (fragmentUniforms.blend == COMPOS_COPY) {
        result.a = scale;
    }
    return result;
}

// the two outputs dual source blending reads, which leaves the destination available to the blender
struct ExactFragment
{
    float4 color : SV_Target0;
    float4 control : SV_Target1;
};

// shade one fragment of a mode that needs the destination, which the fixed factors alone cannot express
ExactFragment albor_fragment_exact(Varyings pixel) {
    float4 source = fragmentUniforms.color;
    if (fragmentUniforms.hasTexture != 0) {
        source = paint.Sample(paintSampler, pixel.uv) * fragmentUniforms.color;
    }
    float scale = coverage(pixel, fragmentUniforms) * scissor_mask(pixel.position.xy, fragmentUniforms);
    ExactFragment shaded;
    shaded.color = source * scale;
    // destination atop dims the destination by the source alpha, which the blend control leaves uncovered
    if (fragmentUniforms.blend == COMPOS_DESTINATION_ATOP) {
        float control = scale * (1.0 - source.a);
        shaded.control = float4(control, control, control, scale);
    } else {
        shaded.control = float4(scale, scale, scale, scale);
    }
    return shaded;
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

// the shadow uniform block the silhouette and blur passes read
cbuffer ShadowUniformsCb : register(b0)
{
    ShadowUniforms shadowUniforms;
}

// map a full target quad into clip space, which the offscreen passes cover
Varyings shadow_vertex(uint vertexId : SV_VertexID) {
    float2 corners[4] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0)};
    float2 corner = corners[vertexId];
    Varyings shaded;
    shaded.position = float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    shaded.lateral = 1.0;
    shaded.longitudinal = 1.0;
    shaded.uv = corner;
    return shaded;
}

// rasterize the rounded silhouette a cast is blurred from, which carries the shadow colour already
float4 shadow_shape(Varyings pixel) : SV_Target0 {
    float2 origin = shadowUniforms.shape.zw;
    float3 raster = float3(pixel.position.xy + origin, 1.0);
    float2 local = float2(dot(shadowUniforms.inverse0.xyz, raster), dot(shadowUniforms.inverse1.xyz, raster));
    float2 halfExtent = shadowUniforms.shape.xy;
    float radius = min(shadowUniforms.params.x, min(halfExtent.x, halfExtent.y));
    float2 span = local - halfExtent;
    float2 corner = abs(span) - (halfExtent - float2(radius, radius));
    float2 outward = max(corner, float2(0.0, 0.0));
    float2 axis = float2(span.x < 0.0 ? -1.0 : 1.0, span.y < 0.0 ? -1.0 : 1.0);
    float2 faceNormal = corner.x > corner.y ? float2(axis.x, 0.0) : float2(0.0, axis.y);
    if (outward.x > 0.0 || outward.y > 0.0)
        faceNormal = normalize(outward * axis);
    float signedDistance = length(outward) + min(max(corner.x, corner.y), 0.0) - radius;
    float2 deviceNormal = float2(shadowUniforms.inverse0.x * faceNormal.x + shadowUniforms.inverse1.x * faceNormal.y, shadowUniforms.inverse0.y * faceNormal.x + shadowUniforms.inverse1.y * faceNormal.y);
    return shadowUniforms.color * clamp(0.5 - signedDistance * length(deviceNormal), 0.0, 1.0);
}

// the blur source the offscreen pass samples
Texture2D<float4> blurSource : register(t1);

// run one separable gaussian pass over the silhouette, which the two directions share
float4 shadow_blur(Varyings pixel) : SV_Target0 {
    float2 stride = shadowUniforms.params.yz * shadowUniforms.params.w;
    float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
    float4 sum = blurSource.Sample(paintSampler, pixel.uv) * weights[0];
    for (int tap = 1; tap < 5; tap++) {
        sum += blurSource.Sample(paintSampler, pixel.uv + stride * float(tap)) * weights[tap];
        sum += blurSource.Sample(paintSampler, pixel.uv - stride * float(tap)) * weights[tap];
    }
    return sum;
}
