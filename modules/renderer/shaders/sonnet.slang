// Shared declarations for engine shaders: the bindless arrays of descriptor set 0, the per-pass
// buffers of set 1, the per-draw push constants, the vertex layout and the data the renderer
// hands down. Shaders `import sonnet` and never declare their own descriptor sets
// (docs/rendering.md, "Shaders"). Layouts are scalar and match the C++ structs in the renderer
// module byte for byte.
module sonnet;

// ---- Bindless set 0 (docs/rendering.md, "Frame structure") ----

[[vk::binding(0, 0)]]
public Texture2D<float4> textures[];

[[vk::binding(1, 0)]]
public SamplerState samplers[];

[[vk::binding(2, 0)]]
[format("rgba16f")]
public RWTexture2DArray<float4> storageImages[];

[[vk::binding(3, 0)]]
public TextureCube<float4> cubes[];

[[vk::binding(4, 0)]]
public SamplerComparisonState comparisonSamplers[];

// ---- Geometry ----

public struct Vertex {
  public float3 position;
  public float3 normal;
  public float4 tangent; // xyz in the normal's frame, w the bitangent sign
  public float2 uv;
};

[[vk::binding(5, 0)]]
public StructuredBuffer<Vertex> vertexBuffers[];

// ---- Lights ----

public static const uint LightPoint = 0;
public static const uint LightSpot = 1;

public struct Light {
  public float3 position;
  public float range;
  public float3 color; // radiance scale: colour times intensity
  public uint type;
  public float3 direction; // spot lights: the direction the light travels
  public float innerCos;
  public float outerCos;
  public float padding0;
  public float padding1;
  public float padding2;
};

// Clusters tile the screen and slice view depth exponentially; each holds the lights that
// touch it (ADR-0008).
public static const uint ClusterGridX = 16;
public static const uint ClusterGridY = 9;
public static const uint ClusterGridZ = 24;
public static const uint ClusterCount = ClusterGridX * ClusterGridY * ClusterGridZ;
public static const uint MaxLightsPerCluster = 63; // with the count, 64 uints per cluster

public struct Cluster {
  public uint count;
  public uint lights[MaxLightsPerCluster];
};

// ---- Materials ----

public static const uint AlphaOpaque = 0;
public static const uint AlphaMask = 1;
public static const uint AlphaBlend = 2;
public static const uint MaterialDoubleSided = 1u << 8;

public struct MaterialData {
  public float4 baseColor;
  public float4 emissive; // rgb: emissive colour, a: alpha cutoff
  public float metallic;
  public float roughness;
  public float normalScale;
  public float occlusionStrength;
  public uint baseColorTexture;
  public uint metallicRoughnessTexture;
  public uint normalTexture;
  public uint occlusionTexture;
  public uint emissiveTexture;
  public uint flags; // alpha mode in the low byte, MaterialDoubleSided above it
  public uint sampler;
  public uint padding0;
};

// ---- Shadows ----

public static const uint CascadeCount = 4;

// ---- Per pass ----

public struct FrameConstants {
  public float4x4 view;
  public float4x4 projection;
  public float4x4 viewProjection;
  public float4x4 inverseProjection;
  public float4x4 inverseViewProjection;
  public float4x4 cascadeMatrices[CascadeCount];
  public float4 cascadeSplits; // view depth where each cascade ends
  public float4 cameraPosition;
  public float4 sunDirection; // xyz: direction the light travels, normalised; w: 1 when there is a sun
  public float4 sunColor;     // rgb: colour times intensity
  public float4 ambient;      // rgb: ambient radiance without an environment
  public float4 environment;  // x: intensity, y: prefiltered mip count, z: exposure, w: 1 when present
  public float4 clusterScaleBias; // x, y: tile size in pixels; z: slice scale; w: slice bias
  public float2 targetSize;
  public float nearPlane;
  public float shadowBias;
  public uint cascadeImages[CascadeCount];
  public uint irradianceCube;
  public uint prefilteredCube;
  public uint skyboxCube;
  public uint brdfLut;
  public uint linearSampler;
  public uint shadowSampler;
  public uint lightCount;
  public uint padding0;
  public MaterialData *materials;
  public Light *lights;
  public Cluster *clusters;
};

public struct ObjectData {
  public float4x4 model;
  public float4 color; // multiplies the material's base colour
  public uint id;      // entity id for picking and the selection outline, 0 for none
  public uint material;
  // Bindless index into vertexBuffers[]: the mesh's buffer, or the skinned buffer the skinning
  // pass wrote. Lives here rather than in push constants because indirect draws can't carry it
  // (ADR-0012). Replaces PhysicalStorageBuffer to avoid the Metal thread-local-pointer issue.
  public uint vertexBuffer;
  public uint vertexPad;
};

// A draw's world basis: the three axis vectors of its model matrix. They come out of the same mul
// the position goes through, so nothing here depends on how the matrix is laid out in memory, and
// the compiler folds each one down to a swizzle.
public struct WorldBasis {
  public float3 x;
  public float3 y;
  public float3 z;
};

public WorldBasis worldBasis(float4x4 model) {
  WorldBasis basis;
  basis.x = mul(model, float4(1.0, 0.0, 0.0, 0.0)).xyz;
  basis.y = mul(model, float4(0.0, 1.0, 0.0, 0.0)).xyz;
  basis.z = mul(model, float4(0.0, 0.0, 1.0, 0.0)).xyz;
  return basis;
}

// A direction through the model matrix: the basis combination, no translation.
public float3 transformDirection(WorldBasis basis, float3 direction) {
  return basis.x * direction.x + basis.y * direction.y + basis.z * direction.z;
}

