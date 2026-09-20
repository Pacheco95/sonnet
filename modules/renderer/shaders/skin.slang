// GPU skinning (ADR-0010): one thread per vertex blends up to four joint matrices and writes the
// deformed vertex into the instance's own buffer, which every later pass of the frame pulls from
// as it would the mesh's. A pass of its own: it declares its push constants instead of importing
// sonnet (docs/rendering.md, "Shaders"), so the vertex layout is repeated here.

struct Vertex {
  float3 position;
  float3 normal;
  float4 tangent; // xyz in the normal's frame, w the bitangent sign
  float2 uv;
};

struct SkinWeights {
  uint4 joints;
  float4 weights;
};

struct SkinConstants {
  Vertex *source;
  SkinWeights *skin;
  float4x4 *joints;
  Vertex *destination;
  uint vertexCount;
  uint jointCount;
};

[[vk::push_constant]]
ConstantBuffer<SkinConstants> constants;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
  const uint index = id.x;
  if (index >= constants.vertexCount) {
    return;
  }
  Vertex vertex = constants.source[index];
  const SkinWeights skin = constants.skin[index];
  const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
  // An unweighted vertex stays in the bind pose rather than collapsing to the origin; an index
  // past the skin is clamped, since the joints are read through a raw address.
  if (total > 1.0e-6) {
    float4x4 blended = float4x4(0.0);
    for (uint i = 0; i < 4; ++i) {
      blended += constants.joints[min(skin.joints[i], constants.jointCount - 1)] * (skin.weights[i] / total);
    }
    vertex.position = mul(blended, float4(vertex.position, 1.0)).xyz;
    // Normals go through the blended matrix itself, which is exact for the rotations and uniform
    // scales joints carry; a non-uniform scale would need its inverse transpose.
    const float3x3 linear = (float3x3)blended;
    vertex.normal = normalize(mul(linear, vertex.normal));
    vertex.tangent.xyz = normalize(mul(linear, vertex.tangent.xyz));
  }
  constants.destination[index] = vertex;
}
