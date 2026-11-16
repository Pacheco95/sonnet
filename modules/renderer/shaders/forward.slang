// The forward shading pass (ADR-0008): metallic-roughness PBR with the sun, the clustered
// punctual lights, cascaded shadows and image-based lighting, into the HDR scene colour. An unlit
// material (KHR_materials_unlit) is its base colour plus emissive, none of that. Opaque draws run
// after the depth pre-pass with an equal depth test; blended draws run afterwards, sorted back to
// front.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPosition : POSITION0;
  float3 normal : NORMAL0;
  float4 tangent : TANGENT0;
  float4 uvs : TEXCOORD0; // Vertex::uv, then Vertex::uv1
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
  output.uvs = float4(vertex.uv, vertex.uv1);
  output.color = objects[objectIndex].color * vertexColor(vertex);
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

// Cook-Torrance for one light direction; radiance already includes attenuation. `diffuseWeight` is
// 1 - t where transmission replaces the diffuse lobe (ADR-0033), 1 otherwise.
float3 shade(float3 n, float3 v, float3 l, float3 radiance, float3 albedo, float metallic, float roughness, float3 f0,
             float diffuseWeight) {
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
  return (diffuseWeight * kd * albedo / Pi + specular) * radiance * nDotL;
}

// ---- Spike (#502): skin and hair, chosen per draw by the material's shading model ----

struct Surface {
  uint model;
  float3 n; // the mapped normal, which every specular term uses
  // Skin: each channel's diffuse normal, between the mapped and the smooth one, red the smoothest;
  // the skin LUT's curvature row; the subsurface weight.
  float3 diffuseR;
  float3 diffuseG;
  float3 diffuseB;
  float skinRow;
  float subsurface;
  // Hair: the primary and secondary lobes' strand directions, shifted along the normal, and the
  // secondary's exponent as a fraction of the primary's.
  float3 strand1;
  float3 strand2;
  float hairWidth;
};

float3 skinLutAt(float nDotL, float row) {
  return textures[frame.skinLut].SampleLevel(samplers[frame.linearSampler], float2(nDotL * 0.5 + 0.5, row), 0.0).rgb;
}

// Pre-integrated skin (Penner): the specular of the standard model on the mapped normal, and a
// diffuse whose cosine per channel comes from the LUT at that channel's normal and the curvature.
float3 shadeSkin(Surface s, float3 v, float3 l, float3 radiance, float3 albedo, float metallic, float roughness,
                 float3 f0) {
  const float3 h = normalize(v + l);
  const float nDotL = saturate(dot(s.n, l));
  const float nDotV = max(dot(s.n, v), 1.0e-4);
  const float d = distributionGgx(saturate(dot(s.n, h)), roughness);
  const float g = geometrySmith(nDotV, nDotL, roughness);
  const float3 f = fresnelSchlick(saturate(dot(h, v)), f0);
  const float3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 1.0e-4) * nDotL;
  const float3 cosines = float3(dot(s.diffuseR, l), dot(s.diffuseG, l), dot(s.diffuseB, l));
  const float3 scattered =
      float3(skinLutAt(cosines.r, s.skinRow).r, skinLutAt(cosines.g, s.skinRow).g, skinLutAt(cosines.b, s.skinRow).b);
  const float3 diffuseCos = lerp(saturate(cosines), scattered, s.subsurface);
  const float3 kd = (1.0 - f) * (1.0 - metallic);
  return (kd * albedo / Pi * diffuseCos + specular) * radiance;
}

// Kajiya-Kay's strand lobe, normalised: sin(T, H) to the exponent.
float strandLobe(float3 strand, float3 h, float exponent) {
  const float th = dot(strand, h);
  // Narrow in one direction only, so normalised by about sqrt(e), not a 2D lobe's (e + 2) / 2 pi,
  // which made a white band where the strands of a cap turn slowly.
  return pow(sqrt(saturate(1.0 - th * th)), exponent) * (sqrt(exponent) + 1.0) / (2.0 * Pi);
}

float hairExponent(float roughness) {
  const float a = roughness * roughness;
  return clamp(2.0 / max(a * a, 1.0e-4) - 2.0, 2.0, 2048.0);
}

