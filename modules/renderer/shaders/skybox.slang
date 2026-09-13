// The skybox: a full-screen triangle at the far plane, tested against the scene's depth so it
// only covers what nothing else drew, sampling the environment cube by view direction.
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
  const float4 nearPoint = mul(frame.inverseViewProjection, float4(ndc, 1.0, 1.0));
  const float4 farPoint = mul(frame.inverseViewProjection, float4(ndc, 0.5, 1.0));
  const float3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
  const float3 color = cubes[frame.skyboxCube].SampleLevel(samplers[frame.linearSampler], direction, 0.0).rgb;
  return float4(color * frame.environment.x, 1.0);
}
