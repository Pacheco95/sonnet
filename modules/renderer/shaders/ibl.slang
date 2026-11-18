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
  // A quarter of the map's width spans a face, so a cube smaller than that takes n x n samples per
  // texel, n the map texels across one, rather than skipping texels and aliasing sharp edges.
  uint width, height, levels;
  textures[ibl.source].GetDimensions(0, width, height, levels);
  const uint n = clamp(uint(ceil(float(width) / (4.0 * float(ibl.size)))), 1u, 4u);
  float3 color = float3(0.0);
  for (uint j = 0; j < n; ++j) {
    for (uint i = 0; i < n; ++i) {
      const float2 uv = (float2(id.xy) + (float2(i, j) + 0.5) / float(n)) / float(ibl.size);
      color += textures[ibl.source].SampleLevel(samplers[ibl.sampler], equirectUv(cubeDirection(id.z, uv)), 0.0).rgb;
    }
  }
  storageImages[ibl.destination][id] = float4(color / float(n * n), 1.0);
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
  uint sourceSize, sourceHeight, sourceLevels;
  cubes[ibl.source].GetDimensions(0, sourceSize, sourceHeight, sourceLevels);
  const float texelSolidAngle = 4.0 * Pi / (6.0 * float(sourceSize) * float(sourceSize));
  float3 sum = float3(0.0);
  for (uint i = 0; i < ibl.sampleCount; ++i) {
    const float2 xi = hammersley(i, ibl.sampleCount);
    const float phi = 2.0 * Pi * xi.x;
    const float cosTheta = sqrt(1.0 - xi.y);
    const float sinTheta = sqrt(xi.y);
    const float3 l = toWorld(frameRows, float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta));
    // A sample stands for the solid angle 1 / (N pdf) = pi / (N cos theta) of the sky, many texels
    // of the source: reading a level whose texels are that big keeps neighbouring normals from
    // seeing different handfuls of the sky, which showed as blotches on a smooth surface. The
    // level the settings ask for is a floor.
    const float sampleSolidAngle = Pi / (float(ibl.sampleCount) * max(cosTheta, 1.0e-3));
    const float lod = max(ibl.sourceLod, 0.5 * log2(sampleSolidAngle / texelSolidAngle));
    sum += cubes[ibl.source].SampleLevel(samplers[ibl.sampler], l, min(lod, float(sourceLevels - 1))).rgb;
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

// The pre-integrated skin diffuse (ADR-0041; Penner 2011). x is n.l from -1 to 1; y is the curvature
// as (one profile millimetre) / (surface radius in profile millimetres), from 0 to 0.5, so the bottom
// row is flat and the top a 2 mm radius. Each texel integrates the clamped cosine around a ring of
// that radius, weighted per channel by d'Eon and Luebke's six-Gaussian skin profile at the chord's
// length. A narrow Gaussian would be undersampled over a wide ring, so the ring is integrated only
// over the arc where the widest Gaussian still weighs, three deviations.
float skinGaussian(float variance, float distance2) {
  return exp(-distance2 / (2.0 * variance)) / (2.0 * Pi * variance);
}

float3 skinProfile(float distance) {
  const float d2 = distance * distance;
  return float3(0.233, 0.455, 0.649) * skinGaussian(0.0064, d2) + float3(0.100, 0.336, 0.344) * skinGaussian(0.0484, d2) +
         float3(0.118, 0.198, 0.000) * skinGaussian(0.187, d2) + float3(0.113, 0.007, 0.007) * skinGaussian(0.567, d2) +
         float3(0.358, 0.004, 0.000) * skinGaussian(1.99, d2) + float3(0.078, 0.000, 0.000) * skinGaussian(7.41, d2);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void skinLut(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ibl.size || id.y >= ibl.size) {
    return;
  }
  const float cosTheta = (float(id.x) + 0.5) / float(ibl.size) * 2.0 - 1.0;
  const float theta = acos(clamp(cosTheta, -1.0, 1.0));
  const float inverseRadius = (float(id.y) + 0.5) / float(ibl.size) * 0.5;
  const float radius = 1.0 / inverseRadius; // profile millimetres
  // Three standard deviations of the widest Gaussian, as an angle on the ring.
  const float span = min(Pi, 3.0 * sqrt(7.41) / radius);
  float3 lit = float3(0.0);
  float3 weight = float3(0.0);
  for (uint i = 0; i < ibl.sampleCount; ++i) {
    const float x = ((float(i) + 0.5) / float(ibl.sampleCount) * 2.0 - 1.0) * span;
    const float3 r = skinProfile(2.0 * radius * sin(abs(x) * 0.5));
    lit += saturate(cos(theta + x)) * r;
    weight += r;
  }
  storageImages[ibl.destination][uint3(id.xy, 0)] = float4(lit / max(weight, float3(1.0e-8)), 1.0);
}
