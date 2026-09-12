# ADR-0005: Slang as the only shading language

- **Status:** Accepted
- **Date:** 2026-09-12

## Context

The previous iteration wrote GLSL, compiled it at runtime with glslang, and used SPIRV-Reflect to recover descriptor layouts. Sharing code between shaders relied on preprocessor includes, per-backend `#ifdef` blocks accumulated, and struct layouts had to be kept in sync with C++ by hand. Slang is a shading language with modules, generics and interfaces, compiles to SPIR-V (and to Metal, HLSL and others), ships a reflection API, and is bundled with the Vulkan SDK and available as the vcpkg port `shader-slang`.

## Decision

All shaders are written in Slang. Engine shaders are compiled to SPIR-V at build time by a CMake custom command running `slangc`, so release builds carry no compiler. The editor links the Slang compiler library to recompile changed shaders at runtime for hot reload. Slang reflection is the only source of descriptor layout information; there is no separate reflection library. A shared `sonnet.slang` module defines the bindless resource declarations and parameter blocks that every shader imports.

## Consequences

- Shader code is modular and type-checked; struct layouts shared with C++ are declared once and checked by reflection at load time.
- One compiler dependency instead of glslang plus SPIRV-Reflect.
- Slang is younger than GLSL; tooling in editors is thinner and some SPIR-V features arrive later. Accepted for a personal engine.
- A native Metal backend, if ever wanted, has its shader story already solved.

## Alternatives considered

- **GLSL with glslang**: known and well supported, but no modules, no generics, hand-maintained layouts.
- **HLSL with DXC to SPIR-V**: mature, but the Vulkan path is a second-class citizen in parts of the toolchain and DXC is heavier to build and ship than Slang.
