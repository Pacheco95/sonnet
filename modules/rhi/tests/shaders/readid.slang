// Full-screen pass reading the id image through the pass set's sampled-image binding with Load,
// so no sampler is involved: white where the id matches the push constant, black elsewhere.

struct PushConstants {
  uint id;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

[[vk::binding(2, 1)]]
Texture2D<uint> ids;

struct VertexOutput {
  float4 position : SV_Position;
};

// One triangle covering clip space; the parts outside are clipped.
[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
  output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  const uint id = ids.Load(int3(int2(input.position.xy), 0));
  return id == push.id ? float4(1.0, 1.0, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}
