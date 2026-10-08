// GPU skinning (ADR-0010) and morph targets (ADR-0023): one thread per vertex first adds the
// weighted deltas of the mesh's morph targets, then blends up to four joint matrices, and writes the
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

// How far a morph target moves one vertex at weight 1; the targets follow each other in the
// buffer, vertexCount deltas each.
struct MorphDelta {
  float3 position;
  float3 normal;
  float3 tangent;
};

// A pass without skin weights or without morph targets leaves the pointer of the other null and
// its count zero.
struct SkinConstants {
  Vertex *source;
  SkinWeights *skin;
  float4x4 *joints;
  Vertex *destination;
  MorphDelta *morph;
  float *weights;
  uint vertexCount;
  uint jointCount;
  uint morphCount;
  uint padding;
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
  if (constants.morphCount > 0) {
    for (uint target = 0; target < constants.morphCount; ++target) {
      const float weight = constants.weights[target];
      if (weight != 0.0) {
        const MorphDelta delta = constants.morph[target * constants.vertexCount + index];
        vertex.position += delta.position * weight;
        vertex.normal += delta.normal * weight;
        vertex.tangent.xyz += delta.tangent * weight;
      }
    }
    vertex.normal = normalize(vertex.normal);
    vertex.tangent.xyz = normalize(vertex.tangent.xyz);
  }
  if (constants.jointCount == 0) {
    constants.destination[index] = vertex;
    return;
  }
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
