// The forward shading pass (ADR-0008): metallic-roughness PBR with the sun, the clustered
// punctual lights, cascaded shadows and image-based lighting, into the HDR scene colour. Opaque
// draws run after the depth pre-pass with an equal depth test; blended draws run afterwards,
// sorted back to front.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPosition : POSITION0;
  float3 normal : NORMAL0;
  float4 tangent : TANGENT0;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
  nointerpolation uint objectIndex : OBJECT0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                        uint instance : SV_InstanceID) {
  const uint objectIndex = drawObject(startInstance, instance);
  const float4x4 model = objects[objectIndex].model;
  const Vertex vertex = vertexBuffers[objects[objectIndex].vertexBuffer][vertexId];
  const float4 world = mul(model, float4(vertex.position, 1.0));
  const WorldBasis basis = worldBasis(model);
  VertexOutput output;
  output.objectIndex = objectIndex;
  output.position = mul(frame.viewProjection, world);
  output.worldPosition = world.xyz;
  output.normal = transformNormal(basis, vertex.normal);
  output.tangent = float4(transformDirection(basis, vertex.tangent.xyz), vertex.tangent.w * mirrorSign(basis));
  output.uv = vertex.uv;
  output.color = objects[objectIndex].color;
  return output;
}

float distributionGgx(float nDotH, float roughness) {
  const float a = roughness * roughness;
  const float a2 = a * a;
  const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
  return a2 / (Pi * d * d);
}

float geometrySchlickGgx(float nDotX, float roughness) {
  const float r = roughness + 1.0;
  const float k = (r * r) / 8.0;
  return nDotX / (nDotX * (1.0 - k) + k);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
  return geometrySchlickGgx(nDotV, roughness) * geometrySchlickGgx(nDotL, roughness);
}

float3 fresnelSchlick(float cosTheta, float3 f0) {
  return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 f0, float roughness) {
  return f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Cook-Torrance for one light direction; radiance already includes attenuation.
float3 shade(float3 n, float3 v, float3 l, float3 radiance, float3 albedo, float metallic, float roughness, float3 f0) {
  const float3 h = normalize(v + l);
  const float nDotL = saturate(dot(n, l));
  const float nDotV = max(dot(n, v), 1.0e-4);
  const float nDotH = saturate(dot(n, h));
  const float hDotV = saturate(dot(h, v));
  const float d = distributionGgx(nDotH, roughness);
  const float g = geometrySmith(nDotV, nDotL, roughness);
  const float3 f = fresnelSchlick(hDotV, f0);
  const float3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 1.0e-4);
  const float3 kd = (1.0 - f) * (1.0 - metallic);
  return (kd * albedo / Pi + specular) * radiance * nDotL;
}

uint shadowCascade(float viewDepth) {
  return uint(viewDepth >= frame.cascadeSplits[0]) + uint(viewDepth >= frame.cascadeSplits[1]) +
         uint(viewDepth >= frame.cascadeSplits[2]);
}

// A geometric-normal offset follows the filter's world-space footprint, independently of
// normal mapping. Reversed-Z's small depth bias is the same in every cascade.
bool sampleCascade(uint cascade, float3 worldPosition, float3 normal, out float lit) {
  const float nDotL = saturate(dot(normal, -frame.sunDirection.xyz));
  const float offset = 3.0 * frame.cascadeTexelSizes[cascade] * sqrt(saturate(1.0 - nDotL * nDotL));
  const float4 clip = mul(frame.cascadeMatrices[cascade], float4(worldPosition + normal * offset, 1.0));
  const float3 ndc = clip.xyz / clip.w;
  const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  uint width, height;
  depthTextures[frame.cascadeImages[cascade]].GetDimensions(width, height);
  const float2 texel = 1.0 / float2(width, height);
  // The outer tap also bilinearly reads its neighbour. Never clamp an incomplete kernel.
  const float2 margin = 1.5 * texel;
  lit = 1.0;
  if (any(uv < margin) || any(uv > 1.0 - margin) || ndc.z < 0.0 || ndc.z > 1.0) {
    return false;
  }
  lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      lit += depthTextures[frame.cascadeImages[cascade]].SampleCmpLevelZero(
          comparisonSamplers[frame.shadowSampler], uv + float2(x, y) * texel, ndc.z + frame.shadowBias);
    }
  }
  lit /= 9.0;
  return true;
}

float sunShadow(float3 worldPosition, float viewDepth, float3 normal) {
  if (frame.cascadeImages[0] == 0xFFFFFFFF || viewDepth >= frame.cascadeSplits[CascadeCount - 1]) {
    return 1.0;
  }
  // Keep the sampled image index uniform within each branch. Dynamically indexing the
  // comparison images across a split produced a dark row on both Intel and Lavapipe.
  [unroll]
  for (uint cascade = 0; cascade < CascadeCount; ++cascade) {
    if (cascade < shadowCascade(viewDepth)) {
      continue;
    }
    float lit;
    if (!sampleCascade(cascade, worldPosition, normal, lit)) {
      continue;
    }
    const float blend = smoothstep(frame.cascadeBlendStarts[cascade], frame.cascadeSplits[cascade], viewDepth);
    if (blend > 0.0) {
      float nextLit = 1.0;
      if (cascade + 1 < CascadeCount) {
        if (!sampleCascade(cascade + 1, worldPosition, normal, nextLit)) {
          return lit;
        }
      }
      lit = lerp(lit, nextLit, blend);
    }
    return lit;
  }
  return 1.0;
}

