// The skybox: a full-screen triangle at the far plane, tested against the scene's depth so it
// only covers what nothing else drew, sampling the equirectangular map by view direction. The
// direction is the unjittered camera's: the sky has no edges for TAA to resolve, and jittering it
// would only average each pixel with its neighbours and soften the background.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  VertexOutput output;
  output.position = fullscreenTriangle(vertexId);
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  const float2 ndc = float2(input.position.x / frame.targetSize.x * 2.0 - 1.0,
                            1.0 - input.position.y / frame.targetSize.y * 2.0);
  const float3 direction = skyRay(ndc);
  const float2 uv = float2(atan2(direction.z, direction.x) / (2.0 * Pi) + 0.5,
                           acos(clamp(direction.y, -1.0, 1.0)) / Pi);
  // Derivatives of the direction stay continuous across atan2's wrap. Compare the pixel's
  // solid angle with a map texel's at this latitude; the angular footprint bounds the pole
  // singularity so a pixel containing the pole remains finite.
  uint width, height, levels;
  textures[frame.skyboxMap].GetDimensions(0, width, height, levels);
  const float3 dx = ddx(direction);
  const float3 dy = ddy(direction);
  const float pixelAngle = max(length(cross(dx, dy)), 1.0e-12);
  const float latitude = max(length(direction.xz), sqrt(pixelAngle));
  const float texelAngle = 2.0 * Pi * Pi * latitude / (float(width) * float(height));
  const float lod = clamp(0.5 * log2(pixelAngle / texelAngle), 0.0, float(levels - 1));
  const float3 color = textures[frame.skyboxMap].SampleLevel(samplers[frame.skyboxSampler], uv, lod).rgb;
  return float4(color * frame.environment.x, 1.0);
}
