# ADR-0019: Vulkan 1.3 devices that carry the engine's 1.4 features as extensions

- **Status:** Accepted
- **Date:** 2026-09-23

## Context

[ADR-0001](0001-vulkan-1.4-only.md) makes Vulkan 1.4 a hard minimum on the device, and counts on Android devices launching with Android 16 to guarantee it. M9's Android device is a Samsung Galaxy S25 Ultra (SM-S938B, Snapdragon 8 Elite, Adreno 830) updated to Android 16, which launched with Android 15 (`ro.product.first_api_level` 35). `adb shell cmd gpu vkjson`, read on 2026-09-23, reports an instance at 1.4.0 and a device at **1.3.284**, with Qualcomm's driver dated 2026-03-26 ([ADR-0018](0018-mobile-export.md), open question 6). The device selector rejects it.

The same report shows the device has everything the engine uses:

- Every feature `VulkanDevice.cpp` requires from Vulkan 1.0 to 1.3 is present, as core.
- The four it requires from Vulkan 1.4 (push descriptors, dynamic rendering local read, maintenance 5 and maintenance 6) are present as the extensions 1.4 promoted: `VK_KHR_push_descriptor`, `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_maintenance5` and `VK_KHR_maintenance6`.
- Every limit the bindless set needs is met many times over: 16 777 216 update-after-bind descriptors of each kind against 4 224 at most. ASTC and BC are both there.

What the engine takes from 1.4 is exactly those four features, which the code confirms:

- `VulkanDevice.cpp` requests them through `VkPhysicalDeviceVulkan14Features`, and requests nothing else from that structure.
- The one 1.4 command the engine records is `vkCmdPushDescriptorSet`, in `VulkanCommandList::pushDescriptors`, under `bindBuffers` and `bindImages`. Vulkan-HPP's RAII device dispatcher falls back to `vkCmdPushDescriptorSetKHR` when the core entry point is null (`vulkan_raii.hpp`, `if ( !vkCmdPushDescriptorSet ) vkCmdPushDescriptorSet = vkCmdPushDescriptorSetKHR;`).
- The descriptor-set-layout flag `ePushDescriptor` has the same value in the core and the extension form.
- No code in `rhi` calls a maintenance 5 or 6 command or relies on dynamic rendering local read beyond enabling it.
- VMA is told `VK_API_VERSION_1_4` unconditionally, so it would call 1.4 entry points on a 1.3 device.
- The limits 1.4 raised are not ones the engine reaches: it uses 128 bytes of push constants and two descriptor sets, within 1.3's minimums.

ADR-0001 considered a 1.3 baseline with optional 1.4 features and rejected it for "feature queries and two code paths for push descriptors and local reads". That concern is about features that are sometimes there. This case is different: the features stay required, and only the form in which a device offers them varies.

## Decision

- **The device selector accepts a device at Vulkan 1.3 or later that has all four of the engine's 1.4 features**: as core on a 1.4 device, or as `VK_KHR_push_descriptor`, `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_maintenance5` and `VK_KHR_maintenance6` with their feature bits on a 1.3 device. Anything below 1.3, or a 1.3 device missing any of the four, is rejected as today, and the rejection names what was missing. A 1.4 device is not preferred over a suitable 1.3 one.
- **The features stay required, so there is one code path.** On a 1.3 device `VulkanDevice` enables the four extensions and chains their feature structures (`VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR`, `VkPhysicalDeviceMaintenance5FeaturesKHR`, `VkPhysicalDeviceMaintenance6FeaturesKHR`; push descriptors have no feature structure) in place of `VkPhysicalDeviceVulkan14Features`. Recording is the same on both: `vkCmdPushDescriptorSet` resolves to whichever entry point the device has.
- **Versions follow the device.** VMA's `vulkanApiVersion` is the device's version capped at 1.4, not a constant, and `DeviceInfo` reports the version and whether the 1.4 features came as extensions. The instance keeps asking for 1.4.
- **The rule for later code**: nothing may use Vulkan 1.4 beyond those four features without an ADR that either retires 1.3 devices or names the extension that brings the new feature to them. A 1.4-only feature used by accident fails on Lavapipe in CI, below, rather than on a phone.
- **CI takes the 1.3 form too.** `DeviceDesc::apiVersionCap` makes the selector treat a device as the version given, for the tests alone, the way `disableDrawIndirectCount` once did. Lavapipe exposes all four extensions, so `rhi_tests` creates a device at a cap of 1.3 and draws with push descriptors, and the validation layer, which fails every test on any message, catches a 1.4 command or structure used without its extension.
- ADR-0001 stays accepted, and its status line gains "amended by ADR-0019" when this is accepted. Its decision reads: Vulkan 1.4, or Vulkan 1.3 with the engine's 1.4 features as extensions. Everything else it settles (one backend, no OpenGL, no Metal, no fallbacks) is unchanged.

## Consequences

- The Galaxy S25 Ultra can run the engine, and so can any other device whose 1.3 driver carries these four extensions. How many such phones there are has not been surveyed. ADR-0001's device floor becomes a set of features rather than a version number.
- There is still one rendering path. The difference between the two forms is confined to device creation and VMA's configuration, a few dozen lines in `VulkanDevice.cpp`.
- The tests grow by a device configuration, not by a second implementation: Lavapipe runs the `rhi` suite once at a cap of 1.3.
- [rendering.md](../rendering.md#vulkan-baseline) says 1.3 with the four extensions is accepted, and its Android note stops relying on the launch-with-Android-16 guarantee. [roadmap.md](../roadmap.md#m9-mobile-export)'s criterion, "an Android 16 device", stays as it is and is met by this phone.
- A driver update that brings the S25 Ultra to 1.4 changes nothing, since the core form is then taken.
- The next 1.4 feature the engine wants, host image copy for example, which this phone lacks even as an extension, has to be optional or come with the decision the rule above asks for.

## Alternatives considered

- **Keep ADR-0001 and use a phone that launched with Android 16**: no engine change. The device the project has cannot be used, although it has every feature the engine uses. The guarantee ADR-0001 relied on is a statement about version numbers, and the engine depends on features.
- **Wait for a 1.4 driver for the S25 Ultra**: no engine change, and no date. M9's Android checks would block on Samsung's update schedule.
- **Make the four features optional**: this is the 1.3 baseline ADR-0001 rejected, with feature queries and a path without push descriptors. Nothing here needs it, since the device has them all.
- **Require the extensions on every device, 1.4 included**: one form of request instead of two. A 1.4 driver is not obliged to advertise extensions it promoted, and MoltenVK's list is unverified, so it could reject 1.4 devices ADR-0001 accepts.
- **Report the device's version as 1.4 through a layer**, such as the Khronos profiles layer: the version would be a pretence the validation layer could not police, and the app would ship a layer.
