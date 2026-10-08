// GPU particles (ADR-0023): `simulate` runs one thread per slot of an emitter's ring, starts the
// particles this frame emits and integrates the live ones, and appends the survivors' slots to the
// alive list with an atomic on the instance count of the emitter's indirect command; `drawMain`
// then draws that many camera-facing quads from it. A pass of its own: it declares its bindings
// and push constants instead of importing sonnet (docs/rendering.md, "Shaders"), so the structs
// repeat the C++ ones in the renderer byte for byte.

struct Particle {
  float3 position;
  float age; // seconds since it was born; a particle is alive while age < lifetime
  float3 velocity;
  float lifetime;
};

static const uint FlagLocal = 1u << 0;    // simulated and drawn in the emitter's space
static const uint FlagAdditive = 1u << 1; // colour is weighted by alpha, since the blend adds

struct EmitterParams {
  float4x4 model; // emitter space into the world
  float4 colorStart;
  float4 colorEnd;
  float3 gravity;
  float drag;
  float2 lifetime; // seconds, min and max
  float2 speed;    // metres per second, min and max
  float coneAngle; // half angle of the emission cone about +Y, radians
  float sizeStart;
  float sizeEnd;
  float dt;
  uint maxParticles;
  uint spawnCount; // slots from spawnHead that start a particle this frame
  uint spawnHead;
  uint serial; // the number of particles started before this frame, which seeds them
  uint seed;
  uint texture; // bindless sampled image, or 0xFFFFFFFF for a soft disc
  uint flags;
  uint sampler; // bindless sampler for the texture
};

struct SimulateConstants {
  Particle *particles;
  uint *alive;
  EmitterParams *params;
};

struct DrawConstants {
  float4x4 viewProjection;
  float3 cameraRight;
  float3 cameraUp;
  Particle *particles;
  uint *alive;
  EmitterParams *params;
};

[[vk::push_constant]]
ConstantBuffer<SimulateConstants> simulation;

[[vk::push_constant]]
ConstantBuffer<DrawConstants> drawing;

// The emitter's indirect command as words, Vulkan's VkDrawIndexedIndirectCommand: the instance
// count is a plain structured-buffer atomic.
[[vk::binding(1, 1)]]
RWStructuredBuffer<uint> commandWords;

[[vk::binding(0, 0)]]
Texture2D<float4> textures[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

static const uint Threads = 64;
static const float Pi = 3.14159265358979;

// PCG hash, from a particle's serial and the draw of random number it asks for: the same
// particles whatever the dispatch order, and the same from run to run.
uint pcg(uint v) {
  const uint state = v * 747796405u + 2891336453u;
  const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

float random(uint seed, uint serial, uint draw) {
  return float(pcg(seed ^ pcg(serial + pcg(draw * 2654435761u)))) * (1.0 / 4294967296.0);
}

// Every frame starts with a command that draws no instance; `simulate` only adds to it.
[shader("compute")]
[numthreads(1, 1, 1)]
void resetCommand(uint3 thread : SV_DispatchThreadID) {
  commandWords[0] = 6; // two triangles
  commandWords[1] = 0;
  commandWords[2] = 0;
  commandWords[3] = 0;
  commandWords[4] = 0;
}

[shader("compute")]
[numthreads(Threads, 1, 1)]
void simulate(uint3 thread : SV_DispatchThreadID) {
  const EmitterParams params = *simulation.params;
  const uint slot = thread.x;
  if (slot >= params.maxParticles) {
    return;
  }
  Particle particle = simulation.particles[slot];
  // The slots start from the head round the ring are the ones born this frame.
  const uint ring = (slot + params.maxParticles - params.spawnHead) % params.maxParticles;
  if (ring < params.spawnCount) {
    const uint serial = params.serial + ring;
    const float cosTheta = lerp(1.0, cos(params.coneAngle), random(params.seed, serial, 0));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * Pi * random(params.seed, serial, 1);
    const float3 direction = float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
    const float speed = lerp(params.speed.x, params.speed.y, random(params.seed, serial, 2));
    particle.lifetime = lerp(params.lifetime.x, params.lifetime.y, random(params.seed, serial, 3));
    // Born somewhere inside the frame, so a fast emitter at a low frame rate is not a row of
    // bunches.
    particle.age = random(params.seed, serial, 4) * params.dt;
    float3 origin = float3(0.0, 0.0, 0.0);
    particle.velocity = direction * speed;
    if ((params.flags & FlagLocal) == 0) {
      origin = mul(params.model, float4(0.0, 0.0, 0.0, 1.0)).xyz;
      particle.velocity = mul(params.model, float4(particle.velocity, 0.0)).xyz;
    }
    particle.position = origin + particle.velocity * particle.age;
  } else if (particle.age < particle.lifetime) {
    particle.velocity += params.gravity * params.dt;
    particle.velocity *= exp(-params.drag * params.dt);
    particle.position += particle.velocity * params.dt;
    particle.age += params.dt;
  }
  simulation.particles[slot] = particle;
  if (particle.age < particle.lifetime) {
    uint index;
    InterlockedAdd(commandWords[1], 1u, index);
    simulation.alive[index] = slot;
  }
}

struct VertexOutput {
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 corner : TEXCOORD0; // -1 to 1 across the quad
  nointerpolation uint texture : TEXCOORD1;
  nointerpolation uint flags : TEXCOORD2;
  nointerpolation uint sampler : TEXCOORD3;
};

[shader("vertex")]
VertexOutput drawMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
  const EmitterParams params = *drawing.params;
  const Particle particle = drawing.particles[drawing.alive[instanceId]];
  const float t = saturate(particle.age / max(particle.lifetime, 1.0e-6));
  const float size = lerp(params.sizeStart, params.sizeEnd, t);
  // Corners 0 to 3 of the quad the index buffer's [0 1 2 2 1 3] makes of two triangles.
  const float2 corner = float2((vertexId & 1u) != 0u ? 1.0 : -1.0, (vertexId & 2u) != 0u ? 1.0 : -1.0);
  float3 center = particle.position;
  if ((params.flags & FlagLocal) != 0) {
    center = mul(params.model, float4(center, 1.0)).xyz;
  }
  const float3 world = center + (drawing.cameraRight * corner.x + drawing.cameraUp * corner.y) * (size * 0.5);
  VertexOutput output;
  output.position = mul(drawing.viewProjection, float4(world, 1.0));
  output.color = lerp(params.colorStart, params.colorEnd, t);
  output.corner = corner;
  output.texture = params.texture;
  output.flags = params.flags;
  output.sampler = params.sampler;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  float4 color = input.color;
  if (input.texture != 0xFFFFFFFFu) {
    const float2 uv = float2(input.corner.x * 0.5 + 0.5, 0.5 - input.corner.y * 0.5);
    color *= textures[NonUniformResourceIndex(input.texture)].Sample(samplers[NonUniformResourceIndex(input.sampler)], uv);
  } else {
    // A soft disc: full in the middle, nothing at the quad's edge.
    const float fall = saturate(1.0 - length(input.corner));
    color.a *= fall * fall;
  }
  if ((input.flags & FlagAdditive) != 0u) {
    color.rgb *= color.a;
  }
  return color;
}
