// Screen-space ambient occlusion (ADR-0039): ground-truth ambient occlusion (GTAO) at half
// resolution from the completed opaque and masked depth, with normals reconstructed from that depth.
// `search` integrates the cosine-weighted visibility between the two horizons of a few slices
// through each half-resolution pixel's representative surface; under TAA `temporal` blends it with
// the view's own scalar history; `blur` is a separable five-tap bilateral filter; `upsample` writes
// the full-resolution R8 mask the forward pass multiplies into the indirect light. Every half-
// resolution image holds the visibility in x, the surface's positive view depth in y (0: no surface)
// and its octahedral world-space normal in zw, which the filters and the history compare. The
// visibility stays unclamped up to 2 until the mask: a few slices through an open surface at a
// grazing angle overshoot 1 as often as they undershoot it, and clamping each texel before the
// filters average them would darken every slanted surface.
import sonnet;

struct AoConstants {
  float4x4 inverseProjection; // of the jittered raster projection: a depth sample into view space
  float4 previousDepthRow;    // temporal: dot with a view-space position gives its last frame's view depth
  float2 jitterDelta;         // temporal: last frame's raster jitter minus this one's, in UV
  float pixelRadius;          // the radius in full-resolution pixels, at view depth 1 under perspective
  float radius;               // metres
  uint depth;                 // depthTextures[]: the completed full-resolution depth
  uint source;                // textures[]: the pass's half-resolution input
  uint history;               // textures[]: temporal, last frame's guides
  uint motion;                // textures[]: temporal, the pre-pass's motion
  uint counts;                // slices in the low byte, steps per side in the next, the jitter phase above
  uint flags;                 // AoHistoryValid, AoVertical
  float strength;
  uint padding;
};

static const uint AoHistoryValid = 1u;
static const uint AoVertical = 2u; // blur: along y

[[vk::push_constant]]
ConstantBuffer<AoConstants> ao;

struct VertexOutput {
  float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = fullscreenTriangle(vertexId);
  return output;
}

bool orthographic() {
  return frame.cameraForward.w != 0.0;
}

int2 fullSize() {
  uint width, height;
  depthTextures[ao.depth].GetDimensions(width, height);
  return int2(width, height);
}

// ceil(full / 2): an odd edge's last footprint holds only its valid pixels.
int2 halfSize() {
  return (fullSize() + 1) / 2;
}

bool inside(int2 p, int2 size) {
  return all(p >= int2(0, 0)) && all(p < size);
}

// Reversed-Z, 0 is the sky; off the image there is nothing.
float depthAt(int2 p) {
  return inside(p, fullSize()) ? depthTextures[ao.depth].Load(int3(p, 0)) : 0.0;
}

// The view-space point a full-resolution sample at `pixel` (its centre at +0.5) with `depth` stands
// for, through the jittered projection it was rasterised with: the Y-flipped viewport puts NDC +1 at
// the top row.
float3 viewPosition(float2 pixel, float depth) {
  const float2 size = float2(fullSize());
  const float2 ndc = float2(pixel.x / size.x * 2.0 - 1.0, 1.0 - pixel.y / size.y * 2.0);
  const float4 h = mul(ao.inverseProjection, float4(ndc, depth, 1.0));
  return h.xyz / h.w;
}

// A half-resolution texel's surface at a positive view depth, on the ray through its footprint's
// centre: what the filters compare, without reading the full-resolution depth again.
float3 halfViewPosition(int2 texel, float viewDepth) {
  const float3 near = viewPosition(float2(texel * 2 + 1), 1.0);
  if (orthographic()) {
    return float3(near.xy, -viewDepth);
  }
  return near * (viewDepth / max(-near.z, 1.0e-6));
}

// Towards the camera from a view-space point.
float3 toViewer(float3 position) {
  return orthographic() ? float3(0.0, 0.0, 1.0) : normalize(-position);
}

