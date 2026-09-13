// The depth pre-pass and the shadow cascades: positions only, with the alpha test of masked
// materials. Entry points share the vertex shader; the shadow variant projects with a cascade's
// matrix from the push constants instead of the camera's.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

VertexOutput transform(uint vertexId, float4x4 viewProjection) {
  const Vertex vertex = draw.vertices[vertexId];
  const ObjectData object = objects[draw.objectIndex];
  VertexOutput output;
  output.position = mul(viewProjection, mul(object.model, float4(vertex.position, 1.0)));
  output.uv = vertex.uv;
  return output;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  return transform(vertexId, frame.viewProjection);
}

[shader("vertex")]
VertexOutput shadowVertexMain(uint vertexId : SV_VertexID) {
  return transform(vertexId, frame.cascadeMatrices[draw.cascade]);
}

// Masked materials cut holes; everything else keeps the rasterised depth.
[shader("fragment")]
void fragmentMain(VertexOutput input) {
  const ObjectData object = objects[draw.objectIndex];
  const MaterialData material = frame.materials[object.material];
  if ((material.flags & 0xFF) == AlphaMask) {
    const float alpha = material.baseColor.a * object.color.a *
                        textures[material.baseColorTexture].Sample(samplers[material.sampler], input.uv).a;
    if (alpha < material.emissive.a) {
      discard;
    }
  }
}
