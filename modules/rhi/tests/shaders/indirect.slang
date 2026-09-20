// The GPU-driven path of ADR-0012: a compute shader writes draw commands and a count, and a
// draw reads both. Each command names its object in firstInstance, which the vertex shader
// receives as SV_StartInstanceLocation. Both entry points share one push-constant block and
// read the half they need.

struct Vertex {
  float3 position;
};

struct Object {
  float4 color;
  float4 offset;
};

// Vulkan's VkDrawIndexedIndirectCommand, the layout rhi::IndirectCommand mirrors.
struct IndirectCommand {
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int vertexOffset;
  uint firstInstance;
};

struct PushConstants {
  IndirectCommand *commands; // the compute entry writes these
  uint *count;
  Vertex *vertices; // the vertex entry pulls from these
  uint objectCount;
  uint indexCount;
  uint keepMask; // bit i keeps object i, standing in for a frustum test
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

[[vk::binding(1, 1)]]
StructuredBuffer<Object> objects;

// One thread, so the commands come out in a fixed order and the test can name them.
[shader("compute")]
[numthreads(1, 1, 1)]
void buildCommands() {
  uint written = 0;
  for (uint i = 0; i < push.objectCount; ++i) {
    if ((push.keepMask & (1u << i)) == 0) {
      continue;
    }
    IndirectCommand command;
    command.indexCount = push.indexCount;
    command.instanceCount = 1;
    command.firstIndex = 0;
    command.vertexOffset = 0;
    command.firstInstance = i;
    push.commands[written] = command;
    ++written;
  }
  *push.count = written;
}

struct VertexOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint objectIndex : SV_StartInstanceLocation) {
  const Vertex vertex = push.vertices[vertexId];
  const Object object = objects[objectIndex];
  VertexOutput output;
  output.position = float4(vertex.position + object.offset.xyz, 1.0);
  output.color = object.color;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return input.color;
}
