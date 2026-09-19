// Debug lines: world-space segments with a colour per vertex, pulled from the pass storage buffer
// and depth-tested against the scene without writing depth. A pass of its own rather than a scene
// pass: it declares its binding and push constants instead of importing sonnet (docs/rendering.md,
// "Shaders").

struct DebugVertex {
  float4 position; // xyz in world space
  float4 color;
};

struct DebugConstants {
  float4x4 viewProjection;
};

[[vk::push_constant]]
ConstantBuffer<DebugConstants> constants;

[[vk::binding(1, 1)]]
StructuredBuffer<DebugVertex> vertices;

struct VertexOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

// Lines drawn on a surface, such as a collider around its mesh, would fight with it for depth:
// they are pulled slightly towards the camera, which is a larger depth under reversed-Z.
static const float DepthPull = 1.002;

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  const DebugVertex vertex = vertices[vertexId];
  VertexOutput output;
  output.position = mul(constants.viewProjection, float4(vertex.position.xyz, 1.0));
  output.position.z *= DepthPull;
  output.color = vertex.color;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return input.color;
}
