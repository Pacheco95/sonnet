// Light clustering (ADR-0008): one thread per cluster builds the cluster's view-space box from
// its screen tile and depth slice and collects the lights whose sphere touches it.
import sonnet;

// The view-space point of a pixel on the near plane, scaled to a given view depth.
float3 viewPoint(float2 pixel, float viewDepth) {
  const float2 ndc = float2(pixel.x / frame.targetSize.x * 2.0 - 1.0, 1.0 - pixel.y / frame.targetSize.y * 2.0);
  // On the near plane reversed-Z depth is 1.
  const float4 clip = float4(ndc, 1.0, 1.0);
  float4 view = mul(frame.inverseProjection, clip);
  view /= view.w;
  // The ray from the eye through the near-plane point, extended to the requested depth.
  return view.xyz * (viewDepth / -view.z);
}

float sliceDepth(uint slice) {
  return exp((float(slice) - frame.clusterScaleBias.w) / frame.clusterScaleBias.z);
}

[shader("compute")]
[numthreads(4, 3, 4)]
void computeMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ClusterGridX || id.y >= ClusterGridY || id.z >= ClusterGridZ) {
    return;
  }
  const float2 tile = frame.clusterScaleBias.xy;
  const float2 pixelMin = float2(id.xy) * tile;
  const float2 pixelMax = pixelMin + tile;
  const float nearDepth = sliceDepth(id.z);
  const float farDepth = sliceDepth(id.z + 1);

  float3 boxMin = float3(1.0e9);
  float3 boxMax = float3(-1.0e9);
  const float2 corners[4] = {pixelMin, float2(pixelMax.x, pixelMin.y), float2(pixelMin.x, pixelMax.y), pixelMax};
  for (uint i = 0; i < 4; ++i) {
    const float3 nearPoint = viewPoint(corners[i], nearDepth);
    const float3 farPoint = viewPoint(corners[i], farDepth);
    boxMin = min(boxMin, min(nearPoint, farPoint));
    boxMax = max(boxMax, max(nearPoint, farPoint));
  }

  const uint index = id.x + ClusterGridX * (id.y + ClusterGridY * id.z);
  uint count = 0;
  for (uint light = 0; light < frame.lightCount && count < MaxLightsPerCluster; ++light) {
    const float3 centre = mul(frame.view, float4(frame.lights[light].position, 1.0)).xyz;
    const float3 closest = clamp(centre, boxMin, boxMax);
    const float3 delta = closest - centre;
    const float range = frame.lights[light].range;
    if (dot(delta, delta) <= range * range) {
      frame.clusters[index].lights[count] = light;
      ++count;
    }
  }
  frame.clusters[index].count = count;
}
