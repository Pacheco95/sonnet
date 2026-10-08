// The depth pre-pass and the shadow cascades: positions only, with the alpha test of masked
// materials. Entry points share the vertex shader; the shadow variant projects with a cascade's or
// a spot light's matrix, chosen by the push constants, instead of the camera's. The draw's object comes from the
// visible list and is carried to the fragment stage uninterpolated (ADR-0016).
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

// Masked materials cut holes; everything else keeps the rasterised depth.
void alphaTest(uint objectIndex, float2 uv) {
  const MaterialData material = frame.materials[objects[objectIndex].material];
  if ((material.flags & 0xFF) == AlphaMask) {
    const float alpha = material.baseColor.a * objects[objectIndex].color.a *
                        textures[material.baseColorTexture].Sample(samplers[material.sampler], uv).a;
    if (alpha < material.emissive.a) {
      discard;
    }
  }
}

[shader("fragment")]
void fragmentMain(VertexOutput input) {
  alphaTest(input.objectIndex, input.uv);
}

// The pre-pass under temporal anti-aliasing (ADR-0024): the same depth, and the screen-space motion
// of each surface in UV units as a second attachment. The clip positions are interpolated and
// divided in the fragment stage, where they are perspective correct.
struct MotionVertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
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
  output.uv = base.uv;
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
  alphaTest(input.objectIndex, input.uv);
  if (input.previousClip.w <= 1.0e-4 || input.currentClip.w <= 1.0e-4) {
    return float2(0.0, 0.0);
  }
  return clipToUv(input.currentClip) - clipToUv(input.previousClip);
}