// The parameters are not named `texture` or `sampler`: those are Metal types, and a parameter of
// that name makes the translated MSL declare `constant array<sampler, 64>&` against the parameter
// rather than the type, which Metal rejects. It bites the editor's runtime recompile, where the
// function survives as a function (ADR-0015 is the other shape of the same problem).
float4 sampleMaterial(uint textureIndex, uint samplerIndex, float2 uv) {
  return textures[textureIndex].Sample(samplers[samplerIndex], uv);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target {
  const MaterialData material = frame.materials[objects[input.objectIndex].material];
  const uint alphaMode = material.flags & 0xFF;

  float4 baseColor = material.baseColor * input.color * sampleMaterial(material.baseColorTexture, material.sampler, input.uv);
  if (alphaMode == AlphaMask && baseColor.a < material.emissive.a) {
    discard;
  }
  const float4 metallicRoughness = sampleMaterial(material.metallicRoughnessTexture, material.sampler, input.uv);
  const float metallic = saturate(material.metallic * metallicRoughness.b);
  const float roughness = clamp(material.roughness * metallicRoughness.g, 0.04, 1.0);
  const float occlusion = lerp(1.0, sampleMaterial(material.occlusionTexture, material.sampler, input.uv).r,
                               material.occlusionStrength);
  const float3 emissive = material.emissive.rgb * sampleMaterial(material.emissiveTexture, material.sampler, input.uv).rgb;

  float3 n = normalize(input.normal);
  if (!frontFacing) {
    n = -n;
  }
  const float3 geometricNormal = n;
  const float3 t = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
  const float3 b = cross(n, t) * input.tangent.w;
  float3 tangentNormal = sampleMaterial(material.normalTexture, material.sampler, input.uv).xyz * 2.0 - 1.0;
  tangentNormal.xy *= material.normalScale;
  n = normalize(t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z);

  const float3 v = normalize(frame.cameraPosition.xyz - input.worldPosition);
  const float3 albedo = baseColor.rgb;
  const float3 f0 = lerp(float3(0.04), albedo, metallic);
  const float viewDepth = -mul(frame.view, float4(input.worldPosition, 1.0)).z;

  float3 color = float3(0.0);
  float3 sunDirect = float3(0.0);
  float shadow = 1.0;
  if (frame.sunDirection.w > 0.0) {
    const float3 l = -frame.sunDirection.xyz;
    shadow = sunShadow(input.worldPosition, viewDepth, geometricNormal);
    sunDirect = shade(n, v, l, frame.sunColor.rgb, albedo, metallic, roughness, f0);
    color += sunDirect * shadow;
  }

  // The punctual lights of this fragment's cluster (ADR-0008).
  const Cluster cluster = frame.clusters[clusterIndex(input.position.xy, viewDepth)];
  for (uint i = 0; i < min(cluster.count, MaxLightsPerCluster); ++i) {
    const Light light = frame.lights[cluster.lights[i]];
    const float3 toLight = light.position - input.worldPosition;
    const float distance2 = max(dot(toLight, toLight), 1.0e-4);
    const float distance = sqrt(distance2);
    const float3 l = toLight / distance;
    // Inverse square falloff windowed to zero at the range.
    const float window = saturate(1.0 - pow(distance / light.range, 4.0));
    float attenuation = window * window / distance2;
    if (light.type == LightSpot) {
      const float cosAngle = dot(-l, light.direction);
      attenuation *= saturate((cosAngle - light.outerCos) / max(light.innerCos - light.outerCos, 1.0e-4));
    }
    if (attenuation > 0.0) {
      color += shade(n, v, l, light.color * attenuation, albedo, metallic, roughness, f0);
    }
  }

  // Image-based lighting through the split-sum approximation, or a flat ambient term.
  const float nDotV = max(dot(n, v), 1.0e-4);
  float3 iblDiffuse = float3(0.0);
  float3 iblSpecular = float3(0.0);
  float2 brdf = float2(0.0);
  if (frame.environment.w > 0.0) {
    const float3 f = fresnelSchlickRoughness(nDotV, f0, roughness);
    const float3 kd = (1.0 - f) * (1.0 - metallic);
    const float3 irradiance = cubes[frame.irradianceCube].SampleLevel(samplers[frame.linearSampler], n, 0.0).rgb;
    const float3 r = reflect(-v, n);
    const float lod = roughness * (frame.environment.y - 1.0);
    const float3 prefiltered = cubes[frame.prefilteredCube].SampleLevel(samplers[frame.linearSampler], r, lod).rgb;
    brdf = textures[frame.brdfLut].SampleLevel(samplers[frame.linearSampler], float2(nDotV, roughness), 0.0).rg;
    iblDiffuse = kd * irradiance * albedo * frame.environment.x * occlusion;
    iblSpecular = prefiltered * (f * brdf.x + brdf.y) * frame.environment.x * occlusion;
    color += iblDiffuse + iblSpecular;
  } else {
    color += frame.ambient.rgb * albedo * occlusion;
  }
  color += emissive;

  // RendererSettings::debugView: one term in place of the colour.
  switch (frame.debugView) {
  case 1: color = albedo; break;
  case 2: color = n * 0.5 + 0.5; break;
  case 3: color = sunDirect; break;
  case 4: color = float3(shadow); break;
  case 5: color = iblDiffuse; break;
  case 6: color = iblSpecular; break;
  case 7: color = float3(brdf, 0.0); break;
  case 8: {
    const float3 colors[4] = {float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1), float3(1, 1, 0)};
    color = colors[shadowCascade(viewDepth)];
    break;
  }
  default: break;
  }

  const float alpha = alphaMode == AlphaBlend ? baseColor.a : 1.0;
  return float4(color, alpha);
}
