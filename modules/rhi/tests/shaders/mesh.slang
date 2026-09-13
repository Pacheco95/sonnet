// Vertex pulling through a buffer device address in the push constants, plus one uniform and
// one storage buffer from the per-pass push descriptor set: the rhi paths the renderer builds on.

struct Vertex {
  float3 position;
  float3 color;
};

struct PushConstants {
  Vertex *vertices;
  float4 tint;
};

struct PassConstants {
  float4 offset;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

[[vk::binding(0, 1)]]
ConstantBuffer<PassConstants> pass;

[[vk::binding(1, 1)]]
StructuredBuffer<float4> scales;

struct VertexOutput {
  float4 position : SV_Position;
  float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  const Vertex vertex = push.vertices[vertexId];
  VertexOutput output;
  output.position = float4(vertex.position + pass.offset.xyz, 1.0);
  output.color = vertex.color * scales[0].rgb;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return float4(input.color, 1.0) * push.tint;
}
