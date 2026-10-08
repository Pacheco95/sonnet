// The id pass: every draw's entity id into an R32Uint attachment, depth-tested against the
// forward pass's depth so only the visible surface remains. The editor picks from it and the
// outline pass reads it. The same shader without a depth attachment draws the selection mask.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  nointerpolation uint objectIndex : OBJECT0;
};

VertexOutput transform(uint vertexId, uint startInstance, uint instance, float4x4 viewProjection) {
  const uint objectIndex = drawObject(startInstance, instance);
  const Vertex vertex = vertexBuffers[objects[objectIndex].vertexBuffer][vertexId];
  VertexOutput output;
  output.position = mul(viewProjection, mul(objects[objectIndex].model, float4(vertex.position, 1.0)));
  output.objectIndex = objectIndex;
  return output;
}

// Tested against the scene's depth, so it is drawn with the matrix that depth was (jittered under
// temporal anti-aliasing).
[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                        uint instance : SV_InstanceID) {
  return transform(vertexId, startInstance, instance, frame.viewProjection);
}

// The selection mask has no depth and is compared with the resolved image: unjittered.
[shader("vertex")]
VertexOutput maskVertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                            uint instance : SV_InstanceID) {
  return transform(vertexId, startInstance, instance, frame.unjitteredViewProjection);
}

[shader("fragment")]
uint fragmentMain(VertexOutput input) : SV_Target {
  return objects[input.objectIndex].id;
}
