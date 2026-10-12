// The overlay: rectangles of a bindless texture in target pixels, pulled from the pass storage
// buffer six vertices to a quad and alpha blended over a finished, display-encoded image. The game
// UI draws through it, images and text in one draw: a quad of the glyph kind reads coverage from
// the red channel of a signed distance field. A pass of its own rather than a scene pass: it declares its binding and push
// constants instead of importing sonnet (docs/rendering.md, "Shaders").

struct Quad {
  float2 minimum;
  float2 maximum;
  float2 uvMinimum;
  float2 uvMaximum;
  float4 color;
  uint texture;
  uint kind; // 0 an image, 1 a glyph
  uint padding0; // to 64 bytes, a stride both sides agree on
  uint padding1;
};

struct OverlayConstants {
  float2 targetSize;
  uint sampler;
};

[[vk::push_constant]]
ConstantBuffer<OverlayConstants> overlay;

[[vk::binding(1, 1)]]
StructuredBuffer<Quad> quads;

[[vk::binding(0, 0)]]
Texture2D<float4> textures[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

struct VertexOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
  nointerpolation uint texture : TEXCOORD1;
  nointerpolation uint kind : TEXCOORD2;
};

// Corners 0 to 3 (x in bit 0, y in bit 1) of the two triangles of a quad.
static const uint Corners[6] = {0, 1, 2, 2, 1, 3};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  const Quad quad = quads[vertexId / 6];
  const uint corner = Corners[vertexId % 6];
  const float2 at = float2(corner & 1u, (corner >> 1) & 1u);
  const float2 pixel = lerp(quad.minimum, quad.maximum, at);
  VertexOutput output;
  // Pixels run down from the top-left; the engine's viewport is flipped, so clip space runs up.
  const float2 unit = pixel / overlay.targetSize;
  output.position = float4(unit.x * 2.0 - 1.0, 1.0 - unit.y * 2.0, 0.0, 1.0);
  output.color = quad.color;
  output.uv = lerp(quad.uvMinimum, quad.uvMaximum, at);
  output.texture = quad.texture;
  output.kind = quad.kind;
  return output;
}

float3 encodeSrgb(float3 linearColor) {
  const float3 low = linearColor * 12.92;
  const float3 high = 1.055 * pow(max(linearColor, 0.0), 1.0 / 2.4) - 0.055;
  return select(linearColor <= 0.0031308, low, high);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  // An sRGB texture is decoded by the sampler; the target is display-encoded, so it goes back.
  const float4 texel = textures[NonUniformResourceIndex(input.texture)].Sample(
      samplers[NonUniformResourceIndex(overlay.sampler)], input.uv);
  // Taken before the branch, so every invocation of a 2x2 block takes part in the derivative.
  const float slope = fwidth(texel.r);
  if (input.kind == 1) {
    // The field is 0.5 on the outline and falls to 0 six texels out at the stored size. Its
    // gradient per pixel is what fwidth reads, so the edge is one pixel wide at any scale.
    const float coverage = saturate((texel.r - 0.5) / max(slope, 1e-5) + 0.5);
    return float4(input.color.rgb, input.color.a * coverage);
  }
  return float4(encodeSrgb(texel.rgb), texel.a) * input.color;
}
