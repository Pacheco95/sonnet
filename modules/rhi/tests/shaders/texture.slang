// Full-screen passes reading the bindless set: a 2D texture through a sampler, or a cube map by
// direction, each picked by index from the push constants (docs/rendering.md, "Frame structure").

struct PushConstants {
  uint texture;
  uint sampler;
  float lod;
  float padding;
  float4 direction;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

[[vk::binding(0, 0)]]
Texture2D<float4> textures[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

[[vk::binding(3, 0)]]
TextureCube<float4> cubes[];

struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

// One triangle covering clip space; the parts outside are clipped. The Y flip of the viewport
// puts uv (0, 0) at the top-left pixel.
[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
  output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
  output.uv = float2(uv.x, 1.0 - uv.y);
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return textures[push.texture].SampleLevel(samplers[push.sampler], input.uv, push.lod);
}

[shader("fragment")]
float4 cubeFragmentMain(VertexOutput input) : SV_Target {
  return cubes[push.texture].SampleLevel(samplers[push.sampler], push.direction.xyz, push.lod);
}
