// Analytically filtered, unjittered editor overlay. No history or depth writes: the scene's
// temporal resolve is already finished. The plane and its U direction are supplied by the caller.
struct GridConstants {
  float4x4 inverseViewProjection;
  float4 cameraNear;
  float4 plane;
  float4 axisU;
  float4 viewport;
};
[[vk::push_constant]] ConstantBuffer<GridConstants> grid;

[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position {
  return float4(float2((id << 1) & 2, id & 2) * 2.0 - 1.0, 0.0, 1.0);
}

float3 nearPoint(float2 ndc) {
  float4 p = mul(grid.inverseViewProjection, float4(ndc, 1.0, 1.0));
  return p.xyz / p.w;
}

float lines(float2 coordinate, float2 footprint, float spacing) {
  float2 distance = abs(frac(coordinate / spacing + 0.5) - 0.5) * spacing;
  float2 coverage = saturate(1.0 - distance / footprint);
  // Remove a level before it becomes subpixel; coarser levels remain at the same world positions.
  float2 visibility = 1.0 - smoothstep(spacing * 0.08, spacing * 0.25, footprint);
  return max(coverage.x * visibility.x, coverage.y * visibility.y);
}

struct FragmentOutput {
  float4 color : SV_Target;
  float depth : SV_Depth;
};

[shader("fragment")]
FragmentOutput fragmentMain(float4 pixel : SV_Position) {
  float2 ndc = pixel.xy / grid.viewport.xy * float2(2.0, -2.0) + float2(-1.0, 1.0);
  float3 origin = grid.cameraNear.xyz;
  float3 ray = normalize(nearPoint(ndc) - origin);
  float denominator = dot(ray, grid.plane.xyz);
  // A finite intersection keeps derivatives well behaved even in quads crossing the horizon.
  float safeDenominator = abs(denominator) < 1e-6 ? (denominator < 0.0 ? -1e-6 : 1e-6) : denominator;
  float t = -(dot(origin, grid.plane.xyz) + grid.plane.w) / safeDenominator;
  float3 hit = origin + ray * t;
  float3 axisV = cross(grid.axisU.xyz, grid.plane.xyz);
  float2 coordinate = float2(dot(hit, grid.axisU.xyz), dot(hit, axisV));
  float2 footprint = max(fwidth(coordinate), float2(1e-5));
  float minor = lines(coordinate, footprint, 1.0);
  float major = lines(coordinate, footprint, 10.0);
  float coarse = lines(coordinate, footprint, 100.0);
  float alpha = max(minor * 0.28, max(major * 0.45, coarse * 0.5));
  float3 color = float3(0.55);
  float2 axes = saturate(1.0 - abs(coordinate) / (footprint * 1.5));
  // Constant V is the X axis (red), constant U the Z axis (blue).
  if (axes.y > alpha) { color = float3(0.9, 0.18, 0.16); alpha = axes.y * 0.8; }
  if (axes.x > alpha) { color = float3(0.2, 0.4, 0.95); alpha = axes.x * 0.8; }
  alpha *= smoothstep(0.015, 0.12, abs(denominator)) * (1.0 - smoothstep(100.0, 1000.0, t));
  float3 forward = normalize(nearPoint(float2(0.0)) - origin);
  float viewDepth = dot(hit - origin, forward);
  float depth = grid.cameraNear.w / max(viewDepth, grid.cameraNear.w);
  // Scene depth is jittered by up to half a pixel. Use a conservative one-pixel depth
  // footprint so a coplanar tilted floor still owns its pixels throughout the jitter cycle.
  float depthFootprint = fwidth(depth);
  if (t <= 0.0 || viewDepth < grid.cameraNear.w || alpha <= 0.001) { discard; }
  FragmentOutput result;
  result.color = float4(color, alpha);
  // Slightly behind the plane: a coplanar ground owns its pixels instead of fighting the grid.
  result.depth = max(0.0, depth - depthFootprint) * 0.9999;
  return result;
}
