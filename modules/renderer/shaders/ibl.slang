// Environment precomputation in compute (docs/rendering.md, "Image-based lighting"): the
// equirectangular map into a cube, the cube's mip chain, the diffuse irradiance cube, the
// GGX-prefiltered specular cube per roughness level and the split-sum BRDF lookup table.
import sonnet;

struct IblConstants {
  uint source;      // a texture, or a cube, in the bindless arrays
  uint destination; // a storage image level
  uint size;        // the destination level's width and height
  uint sampleCount;
  float roughness;
  float sourceLod;
  uint sampler;
  uint padding;
};

[[vk::push_constant]]
ConstantBuffer<IblConstants> ibl;

float2 equirectUv(float3 direction) {
  return float2(atan2(direction.z, direction.x) / (2.0 * Pi) + 0.5, 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / Pi);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void equirectToCube(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ibl.size || id.y >= ibl.size) {
    return;
  }
  const float2 uv = (float2(id.xy) + 0.5) / float(ibl.size);
  const float3 direction = cubeDirection(id.z, uv);
  const float4 color = textures[ibl.source].SampleLevel(samplers[ibl.sampler], equirectUv(direction), 0.0);
  storageImages[ibl.destination][id] = float4(color.rgb, 1.0);
}

// The next level of a cube's mip chain, read through the sampled view at the previous level.
[shader("compute")]
[numthreads(8, 8, 1)]
void cubeMip(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ibl.size || id.y >= ibl.size) {
    return;
  }
  const float2 uv = (float2(id.xy) + 0.5) / float(ibl.size);
  const float3 direction = cubeDirection(id.z, uv);
  storageImages[ibl.destination][id] =
      cubes[ibl.source].SampleLevel(samplers[ibl.sampler], direction, ibl.sourceLod);
}

// Cosine-weighted hemisphere samples of the environment, which is what a Lambert surface sees.
[shader("compute")]
[numthreads(8, 8, 1)]
void irradiance(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ibl.size || id.y >= ibl.size) {
    return;
  }
  const float2 uv = (float2(id.xy) + 0.5) / float(ibl.size);
  const float3 n = cubeDirection(id.z, uv);
  const float3x3 frameRows = tangentFrame(n);
  float3 sum = float3(0.0);
  for (uint i = 0; i < ibl.sampleCount; ++i) {
    const float2 xi = hammersley(i, ibl.sampleCount);
    const float phi = 2.0 * Pi * xi.x;
    const float cosTheta = sqrt(1.0 - xi.y);
    const float sinTheta = sqrt(xi.y);
    const float3 l = toWorld(frameRows, float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta));
    sum += cubes[ibl.source].SampleLevel(samplers[ibl.sampler], l, ibl.sourceLod).rgb;
  }
  storageImages[ibl.destination][id] = float4(sum / float(ibl.sampleCount), 1.0);
}

// GGX importance sampling with the view along the normal; the source is read at a lod chosen
// by the sample's solid angle to avoid fireflies from bright texels.
[shader("compute")]
[numthreads(8, 8, 1)]
void prefilter(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ibl.size || id.y >= ibl.size) {
    return;
  }
  const float2 uv = (float2(id.xy) + 0.5) / float(ibl.size);
  const float3 n = cubeDirection(id.z, uv);
  const float3 v = n;
  uint sourceSize, sourceHeight, sourceLevels;
  cubes[ibl.source].GetDimensions(0, sourceSize, sourceHeight, sourceLevels);
  const float texelSolidAngle = 4.0 * Pi / (6.0 * float(sourceSize) * float(sourceSize));
  float3 sum = float3(0.0);
  float weight = 0.0;
  for (uint i = 0; i < ibl.sampleCount; ++i) {
    const float2 xi = hammersley(i, ibl.sampleCount);
    const float3 h = importanceSampleGgx(xi, n, ibl.roughness);
    const float3 l = normalize(2.0 * dot(v, h) * h - v);
    const float nDotL = dot(n, l);
    if (nDotL > 0.0) {
      const float nDotH = saturate(dot(n, h));
      const float a = ibl.roughness * ibl.roughness;
      const float d = (nDotH * nDotH * (a * a - 1.0) + 1.0);
      const float pdf = (a * a / (Pi * d * d)) * nDotH / (4.0 * nDotH) + 1.0e-4;
      const float sampleSolidAngle = 1.0 / (float(ibl.sampleCount) * pdf);
      const float lod = ibl.roughness == 0.0 ? 0.0 : 0.5 * log2(sampleSolidAngle / texelSolidAngle);
      sum += cubes[ibl.source].SampleLevel(samplers[ibl.sampler], l, max(lod, 0.0)).rgb * nDotL;
      weight += nDotL;
    }
  }
  storageImages[ibl.destination][id] = float4(sum / max(weight, 1.0e-4), 1.0);
}

float geometrySchlickGgxIbl(float nDotX, float roughness) {
  const float k = (roughness * roughness) / 2.0;
  return nDotX / (nDotX * (1.0 - k) + k);
}

// x: n.v, y: roughness; the result scales and biases F0 (split-sum).
[shader("compute")]
[numthreads(8, 8, 1)]
void brdfLut(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ibl.size || id.y >= ibl.size) {
    return;
  }
  const float nDotV = max((float(id.x) + 0.5) / float(ibl.size), 1.0e-3);
  const float roughness = (float(id.y) + 0.5) / float(ibl.size);
  const float3 v = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
  const float3 n = float3(0.0, 0.0, 1.0);
  float a = 0.0;
  float b = 0.0;
  for (uint i = 0; i < ibl.sampleCount; ++i) {
    const float2 xi = hammersley(i, ibl.sampleCount);
    const float3 h = importanceSampleGgx(xi, n, roughness);
    const float3 l = normalize(2.0 * dot(v, h) * h - v);
    const float nDotL = saturate(l.z);
    const float nDotH = saturate(h.z);
    const float vDotH = saturate(dot(v, h));
    if (nDotL > 0.0) {
      const float g = geometrySchlickGgxIbl(nDotV, roughness) * geometrySchlickGgxIbl(nDotL, roughness);
      const float gVis = (g * vDotH) / max(nDotH * nDotV, 1.0e-4);
      const float fc = pow(1.0 - vDotH, 5.0);
      a += (1.0 - fc) * gVis;
      b += fc * gVis;
    }
  }
  storageImages[ibl.destination][uint3(id.xy, 0)] = float4(a / float(ibl.sampleCount), b / float(ibl.sampleCount), 0.0, 1.0);
}
