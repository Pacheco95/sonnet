// The GPU-driven path of ADR-0016: a compute shader writes an instanced draw command and the
// visible list it draws, and a draw reads both. The command's firstInstance is where its run of
// the visible list starts; each instance adds SV_InstanceID to SV_StartInstanceLocation and
// reads its object there. Both entry points share one push-constant block and read the half
// they need.

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
  uint *visible;             // and the object indices they draw
  Vertex *vertices;          // the vertex entry pulls from these
  uint objectCount;
  uint indexCount;
  uint keepMask;     // bit i keeps object i, standing in for a frustum test
  uint firstVisible; // where the command's run of the visible list starts
  uint visibleSize;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

[[vk::binding(1, 1)]]
StructuredBuffer<Object> objects;

static const uint DecoyObject = 1;

// One thread, so the list comes out in a fixed order. Every slot outside the run names the
// green object, so a vertex shader that reads the list at the wrong place draws green.
[shader("compute")]
[numthreads(1, 1, 1)]
void buildCommands() {
  for (uint i = 0; i < push.visibleSize; ++i) {
    push.visible[i] = DecoyObject;
  }
  uint kept = 0;
  for (uint i = 0; i < push.objectCount; ++i) {
    if ((push.keepMask & (1u << i)) != 0) {
      push.visible[push.firstVisible + kept] = i;
      ++kept;
    }
  }
  IndirectCommand command;
  command.indexCount = push.indexCount;
  command.instanceCount = kept;
  command.firstIndex = 0;
  command.vertexOffset = 0;
  command.firstInstance = push.firstVisible;
  push.commands[0] = command;
  // A batch nothing survived in: the same run with no instances, which must draw nothing.
  command.instanceCount = 0;
  command.firstInstance = 0;
  push.commands[1] = command;
}

struct VertexOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint startInstance : SV_StartInstanceLocation,
                        uint instance : SV_InstanceID) {
  const Vertex vertex = push.vertices[vertexId];
  const Object object = objects[push.visible[startInstance + instance]];
  VertexOutput output;
  output.position = float4(vertex.position + object.offset.xyz, 1.0);
  output.color = object.color;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return input.color;
}
