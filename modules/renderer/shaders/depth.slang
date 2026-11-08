// The depth pre-pass and the shadow cascades: positions only, with the alpha test of masked
// materials and, in the pre-pass, the dither of a faded opaque draw. Entry points share the vertex shader; the shadow variant projects with a cascade's or
// a spot light's matrix, chosen by the push constants, instead of the camera's. The draw's object comes from the
// visible list and is carried to the fragment stage uninterpolated (ADR-0016).
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  float4 uvs : TEXCOORD0; // Vertex::uv, then Vertex::uv1
  float alpha : COLOR0; // Vertex::color's alpha
  nointerpolation uint objectIndex : OBJECT0;
};

VertexOutput transform(uint vertexId, uint objectIndex, float4x4 viewProjection) {
  const float4x4 model = objects[objectIndex].model;
  const Vertex vertex = vertexBuffers[objects[objectIndex].vertexBuffer][vertexId];
  VertexOutput output;
  output.position = mul(viewProjection, mul(model, float4(vertex.position, 1.0)));
  output.uvs = float4(vertex.uv, vertex.uv1);
  output.alpha = vertexColor(vertex).a;
  output.objectIndex = objectIndex;
  return output;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                        uint instance : SV_InstanceID) {
  return transform(vertexId, drawObject(startInstance, instance), frame.viewProjection);
}

[shader("vertex")]
VertexOutput shadowVertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                        uint instance : SV_InstanceID) {
  const float4x4 viewProjection = draw.shadow < CascadeCount ? frame.cascadeMatrices[draw.shadow]
                                                             : frame.localShadows[draw.shadow - CascadeCount].matrix;
  return transform(vertexId, drawObject(startInstance, instance), viewProjection);
}

float surfaceAlpha(MaterialData material, uint objectIndex, float4 uvs, float vertexAlpha) {
  return material.baseColor.a * objects[objectIndex].color.a * vertexAlpha *
         textures[material.baseColorTexture]
             .Sample(samplers[materialSampler(material, SlotBaseColor)], uvTransform(material.baseColorUv, material.baseColorUvOffset, uvSet(material, SlotBaseColor, uvs)))
             .a;
}

// Masked materials cut holes; everything else keeps the rasterised depth.
void alphaTest(uint objectIndex, float4 uvs, float vertexAlpha) {
  const MaterialData material = frame.materials[objects[objectIndex].material];
  if ((material.flags & 0xFF) == AlphaMask && surfaceAlpha(material, objectIndex, uvs, vertexAlpha) < material.emissive.a) {
    discard;
  }
}

// The camera's pre-pass: the alpha test, and the dither of an opaque draw faded by its tint. The
// shadow passes take neither, so a faded draw still casts its whole shadow.
void prePassTest(uint objectIndex, float4 uvs, float vertexAlpha, float2 pixel) {
  alphaTest(objectIndex, uvs, vertexAlpha);
  if (fadedOut(objectIndex, frame.materials[objects[objectIndex].material], pixel)) {
    discard;
  }
}

// The effective transmission T = t * (1 - m) of a draw on the transmission path (ADR-0033).
float transmittance(MaterialData material, float4 uvs) {
  const float t = saturate(material.transmission *
                           textures[material.transmissionTexture]
                               .Sample(samplers[materialSampler(material, SlotTransmission)],
                                       uvTransform(material.transmissionUv, material.transmissionUvOffset, uvSet(material, SlotTransmission, uvs)))
                               .r);
  const float metallic = saturate(material.metallic *
                                  textures[material.metallicRoughnessTexture]
                                      .Sample(samplers[materialSampler(material, SlotMetallicRoughness)],
                                              uvTransform(material.metallicRoughnessUv, material.metallicRoughnessUvOffset,
                                                          uvSet(material, SlotMetallicRoughness, uvs)))
                                      .b);
  return t * (1.0 - metallic);
}

// The shadow maps' fragment stage. A masked material keeps its cutoff; a blended one discards
// under the pixel's threshold, so the 3x3 comparison of the receiver averages the survivors into a
// partial shadow whose darkness follows alpha. A draw on the transmission path takes the same
// hashed test on its coverage a * (1 - T), after a masked material's cutoff (ADR-0033, "Shadows").
[shader("fragment")]
void shadowFragmentMain(VertexOutput input) {
  const MaterialData material = frame.materials[objects[input.objectIndex].material];
  const uint alphaMode = material.flags & 0xFF;
  if (alphaMode == AlphaMask) {
    alphaTest(input.objectIndex, input.uvs, input.alpha);
  }
  const bool transmissive = (material.flags & MaterialTransmissive) != 0;
  if (alphaMode != AlphaBlend && !transmissive) {
    return;
  }
  float coverage = alphaMode == AlphaBlend ? surfaceAlpha(material, input.objectIndex, input.uvs, input.alpha) : 1.0;
  if (transmissive) {
    coverage *= 1.0 - transmittance(material, input.uvs);
  }
  if (coverage <= orderedThreshold(input.position.xy)) {
    discard;
  }
}

[shader("fragment")]
void fragmentMain(VertexOutput input) {
  prePassTest(input.objectIndex, input.uvs, input.alpha, input.position.xy);
}

// The pre-pass under temporal anti-aliasing (ADR-0024): the same depth, and the screen-space motion
// of each surface in UV units as a second attachment. The clip positions are interpolated and
// divided in the fragment stage, where they are perspective correct.
struct MotionVertexOutput {
  float4 position : SV_Position;
  float4 uvs : TEXCOORD0; // Vertex::uv, then Vertex::uv1
  float alpha : COLOR0; // Vertex::color's alpha
  nointerpolation uint objectIndex : OBJECT0;
  float4 currentClip : CLIP0;
  float4 previousClip : CLIP1;
};

[shader("vertex")]
MotionVertexOutput motionVertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                                    uint instance : SV_InstanceID) {
  const uint objectIndex = drawObject(startInstance, instance);
  const VertexOutput base = transform(vertexId, objectIndex, frame.viewProjection);
  const float4x4 model = objects[objectIndex].model;
  const Vertex vertex = vertexBuffers[objects[objectIndex].vertexBuffer][vertexId];
  const PreviousObject previous = frame.previousObjects[objectIndex];
  const float4 previousPosition = float4(vertexBuffers[previous.vertexBuffer][vertexId].position, 1.0);
  const float3 previousWorld =
      float3(dot(previous.row0, previousPosition), dot(previous.row1, previousPosition), dot(previous.row2, previousPosition));
  MotionVertexOutput output;
  output.position = base.position;
  output.uvs = base.uvs;
  output.alpha = base.alpha;
  output.objectIndex = objectIndex;
  output.currentClip = mul(frame.unjitteredViewProjection, mul(model, float4(vertex.position, 1.0)));
  output.previousClip = mul(frame.previousViewProjection, float4(previousWorld, 1.0));
  return output;
}

float2 clipToUv(float4 clip) {
  return float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
}

// Where the surface is now minus where it was, so the history is read at the pixel's uv less this.
// A point that was behind the camera has no previous position and reads as still.
[shader("fragment")]
float2 motionFragmentMain(MotionVertexOutput input) : SV_Target {
  prePassTest(input.objectIndex, input.uvs, input.alpha, input.position.xy);
  if (input.previousClip.w <= 1.0e-4 || input.currentClip.w <= 1.0e-4) {
    return float2(0.0, 0.0);
  }
  return clipToUv(input.currentClip) - clipToUv(input.previousClip);
}
