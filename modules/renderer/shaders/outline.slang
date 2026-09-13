// The selection outline: a full-screen pass over the selection mask that colours the pixels next
// to a masked entity's silhouette and discards everything else, so the scene colour stays. The
// mask is drawn without the depth test, so an occluder never cuts a hole into the outline. A post
// pass, not a scene pass: it declares the pass image binding and its own push constants instead
// of importing sonnet (docs/rendering.md, "Shaders").

static const int Radius = 2;

struct OutlineConstants {
  float4 color;
};

[[vk::push_constant]]
ConstantBuffer<OutlineConstants> outline;

[[vk::binding(2, 1)]]
Texture2D<uint> mask;

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
  uint width, height;
  mask.GetDimensions(width, height);
  const int2 pixel = int2(input.position.xy);
  if (mask.Load(int3(pixel, 0)) != 0) {
    discard;
  }
  for (int y = -Radius; y <= Radius; ++y) {
    for (int x = -Radius; x <= Radius; ++x) {
      const int2 neighbour = clamp(pixel + int2(x, y), int2(0, 0), int2(int(width) - 1, int(height) - 1));
      if (mask.Load(int3(neighbour, 0)) != 0) {
        return outline.color;
      }
    }
  }
  discard;
  return float4(0.0, 0.0, 0.0, 0.0);
}