// One axis's derivative of the surface at `p`: the side whose depth, extrapolated through its next
// neighbour, best continues the centre, so a silhouette is never averaged across. In reversed-Z a
// plane's depth is affine in screen space for either projection, so the extrapolation is exact on a
// plane. False when neither side holds a surface.
bool axisDerivative(int2 p, int2 step, float centre, float3 position, out float3 derivative) {
  derivative = float3(0.0);
  const float forward1 = depthAt(p + step);
  const float backward1 = depthAt(p - step);
  const bool hasForward = forward1 > 0.0;
  const bool hasBackward = backward1 > 0.0;
  if (!hasForward && !hasBackward) {
    return false;
  }
  const float forward2 = depthAt(p + 2 * step);
  const float backward2 = depthAt(p - 2 * step);
  const float forwardError = !hasForward ? 1.0e30 : forward2 > 0.0 ? abs(2.0 * forward1 - forward2 - centre) : abs(forward1 - centre);
  const float backwardError = !hasBackward ? 1.0e30 : backward2 > 0.0 ? abs(2.0 * backward1 - backward2 - centre) : abs(backward1 - centre);
  if (forwardError <= backwardError) {
    derivative = viewPosition(float2(p + step) + 0.5, forward1) - position;
  } else {
    derivative = position - viewPosition(float2(p - step) + 0.5, backward1);
  }
  return true;
}

// The geometric normal of the surface at full-resolution `p`, in view space and facing the camera;
// false for a degenerate reconstruction, which is unoccluded.
bool reconstructNormal(int2 p, float depth, float3 position, out float3 normal) {
  normal = float3(0.0, 0.0, 1.0);
  float3 dx, dy;
  if (!axisDerivative(p, int2(1, 0), depth, position, dx) || !axisDerivative(p, int2(0, 1), depth, position, dy)) {
    return false;
  }
  const float3 c = cross(dx, dy);
  const float area = length(c);
  if (!(area > 1.0e-6 * length(dx) * length(dy)) || !(area > 0.0)) {
    return false;
  }
  normal = c / area;
  if (dot(normal, toViewer(position)) < 0.0) {
    normal = -normal;
  }
  return true;
}

// The rotation part of the view matrix, transposed: view space to world space, and back.
float3 viewToWorld(float3 v) {
  return v.x * frame.view[0].xyz + v.y * frame.view[1].xyz + v.z * frame.view[2].xyz;
}

float3 worldToView(float3 w) {
  return float3(dot(frame.view[0].xyz, w), dot(frame.view[1].xyz, w), dot(frame.view[2].xyz, w));
}

float2 encodeNormal(float3 n) {
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  if (n.z < 0.0) {
    const float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return (1.0 - abs(n.yx)) * s;
  }
  return n.xy;
}

float3 decodeNormal(float2 e) {
  float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
  if (n.z < 0.0) {
    const float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    n.xy = (1.0 - abs(n.yx)) * s;
  }
  return normalize(n);
}

bool finite4(float4 v) {
  return !any(isnan(v)) && !any(isinf(v));
}

// The closest surface of a half-resolution texel's full-resolution 2x2 footprint, the largest
// reversed-Z depth, never an average of foreground and background; depth 0 when all of it is sky.
int2 representative(int2 texel, out float depth) {
  const int2 size = fullSize();
  int2 best = texel * 2;
  depth = 0.0;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      const int2 p = texel * 2 + int2(x, y);
      if (inside(p, size)) {
        const float d = depthTextures[ao.depth].Load(int3(p, 0));
        if (d > depth) {
          depth = d;
          best = p;
        }
      }
    }
  }
  return best;
}

