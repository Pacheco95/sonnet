// The id pass: every draw's entity id into an R32Uint attachment, depth-tested against the
// forward pass's depth so only the visible surface remains. The editor picks from it and the
// outline pass reads it. The same shader without a depth attachment draws the selection mask.
import sonnet;

struct VertexOutput {
  float4 position : SV_Position;
  nointerpolation uint objectIndex : OBJECT0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint objectIndex : SV_StartInstanceLocation) {
  const Vertex vertex = vertexBuffers[objects[objectIndex].vertexBuffer][vertexId];
  VertexOutput output;
  output.position = mul(frame.viewProjection, mul(objects[objectIndex].model, float4(vertex.position, 1.0)));
  output.objectIndex = objectIndex;
  return output;
}

[shader("fragment")]
uint fragmentMain(VertexOutput input) : SV_Target {
  return objects[input.objectIndex].id;
}
