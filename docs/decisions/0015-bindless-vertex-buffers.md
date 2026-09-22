# ADR-0015: Bindless vertex buffers instead of a vertex pointer per object

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

[ADR-0012](0012-gpu-driven-rendering.md) moved the mesh's vertex address from the push constants into `ObjectData`, as a `Vertex *` the vertex shader pulls through with `SV_VertexID`. On MoltenVK the scene shaders then failed to compile, with `member reference type 'const device Vertex_natural *' is a pointer` in the translated depth shader. The object array uses scalar layout, and Slang reads an element of it by loading the whole `ObjectData` and converting it member by member into its logical form. It does this whatever the source reads: indexing `objects[i].model` alone emits the same whole-struct `OpLoad`. SPIRV-Cross translates that conversion into a thread-address-space copy, and for the pointer member it writes `copy.vertices.position = objects[i].vertices.position`, treating the pointer as if it were a `Vertex`, which is not valid MSL.

The engine's other pointers live in the frame constants, a uniform buffer, and in the cull and skin push constants. Slang reaches their fields through access chains without loading the whole block, so no copy is made and they compile. With the change below, the editor and export run on an Apple M4 Max.

## Decision

- **Scene shaders pull vertices from a bindless array.** Set 0 gains binding 5, `StructuredBuffer<Vertex> vertexBuffers[]`, a runtime array of up to `MaxBindlessStorageBuffers` (4096), partially bound and updated after bind like the others. `ObjectData` carries a `uint vertexBuffer` index in place of the pointer, padded so the entry stays 96 bytes. The vertex shaders read `vertexBuffers[objects[i].vertexBuffer][vertexId]`.
- **`IDevice::storageBufferIndex` hands out the slots.** A buffer with `Storage` usage takes one on the first request and keeps it. The slot goes back to the free list when the buffer's deferred destruction runs, so a frame still in flight never sees it rewritten. A buffer without `Storage` usage gets `InvalidBindlessIndex`. A full array logs one error for that buffer and also returns `InvalidBindlessIndex`: nothing is written past the end, and the renderer skips the draw the way it skips a stale mesh handle.
- **The index is per draw, so it is dynamically uniform.** Each command of a multi-draw indirect call is its own invocation group, so no `NonUniformResourceIndex` is needed. `shaderStorageBufferArrayNonUniformIndexing` is in the baseline regardless.
- **No struct stored in a storage buffer holds a pointer.** Pointers belong in uniform and push-constant blocks, where they compile on Metal. No shader-source pattern avoids the whole-struct load of a storage-buffer element. Today `ObjectData`, `CullDraw`, `MaterialData`, `Light` and `Cluster` hold none.
- Buffer device addresses stay everywhere else: the frame constants' materials, lights and clusters, the culling pass's candidates and commands, and the skinning pass's buffers.

## Consequences

- The scene shaders compile on Metal. On the RTX 4090 the benchmark's pass times are the same as with the pointer, within run-to-run noise, and `ObjectData` does not grow.
- A scene can hold at most 4096 vertex buffers at once, counting meshes and skinned instances. Past that, draws disappear with a logged error rather than corrupt memory. Raising the limit is a constant, bounded by the device's `maxDescriptorSetUpdateAfterBindStorageBuffers`, which descriptor-indexing devices report as at least 500 000.
- `IDevice` gains a pure virtual method, which every implementation (the Vulkan device and the null device) provides.
- The skinning pass keeps writing through a device address. Its output buffers take a slot when their draw first resolves, and give it back when they are released.

## Alternatives considered

- **Keep the pointer and read `ObjectData` field by field in the source**: tried on the M4 Max with MoltenVK 1.4.1 against the commit before this change. It fails with the same Metal error at the same line of the translated shader. The SPIR-V still loads the whole struct, which the same Slang and SPIRV-Cross reproduce on Linux, in debug and release builds alike.
- **Vertex input bindings instead of pulling**: an indirect draw cannot rebind vertex buffers per command, so this would split batches down to one draw each, which ADR-0012 exists to avoid.
- **One shared vertex arena with offsets**: removes the index and the limit, but it is the suballocator ADR-0012 already deferred.
