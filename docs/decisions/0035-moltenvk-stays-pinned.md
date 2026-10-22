# ADR-0035: MoltenVK is deprecated upstream and stays pinned

- **Status:** Accepted
- **Date:** 2026-10-22

## Context

Khronos deprecated MoltenVK ([commit 0aa76ad](https://github.com/KhronosGroup/MoltenVK/commit/0aa76ad7800f3c470103e2004d9a3dc0401693ee)). The repository is now maintained by the community on a best-effort basis, with no end-of-life date and no archive. Khronos recommends KosmicKrisp on Apple Silicon Macs and keeps MoltenVK as the recommended option for Intel Macs and for iOS.

The player links MoltenVK 1.4.2 statically from the `moltenvk` port ([ADR-0018](0018-mobile-export.md#builds)), and [ADR-0001](0001-vulkan-1.4-only.md) rules out a native Metal backend. The MoltenVK quirks were already paid for in [ADR-0014](0014-indirect-draws-without-count.md) to [ADR-0017](0017-depth-images-in-their-own-bindless-array.md).

## Decision

The project stays pinned to MoltenVK 1.4.2 on macOS and iOS. It does not drop either platform, and it does not write a Metal backend now.

## Consequences

- Nothing breaks until the pin is bumped, and iOS has no alternative driver.
- A fix that the project waits on in MoltenVK may never be released. [#59](https://github.com/Pacheco95/sonnet/issues/59) is such a case.
- Whether macOS should ship KosmicKrisp instead of the static MoltenVK link is a separate trial, [#368](https://github.com/Pacheco95/sonnet/issues/368). Its result may supersede this ADR for macOS; iOS stays on MoltenVK either way.
- This decision is revisited when:
  - the upstream repository is archived;
  - a new iOS or Xcode release breaks the pinned MoltenVK and nobody fixes it and it cannot be patched cheaply;
  - an Apple-specific need is beyond what MoltenVK can meet, or Apple becomes a primary market. That needs a new ADR superseding the "no Metal backend" line of ADR-0001, iOS first, after measuring MoltenVK's overhead on the iPad.

## Alternatives considered

- **Replace MoltenVK with KosmicKrisp on macOS now**: it is the recommended driver for Apple Silicon, but it has not been run against the macOS validation checks or the ADR-0017 workarounds, and it does not cover iOS. That is the trial in #368.
- **A native Metal backend**: the one way off MoltenVK on iOS, and a second renderer to maintain. ADR-0001 rules it out until an Apple-specific need justifies it.
- **Drop macOS and iOS**: loses two of the five target platforms for a risk that has not materialised.
