// The selection outline: a full-screen pass over the id image that colours the pixels next to
// a selected entity's silhouette and discards everything else, so the scene colour stays. A post
// pass, not a scene pass: it declares the pass image binding and its own push constants instead
// of importing sonnet (docs/rendering.md, "Shaders").

static const int Radius = 2;
static const uint MaxSelected = 16;

struct OutlineConstants {
  float4 color;
  uint4 ids[MaxSelected / 4];
  uint count;
};

[[vk::push_constant]]
ConstantBuffer<OutlineConstants> outline;

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

bool isSelected(uint id) {
  if (id == 0) {
    return false;
  }
  for (uint i = 0; i < outline.count; ++i) {
    if (outline.ids[i / 4][i % 4] == id) {
      return true;
    }
  }
  return false;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  uint width, height;
  ids.GetDimensions(width, height);
  const int2 pixel = int2(input.position.xy);
  if (isSelected(ids.Load(int3(pixel, 0)))) {
    discard;
  }
  for (int y = -Radius; y <= Radius; ++y) {
    for (int x = -Radius; x <= Radius; ++x) {
      const int2 neighbour = clamp(pixel + int2(x, y), int2(0, 0), int2(int(width) - 1, int(height) - 1));
      if (isSelected(ids.Load(int3(neighbour, 0)))) {
        return outline.color;
      }
    }
  }
  discard;
  return float4(0.0, 0.0, 0.0, 0.0);
}
