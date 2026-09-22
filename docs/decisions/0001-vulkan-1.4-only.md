# ADR-0001: Vulkan 1.4 is the only graphics backend and a hard minimum

- **Status:** Accepted
- **Date:** 2026-09-12

## Context

The previous iteration kept OpenGL 4.6 and Vulkan 1.3 behind one interface. Backend parity was the largest source of regressions, every renderer feature had to be written twice, and the interface was pulled toward the lowest common denominator, which blocked bindless resources and dynamic rendering. Vulkan 1.4 (December 2024) makes push descriptors, dynamic rendering local read and maintenance 5 and 6 core, and raises minimum limits, so a 1.4 baseline removes most feature queries and fallback paths.

Availability: current desktop drivers and Mesa ship 1.4. MoltenVK 1.4 (August 2025) brings 1.4 to macOS 12+ and iOS 15+ as a portability implementation. On Android, devices launching with Android 16 or later must support 1.4; devices that launched with Android 14 or 15 only guarantee the Android Baseline 2021 profile.

## Decision

Sonnet has exactly one graphics implementation, Vulkan, and requires Vulkan 1.4 core. The device selector rejects lower versions. There is no OpenGL, Direct3D or native Metal backend, and no plan to add one. The features and limits the engine relies on are listed in [rendering.md](../rendering.md#vulkan-baseline).

## Consequences

- One rendering code path, shaped by explicit synchronization, dynamic rendering and bindless resources; the render hardware interface can be thin.
- Android support is limited to devices that launched with Android 16 or later. This is accepted: the mobile milestone is last, and by then that device class is the target market for a new game.
- Apple platforms depend on MoltenVK 1.4+ and its portability-subset rules: the instance enables portability enumeration, the device enables the portability subset, and the engine avoids geometry shaders, triangle fans and other unsupported features. macOS 12 and iOS 15 become the minimum OS versions.
- Users with old desktop drivers get a clear error at startup instead of degraded rendering.
- If a native Metal backend is ever wanted, Slang can already emit Metal, and the `rhi` interface is explicit enough to admit it.

## Alternatives considered

- **Vulkan 1.3 baseline with optional 1.4 features**: keeps Android 14 and 15 devices, at the cost of feature queries and two code paths for push descriptors and local reads. Rejected for simplicity; the wider device reach was judged not worth the permanent complexity.
- **Vulkan Profiles as the baseline definition**: precise, but adds tooling for a single-implementation engine. Can be adopted later for device-selection checks without changing this decision.
- **Keeping OpenGL as a fallback**: rejected for the parity cost described above.
- **SDL3 GPU API**: portable across Vulkan, Direct3D 12 and Metal, but its abstraction hides the bindless and explicit-synchronization features this engine is built around.
