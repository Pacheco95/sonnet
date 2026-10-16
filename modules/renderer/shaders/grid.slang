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

float3 unproject(float2 ndc, float depth) {
  float4 p = mul(grid.inverseViewProjection, float4(ndc, depth, 1.0));
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
  bool orthographic = grid.viewport.w > 0.5;
  float3 near = unproject(ndc, 1.0);
  float3 origin = orthographic ? near : grid.cameraNear.xyz;
  float3 ray = orthographic ? normalize(unproject(ndc, 0.0) - near) : normalize(near - origin);
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
  float3 red = float3(0.9, 0.18, 0.16);
  float3 green = float3(0.2, 0.8, 0.25);
  float3 blue = float3(0.2, 0.4, 0.95);
  float3 u = abs(grid.axisU.xyz);
  float3 v = abs(axisV);
  if (axes.y > alpha) { color = u.x * red + u.y * green + u.z * blue; alpha = axes.y * 0.8; }
  if (axes.x > alpha) { color = v.x * red + v.y * green + v.z * blue; alpha = axes.x * 0.8; }
  if (!orthographic) {
    alpha *= smoothstep(0.015, 0.12, abs(denominator)) * (1.0 - smoothstep(100.0, 1000.0, t));
  }
  float3 forward = orthographic ? ray : normalize(unproject(float2(0.0), 1.0) - origin);
  float viewDepth = dot(hit - origin, forward) + (orthographic ? grid.cameraNear.w : 0.0);
  float depth = orthographic ? (grid.viewport.z - viewDepth) / (grid.viewport.z - grid.cameraNear.w)
                             : grid.cameraNear.w / max(viewDepth, grid.cameraNear.w);
  // Scene depth is jittered by up to half a pixel. Use a conservative one-pixel depth
  // footprint so a coplanar tilted floor still owns its pixels throughout the jitter cycle.
  float depthFootprint = fwidth(depth);
  if (t <= 0.0 || viewDepth < grid.cameraNear.w || (orthographic && depth < 0.0) || alpha <= 0.001) { discard; }
  FragmentOutput result;
  result.color = float4(color, alpha);
  // Slightly behind the plane: a coplanar ground owns its pixels instead of fighting the grid.
  result.depth = max(0.0, depth - depthFootprint) * 0.9999;
  return result;
}