// Scheuermann's two shifted lobes: the primary untinted, the secondary tinted by the base colour and
// wider; a wrapped diffuse. The lobes fade on the side away from the light rather than stop at the
// card's terminator, which a card does not have.
float3 shadeHair(Surface s, float3 v, float3 l, float3 radiance, float3 albedo, float roughness, float3 f0) {
  const float3 h = normalize(v + l);
  const float nDotL = dot(s.n, l);
  const float exponent = hairExponent(roughness);
  // No Schlick term: without a geometry term to damp it, a backlit half vector took the lobe to white.
  // The secondary (TRT) passes through the strand, so it is the base colour's and wider.
  const float3 specular = f0 * strandLobe(s.strand1, h, exponent) + 0.5 * albedo * strandLobe(s.strand2, h, exponent * s.hairWidth);
  const float3 diffuse = (1.0 - f0) * albedo / Pi * saturate((nDotL + 0.5) / 1.5);
  return (diffuse + specular * saturate(nDotL * 0.75 + 0.25)) * radiance;
}

float3 shadeSurface(Surface s, float3 v, float3 l, float3 radiance, float3 albedo, float metallic, float roughness,
                    float3 f0, float diffuseWeight) {
  if (s.model == ShadingSkin) {
    return shadeSkin(s, v, l, radiance, albedo, metallic, roughness, f0);
  }
  if (s.model == ShadingHair) {
    return shadeHair(s, v, l, radiance, albedo, roughness, f0);
  }
  return shade(s.n, v, l, radiance, albedo, metallic, roughness, f0, diffuseWeight);
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

// A spot light's shadow: the fraction of the light that reaches the point, 1 outside the map.
// The normal offset follows the map's texel at the point's distance, so a grazing surface does
// not shadow itself. The map index comes from the light, so it differs between fragments of one
// draw; NonUniformResourceIndex says so where the image and its sampler are chosen.
float spotShadow(Light light, float3 worldPosition, float3 normal, float3 toLight) {
  const LocalShadow shadowMap = frame.localShadows[light.shadow];
  const float nDotL = saturate(dot(normal, toLight));
  const float3 offsetNormal = normal * sqrt(saturate(1.0 - nDotL * nDotL));
  const uint image = NonUniformResourceIndex(shadowMap.image);
  uint width, height;
  depthTextures[image].GetDimensions(width, height);
  const float2 texel = 1.0 / float2(width, height);
  // One texel at the cone's edge, widened with the distance from the light.
  const float distance = length(light.position - worldPosition);
  const float footprint = 2.0 * distance * tan(acos(clamp(light.outerCos, 0.0, 0.9999))) / float(width);
  const float4 clip = mul(shadowMap.matrix, float4(worldPosition + offsetNormal * 3.0 * footprint, 1.0));
  if (clip.w <= 0.0) {
    return 1.0;
  }
  const float3 ndc = clip.xyz / clip.w;
  const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  const float2 margin = 1.5 * texel;
  if (any(uv < margin) || any(uv > 1.0 - margin) || ndc.z < 0.0 || ndc.z > 1.0) {
    return 1.0;
  }
  // The depth is near / distance, so a constant bias in it is a distance that grows with the
  // square of the light's: 0.0015 was 80 centimetres at ten metres, enough to leave a gap of light
  // between a plinth and its shadow and to stripe the edge of it. The receiver moves towards the
  // light by two texels of the map at its distance instead, in the view depth clip.w.
  const float compareDepth = ndc.z * clip.w / max(clip.w - 2.0 * footprint, 1.0e-3);
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      lit += depthTextures[image].SampleCmpLevelZero(comparisonSamplers[frame.shadowSampler],
                                                     uv + float2(x, y) * texel, compareDepth);
    }
  }
  return lit / 9.0;
}

