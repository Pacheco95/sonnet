# ADR-0002: SDL3 for windowing, input and the main loop

- **Status:** Accepted
- **Date:** 2026-09-12

## Context

The first iteration used SDL3, the second GLFW. GLFW is small and pleasant on desktop but has no Android or iOS support, no gamepad rumble or touch model, and no audio. The target platform list includes Android and iOS, where the operating system owns the main loop and the application receives callbacks.

## Decision

SDL3 is the platform implementation behind `platform::IWindow`, the input event types and the application callback interface. The engine uses SDL3's callback main-loop model (`SDL_MAIN_USE_CALLBACKS`) on every platform, so desktop and mobile share one lifecycle. SDL3 also provides the Vulkan surface, the Vulkan loader entry point, gamepad and touch input, file-system base paths, and is the candidate audio device layer for M5.

## Consequences

- One lifecycle model on all five platforms; the mobile milestone is packaging work, not a restructuring.
- Dear ImGui uses its SDL3 backend, available through the vcpkg `imgui[sdl3-binding]` feature.
- The engine never calls SDL outside the `platform` module (and the `ui` module's ImGui backend), so the interface stays honest.
- SDL3 is larger than GLFW and brings subsystems the engine does not use; they are disabled at init.

## Alternatives considered

- **GLFW**: desktop-only; would require a second platform implementation for mobile from the start.
- **Native per-platform code**: maximum control, maximum maintenance; not justified for a personal engine.