// Interleaved gradient noise (Jimenez), in [0, 1).
float gradientNoise(float2 p) {
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

float horizonArc(float h, float n, float cosN) {
  return (cosN + 2.0 * h * sin(n) - cos(2.0 * h - n)) * 0.25;
}

[shader("fragment")]
float4 search(VertexOutput input) : SV_Target {
  const int2 texel = int2(input.position.xy);
  float depth;
  const int2 p = representative(texel, depth);
  if (depth <= 0.0) {
    return float4(1.0, 0.0, 0.0, 0.0); // sky: unoccluded, and no surface to compare
  }
  const float3 position = viewPosition(float2(p) + 0.5, depth);
  const float viewDepth = orthographic() ? -position.z : max(-position.z, 1.0e-6);
  const float3 v = toViewer(position);
  float3 normal;
  if (!reconstructNormal(p, depth, position, normal)) {
    return float4(1.0, viewDepth, encodeNormal(viewToWorld(v)));
  }
  const float2 guides = encodeNormal(viewToWorld(normal));
  const uint slices = max(ao.counts & 0xFFu, 1u);
  const uint steps = max((ao.counts >> 8) & 0xFFu, 1u);
  const float phase = float(ao.counts >> 16);
  const float pixels = orthographic() ? ao.pixelRadius : ao.pixelRadius / viewDepth;
  if (pixels < 1.0) {
    return float4(1.0, viewDepth, guides); // the radius is under a pixel: nothing to find
  }
  // A deterministic function of the raster jitter's phase: eight patterns under TAA, one without.
  const float rotation = frac(orderedThreshold(float2(texel)) + phase * 0.125);
  const float offset = frac(gradientNoise(float2(texel)) + phase * 0.6180339887);
  const float2 centre = float2(p) + 0.5;
  const int2 size = fullSize();
  const float falloff = max(ao.radius * 0.4, 1.0e-4);

  float visibility = 0.0;
  for (uint slice = 0; slice < slices; ++slice) {
    const float phi = (float(slice) + rotation) * Pi / float(slices);
    const float2 direction = float2(cos(phi), sin(phi)); // in pixels, y down
    const float3 directionView = float3(direction.x, -direction.y, 0.0);
    const float3 orthoDirection = directionView - dot(directionView, v) * v;
    const float3 axis = normalize(cross(orthoDirection, v));
    const float3 projected = normal - axis * dot(normal, axis);
    const float projectedLength = length(projected);
    const float cosN = saturate(dot(projected, v) / max(projectedLength, 1.0e-6));
    const float n = (dot(orthoDirection, projected) >= 0.0 ? 1.0 : -1.0) * acos(cosN);
    // The tangent plane is the lowest horizon on each side.
    const float lowPositive = cos(n + Pi * 0.5);
    const float lowNegative = cos(n - Pi * 0.5);
    float horizonPositive = lowPositive;
    float horizonNegative = lowNegative;
    // Samples stay on the slice's line: whole pixels along its major axis, and the depth interpolated
    // between the two pixel centres the line passes between on the other. A sample at the nearest
    // pixel's centre instead lies off the slice, beside it, where an open plane already rises above
    // the slice's horizon on one side or the other, and the maximum keeps that as occlusion.
    const bool alongX = abs(direction.x) >= abs(direction.y);
    const float major = alongX ? abs(direction.x) : abs(direction.y);
    for (uint s = 0; s < steps; ++s) {
      const float t = (float(s) + offset) / float(steps);
      const float distance = 1.0 + t * t * (pixels - 1.0);
      const float pixelsAlong = max(round(distance * major), 1.0);
      for (int side = 0; side < 2; ++side) {
        const float2 at = centre + (side == 0 ? direction : -direction) * (pixelsAlong / major);
        const float minor = (alongX ? at.y : at.x) - 0.5;
        const float low = floor(minor);
        const float fraction = minor - low;
        const int2 q0 = alongX ? int2(int(floor(at.x)), int(low)) : int2(int(low), int(floor(at.y)));
        const int2 q1 = q0 + (alongX ? int2(0, 1) : int2(1, 0));
        const float d0 = depthAt(q0);
        const float d1 = depthAt(q1);
        // Off screen or sky on the nearer side: no occluder. Sky on the farther side: the nearer alone.
        const float nearer = fraction < 0.5 ? d0 : d1;
        if (nearer <= 0.0) {
          continue;
        }
        const float sampleDepth = d0 > 0.0 && d1 > 0.0 ? lerp(d0, d1, fraction) : nearer;
        const float3 delta = viewPosition(at, sampleDepth) - position;
        const float length2 = dot(delta, delta);
        if (length2 < 1.0e-12) {
          continue;
        }
        const float len = sqrt(length2);
        // Nothing past the radius, and the contribution falls smoothly to zero at it.
        const float weight = smoothstep(0.0, 1.0, saturate((ao.radius - len) / falloff));
        if (side == 0) {
          horizonPositive = max(horizonPositive, lerp(lowPositive, dot(delta / len, v), weight));
        } else {
          horizonNegative = max(horizonNegative, lerp(lowNegative, dot(delta / len, v), weight));
        }
      }
    }
    float h0 = -acos(clamp(horizonNegative, -1.0, 1.0));
    float h1 = acos(clamp(horizonPositive, -1.0, 1.0));
    h0 = n + max(h0 - n, -Pi * 0.5);
    h1 = n + min(h1 - n, Pi * 0.5);
    visibility += projectedLength * (horizonArc(h0, n, cosN) + horizonArc(h1, n, cosN));
  }
  visibility = clamp(visibility / float(slices), 0.0, 2.0);
  return float4(isnan(visibility) ? 1.0 : visibility, viewDepth, guides);
}

// Whether two guides describe the same surface: within the relative view depth and the angle.
bool compatible(float4 a, float4 b, float depthTolerance, float normalCos) {
  return b.y > 0.0 && abs(a.y - b.y) <= depthTolerance * a.y + 1.0e-3 &&
         dot(decodeNormal(a.zw), decodeNormal(b.zw)) >= normalCos;
}

[shader("fragment")]
float4 temporal(VertexOutput input) : SV_Target {
  const int2 texel = int2(input.position.xy);
  const Texture2D<float4> source = textures[ao.source];
  const float4 current = source.Load(int3(texel, 0));
  if (current.y <= 0.0 || (ao.flags & AoHistoryValid) == 0) {
    return current;
  }
  // The selected sample's own motion, then last frame's raster position of the same surface.
  float depth;
  const int2 p = representative(texel, depth);
  const float2 full = float2(fullSize());
  const float2 motion = textures[ao.motion].Load(int3(p, 0)).xy;
  const float2 uv = (float2(p) + 0.5) / full - motion + ao.jitterDelta;
  if (any(uv < 0.0) || any(uv > 1.0)) {
    return current; // off screen last frame
  }
  // Where the surface was in last frame's view depth, if it did not move; a surface that did may
  // lose its history, which is better than keeping another surface's occlusion.
  const float predicted = dot(ao.previousDepthRow, float4(viewPosition(float2(p) + 0.5, depth), 1.0));
  const float3 normal = decodeNormal(current.zw);
  // A half-resolution texel's footprint centre is at full-resolution 2t + 1.
  const float2 at = uv * full * 0.5 - 0.5;
  const int2 base = int2(floor(at));
  const float2 f = at - float2(base);
  const int2 size = halfSize();
  const Texture2D<float4> history = textures[ao.history];
  float sum = 0.0;
  float weights = 0.0;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      const int2 h = base + int2(x, y);
      if (!inside(h, size)) {
        continue;
      }
      const float4 guide = history.Load(int3(h, 0));
      // The guides are half floats: the depth tolerance is well above their precision, and a
      // non-finite or empty guide is no history.
      if (!finite4(guide) || guide.y <= 0.0 || abs(guide.y - predicted) > 0.02 * predicted + 0.005 ||
          dot(decodeNormal(guide.zw), normal) < 0.9) {
        continue;
      }
      const float w = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y) + 1.0e-4;
      sum += clamp(guide.x, 0.0, 2.0) * w;
      weights += w;
    }
  }
  if (weights <= 0.0) {
    return current; // disoccluded: the current estimate alone
  }
  // Clamped to what the compatible current neighbourhood spans.
  float low = current.x;
  float high = current.x;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      const int2 q = texel + int2(x, y);
      if ((x != 0 || y != 0) && inside(q, size)) {
        const float4 neighbour = source.Load(int3(q, 0));
        if (compatible(current, neighbour, 0.05, 0.8)) {
          low = min(low, neighbour.x);
          high = max(high, neighbour.x);
        }
      }
    }
  }
  const float accumulated = clamp(sum / weights, low, high);
  return float4(lerp(accumulated, current.x, 1.0 / 8.0), current.yzw);
}

