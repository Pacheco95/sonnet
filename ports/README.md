# Overlay ports

Patched copies of vcpkg ports, picked up through `vcpkg-configuration.json` ahead of the registry ([ADR-0004](../docs/decisions/0004-vcpkg-first.md)). Each directory says what was changed and why, so the patch can be dropped once upstream carries it.

| Port | Change |
|---|---|
| `imgui` | Two changes, copied from the registry at baseline `a1cae005` and bumped to port-version 2. The `sdl3-binding`, `sdl3-renderer-binding` and `sdlgpu3-binding` features depend on `sdl3` with its default features off: the registry port depends on `sdl3` with defaults on, which on Linux re-enables `ibus`, and through it `dbus[systemd]` and `libsystemd`, for every consumer, even one that disabled them at the top level. The `vulkan-binding` feature depends on `vulkan-headers` only and compiles the backend with `IMGUI_IMPL_VULKAN_NO_PROTOTYPES`: the registry port links the `vulkan-loader` port, which put a second loader into the process ahead of the one SDL loads, and vcpkg's loader is built without X11 or Wayland surface support, so the editor could not open a window outside an environment that put the SDK's loader first ([ADR-0006](../docs/decisions/0006-vulkan-object-ownership.md)). |
