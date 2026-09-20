// Post-processing over the HDR scene colour: the bloom chain (a 13-tap downsample and a tent
// upsample per level), tone mapping with exposure into the display-encoded target, and FXAA.
// Full-screen passes read their inputs from the bindless set by index.
import sonnet;

struct PostConstants {
  uint source;
  uint secondary; // the bloom level below, for upsample, or the bloom result, for tone mapping
  uint sampler;
  uint padding;
  float2 sourceTexel; // 1 / the source size
  float2 targetSize;
  float exposure;
  float bloomStrength;
  float padding1;
  float padding2;
};

[[vk::push_constant]]
ConstantBuffer<PostConstants> post;

struct VertexOutput {
  float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = fullscreenTriangle(vertexId);
  return output;
}

float3 fetch(uint image, float2 uv) {
  return textures[image].SampleLevel(samplers[post.sampler], uv, 0.0).rgb;
}

// Jimenez's 13-tap downsample, which suppresses fireflies better than a box filter.
[shader("fragment")]
float4 bloomDownsample(VertexOutput input) : SV_Target {
  const float2 uv = pixelUv(input.position.xy, post.targetSize);
  const float2 t = post.sourceTexel;
  const float3 a = fetch(post.source, uv + t * float2(-2.0, -2.0));
  const float3 b = fetch(post.source, uv + t * float2(0.0, -2.0));
  const float3 c = fetch(post.source, uv + t * float2(2.0, -2.0));
  const float3 d = fetch(post.source, uv + t * float2(-2.0, 0.0));
  const float3 e = fetch(post.source, uv);
  const float3 f = fetch(post.source, uv + t * float2(2.0, 0.0));
  const float3 g = fetch(post.source, uv + t * float2(-2.0, 2.0));
  const float3 h = fetch(post.source, uv + t * float2(0.0, 2.0));
  const float3 i = fetch(post.source, uv + t * float2(2.0, 2.0));
  const float3 j = fetch(post.source, uv + t * float2(-1.0, -1.0));
  const float3 k = fetch(post.source, uv + t * float2(1.0, -1.0));
  const float3 l = fetch(post.source, uv + t * float2(-1.0, 1.0));
  const float3 m = fetch(post.source, uv + t * float2(1.0, 1.0));
  float3 color = e * 0.125;
  color += (a + c + g + i) * 0.03125;
  color += (b + d + f + h) * 0.0625;
  color += (j + k + l + m) * 0.125;
  return float4(color, 1.0);
}

// The level below, blurred with a 3x3 tent, added to this level.
[shader("fragment")]
float4 bloomUpsample(VertexOutput input) : SV_Target {
  const float2 uv = pixelUv(input.position.xy, post.targetSize);
  const float2 t = post.sourceTexel;
  float3 color = fetch(post.secondary, uv) * 4.0;
  color += (fetch(post.secondary, uv + t * float2(-1.0, 0.0)) + fetch(post.secondary, uv + t * float2(1.0, 0.0)) +
            fetch(post.secondary, uv + t * float2(0.0, -1.0)) + fetch(post.secondary, uv + t * float2(0.0, 1.0))) *
           2.0;
  color += fetch(post.secondary, uv + t * float2(-1.0, -1.0)) + fetch(post.secondary, uv + t * float2(1.0, -1.0)) +
           fetch(post.secondary, uv + t * float2(-1.0, 1.0)) + fetch(post.secondary, uv + t * float2(1.0, 1.0));
  color /= 16.0;
  return float4(fetch(post.source, uv) + color, 1.0);
}

// ACES filmic approximation (Narkowicz).
float3 aces(float3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Into the UNORM target: tone-mapped and encoded for display.
[shader("fragment")]
float4 tonemap(VertexOutput input) : SV_Target {
  const float2 uv = pixelUv(input.position.xy, post.targetSize);
  float3 hdr = fetch(post.source, uv);
  if (post.secondary != 0xFFFFFFFF) {
    hdr = lerp(hdr, fetch(post.secondary, uv), post.bloomStrength);
  }
  const float3 mapped = aces(hdr * post.exposure);
  return float4(pow(mapped, 1.0 / 2.2), 1.0);
}

// A straight copy of a display-encoded image into a target of another format: what the player
// puts its finished scene into the acquired swapchain image with.
[shader("fragment")]
float4 blit(VertexOutput input) : SV_Target {
  return float4(fetch(post.source, pixelUv(input.position.xy, post.targetSize)), 1.0);
}

float luma(float3 color) {
  return dot(color, float3(0.299, 0.587, 0.114));
}

// FXAA on the display-encoded image: edge direction from the 3x3 neighbourhood, a blend
// towards the steeper side.
[shader("fragment")]
float4 fxaa(VertexOutput input) : SV_Target {
  const float2 uv = pixelUv(input.position.xy, post.targetSize);
  const float2 t = post.sourceTexel;
  const float3 colorC = fetch(post.source, uv);
  const float3 colorN = fetch(post.source, uv + t * float2(0.0, -1.0));
  const float3 colorS = fetch(post.source, uv + t * float2(0.0, 1.0));
  const float3 colorE = fetch(post.source, uv + t * float2(1.0, 0.0));
  const float3 colorW = fetch(post.source, uv + t * float2(-1.0, 0.0));
  const float3 colorNE = fetch(post.source, uv + t * float2(1.0, -1.0));
  const float3 colorNW = fetch(post.source, uv + t * float2(-1.0, -1.0));
  const float3 colorSE = fetch(post.source, uv + t * float2(1.0, 1.0));
  const float3 colorSW = fetch(post.source, uv + t * float2(-1.0, 1.0));
  const float lumC = luma(colorC);
  const float lumN = luma(colorN);
  const float lumS = luma(colorS);
  const float lumE = luma(colorE);
  const float lumW = luma(colorW);
  const float lumNE = luma(colorNE);
  const float lumNW = luma(colorNW);
  const float lumSE = luma(colorSE);
  const float lumSW = luma(colorSW);
  const float lumMin = min(lumC, min(min(lumN, lumS), min(lumE, lumW)));
  const float lumMax = max(lumC, max(max(lumN, lumS), max(lumE, lumW)));
  const float lumRange = lumMax - lumMin;
  if (lumRange < max(0.0312, lumMax * 0.125)) {
    return float4(colorC, 1.0);
  }
  const float vertical = abs(lumNW + lumN + lumNE - lumSW - lumS - lumSE);
  const float horizontal = abs(lumNW + lumW + lumSW - lumNE - lumE - lumSE);
  const bool isHorizontal = vertical > horizontal;
  const float lum1 = isHorizontal ? lumS : lumW;
  const float lum2 = isHorizontal ? lumN : lumE;
  const float gradient1 = abs(lum1 - lumC);
  const float gradient2 = abs(lum2 - lumC);
  float2 step = isHorizontal ? float2(0.0, t.y) : float2(t.x, 0.0);
  if (gradient1 < gradient2) {
    step = -step;
  }
  const float lumAverage = (lumN + lumS + lumE + lumW + lumNE + lumNW + lumSE + lumSW) * (1.0 / 8.0);
  float blend = saturate(abs(lumAverage - lumC) / lumRange);
  blend = smoothstep(0.0, 1.0, blend);
  blend = blend * blend * 0.75;
  const float3 blended = fetch(post.source, uv + step * 0.5);
  return float4(lerp(colorC, blended, blend), 1.0);
}