// A point light's shadow: the cube's face that the point lies in, the major axis of its offset
// from the light, in the order of the maps (+X, -X, +Y, -Y, +Z, -Z). Each face is the spot light's
// kind of map, a little wider than 90 degrees so the filter never leaves it, and the normal offset
// is one texel of the face at the point's distance, which is the distance times 2 / size.
float pointShadow(Light light, float3 worldPosition, float3 normal, float3 toLight) {
  const float3 offset = worldPosition - light.position;
  const float3 magnitude = abs(offset);
  uint face;
  if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
    face = offset.x >= 0.0 ? 0 : 1;
  } else if (magnitude.y >= magnitude.z) {
    face = offset.y >= 0.0 ? 2 : 3;
  } else {
    face = offset.z >= 0.0 ? 4 : 5;
  }
  const LocalShadow shadowMap = frame.localShadows[light.shadow + face];
  const float nDotL = saturate(dot(normal, toLight));
  const float3 offsetNormal = normal * sqrt(saturate(1.0 - nDotL * nDotL));
  const uint image = NonUniformResourceIndex(shadowMap.image);
  uint width, height;
  depthTextures[image].GetDimensions(width, height);
  const float2 texel = 1.0 / float2(width, height);
  const float footprint = 2.0 * length(offset) / float(width);
  const float4 clip = mul(shadowMap.matrix, float4(worldPosition + offsetNormal * 3.0 * footprint, 1.0));
  if (clip.w <= 0.0) {
    return 1.0;
  }
  const float3 ndc = clip.xyz / clip.w;
  const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  // The receiver moves towards the light by two texels in view depth: a bias in the depth itself,
  // near over distance, would grow with the square of the distance.
  const float compareDepth = ndc.z * clip.w / max(clip.w - 2.0 * footprint, 1.0e-3);
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      lit += depthTextures[image].SampleCmpLevelZero(comparisonSamplers[frame.shadowSampler],
                                                     clamp(uv + float2(x, y) * texel, texel, 1.0 - texel),
                                                     compareDepth);
    }
  }
  return lit / 9.0;
}

// The sun's contact visibility at the pixel (ADR-0039), 1 without the mask. Only opaque and masked
// receivers drew the pre-pass depth it was traced from; a blended or transmissive surface keeps to
// its shadow maps rather than taking the surface behind it's contact as its own.
float contactShadow(float2 pixel, MaterialData material) {
  if (frame.contactShadowImage == 0xFFFFFFFF || (material.flags & 0xFF) == AlphaBlend ||
      (material.flags & MaterialTransmissive) != 0) {
    return 1.0;
  }
  return textures[frame.contactShadowImage].Load(int3(int2(pixel), 0)).r;
}

// The parameters are not named `texture` or `sampler`: those are Metal types, and a parameter of
// that name makes the translated MSL declare `constant array<sampler, 64>&` against the parameter
// rather than the type, which Metal rejects. It bites the editor's runtime recompile, where the
// function survives as a function (ADR-0015 is the other shape of the same problem).
float4 sampleMaterial(uint textureIndex, uint samplerIndex, float2 uv) {
  return textures[textureIndex].SampleBias(samplers[samplerIndex], uv, frame.mipBias);
}

// A normal map's xy, from the frame of its transformed UVs back to the mesh's tangent frame: the
// inverse of the transform's 2x2 part, kept at its length so only its direction turns. A rotated
// or mirrored texture then shades as it looks; the identity leaves it as it was.
float2 textureToMeshFrame(float4 rows, float2 xy) {
  const float2 turned = float2(rows.w * xy.x - rows.y * xy.y, rows.x * xy.y - rows.z * xy.x);
  const float determinant = rows.x * rows.w - rows.y * rows.z;
  const float turnedLength = length(turned);
  return turnedLength > 0.0 ? turned * (sign(determinant) * length(xy) / turnedLength) : xy;
}

