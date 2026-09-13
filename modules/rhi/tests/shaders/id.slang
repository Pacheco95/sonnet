// Writes one integer id per fragment into an R32Uint attachment: the id image behind picking and
// the selection outline. The triangle covers the centre of the target like triangle.slang.

struct PushConstants {
  uint id;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

static const float2 Positions[3] = { float2(0.0, 0.5), float2(-0.5, -0.5), float2(0.5, -0.5) };

[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID) : SV_Position {
  return float4(Positions[vertexId], 0.0, 1.0);
}

[shader("fragment")]
uint fragmentMain() : SV_Target {
  return push.id;
}
