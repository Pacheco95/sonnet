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
  // Occlusion culling and the survivor counts (ADR-0024). `pyramid` is the pyramid to test
  // against (OcclusionTest); `words` is the state buffer, which starts with the counters and
  // holds, from `stateBase`, one word per candidate that phase one records (RecordState) as 1 when
  // it kept the candidate; HasCounter adds each survivor, and its triangles, to the pair of
  // counters at `counter`.
  float *pyramid;
  uint *words;
  uint lateBase; // phase two: this job's first late command
  uint flags;
  uint stateBase;
  uint counter;
};

static const uint OcclusionTest = 1u << 0;
static const uint RecordState = 1u << 1;
static const uint HasCounter = 1u << 2;

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
static const uint PyramidHeaderWords = 32;

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

// Whether a box is hidden behind what the pyramid holds, projecting its corners with `m`. A corner
// behind the camera cannot be projected, so such a box is never hidden. The pyramid's own
// depths are reversed-Z: the box is hidden when its nearest corner is still farther than the
// farthest surface in the texels it covers.
bool occluded(float4x4 m, float3 center, float3 extent) {
  const uint2 size = uint2(asuint(cull.pyramid[16]), asuint(cull.pyramid[17]));
  const uint levels = asuint(cull.pyramid[18]);
  float2 low = float2(1.0, 1.0);
  float2 high = float2(0.0, 0.0);
  float nearest = 0.0;
  for (uint corner = 0; corner < 8; ++corner) {
    const float3 sign = float3((corner & 1) != 0 ? 1.0 : -1.0, (corner & 2) != 0 ? 1.0 : -1.0,
                               (corner & 4) != 0 ? 1.0 : -1.0);
    const float4 clip = mul(m, float4(center + sign * extent, 1.0));
    if (clip.w <= 0.0) {
      return false;
    }
    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    low = min(low, clamp(uv, 0.0, 1.0));
    high = max(high, clamp(uv, 0.0, 1.0));
    nearest = max(nearest, ndc.z);
  }
  // One texel of level 0 more on every side. The depth was rasterised with a jittered matrix that
  // moves a surface by up to half a pixel, and a test whose matrix is not the pyramid's own (a
  // phase-one box moved since the pyramid was built) may see its edge there (ADR-0024).
  const float2 margin = 1.0 / float2(size);
  low = max(low - margin, float2(0.0, 0.0));
  high = min(high + margin, float2(1.0, 1.0));
  // The level at which the box spans no more than two texels each way.
  const float2 texels = (high - low) * float2(size);
  const uint level = min(levels - 1, uint(ceil(log2(max(max(texels.x, texels.y), 1.0)))));
  uint offset = PyramidHeaderWords;
  for (uint i = 0; i < level; ++i) {
    const uint2 s = max(size >> i, uint2(1, 1));
    offset += s.x * s.y;
  }
  const uint2 levelSize = max(size >> level, uint2(1, 1));
  const uint2 first = min(uint2(low * float2(levelSize)), levelSize - 1);
  const uint2 last = min(uint2(high * float2(levelSize)), levelSize - 1);
  float farthest = cull.pyramid[offset + first.y * levelSize.x + first.x];
  farthest = min(farthest, cull.pyramid[offset + first.y * levelSize.x + last.x]);
  farthest = min(farthest, cull.pyramid[offset + last.y * levelSize.x + first.x]);
  farthest = min(farthest, cull.pyramid[offset + last.y * levelSize.x + last.x]);
  return nearest < farthest * 0.999;
}

// A survivor counts as a draw and as its triangles in the job's counters, which a later frame
// reads back for the statistics.
void count(CullDraw candidate) {
  if ((cull.flags & HasCounter) != 0) {
    InterlockedAdd(cull.words[cull.counter], 1u);
    InterlockedAdd(cull.words[cull.counter + 1], candidate.indexCount / 3);
  }
}