[shader("fragment")]
float4 blur(VertexOutput input) : SV_Target {
  const int2 texel = int2(input.position.xy);
  const Texture2D<float4> source = textures[ao.source];
  const float4 centre = source.Load(int3(texel, 0));
  if (centre.y <= 0.0) {
    return centre;
  }
  const float3 position = halfViewPosition(texel, centre.y);
  const float3 normal = worldToView(decodeNormal(centre.zw));
  const int2 axis = (ao.flags & AoVertical) != 0 ? int2(0, 1) : int2(1, 0);
  const int2 size = halfSize();
  static const float kernel[5] = {1.0, 4.0, 6.0, 4.0, 1.0};
  float sum = 0.0;
  float weights = 0.0;
  for (int k = -2; k <= 2; ++k) {
    const int2 q = texel + axis * k;
    if (!inside(q, size)) {
      continue;
    }
    const float4 tap = source.Load(int3(q, 0));
    // The same surface: near the centre's tangent plane and facing the same way. The rest are left
    // out and the survivors renormalised.
    if (k != 0 && (tap.y <= 0.0 || abs(dot(halfViewPosition(q, tap.y) - position, normal)) > 0.02 * centre.y + 0.002 ||
                   dot(decodeNormal(tap.zw), decodeNormal(centre.zw)) < 0.8)) {
      continue;
    }
    sum += kernel[k + 2] * tap.x;
    weights += kernel[k + 2];
  }
  return float4(sum / weights, centre.yzw);
}

