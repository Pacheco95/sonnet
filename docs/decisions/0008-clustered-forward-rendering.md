# ADR-0008: Clustered forward rendering as the main pipeline

- **Status:** Proposed
- **Date:** 2026-09-12

## Context

The previous iteration used deferred shading with a fat G-buffer, which made transparency, MSAA and material variety awkward, and which is bandwidth-hungry on the tile-based GPUs in the Android and iOS targets. A pipeline decision shapes the render graph, the material model and the shader conventions, so it needs to be made before M3.

## Decision (proposed)

The main pipeline is clustered forward: a depth pre-pass, a compute pass that assigns lights to view-space clusters, a single forward shading pass that reads the cluster list, then post-processing. Transparent objects use the same shading path sorted back to front. A thin G-buffer (normals, roughness) may be written from the pre-pass for screen-space effects, using dynamic rendering local read on mobile.

## Consequences

- One shading path for opaque and transparent geometry, MSAA works, material variety is unconstrained.
- Many lights are supported through clustering rather than a lighting pass; the light count in the README's performance target is reached this way.
- Screen-space effects that need a full G-buffer (some ambient occlusion and reflection techniques) get less input; they use the thin G-buffer or depth-only variants.
- Bandwidth on mobile is lower than deferred; the depth pre-pass costs an extra geometry pass, which GPU-driven culling later reduces.

## Alternatives considered

- **Deferred shading**: simplest many-light story on desktop and the previous iteration's known path, but poor fit for transparency, MSAA and tile-based mobile GPUs.
- **Plain forward with a light limit per object**: simplest, but does not meet the light-count target and would be replaced anyway.
- **Visibility buffer**: best scaling with geometry, but requires the GPU-driven infrastructure listed under "Later" and is a large step for M3.
