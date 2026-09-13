// A compute pass writing one colour into every texel of a storage image from the bindless
// storage array, whose views are 2D arrays whatever the image.

struct PushConstants {
  uint image;
  uint width;
  uint height;
  uint padding;
  float4 color;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> push;

[[vk::binding(2, 0)]]
[format("rgba16f")]
RWTexture2DArray<float4> storageImages[];

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= push.width || id.y >= push.height) {
    return;
  }
  storageImages[push.image][uint3(id.xy, 0)] = push.color;
}