[shader("fragment")]
float upsample(VertexOutput input) : SV_Target {
  const int2 p = int2(input.position.xy);
  const float depth = depthTextures[ao.depth].Load(int3(p, 0));
  if (depth <= 0.0) {
    return 1.0;
  }
  const float3 position = viewPosition(float2(p) + 0.5, depth);
  float3 normal;
  if (!reconstructNormal(p, depth, position, normal)) {
    return 1.0;
  }
  const float viewDepth = abs(position.z);
  const float3 worldNormal = viewToWorld(normal);
  const Texture2D<float4> source = textures[ao.source];
  const float2 at = (float2(p) + 0.5) * 0.5 - 0.5;
  const int2 base = int2(floor(at));
  const float2 f = at - float2(base);
  const int2 size = halfSize();
  float sum = 0.0;
  float weights = 0.0;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      const int2 h = base + int2(x, y);
      if (!inside(h, size)) {
        continue;
      }
      const float4 tap = source.Load(int3(h, 0));
      if (tap.y <= 0.0 || abs(dot(halfViewPosition(h, tap.y) - position, normal)) > 0.02 * viewDepth + 0.002 ||
          dot(decodeNormal(tap.zw), worldNormal) < 0.8) {
        continue;
      }
      const float w = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y) + 1.0e-3;
      sum += tap.x * w;
      weights += w;
    }
  }
  // No compatible half-resolution surface: unoccluded rather than a dark fringe from another one.
  const float visibility = weights > 0.0 ? saturate(sum / weights) : 1.0;
  return lerp(1.0, visibility, saturate(ao.strength));
}
