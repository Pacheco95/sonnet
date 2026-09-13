// The id pass: every draw's entity id into an R32Uint attachment, depth-tested against the
// forward pass's depth so only the visible surface remains. The editor picks from it and the
// outline pass reads it.
import sonnet;

[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID) : SV_Position {
  const Vertex vertex = draw.vertices[vertexId];
  const ObjectData object = objects[draw.objectIndex];
  return mul(frame.viewProjection, mul(object.model, float4(vertex.position, 1.0)));
}

[shader("fragment")]
uint fragmentMain() : SV_Target {
  return objects[draw.objectIndex].id;
}
