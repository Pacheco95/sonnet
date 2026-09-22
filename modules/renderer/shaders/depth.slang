// The depth pre-pass and the shadow cascades: positions only, with the alpha test of masked
// materials. Entry points share the vertex shader; the shadow variant projects with a cascade's
// matrix from the push constants instead of the camera's. The draw's object arrives in
// SV_StartInstanceLocation and is carried to the fragment stage uninterpolated (ADR-0012).
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  nointerpolation uint objectIndex : OBJECT0;
};

VertexOutput transform(uint vertexId, uint objectIndex, float4x4 viewProjection) {
  const float4x4 model = objects[objectIndex].model;
  const Vertex vertex = vertexBuffers[objects[objectIndex].vertexBuffer][vertexId];
  VertexOutput output;
  output.position = mul(viewProjection, mul(model, float4(vertex.position, 1.0)));
  output.uv = vertex.uv;
  output.objectIndex = objectIndex;
  return output;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint objectIndex : SV_StartInstanceLocation) {
  return transform(vertexId, objectIndex, frame.viewProjection);
}

[shader("vertex")]
VertexOutput shadowVertexMain(uint vertexId : SV_VertexID, uint objectIndex : SV_StartInstanceLocation) {
  return transform(vertexId, objectIndex, frame.cascadeMatrices[draw.cascade]);
}

// Masked materials cut holes; everything else keeps the rasterised depth.
[shader("fragment")]
void fragmentMain(VertexOutput input) {
  const MaterialData material = frame.materials[objects[input.objectIndex].material];
  if ((material.flags & 0xFF) == AlphaMask) {
    const float alpha = material.baseColor.a * objects[input.objectIndex].color.a *
                        textures[material.baseColorTexture].Sample(samplers[material.sampler], input.uv).a;
    if (alpha < material.emissive.a) {
      discard;
    }
  }
}
