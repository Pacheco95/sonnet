// The depth pyramid (ADR-0024): a min-reduced mip chain of the pre-pass depth that the culling pass
// tests bounds against. Depth is reversed-Z, so a texel's minimum is the farthest surface in its
// footprint: a box whose nearest point is farther than that minimum is hidden behind it.
//
// The pyramid is one storage buffer, not an image, so the culling shader reads it through an
// address like the rest of its inputs. It starts with a header of 32 words that the first
// dispatch writes (the view-projection matrix the depth was drawn with, row by row; the size of
// level 0, then the level count as unsigned integers stored as floats' bits), and the levels
// follow, each row-major, level 0 first. Level sizes halve down to one texel.

struct HizConstants {
  float4x4 viewProjection;
  float *pyramid;
  uint2 depthSize; // the pre-pass depth image
  uint2 size;      // level 0
  uint levels;
  uint level; // the level a buildLevel dispatch writes
};

[[vk::push_constant]]
ConstantBuffer<HizConstants> hiz;

[[vk::binding(2, 1)]]
Texture2D<float> depthImage;

static const uint HeaderWords = 32;

uint2 levelSize(uint level) {
  return max(hiz.size >> level, uint2(1, 1));
}

uint levelOffset(uint level) {
  uint offset = HeaderWords;
  for (uint i = 0; i < level; ++i) {
    const uint2 size = levelSize(i);
    offset += size.x * size.y;
  }
  return offset;
}

// Level 0: every texel takes the minimum of the depth texels under it, so a depth image that is
// not a power of two is covered whole.
[shader("compute")]
[numthreads(8, 8, 1)]
void buildBase(uint3 thread: SV_DispatchThreadID) {
  if (thread.x == 0 && thread.y == 0) {
    for (uint row = 0; row < 4; ++row) {
      for (uint column = 0; column < 4; ++column) {
        hiz.pyramid[row * 4 + column] = hiz.viewProjection[row][column];
      }
    }
    hiz.pyramid[16] = asfloat(hiz.size.x);
    hiz.pyramid[17] = asfloat(hiz.size.y);
    hiz.pyramid[18] = asfloat(hiz.levels);
  }
  if (thread.x >= hiz.size.x || thread.y >= hiz.size.y) {
    return;
  }
  const uint2 first = (thread.xy * hiz.depthSize) / hiz.size;
  const uint2 last = max(((thread.xy + 1) * hiz.depthSize + hiz.size - 1) / hiz.size, first + 1);
  float farthest = 1.0;
  for (uint y = first.y; y < last.y; ++y) {
    for (uint x = first.x; x < last.x; ++x) {
      farthest = min(farthest, depthImage.Load(int3(int(x), int(y), 0)));
    }
  }
  hiz.pyramid[HeaderWords + thread.y * hiz.size.x + thread.x] = farthest;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void buildLevel(uint3 thread: SV_DispatchThreadID) {
  const uint2 size = levelSize(hiz.level);
  if (thread.x >= size.x || thread.y >= size.y) {
    return;
  }
  const uint2 source = levelSize(hiz.level - 1);
  const uint sourceOffset = levelOffset(hiz.level - 1);
  const uint2 a = min(thread.xy * 2, source - 1);
  const uint2 b = min(thread.xy * 2 + 1, source - 1);
  float farthest = hiz.pyramid[sourceOffset + a.y * source.x + a.x];
  farthest = min(farthest, hiz.pyramid[sourceOffset + a.y * source.x + b.x]);
  farthest = min(farthest, hiz.pyramid[sourceOffset + b.y * source.x + a.x]);
  farthest = min(farthest, hiz.pyramid[sourceOffset + b.y * source.x + b.x]);
  hiz.pyramid[levelOffset(hiz.level) + thread.y * size.x + thread.x] = farthest;
}
