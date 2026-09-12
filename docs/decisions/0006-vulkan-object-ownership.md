# ADR-0006: vk-bootstrap creates, Vulkan-HPP RAII owns

- **Status:** Accepted
- **Date:** 2026-09-12

## Context

vk-bootstrap removes the boilerplate of instance creation, physical device selection, queue lookup and swapchain creation. It works with C handles and provides `vkb::destroy_instance`, `vkb::destroy_device` and `vkb::destroy_swapchain` to release them. Vulkan-HPP's RAII classes (`vk::raii::Instance`, `vk::raii::Device`, ...) also own and destroy their handles. Using both without a rule means a handle either has two owners or none, and the previous iteration's manual lifetime management in the Vulkan backend was its largest source of shutdown bugs.

## Decision

vk-bootstrap is used only to select and create. Every handle it returns (instance, debug messenger, physical device, device, swapchain, swapchain images and views) is immediately adopted by the corresponding `vk::raii` wrapper, which becomes the single owner. The `vkb::destroy_*` functions are never called and the `vkb::` structs are discarded once adoption is complete. VMA is used through the VulkanMemoryAllocator-Hpp bindings, created after the device and destroyed before it. RAII members in `VulkanDevice` are declared in reverse destruction order so lifetimes are correct by construction. Both vk-bootstrap and the Vulkan-HPP dynamic dispatcher are seeded with the `vkGetInstanceProcAddr` obtained from SDL3, so one loader is used in the process.

## Consequences

- Shutdown order cannot be wrong without the code failing to compile in an obviously suspicious way (a member declared out of order).
- The swapchain recreation path re-runs `vkb::SwapchainBuilder` and adopts the result again; the old RAII swapchain is destroyed by assignment after the device is idle.
- Objects that vk-bootstrap does not create (pipelines, images, buffers, descriptor sets) are created directly through Vulkan-HPP RAII and follow the same rule.
- Vulkan-HPP is header-heavy; compile times are managed with precompiled headers, and the `vulkan.cppm` module remains an optional experiment.

## Alternatives considered

- **vk-bootstrap owning everything with raw handles**: manual destruction order, the previous iteration's failure mode.
- **Dropping vk-bootstrap**: device selection and swapchain creation are a few hundred lines that vk-bootstrap already gets right, including portability enumeration on MoltenVK. Kept for that reason.
- **Vulkan-HPP without RAII, with `vk::UniqueHandle`**: similar ownership semantics with an older API; RAII classes are the direction Vulkan-HPP is moving.
