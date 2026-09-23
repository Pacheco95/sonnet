// GPU culling (ADR-0012, ADR-0016): one thread per candidate draw tests its world-space bounds
// against the pass's frustum and, for a survivor, adds an instance to its batch's command and
// writes its object index into the batch's run of the visible list. A batch is a run of draws
// sharing a pipeline, a front face, a mesh and a submesh, so one command draws all of its
// survivors as instances, and each instance finds its object at SV_StartInstanceLocation plus
// SV_InstanceID in the visible list.
//
// The frustum comes from the pass's view-projection matrix, so the same candidate array serves
// the camera, the depth pre-pass and each shadow cascade; only the matrix differs. This is the
// one scene shader that does not import `sonnet`: it needs the command buffer writable at the
// per-pass storage binding, where the shared module declares the read-only object array.

struct CullDraw {
  float3 center;
  float3 extent;
  uint objectIndex;
  uint indexCount;
  uint firstIndex;
  uint batch;      // which of the job's commands this draw adds an instance to
  uint batchFirst; // the batch's first slot in the job's run of the visible list
  uint flags;
};

static const uint CullSelected = 1u << 0;

struct CullConstants {
  float4x4 viewProjection;
  CullDraw *draws;
  uint *visible;
  uint drawCount;    // candidates to test, or commands to empty
  uint commandBase;  // this job's first command
  uint visibleBase;  // this job's first slot in the visible list
  uint selectedOnly; // the selection mask pass keeps only the selected draws
};

[[vk::push_constant]]
ConstantBuffer<CullConstants> cull;

// The draw commands as words, Vulkan's VkDrawIndexedIndirectCommand five to a command, in the
// per-pass storage binding so the instance count is a plain structured-buffer atomic.
[[vk::binding(1, 1)]]
RWStructuredBuffer<uint> commandWords;

static const uint CommandWords = 5;
static const uint IndexCountWord = 0;
static const uint InstanceCountWord = 1;
static const uint FirstIndexWord = 2;
static const uint VertexOffsetWord = 3;
static const uint FirstInstanceWord = 4;

static const uint Threads = 64;

// Gribb-Hartmann from a row-major read of the matrix, for the zero-to-one depth range: a point
// is inside when w+x, w-x, w+y, w-y, z and w-z are all non-negative. The planes are not
// normalised, which a sign test does not need.
bool visible(float3 center, float3 extent) {
  const float4x4 m = cull.viewProjection;
  float4 planes[6];
  planes[0] = m[3] + m[0];
  planes[1] = m[3] - m[0];
  planes[2] = m[3] + m[1];
  planes[3] = m[3] - m[1];
  planes[4] = m[2];
  planes[5] = m[3] - m[2];
  for (uint i = 0; i < 6; ++i) {
    // The box is outside when even the corner furthest along the plane's normal is behind it.
    const float distance = dot(planes[i].xyz, center) + planes[i].w;
    const float reach = dot(abs(planes[i].xyz), extent);
    if (distance + reach < 0.0) {
      return false;
    }
  }
  return true;
}

// Every batch starts the frame with a command that draws nothing: no indices, no instances. The
// cull dispatch that follows only ever adds.
[shader("compute")]
[numthreads(Threads, 1, 1)]
void clearCommands(uint3 thread: SV_DispatchThreadID) {
  if (thread.x >= cull.drawCount) {
    return;
  }
  const uint first = (cull.commandBase + thread.x) * CommandWords;
  for (uint word = 0; word < CommandWords; ++word) {
    commandWords[first + word] = 0;
  }
}

[shader("compute")]
[numthreads(Threads, 1, 1)]
void computeMain(uint3 thread: SV_DispatchThreadID) {
  if (thread.x >= cull.drawCount) {
    return;
  }
  const CullDraw candidate = cull.draws[thread.x];
  const bool kept = (cull.selectedOnly == 0 || (candidate.flags & CullSelected) != 0) &&
                    visible(candidate.center, candidate.extent);
  if (!kept) {
    return;
  }
  const uint command = (cull.commandBase + candidate.batch) * CommandWords;
  const uint run = cull.visibleBase + candidate.batchFirst;
  uint slot = 0;
  InterlockedAdd(commandWords[command + InstanceCountWord], 1u, slot);
  cull.visible[run + slot] = candidate.objectIndex;
  // Every draw of a batch carries the same index range, so the first survivor writes the rest of
  // the command; the others only add instances. The clear left the vertex offset at zero.
  if (slot == 0) {
    commandWords[command + IndexCountWord] = candidate.indexCount;
    commandWords[command + FirstIndexWord] = candidate.firstIndex;
    commandWords[command + FirstInstanceWord] = run;
  }
}
