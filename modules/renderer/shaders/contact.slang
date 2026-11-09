// The sun's screen-space contact shadows (ADR-0039): a short march through the completed pre-pass
// depth towards the sun, once per pixel, into an R8 visibility mask the forward pass takes the
// minimum of with the cascades. It restores the contacts of feet and small props that the shadow
// maps are too coarse for, and nothing more: what is off screen or hidden behind the nearest
// surface casts nothing here.
import sonnet;

struct ContactConstants {
  float3 toLight; // towards the sun, in view space, normalised
  uint depth;     // the pre-pass depth, an index into depthTextures[]
  float2 jitter;  // the raster jitter the depth was drawn with, in NDC
  float rayLength;
  float thickness; // how far behind a visible surface, in view depth, a sample still counts as inside it
  uint steps;
  float stepOffset; // shifts where each pixel's steps fall, from the raster jitter
  uint padding0;
  uint padding1;
};

[[vk::push_constant]]
ConstantBuffer<ContactConstants> contact;

struct VertexOutput {
  float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = fullscreenTriangle(vertexId);
  return output;
}

// The normal offset of the ray's origin and the depth a sample must be behind a surface to hit it,
// in pixel footprints at the depth concerned. The offset keeps the receiver from shadowing itself:
// a point above a plane is in front of it along every view ray, and the plane's depth at the texel
// centre differs from its depth under the sample by at most half a footprint's slope.
static const float OriginOffset = 1.5;
static const float HitTolerance = 1.0;
// The share of the ray over which a hit fades back to visible, and the screen border it fades over.
static const float FadeShare = 0.2;
static const float BorderPixels = 4.0;

// The view-space point of the surface at `pixel` with raw depth `d`, through the inverse of the
// jittered projection the depth was drawn with: the unjittered inverse after taking the jitter out
// of the NDC, with the Y flip of the negative viewport.
float3 viewPosition(float2 pixel, float d) {
  const float2 uv = pixel / frame.targetSize;
  const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) - contact.jitter;
  const float4 h = mul(frame.inverseProjection, float4(ndc, d, 1.0));
  return h.xyz / h.w;
}

// The surface a pixel shows, false for the sky (depth 0) and outside the image.
bool surfaceAt(int2 p, out float3 position) {
  position = float3(0.0);
  const int2 size = int2(frame.targetSize);
  if (any(p < 0) || any(p >= size)) {
    return false;
  }
  const float d = depthTextures[contact.depth].Load(int3(p, 0));
  if (d <= 0.0) {
    return false;
  }
  position = viewPosition(float2(p) + 0.5, d);
  return true;
}

// The surface's derivative along one axis, from the side that best continues it: each side's
// neighbour and the one past it extrapolate back to the centre, and the side that misses it by less
// is on the same surface. A silhouette is never averaged across.
bool axisDerivative(int2 p, int2 step, float3 center, out float3 derivative) {
  derivative = float3(0.0);
  float3 after, afterNext, before, beforeNext;
  const bool hasAfter = surfaceAt(p + step, after);
  const bool hasBefore = surfaceAt(p - step, before);
  if (!hasAfter && !hasBefore) {
    return false;
  }
  const float Unusable = 3.0e38;
  float afterError = Unusable;
  if (hasAfter) {
    afterError = surfaceAt(p + 2 * step, afterNext) ? abs(2.0 * after.z - afterNext.z - center.z) : abs(after.z - center.z);
  }
  float beforeError = Unusable;
  if (hasBefore) {
    beforeError =
        surfaceAt(p - 2 * step, beforeNext) ? abs(2.0 * before.z - beforeNext.z - center.z) : abs(before.z - center.z);
  }
  derivative = afterError <= beforeError ? after - center : center - before;
  return true;
}

// Interleaved gradient noise: a per-pixel offset whose neighbours differ, so the steps' banding
// turns into a fine pattern the TAA resolve averages away.
float pixelNoise(float2 pixel) {
  return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Target {
  const int2 p = int2(input.position.xy);
  float3 center;
  if (!surfaceAt(p, center)) {
    return 1.0; // the sky receives nothing and occludes nothing
  }
  float3 dx, dy;
  if (!axisDerivative(p, int2(1, 0), center, dx) || !axisDerivative(p, int2(0, 1), center, dy)) {
    return 1.0;
  }
  const float3 crossed = cross(dx, dy);
  const float crossedLength = length(crossed);
  if (!(crossedLength > 1.0e-6 * length(dx) * length(dy))) {
    return 1.0; // degenerate, or not finite
  }
  const bool orthographic = frame.cameraForward.w != 0.0;
  float3 n = crossed / crossedLength;
  const float3 toCamera = orthographic ? float3(0.0, 0.0, 1.0) : -center;
  if (dot(n, toCamera) < 0.0) {
    n = -n;
  }
  // A surface facing away from the sun has no sun light to shadow.
  if (dot(n, contact.toLight) <= 0.0) {
    return 1.0;
  }

  // One pixel's width in view space at the receiver's depth; with perspective it grows with depth.
  const float centerDepth = -center.z;
  const float d = depthTextures[contact.depth].Load(int3(p, 0));
  const float footprint = length(viewPosition(float2(p) + float2(1.5, 0.5), d) - center);
  const float3 origin = center + n * (OriginOffset * footprint);
  const float2 size = frame.targetSize;
  const uint steps = max(contact.steps, 1u);
  const float offset = frac(pixelNoise(input.position.xy) + contact.stepOffset);
  float visibility = 1.0;
  for (uint i = 0; i < steps; ++i) {
    const float t = (float(i) + offset) / float(steps);
    const float3 position = origin + contact.toLight * (contact.rayLength * t);
    const float4 clip = mul(frame.projection, float4(position, 1.0));
    if (clip.w <= 0.0) {
      break;
    }
    const float3 ndc = clip.xyz / clip.w;
    // Past the near plane (reversed-Z: above 1) the ray is behind the camera's clip; it occludes no more.
    if (ndc.z > 1.0 || ndc.z < 0.0) {
      break;
    }
    const float2 uv = float2((ndc.x + contact.jitter.x) * 0.5 + 0.5, 0.5 - (ndc.y + contact.jitter.y) * 0.5);
    const float2 pixel = uv * size;
    if (any(pixel < 0.0) || any(pixel >= size)) {
      break; // off screen: nothing more to see
    }
    float3 surface;
    if (!surfaceAt(int2(pixel), surface)) {
      continue; // the sky occludes nothing
    }
    const float rayDepth = -position.z;
    const float sceneDepth = -surface.z;
    const float tolerance = HitTolerance * (orthographic ? footprint : footprint * rayDepth / centerDepth);
    const float behind = rayDepth - sceneDepth;
    // Behind the visible surface, but not so far that the surface is only a silhouette in front of
    // the ray: a foreground edge is not an infinitely thick occluder.
    if (behind > tolerance && behind < contact.thickness) {
      const float border = min(min(pixel.x, pixel.y), min(size.x - pixel.x, size.y - pixel.y));
      const float hit = saturate((t - (1.0 - FadeShare)) / FadeShare);
      visibility = lerp(1.0, hit, saturate(border / BorderPixels));
      break; // the first hit is the nearest, and the darkest
    }
  }
  return visibility;
}
