# Overlay ports

Patched copies of vcpkg ports, picked up through `vcpkg-configuration.json` ahead of the registry ([ADR-0004](../docs/decisions/0004-vcpkg-first.md)). Each directory says what was changed and why, so the patch can be dropped once upstream carries it.

| Port | Change |
|---|---|
| `imgui` | The `sdl3-binding`, `sdl3-renderer-binding` and `sdlgpu3-binding` features depend on `sdl3` with its default features off. The registry port depends on `sdl3` with defaults on, which on Linux re-enables `ibus`, and through it `dbus[systemd]` and `libsystemd`, for every consumer, even one that disabled them at the top level. Copied from the registry at baseline `a1cae005` and bumped to port-version 1. |
