# ADR-0015: Bindless vertex buffers instead of a vertex pointer per object

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

[ADR-0012](0012-gpu-driven-rendering.md) moved the mesh's vertex address from the push constants into `ObjectData`, as a `Vertex *` the vertex shader pulls through with `SV_VertexID`. On MoltenVK the scene shaders then failed to compile. Every scene shader began with `const ObjectData object = objects[objectIndex];`, and SPIRV-Cross translates that copy into a thread-address-space struct with a `device Vertex *` member, which Metal rejects. The engine's other pointers live in the frame constants and in the cull and skin push constants, and their shaders only read them field by field, never copying the whole struct into a local. They compile, and the editor and export now run on an Apple M4 Max.

## Decision

- **Scene shaders pull vertices from a bindless array.** Set 0 gains binding 5, `StructuredBuffer<Vertex> vertexBuffers[]`, a runtime array of up to `MaxBindlessStorageBuffers` (4096), partially bound and updated after bind like the others. `ObjectData` carries a `uint vertexBuffer` index in place of the pointer, padded so the entry stays 96 bytes. The vertex shaders read `vertexBuffers[objects[i].vertexBuffer][vertexId]`.
- **`IDevice::storageBufferIndex` hands out the slots.** A buffer with `Storage` usage takes one on the first request and keeps it. The slot goes back to the free list when the buffer's deferred destruction runs, so a frame still in flight never sees it rewritten. A buffer without `Storage` usage gets `InvalidBindlessIndex`. A full array logs one error for that buffer and also returns `InvalidBindlessIndex`: nothing is written past the end, and the renderer skips the draw the way it skips a stale mesh handle.
- **The index is per draw, so it is dynamically uniform.** Each command of a multi-draw indirect call is its own invocation group, so no `NonUniformResourceIndex` is needed. `shaderStorageBufferArrayNonUniformIndexing` is in the baseline regardless.
- **Structs that hold a pointer are read field by field, never copied whole into a local.** That is what kept the remaining pointers working on Metal, and it is now a rule for every shader.
- Buffer device addresses stay everywhere else: the frame constants' materials, lights and clusters, the culling pass's candidates and commands, and the skinning pass's buffers.

## Consequences

- The scene shaders compile on Metal. On the RTX 4090 the benchmark's pass times are the same as with the pointer, within run-to-run noise, and `ObjectData` does not grow.
- A scene can hold at most 4096 vertex buffers at once, counting meshes and skinned instances. Past that, draws disappear with a logged error rather than corrupt memory. Raising the limit is a constant, bounded by the device's `maxDescriptorSetUpdateAfterBindStorageBuffers`, which descriptor-indexing devices report as at least 500 000.
- `IDevice` gains a pure virtual method, which every implementation (the Vulkan device and the null device) provides.
- The skinning pass keeps writing through a device address. Its output buffers take a slot when their draw first resolves, and give it back when they are released.

## Alternatives considered

- **Keep the pointer and never copy `ObjectData` whole**: the smallest change, and it follows the same field-by-field rule that keeps the other pointers working. It was not tried on the Mac. It would leave every future scene shader one innocent `const ObjectData object = …` away from breaking Metal, a mistake nothing on Linux or Windows would catch.
- **Vertex input bindings instead of pulling**: an indirect draw cannot rebind vertex buffers per command, so this would split batches down to one draw each, which ADR-0012 exists to avoid.
- **One shared vertex arena with offsets**: removes the index and the limit, but it is the suballocator ADR-0012 already deferred.
