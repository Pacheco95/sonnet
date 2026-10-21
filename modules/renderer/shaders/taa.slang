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
  uint historyAge; // how many frames each history pixel has accumulated, in 1/255ths
  uint glassMotion;    // the glass's own motion, in UV units
  uint useGlassMotion; // nonzero: a pixel the glass covers takes it in place of the surface behind's
  uint padding;
  float2 size;
};

[[vk::push_constant]]
ConstantBuffer<TaaConstants> taa;

struct VertexOutput {
  float4 position : SV_Position;
};

struct ResolveOutput {
  float4 color : SV_Target0; // the resolved colour, with the glass mark below in alpha
  float age : SV_Target1;    // frames accumulated in it, over 255
};

ResolveOutput resolved(float3 color, float mark, float frames) {
  ResolveOutput output;
  output.color = float4(color, mark);
  output.age = frames / 255.0;
  return output;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = fullscreenTriangle(vertexId);
  return output;
}

// How far the glass around a pixel may differ from the history's before the history is dropped.
static const float GlassMarkChange = 0.5;
// The most frames a pixel's history averages evenly; its blend never goes below one over this.
static const float AgeCap = 16.0;

float3 toYCoCg(float3 c) {
  return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float luma(float3 c) {
  return dot(c, float3(0.299, 0.587, 0.114));
}

float2 clipToUv(float4 clip) {
  return float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
}

// The history at `uv` through a Catmull-Rom filter in nine bilinear taps, whose negative lobes are
// clamped away afterwards. Alpha is the glass mark below, filtered the same way.
float4 historyAt(float2 uv) {
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
  float4 result = image.SampleLevel(linear, float2(t0.x, t0.y), 0.0) * (w0.x * w0.y);
  result += image.SampleLevel(linear, float2(t12.x, t0.y), 0.0) * (w12.x * w0.y);
  result += image.SampleLevel(linear, float2(t3.x, t0.y), 0.0) * (w3.x * w0.y);
  result += image.SampleLevel(linear, float2(t0.x, t12.y), 0.0) * (w0.x * w12.y);
  result += image.SampleLevel(linear, float2(t12.x, t12.y), 0.0) * (w12.x * w12.y);
  result += image.SampleLevel(linear, float2(t3.x, t12.y), 0.0) * (w3.x * w12.y);
  result += image.SampleLevel(linear, float2(t0.x, t3.y), 0.0) * (w0.x * w3.y);
  result += image.SampleLevel(linear, float2(t12.x, t3.y), 0.0) * (w12.x * w3.y);
  result += image.SampleLevel(linear, float2(t3.x, t3.y), 0.0) * (w3.x * w3.y);
  return float4(max(result.rgb, float3(0.0, 0.0, 0.0)), saturate(result.a));
}

[shader("fragment")]
ResolveOutput resolve(VertexOutput input) {
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
  // What the alpha of the resolve holds: the share of the 5x5 neighbourhood that is not glass.
  int glassNeighbours = 0;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      glassNeighbours += currentImage.Load(int3(clamp(pixel + int2(x, y), int2(0, 0), last), 0)).a < 0.5 ? 1 : 0;
    }
  }
  const float markNow = 1.0 - float(glassNeighbours) / 25.0;
  if (taa.historyValid == 0) {
    return resolved(center, markNow, 1.0);
  }

  float2 motion;
  if (closest <= 0.0 && frame.cameraForward.w != 0.0) {
    motion = float2(0.0);
  } else if (closest <= 0.0) {
    const float3 direction = skyRay(float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0));
    motion = clipToUv(mul(frame.unjitteredViewProjection, float4(direction, 0.0))) -
             clipToUv(mul(frame.previousViewProjection, float4(direction, 0.0)));
  } else {
    motion = textures[taa.motion].Load(int3(closestPixel, 0)).xy;
  }
  // The experiment of #352: where the glass is, reproject with its own motion. The transmitted
  // background then takes the glass's motion in place of its own.
  if (taa.useGlassMotion != 0 && currentImage.Load(int3(pixel, 0)).a < 0.5) {
    motion = textures[taa.glassMotion].Load(int3(pixel, 0)).xy;
  }
  const float2 previousUv = uv - motion;
  if (any(previousUv < 0.0) || any(previousUv > 1.0)) {
    return resolved(center, markNow, 1.0);
  }

  const float historyFrames = round(textures[taa.historyAge].SampleLevel(samplers[taa.sampler], previousUv, 0.0).r * 255.0);
  // The box the neighbourhood spans: its mean and one and a quarter deviations, within its extremes.
  const float3 mean = sum / 9.0;
  const float3 deviation = sqrt(max(sumSquares / 9.0 - mean * mean, float3(0.0, 0.0, 0.0)));
  const float3 boxLow = max(mean - 1.25 * deviation, low);
  const float3 boxHigh = min(mean + 1.25 * deviation, high);
  const float3 boxCenter = 0.5 * (boxHigh + boxLow);
  const float3 extent = 0.5 * (boxHigh - boxLow) + 1.0e-4;

  const float4 historySample = historyAt(previousUv);
  const float3 history = historySample.rgb;
  // Glass is not in the motion: the forward pass leaves alpha 0 where a blended or transmissive
  // surface drew, and the resolve carries that mark along with the colour. Where the glass around
  // the pixel is not where it was in the history, the history is the other layer's, and no clip can
  // tell it from a plausible neighbour, so it is dropped (ADR-0033, "TAA"). The mark is the share
  // of the 5x5 neighbourhood that is glass rather than the pixel's own: on a still glass edge a
  // pixel's own mark flips with the jitter while its neighbourhood hardly does, and the accumulation
  // is what anti-aliases that pixel.
  if (abs(markNow - historySample.a) > GlassMarkChange) {
    return resolved(center, markNow, 1.0);
  }
  const float3 offset = toYCoCg(history) - boxCenter;
  const float3 scaled = abs(offset / extent);
  const float outside = max(scaled.x, max(scaled.y, scaled.z));
  float3 clipped = toYCoCg(history);
  if (outside > 1.0) {
    clipped = boxCenter + offset / outside;
  }
  const float3 resolvedHistory =
      float3(clipped.x - clipped.z + clipped.y, clipped.x + clipped.z, clipped.x - clipped.z - clipped.y);

  // How much to trust the new frame: one over the frames the history holds, so a pixel that starts
  // over averages its first samples evenly, and no less than the usual 0.1 once it has settled; a
  // history that had to be clipped, or that was moved by more than a fraction of a pixel, is worth
  // less. The age that goes forward is what that weight stands for.
  const float clipAmount = saturate(outside - 1.0);
  const float motionPixels = length(motion * taa.size);
  const float blend = clamp(1.0 / (historyFrames + 1.0) + 0.4 * clipAmount + 0.2 * saturate(motionPixels * 0.25),
                            1.0 / AgeCap, 0.8);
  const float currentWeight = blend / (1.0 + luma(center));
  const float historyWeight = (1.0 - blend) / (1.0 + luma(max(resolvedHistory, float3(0.0, 0.0, 0.0))));
  const float3 result = (center * currentWeight + max(resolvedHistory, float3(0.0, 0.0, 0.0)) * historyWeight) /
                        (currentWeight + historyWeight);
  return resolved(any(isnan(result)) ? center : result, lerp(historySample.a, markNow, blend),
                  clamp(round(1.0 / blend), 1.0, AgeCap));
}
