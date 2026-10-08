// The temporal anti-aliasing resolve (ADR-0024), in HDR between the forward pass and bloom. Each
// pixel takes the motion of the closest surface in its 3x3 neighbourhood, reads last frame's
// resolve there with a Catmull-Rom filter, clips it to the box the neighbourhood's mean and
// variance span in YCoCg, and blends it with the new colour, weighted by luminance so a bright
// pixel does not dominate the average. The blend is 0.1 for a still pixel whose history fits,
// and moves towards the new frame as the clip distance and the motion grow. The sky has no motion
// vector and takes the one the camera's rotation alone gives.
import sonnet;

struct TaaConstants {
  uint current; // the unresolved scene colour
  uint history; // last frame's resolve; the current colour again when there is none
  uint motion;  // the pre-pass's motion, in UV units
  uint depth;   // an index into depthTextures[]
  uint sampler;
  uint historyValid;
  float2 size;
};

[[vk::push_constant]]
ConstantBuffer<TaaConstants> taa;

struct VertexOutput {
  float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = fullscreenTriangle(vertexId);
  return output;
}

float3 toYCoCg(float3 c) {
  return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float luma(float3 c) {
  return dot(c, float3(0.299, 0.587, 0.114));
}

float2 clipToUv(float4 clip) {
  return float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
}

// The ray through the pixel from the camera, in the jittered image: the point on the near plane
// the jittered matrix looks through, less the camera.
float3 skyDirection(float2 uv) {
  const float4 point = mul(frame.inverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0));
  return point.xyz / point.w - frame.cameraPosition.xyz;
}

// The history at `uv` through a Catmull-Rom filter in nine bilinear taps, whose negative lobes are
// clamped away afterwards.
float3 historyAt(float2 uv) {
  const float2 position = uv * taa.size;
  const float2 center = floor(position - 0.5) + 0.5;
  const float2 f = position - center;
  const float2 f2 = f * f;
  const float2 f3 = f2 * f;
  const float2 w0 = f2 - 0.5 * (f3 + f);
  const float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
  const float2 w3 = 0.5 * (f3 - f2);
  const float2 w2 = 1.0 - w0 - w1 - w3;
  const float2 w12 = w1 + w2;
  const float2 t0 = (center - 1.0) / taa.size;
  const float2 t3 = (center + 2.0) / taa.size;
  const float2 t12 = (center + w2 / w12) / taa.size;
  const SamplerState linear = samplers[taa.sampler];
  const Texture2D<float4> image = textures[taa.history];
  float3 result = image.SampleLevel(linear, float2(t0.x, t0.y), 0.0).rgb * (w0.x * w0.y);
  result += image.SampleLevel(linear, float2(t12.x, t0.y), 0.0).rgb * (w12.x * w0.y);
  result += image.SampleLevel(linear, float2(t3.x, t0.y), 0.0).rgb * (w3.x * w0.y);
  result += image.SampleLevel(linear, float2(t0.x, t12.y), 0.0).rgb * (w0.x * w12.y);
  result += image.SampleLevel(linear, float2(t12.x, t12.y), 0.0).rgb * (w12.x * w12.y);
  result += image.SampleLevel(linear, float2(t3.x, t12.y), 0.0).rgb * (w3.x * w12.y);
  result += image.SampleLevel(linear, float2(t0.x, t3.y), 0.0).rgb * (w0.x * w3.y);
  result += image.SampleLevel(linear, float2(t12.x, t3.y), 0.0).rgb * (w12.x * w3.y);
  result += image.SampleLevel(linear, float2(t3.x, t3.y), 0.0).rgb * (w3.x * w3.y);
  return max(result, float3(0.0, 0.0, 0.0));
}

[shader("fragment")]
float4 resolve(VertexOutput input) : SV_Target {
  const int2 pixel = int2(input.position.xy);
  const int2 last = int2(taa.size) - 1;
  const float2 uv = input.position.xy / taa.size;
  const Texture2D<float4> currentImage = textures[taa.current];

  float3 sum = float3(0.0, 0.0, 0.0);
  float3 sumSquares = float3(0.0, 0.0, 0.0);
  float3 low = float3(1.0e9, 1.0e9, 1.0e9);
  float3 high = float3(-1.0e9, -1.0e9, -1.0e9);
  float3 center = float3(0.0, 0.0, 0.0);
  float closest = -1.0;
  int2 closestPixel = pixel;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      const int2 p = clamp(pixel + int2(x, y), int2(0, 0), last);
      const float3 color = max(currentImage.Load(int3(p, 0)).rgb, float3(0.0, 0.0, 0.0));
      const float3 ycocg = toYCoCg(color);
      sum += ycocg;
      sumSquares += ycocg * ycocg;
      low = min(low, ycocg);
      high = max(high, ycocg);
      if (x == 0 && y == 0) {
        center = color;
      }
      // Reversed-Z: the closest surface has the largest depth.
      const float depth = depthTextures[taa.depth].Load(int3(p, 0));
      if (depth > closest) {
        closest = depth;
        closestPixel = p;
      }
    }
  }
  if (taa.historyValid == 0) {
    return float4(center, 1.0);
  }

  float2 motion;
  if (closest <= 0.0) {
    const float3 direction = skyDirection(uv);
    motion = clipToUv(mul(frame.unjitteredViewProjection, float4(direction, 0.0))) -
             clipToUv(mul(frame.previousViewProjection, float4(direction, 0.0)));
  } else {
    motion = textures[taa.motion].Load(int3(closestPixel, 0)).xy;
  }
  const float2 previousUv = uv - motion;
  if (any(previousUv < 0.0) || any(previousUv > 1.0)) {
    return float4(center, 1.0);
  }

  // The box the neighbourhood spans: its mean and one and a quarter deviations, within its extremes.
  const float3 mean = sum / 9.0;
  const float3 deviation = sqrt(max(sumSquares / 9.0 - mean * mean, float3(0.0, 0.0, 0.0)));
  const float3 boxLow = max(mean - 1.25 * deviation, low);
  const float3 boxHigh = min(mean + 1.25 * deviation, high);
  const float3 boxCenter = 0.5 * (boxHigh + boxLow);
  const float3 extent = 0.5 * (boxHigh - boxLow) + 1.0e-4;

  const float3 history = historyAt(previousUv);
  const float3 offset = toYCoCg(history) - boxCenter;
  const float3 scaled = abs(offset / extent);
  const float outside = max(scaled.x, max(scaled.y, scaled.z));
  float3 clipped = toYCoCg(history);
  if (outside > 1.0) {
    clipped = boxCenter + offset / outside;
  }
  const float3 resolvedHistory =
      float3(clipped.x - clipped.z + clipped.y, clipped.x + clipped.z, clipped.x - clipped.z - clipped.y);

  // How much to trust the new frame: a history that had to be clipped, or that was moved by more
  // than a fraction of a pixel, is worth less.
  const float clipAmount = saturate(outside - 1.0);
  const float motionPixels = length(motion * taa.size);
  const float blend = clamp(0.1 + 0.4 * clipAmount + 0.2 * saturate(motionPixels * 0.25), 0.1, 0.8);
  const float currentWeight = blend / (1.0 + luma(center));
  const float historyWeight = (1.0 - blend) / (1.0 + luma(max(resolvedHistory, float3(0.0, 0.0, 0.0))));
  const float3 result = (center * currentWeight + max(resolvedHistory, float3(0.0, 0.0, 0.0)) * historyWeight) /
                        (currentWeight + historyWeight);
  return float4(any(isnan(result)) ? center : result, 1.0);
}
