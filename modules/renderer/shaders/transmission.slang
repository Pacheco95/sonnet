// The copy a transmissive surface samples (ADR-0033): the forward pass's opaque scene and sky
// into level 0 of a mip chain, then each level from the one above with the bloom chain's 13-tap
// filter. One dispatch per level, so the write to a level orders before the read of it.
import sonnet;

struct TransmissionConstants {
  uint source;      // a sampled image: the forward target, or the chain itself
  uint destination; // a storage image level
  uint sampler;
  uint sourceLevel; // the level the filter reads; unused by the copy
  uint2 size;       // the destination level's size
  uint2 padding;
};

[[vk::push_constant]]
ConstantBuffer<TransmissionConstants> transmission;

// Level 0 is the target's texels, bit for bit.
[shader("compute")]
[numthreads(8, 8, 1)]
void transmissionCopy(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= transmission.size)) {
    return;
  }
  storageImages[transmission.destination][uint3(id.xy, 0)] =
      float4(textures[transmission.source].Load(int3(id.xy, 0)).rgb, 1.0);
}

float3 fetch(float2 uv) {
  return textures[transmission.source].SampleLevel(samplers[transmission.sampler], uv, float(transmission.sourceLevel)).rgb;
}

// Jimenez's 13-tap downsample, as the bloom chain's (post.slang), from level sourceLevel.
[shader("compute")]
[numthreads(8, 8, 1)]
void transmissionMip(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= transmission.size)) {
    return;
  }
  uint width, height, levels;
  textures[transmission.source].GetDimensions(transmission.sourceLevel, width, height, levels);
  const float2 uv = (float2(id.xy) + 0.5) / float2(transmission.size);
  const float2 t = 1.0 / float2(width, height);
  const float3 a = fetch(uv + t * float2(-2.0, -2.0));
  const float3 b = fetch(uv + t * float2(0.0, -2.0));
  const float3 c = fetch(uv + t * float2(2.0, -2.0));
  const float3 d = fetch(uv + t * float2(-2.0, 0.0));
  const float3 e = fetch(uv);
  const float3 f = fetch(uv + t * float2(2.0, 0.0));
  const float3 g = fetch(uv + t * float2(-2.0, 2.0));
  const float3 h = fetch(uv + t * float2(0.0, 2.0));
  const float3 i = fetch(uv + t * float2(2.0, 2.0));
  const float3 j = fetch(uv + t * float2(-1.0, -1.0));
  const float3 k = fetch(uv + t * float2(1.0, -1.0));
  const float3 l = fetch(uv + t * float2(-1.0, 1.0));
  const float3 m = fetch(uv + t * float2(1.0, 1.0));
  float3 color = e * 0.125;
  color += (a + c + g + i) * 0.03125;
  color += (b + d + f + h) * 0.0625;
  color += (j + k + l + m) * 0.125;
  storageImages[transmission.destination][uint3(id.xy, 0)] = float4(color, 1.0);
}
