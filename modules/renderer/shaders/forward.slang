// Forward pass for the M1 primitive scene: one directional light, Lambert diffuse with a
// Blinn-Phong highlight and a constant ambient term. PBR replaces this in M3.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPosition : POSITION0;
  float3 normal : NORMAL0;
  float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
  const Vertex vertex = draw.vertices[vertexId];
  const ObjectData object = objects[draw.objectIndex];
  const float4 world = mul(object.model, float4(vertex.position, 1.0));
  VertexOutput output;
  output.position = mul(frame.viewProjection, world);
  output.worldPosition = world.xyz;
  output.normal = mul((float3x3)object.normalMatrix, vertex.normal);
  output.color = object.color;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  const float3 n = normalize(input.normal);
  const float3 l = normalize(-frame.lightDirection.xyz);
  const float3 v = normalize(frame.cameraPosition.xyz - input.worldPosition);
  const float3 h = normalize(l + v);
  const float diffuse = saturate(dot(n, l));
  const float specular = pow(saturate(dot(n, h)), 48.0) * 0.3;
  const float3 radiance = frame.lightColor.rgb * frame.lightColor.a;
  float3 color = input.color.rgb * (frame.ambient.rgb + radiance * diffuse) + radiance * specular * diffuse;
  // The target is UNORM and reaches the display unchanged (docs/rendering.md): a Reinhard curve
  // and the sRGB approximation stand in for M3's tone-mapping pass.
  color = pow(color / (color + 1.0), 1.0 / 2.2);
  return float4(color, 1.0);
}
