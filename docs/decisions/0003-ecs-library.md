# ADR-0003: flecs as the entity component system

- **Status:** Accepted (EnTT remains the documented alternative)
- **Date:** 2026-09-12

## Context

The previous iteration used a `Scene` of `GameObject`s with `std::optional` components and a hand-written transform hierarchy. It worked for a demo but the editor kept needing infrastructure that had to be written by hand: generic inspection of component fields, serialization, prefabs, nested scene instances, and stable ordering of updates. The editor-first goal makes those the core of the engine rather than extras. An ECS with data-oriented storage also fits the renderer's index-based, bindless design: draw lists are built by iterating contiguous component arrays.

Two mature options exist. **flecs** is a C core with a C++ API and ships hierarchy (`ChildOf`), prefabs (`IsA`), a reflection system, JSON serialization, a REST-based explorer for live debugging, and a system scheduler with pipeline phases and multithreading. **EnTT** is header-only C++, minimal and fast, with `entt::meta` for reflection and `entt::snapshot` for serialization; hierarchy, prefabs and scheduling are left to the user.

## Decision

`world` is built on flecs. Components are plain structs registered with flecs reflection. Hierarchy uses `ChildOf`, prefabs and nested scene instances use `IsA`, systems are flecs systems in named pipeline phases, and scene serialization uses flecs JSON with an engine-owned versioning envelope. The flecs explorer is enabled in Debug builds of the editor.

## Consequences

- Hierarchy, prefabs, reflection, serialization and scheduling do not have to be written; the editor inspector and the scripting bindings enumerate component fields through one reflection system.
- Nested scene instances become prefab instantiation with overrides, which was a milestone-2 feature of the previous plan.
- flecs types appear in `world`'s public headers; the engine does not wrap them. Switching ECS later would be a rewrite of `world` and its consumers, which is accepted.
- The flecs API is large and its query language has a learning curve; the team is one person, so the cost is bounded by documentation reading, not coordination.

## Alternatives considered

- **EnTT**: lighter and header-only, excellent runtime performance, but hierarchy, prefabs, scheduling and JSON would be engine code. Chosen against because those are exactly the features the editor needs first. If flecs proves too heavy in M2, EnTT is the fallback and this ADR is superseded.
- **Hand-written GameObject model again**: the known cost from the previous iteration; rejected.