// A normal through the model matrix, derived here rather than sent. ObjectData used to carry the
// inverse transpose in full, 64 of its 160 bytes, written per draw every frame into host-visible
// device memory, which is what the fill was bound by (roadmap.md, "The per-frame fill is
// bandwidth, not computation").
//
// What a normal needs is the cofactor matrix of the model matrix's upper 3x3, whose columns are
// these cross products. It equals the inverse transpose times the determinant, and the
// determinant is a uniform scale that a normalised normal cannot tell from 1, except for its
// sign: under a mirroring transform the cofactor points inward. Multiplying by the sign puts it
// back. It used to cancel against SV_IsFrontFace instead, while the pipelines kept a fixed front
// face and a mirrored draw's reversed winding made its outside read as its back; the renderer now
// sets a clockwise front face for a mirrored draw, so nothing cancels it any more.
public float3 transformNormal(WorldBasis basis, float3 normal) {
  const float3 cofactor = cross(basis.y, basis.z) * normal.x + cross(basis.z, basis.x) * normal.y +
                          cross(basis.x, basis.y) * normal.z;
  return cofactor * mirrorSign(basis);
}

// -1 when the model matrix mirrors, which is when its upper 3x3 has a negative determinant. A
// mirrored draw's bitangent has to take it too: cross(n, t) of the transformed normal and tangent
// is the transformed bitangent times it, so a normal map's green channel would read upside down.
public float mirrorSign(WorldBasis basis) {
  return dot(cross(basis.x, basis.y), basis.z) < 0.0 ? -1.0 : 1.0;
}

// A draw finds its object through SV_StartInstanceLocation, which every command carries in its
// firstInstance and every direct draw passes as its first instance (ADR-0012). What is left per
// pass is the shadow cascade, pushed once.
public struct DrawConstants {
  public uint cascade;
};

// One candidate for the culling pass: the world-space bounds tested against the frustum, and
// what the surviving command is made of. Matches CullDraw in the renderer module.
public struct CullDraw {
  public float3 center;
  public float3 extent;
  public uint objectIndex;
  public uint indexCount;
  public uint firstIndex;
  public uint batch;      // which count this draw bumps
  public uint batchFirst; // the batch's first command slot
  public uint flags;
};

public static const uint CullSelected = 1u << 0;

// Vulkan's VkDrawIndexedIndirectCommand, the layout rhi::IndirectCommand mirrors.
public struct IndirectCommand {
  public uint indexCount;
  public uint instanceCount;
  public uint firstIndex;
  public int vertexOffset;
  public uint firstInstance;
};

[[vk::push_constant]]
public ConstantBuffer<DrawConstants> draw;

[[vk::binding(0, 1)]]
public ConstantBuffer<FrameConstants> frame;

[[vk::binding(1, 1)]]
public StructuredBuffer<ObjectData> objects;

// ---- Helpers ----

public static const float Pi = 3.14159265359;

// One triangle covering clip space; the parts outside are clipped.
public float4 fullscreenTriangle(uint vertexId) {
  const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
  return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

// The uv of a pixel in an image drawn by another pass: pixel centres over the target size, in
// the orientation the Y-flipped viewport gives every image.
public float2 pixelUv(float2 position, float2 size) {
  return position / size;
}

// Exponential depth slicing (ADR-0008): slice = log(z) * scale + bias.
public uint clusterIndex(float2 pixel, float viewDepth) {
  const uint x = min(uint(pixel.x / frame.clusterScaleBias.x), ClusterGridX - 1);
  const uint y = min(uint(pixel.y / frame.clusterScaleBias.y), ClusterGridY - 1);
  const int slice = int(log(max(viewDepth, 1.0e-4)) * frame.clusterScaleBias.z + frame.clusterScaleBias.w);
  const uint z = uint(clamp(slice, 0, int(ClusterGridZ) - 1));
  return x + ClusterGridX * (y + ClusterGridY * z);
}

public float2 hammersley(uint i, uint count) {
  uint bits = i;
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float2(float(i) / float(count), float(bits) * 2.3283064365386963e-10);
}

// A tangent frame around n.
public float3x3 tangentFrame(float3 n) {
  const float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
  const float3 t = normalize(cross(up, n));
  const float3 b = cross(n, t);
  return float3x3(t, b, n); // rows: multiply a tangent-space vector as v.x * t + v.y * b + v.z * n
}

public float3 toWorld(float3x3 frameRows, float3 v) {
  return frameRows[0] * v.x + frameRows[1] * v.y + frameRows[2] * v.z;
}

public float3 importanceSampleGgx(float2 xi, float3 n, float roughness) {
  const float a = roughness * roughness;
  const float phi = 2.0 * Pi * xi.x;
  const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
  const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
  const float3 h = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
  return normalize(toWorld(tangentFrame(n), h));
}

// The direction a cube face texel looks at, Vulkan face order: +X, -X, +Y, -Y, +Z, -Z.
public float3 cubeDirection(uint face, float2 uv) {
  const float2 st = uv * 2.0 - 1.0;
  switch (face) {
  case 0:
    return normalize(float3(1.0, -st.y, -st.x));
  case 1:
    return normalize(float3(-1.0, -st.y, st.x));
  case 2:
    return normalize(float3(st.x, 1.0, st.y));
  case 3:
    return normalize(float3(st.x, -1.0, -st.y));
  case 4:
    return normalize(float3(st.x, -st.y, 1.0));
  default:
    return normalize(float3(-st.x, -st.y, -1.0));
  }
}
