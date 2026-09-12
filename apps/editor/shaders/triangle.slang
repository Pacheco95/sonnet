// A triangle without vertex buffers: positions come from the vertex index. Clip space is
// Vulkan's; the negative-height viewport flips Y, so this winding is counter-clockwise on screen.

struct PushConstants {
  float4 tint;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

struct VertexOutput {
  float4 position : SV_Position;
  float3 color : COLOR0;
};

static const float2 Positions[3] = { float2(0.0, 0.5), float2(-0.5, -0.5), float2(0.5, -0.5) };
static const float3 Colors[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = float4(Positions[vertexId], 0.0, 1.0);
  output.color = Colors[vertexId];
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return float4(input.color, 1.0) * push.tint;
}
