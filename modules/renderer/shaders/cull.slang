// GPU culling (ADR-0012): one thread per candidate draw tests its world-space bounds against
// the pass's frustum and, for a survivor, appends a draw command to its batch's range of the
// command buffer. A batch is a run of draws sharing a pipeline and a mesh, so every command in
// one range is drawn by a single drawIndexedIndirectCount against the mesh's index buffer.
//
// The frustum comes from the pass's view-projection matrix, so the same candidate array serves
// the camera, the depth pre-pass and each shadow cascade; only the matrix differs. This is the
// one scene shader that does not import `sonnet`: it needs a writable counts buffer where the
// shared module declares the read-only object array.

struct CullDraw {
  float3 center;
  float3 extent;
  uint objectIndex;
  uint indexCount;
  uint firstIndex;
  uint batch;
  uint batchFirst;
  uint flags;
};

static const uint CullSelected = 1u << 0;

struct IndirectCommand {
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int vertexOffset;
  uint firstInstance;
};

struct CullConstants {
  float4x4 viewProjection;
  CullDraw *draws;
  IndirectCommand *commands; // already offset to this job's first command slot
  uint drawCount;            // candidates to test, or counts to clear
  uint countBase;            // this job's first slot in the shared counts buffer
  uint selectedOnly;         // the selection mask pass keeps only the selected draws
  uint padding;
};

[[vk::push_constant]]
ConstantBuffer<CullConstants> cull;

// The counts every batch appends against, in the per-pass storage binding so the appends are
// plain structured-buffer atomics.
[[vk::binding(1, 1)]]
RWStructuredBuffer<uint> counts;

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

// Every batch starts the frame at zero; the cull dispatch that follows only ever adds.
[shader("compute")]
[numthreads(Threads, 1, 1)]
void clearCounts(uint3 thread: SV_DispatchThreadID) {
  if (thread.x >= cull.drawCount) {
    return;
  }
  counts[cull.countBase + thread.x] = 0;
}

[shader("compute")]
[numthreads(Threads, 1, 1)]
void computeMain(uint3 thread: SV_DispatchThreadID) {
  if (thread.x >= cull.drawCount) {
    return;
  }
  const CullDraw candidate = cull.draws[thread.x];
  if (cull.selectedOnly != 0 && (candidate.flags & CullSelected) == 0) {
    return;
  }
  if (!visible(candidate.center, candidate.extent)) {
    return;
  }
  uint slot = 0;
  InterlockedAdd(counts[cull.countBase + candidate.batch], 1u, slot);

  IndirectCommand command;
  command.indexCount = candidate.indexCount;
  command.instanceCount = 1;
  command.firstIndex = candidate.firstIndex;
  command.vertexOffset = 0;
  command.firstInstance = candidate.objectIndex;
  cull.commands[candidate.batchFirst + slot] = command;
}