// Every batch starts the frame with a command that draws nothing: no indices, no instances. The
// cull dispatch that follows only ever adds.
[shader("compute")]
[numthreads(Threads, 1, 1)]
void clearCommands(uint3 thread: SV_DispatchThreadID) {
  if (thread.x == 0 && (cull.flags & HasCounter) != 0) {
    cull.words[cull.counter] = 0;
    cull.words[cull.counter + 1] = 0;
  }
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
  bool kept = (cull.selectedOnly == 0 || (candidate.flags & CullSelected) != 0) &&
              visible(candidate.center, candidate.extent);
  if (kept && (cull.flags & OcclusionTest) != 0) {
    // The previous frame's pyramid with the matrix its depth was drawn with; a box it hides is
    // left to phase two, which tests it against this frame's.
    float4x4 previous;
    for (uint row = 0; row < 4; ++row) {
      previous[row] = float4(cull.pyramid[row * 4], cull.pyramid[row * 4 + 1], cull.pyramid[row * 4 + 2],
                             cull.pyramid[row * 4 + 3]);
    }
    kept = !occluded(previous, candidate.center, candidate.extent);
  }
  if ((cull.flags & RecordState) != 0) {
    cull.words[cull.stateBase + thread.x] = kept ? 1u : 0u;
  }
  if (!kept) {
    return;
  }
  count(candidate);
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

// Phase two (ADR-0024): the candidates phase one did not draw, tested against this frame's
// pyramid, built from the depth phase one drew. A survivor goes after the batch's early
// instances in its run of the visible list, counted by the late command, whose first instance is
// the first free slot. The main command's count is stable during this dispatch; `mergeLate`
// folds the late count into it afterwards, so every later pass draws early and late together.
[shader("compute")]
[numthreads(Threads, 1, 1)]
void lateMain(uint3 thread: SV_DispatchThreadID) {
  if (thread.x >= cull.drawCount || cull.words[cull.stateBase + thread.x] != 0) {
    return;
  }
  const CullDraw candidate = cull.draws[thread.x];
  if (!visible(candidate.center, candidate.extent) ||
      occluded(cull.viewProjection, candidate.center, candidate.extent)) {
    return;
  }
  const uint command = (cull.commandBase + candidate.batch) * CommandWords;
  const uint late = (cull.lateBase + candidate.batch) * CommandWords;
  const uint run = cull.visibleBase + candidate.batchFirst;
  const uint early = commandWords[command + InstanceCountWord];
  uint slot = 0;
  InterlockedAdd(commandWords[late + InstanceCountWord], 1u, slot);
  cull.visible[run + early + slot] = candidate.objectIndex;
  count(candidate);
  commandWords[late + IndexCountWord] = candidate.indexCount;
  commandWords[late + FirstIndexWord] = candidate.firstIndex;
  commandWords[late + FirstInstanceWord] = run + early;
}

[shader("compute")]
[numthreads(Threads, 1, 1)]
void mergeLate(uint3 thread: SV_DispatchThreadID) {
  if (thread.x >= cull.drawCount) {
    return;
  }
  const uint command = (cull.commandBase + thread.x) * CommandWords;
  const uint late = (cull.lateBase + thread.x) * CommandWords;
  const uint count = commandWords[late + InstanceCountWord];
  if (count == 0) {
    return;
  }
  if (commandWords[command + InstanceCountWord] == 0) {
    commandWords[command + IndexCountWord] = commandWords[late + IndexCountWord];
    commandWords[command + FirstIndexWord] = commandWords[late + FirstIndexWord];
    commandWords[command + FirstInstanceWord] = commandWords[late + FirstInstanceWord];
  }
  commandWords[command + InstanceCountWord] += count;
}

// Copies the counters, which live with the state in device memory, into the host-visible slot this
// frame's statistics are read from; `visible` is the destination and `drawCount` the word count.
[shader("compute")]
[numthreads(Threads, 1, 1)]
void publishCounters(uint3 thread: SV_DispatchThreadID) {
  if (thread.x < cull.drawCount) {
    cull.visible[thread.x] = cull.words[thread.x];
  }
}