// The level of the transmission copy for a perceptual roughness (ADR-0033, "Lookup"). The desired
// footprint in base pixels is p = 1 + (size - 1) q, with q = (2^(10 r) - 1) / 1023 and size the
// longest dimension; level i has the nominal footprint size / extent[i], from the level's own extent
// rather than 2^i, so a size that is not a power of two reaches its last level only at roughness 1.
// The chain may be smaller than the view (RendererSettings::transmissionDownscale): its extents and
// levels are the image's own, and a footprint finer than level 0's reads level 0.
float transmissionLod(float r) {
  uint width, height, levels;
  textures[frame.transmissionImage].GetDimensions(0, width, height, levels);
  const float2 chain = float2(width, height);
  const float2 target = frame.targetSize;
  const float size = max(target.x, target.y);
  if (r <= 0.0 || size <= 1.0 || levels <= 1) {
    return 0.0;
  }
  if (r >= 1.0) {
    return float(levels - 1);
  }
  const float p = 1.0 + (size - 1.0) * (exp2(10.0 * r) - 1.0) / 1023.0;
  float below = size / max(chain.x, chain.y); // the nominal footprint of level i
  for (uint i = 0; i + 1 < levels; ++i) {
    const float2 next = max(floor(chain / exp2(float(i + 1))), 1.0);
    const float above = size / max(next.x, next.y);
    if (p <= above || i + 2 == levels) {
      return float(i) + saturate(log2(p / below) / log2(above / below));
    }
    below = above;
  }
  return float(levels - 1);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target {
  const MaterialData material = frame.materials[objects[input.objectIndex].material];
  const uint alphaMode = material.flags & 0xFF;

  const float2 baseColorUv = uvTransform(material.baseColorUv, material.baseColorUvOffset, uvSet(material, SlotBaseColor, input.uvs));
  float4 baseColor = material.baseColor * input.color * sampleMaterial(material.baseColorTexture, materialSampler(material, SlotBaseColor), baseColorUv);
  // A dithered mask (spike, #502) is decided by the pre-pass alone: the equal depth test keeps this
  // pass to its pixels, where the two passes' alpha samples might disagree.
  if ((alphaMode == AlphaMask && !dithered(material) && baseColor.a < material.emissive.a) ||
      fadedOut(input.objectIndex, material, input.position.xy)) {
    discard;
  }
  const float4 metallicRoughness =
      sampleMaterial(material.metallicRoughnessTexture, materialSampler(material, SlotMetallicRoughness),
                     uvTransform(material.metallicRoughnessUv, material.metallicRoughnessUvOffset, uvSet(material, SlotMetallicRoughness, input.uvs)));
  const float metallic = saturate(material.metallic * metallicRoughness.b);
  const float roughness = clamp(material.roughness * metallicRoughness.g, 0.04, 1.0);
  // The effective transmission T = t * (1 - m) of ADR-0033: metal lets nothing through. Only a draw
  // on the transmission path samples the texture; every other draw has T = 0.
  float transmittance = 0.0;
  float transmission = 0.0; // t of ADR-0033, before the metal's exclusion
  if ((material.flags & MaterialTransmissive) != 0) {
    const float2 transmissionUv =
        uvTransform(material.transmissionUv, material.transmissionUvOffset, uvSet(material, SlotTransmission, input.uvs));
    transmission = saturate(material.transmission *
                            sampleMaterial(material.transmissionTexture, materialSampler(material, SlotTransmission), transmissionUv).r);
    transmittance = transmission * (1.0 - metallic);
  }
  const float2 occlusionUv = uvTransform(material.occlusionUv, material.occlusionUvOffset, uvSet(material, SlotOcclusion, input.uvs));
  const float materialOcclusion =
      lerp(1.0, sampleMaterial(material.occlusionTexture, materialSampler(material, SlotOcclusion), occlusionUv).r, material.occlusionStrength);
  // Screen-space ambient occlusion (ADR-0039), lerp(1, A, s) already, for the opaque and masked
  // surfaces of the pre-pass only: a blended or transmissive surface is not the one the mask saw.
  float screenOcclusion = 1.0;
  if (frame.ambientOcclusionImage != 0xFFFFFFFF && alphaMode != AlphaBlend && (material.flags & MaterialTransmissive) == 0) {
    screenOcclusion = textures[frame.ambientOcclusionImage].Load(int3(int2(input.position.xy), 0)).r;
  }
  // What the indirect light, and only it, is multiplied by: once, material and screen together.
  const float occlusion = materialOcclusion * screenOcclusion;
  const float2 emissiveUv = uvTransform(material.emissiveUv, material.emissiveUvOffset, uvSet(material, SlotEmissive, input.uvs));
  const float3 emissive = material.emissive.rgb * sampleMaterial(material.emissiveTexture, materialSampler(material, SlotEmissive), emissiveUv).rgb;

  float3 n = normalize(input.normal);
  if (!frontFacing) {
    n = -n;
  }
  const float3 geometricNormal = n;
  const float3 t = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
  const float3 b = cross(n, t) * input.tangent.w;
  const float2 normalUv = uvTransform(material.normalUv, material.normalUvOffset, uvSet(material, SlotNormal, input.uvs));
  float3 tangentNormal = sampleMaterial(material.normalTexture, materialSampler(material, SlotNormal), normalUv).xyz * 2.0 - 1.0;
  tangentNormal.xy = textureToMeshFrame(material.normalUv, tangentNormal.xy) * material.normalScale;
  n = normalize(t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z);

  Surface surface;
  surface.model = shadingModel(material);
  surface.n = n;
  surface.diffuseR = n;
  surface.diffuseG = n;
  surface.diffuseB = n;
  surface.skinRow = 0.0;
  surface.subsurface = 0.0;
  surface.strand1 = t;
  surface.strand2 = t;
  surface.hairWidth = 1.0;
  if (surface.model == ShadingSkin) {
    // The curvature 1/r from the screen derivatives of the smooth normal and the position, in units
    // of the scatter radius (the LUT's 0 to 0.5 per profile millimetre).
    const float curvature = length(fwidth(geometricNormal)) / max(length(fwidth(input.worldPosition)), 1.0e-6);
    surface.skinRow = saturate(curvature * material.shading.y * 2.0);
    surface.subsurface = saturate(material.shading.x);
    const float3 smoothing = surface.subsurface * float3(0.9, 0.6, 0.3);
    surface.diffuseR = normalize(lerp(n, geometricNormal, smoothing.r));
    surface.diffuseG = normalize(lerp(n, geometricNormal, smoothing.g));
    surface.diffuseB = normalize(lerp(n, geometricNormal, smoothing.b));
  } else if (surface.model == ShadingHair) {
    // Strands along the tangent frame's V axis, the bitangent, or U, across the mapped normal.
    const float3 axis = (material.flags & MaterialHairAxisU) != 0 ? t : b;
    const float3 across = axis - n * dot(n, axis);
    const float3 strand = dot(across, across) > 1.0e-8 ? normalize(across) : t;
    // Scheuermann breaks the band up with a per-strand shift texture. Without one, the painted
    // strands' brightness stands in: lighter strands shift one way, darker the other.
    const float jitter = clamp((dot(baseColor.rgb, float3(0.2126, 0.7152, 0.0722)) - 0.08) * 4.0, -0.3, 0.3);
    surface.strand1 = normalize(strand + n * (material.shading.x + jitter));
    surface.strand2 = normalize(strand + n * (material.shading.y + jitter));
    surface.hairWidth = material.shading.z;
  }

  const float3 v = frame.cameraForward.w != 0.0 ? -frame.cameraForward.xyz : normalize(frame.cameraPosition.xyz - input.worldPosition);
  const float3 albedo = baseColor.rgb;
  const float3 f0 = lerp(float3(0.04), albedo, metallic);
  // With the copy of the opaque scene (ADR-0033) transmission replaces the diffuse lobe; without it
  // the surface is lit as an ordinary material and only its alpha follows the transmission.
  const bool copied = transmission > 0.0 && frame.transmissionImage != 0xFFFFFFFF;
  const float diffuseWeight = copied ? 1.0 - transmission : 1.0;
  const float viewDepth = -mul(frame.view, float4(input.worldPosition, 1.0)).z;

  float3 color = float3(0.0);
  float3 sunDirect = float3(0.0);
  float shadow = 1.0;
  float3 iblDiffuse = float3(0.0);
  float3 iblSpecular = float3(0.0);
  float2 brdf = float2(0.0);
  // An unlit material takes no light and receives no shadow, but still writes depth and casts one.
  if ((material.flags & MaterialUnlit) != 0) {
    color = albedo;
  } else {
    if (frame.sunDirection.w > 0.0) {
      const float3 l = -frame.sunDirection.xyz;
      // The minimum, not the product, of two estimates of the same partial shadow (ADR-0039); past
      // the cascades' reach theirs is 1, so a nearby occluder still casts its contact.
      shadow = min(sunShadow(input.worldPosition, viewDepth, geometricNormal), contactShadow(input.position.xy, material));
      sunDirect = shadeSurface(surface, v, l, frame.sunColor.rgb, albedo, metallic, roughness, f0, diffuseWeight);
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
        if (light.type == LightSpot && light.shadow != 0xFFFFFFFF) {
          attenuation *= spotShadow(light, input.worldPosition, geometricNormal, l);
        } else if (light.type == LightPoint && light.shadow != 0xFFFFFFFF) {
          attenuation *= pointShadow(light, input.worldPosition, geometricNormal, l);
        }
        color += shadeSurface(surface, v, l, light.color * attenuation, albedo, metallic, roughness, f0, diffuseWeight);
      }
    }

    // Image-based lighting through the split-sum approximation, or a flat ambient term.
    const float nDotV = max(dot(n, v), 1.0e-4);
    if (frame.environment.w > 0.0) {
      float3 irradiance = cubes[frame.irradianceCube].SampleLevel(samplers[frame.linearSampler], n, 0.0).rgb;
      float3 r = reflect(-v, n);
      float specularRoughness = roughness;
      float specularNDotV = nDotV;
      if (surface.model == ShadingSkin) {
        // Each channel's irradiance at its own diffuse normal.
        irradiance = float3(cubes[frame.irradianceCube].SampleLevel(samplers[frame.linearSampler], surface.diffuseR, 0.0).r,
                            cubes[frame.irradianceCube].SampleLevel(samplers[frame.linearSampler], surface.diffuseG, 0.0).g,
                            cubes[frame.irradianceCube].SampleLevel(samplers[frame.linearSampler], surface.diffuseB, 0.0).b);
      } else if (surface.model == ShadingHair) {
        // The reflection off the normal across the strand that faces the viewer, rougher.
        const float3 across = v - surface.strand1 * dot(v, surface.strand1);
        const float3 bent = dot(across, across) > 1.0e-8 ? normalize(across) : n;
        r = reflect(-v, bent);
        specularRoughness = saturate(roughness + 0.25);
        specularNDotV = max(dot(bent, v), 1.0e-4);
      }
      const float3 f = fresnelSchlickRoughness(specularNDotV, f0, specularRoughness);
      const float3 kd = (1.0 - f) * (1.0 - metallic);
      const float lod = specularRoughness * (frame.environment.y - 1.0);
      const float3 prefiltered = cubes[frame.prefilteredCube].SampleLevel(samplers[frame.linearSampler], r, lod).rgb;
      brdf = textures[frame.brdfLut].SampleLevel(samplers[frame.linearSampler], float2(specularNDotV, specularRoughness), 0.0).rg;
      iblDiffuse = diffuseWeight * kd * irradiance * albedo * frame.environment.x * occlusion;
      iblSpecular = prefiltered * (f * brdf.x + brdf.y) * frame.environment.x * occlusion;
      color += iblDiffuse + iblSpecular;
    } else {
      color += (copied ? 1.0 - transmittance : 1.0) * frame.ambient.rgb * albedo * occlusion;
    }
    if (copied) {
      // The scene behind the pixel at the surface's own position (zero thickness), blurred by the
      // roughness the specular terms' floor does not clamp, through the base colour and what the
      // reflection leaves. The copy already holds that light shaded, so occlusion does not touch it.
      const float lookupRoughness = saturate(material.roughness * metallicRoughness.g);
      const float2 uv = input.position.xy / frame.targetSize;
      const float3 behind = textures[frame.transmissionImage]
                                .SampleLevel(samplers[frame.linearSampler], uv, transmissionLod(lookupRoughness))
                                .rgb;
      const float3 f = fresnelSchlickRoughness(nDotV, f0, roughness);
      color += transmission * (1.0 - f) * (1.0 - metallic) * albedo * behind;
    }
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
  case 9: color = float3(screenOcclusion); break;
  case 10:
    color = surface.model == ShadingSkin   ? float3(surface.skinRow, 0.0, 0.0)
            : surface.model == ShadingHair ? surface.strand1 * 0.5 + 0.5
                                           : float3(0.0);
    break;
  default: break;
  }

  // The coverage a. Without the copy a transmissive draw is a plain blend, a times 1 - T, so the
  // light the surface lets through shows what is behind it (ADR-0033, "Off and mobile").
  const float coverage = alphaMode == AlphaBlend ? baseColor.a : 1.0;
  return float4(color, copied ? coverage : coverage * (1.0 - transmittance));
}
