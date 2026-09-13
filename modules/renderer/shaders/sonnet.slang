// Shared declarations for engine shaders: the per-pass buffers of descriptor set 1, the per-draw
// push constants and the vertex layout. Shaders `import sonnet` and never declare their own
// descriptor sets (docs/rendering.md, "Shaders"). Layouts are scalar and match the C++ structs
// in the renderer module byte for byte.
module sonnet;

public struct Vertex {
  public float3 position;
  public float3 normal;
  public float2 uv;
};

public struct FrameConstants {
  public float4x4 viewProjection;
  public float4 cameraPosition;
  public float4 lightDirection; // xyz: direction the light travels, normalised
  public float4 lightColor;     // rgb: colour, a: intensity
  public float4 ambient;
};

public struct ObjectData {
  public float4x4 model;
  public float4x4 normalMatrix;
  public float4 color;
};

// Vertices are pulled through the mesh's buffer device address; the object index selects the
// draw's entry in the per-pass storage buffer.
public struct DrawConstants {
  public Vertex *vertices;
  public uint objectIndex;
};

[[vk::push_constant]]
public ConstantBuffer<DrawConstants> draw;

[[vk::binding(0, 1)]]
public ConstantBuffer<FrameConstants> frame;

[[vk::binding(1, 1)]]
public StructuredBuffer<ObjectData> objects;
