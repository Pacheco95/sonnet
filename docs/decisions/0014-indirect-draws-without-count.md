# ADR-0014: Indirect draws without drawIndirectCount

- **Status:** Superseded by [ADR-0016](0016-instanced-batches.md)
- **Date:** 2026-09-22

## Context

[ADR-0012](0012-gpu-driven-rendering.md) made `drawIndirectCount` a required device feature, on the belief that every Vulkan 1.4 implementation has it. MoltenVK does not. On an Apple M4 Max under macOS 26, MoltenVK 1.4.1 and 1.4.2 report `VkPhysicalDeviceVulkan12Features::drawIndirectCount = false`, so device selection rejects the only GPU and neither the editor nor the player starts. The feature is optional in Vulkan 1.2 and later, and MoltenVK cannot provide it: Metal's indirect draws take their arguments from a GPU buffer but not their number, so a draw count written by a compute pass has nowhere to go ([MoltenVK issue #168](https://github.com/KhronosGroup/MoltenVK/issues/168), open since 2018). MoltenVK's `MVKDevice.mm` reports every other feature in the [Vulkan baseline](../rendering.md#vulkan-baseline), including `multiDrawIndirect` and `drawIndirectFirstInstance`; the M4 Max run is what confirms it.

MoltenVK encodes `vkCmdDrawIndexedIndirect` with a draw count of N as N Metal indirect draws, one per command, each reading its arguments from the buffer. `firstInstance` goes through unchanged, and a command with no instances draws nothing.

## Decision

- **`drawIndirectCount` is enabled where the device has it rather than required**, and `DeviceInfo::drawIndirectCountSupported` reports which. Every desktop driver and Lavapipe have it, so on them nothing changes.
- **Without it, every command slot is drawn and a culled draw's slot holds zero instances.** This is the alternative ADR-0012 turned down as the only path, taken here only where the count cannot exist. `cull.slang` gains a `fixedSlots` push constant. With it set, each candidate writes the slot at its own position in the order list, with an instance count of one if it survives and zero otherwise, and takes no atomics. The renderer skips the dispatch that clears the counters and records `drawIndexedIndirect` over the batch's whole range instead of `drawIndexedIndirectCount`. Batches, command-buffer sizing, barriers, the scene shaders and the blended direct path are the same on both paths.
- **`rhi` gains `ICommandList::drawIndexedIndirect`**, which traces as `drawIndexedIndirect "name" count N` on the null device. Calling `drawIndexedIndirectCount` on a device without the feature is an assertion.
- **`DeviceDesc::disableDrawIndirectCount` exists for the tests.** It leaves the feature off on a device that has it, so the GPU tests on Lavapipe draw through the path MoltenVK takes. Nothing else sets it.
- **A device-selection failure lists vk-bootstrap's reasons for each rejected device**, so the next missing feature on a new platform names itself instead of reading "no suitable device".

## Consequences

- The editor and player can create a device on macOS. The count path is untouched: `renderer_tests "[benchmark]"` on the RTX 4090 records the same 12 indirect calls, and the pass times before and after the change are the same within run-to-run noise.
- On MoltenVK the CPU encodes one Metal draw per candidate per pass rather than one per batch. The GPU skips the culled ones, but the encoding cost scales with the draw count again. This is still cheaper than before M7, with no push constants and no index-buffer bind per draw, and for scenes of a few hundred draws it is invisible. The benchmark's ten thousand draws across six passes would show it; that is measured when macOS is benchmarked.
- The GPU fetches every slot on MoltenVK, including the culled ones. That is the cost ADR-0012 declined to pay everywhere, and here it is paid only on the platform that has no alternative.
- Both paths are tested. The null-device tests assert each path's calls. On Lavapipe, `rhi_tests` draws zero-instance slots, and the renderer's culling test and the picking tests run once with the count and once without. The selection-mask test with an unselected box beside a selected one is the test that fails if a culled slot draws.
- A stale slot would draw on this path, where a stale count would not. When the frame's candidates do not fit in transient memory, culling does not run, so the renderer skips a job's draws on this path rather than draw the previous frame's commands.

## Alternatives considered

- **Instanced batches: one command per batch whose instance count is the survivor count, with object indices in a visible list.** This removes the count requirement on every platform and gives MoltenVK one Metal draw per batch. It needs batches split by submesh, a second indirection in every scene shader through `SV_InstanceID`, and a visible-list entry for each blended draw on the direct path, so it changes the shaders on every platform in order to fix one. It is the next step if macOS's benchmark shows the per-candidate encoding matters.
- **Reading the counts back and drawing the next frame with a CPU count**: a frame of latency on culling. Draws popping in at the edge of a moving camera are a correctness problem, not a cost.
- **Metal indirect command buffers**: encoded on the GPU, and they would fit exactly, but they are not reachable through Vulkan. They are what a native Metal backend under [ADR-0001](0001-vulkan-1.4-only.md) would use.
- **The fixed-slot path everywhere**: one path instead of two, at the cost ADR-0012 already weighed and declined for the platforms that have the count.
