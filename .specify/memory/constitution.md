<!--
## Sync Impact Report

- **Version change**: Template (unversioned) → 1.0.0
- **Added principles**:
  - I. Modularity (new)
  - II. Usability (new)
  - III. Stability (new)
  - IV. Predictability (new)
  - V. Testability (new)
  - VI. Portability (new)
  - VII. Internationalization (new)
- **Added sections**: Quality Standards, Development Workflow
- **Removed sections**: None
- **Templates reviewed**:
  - `.specify/templates/plan-template.md`: Constitution Check gate is derived at plan time — no changes required ✅
  - `.specify/templates/spec-template.md`: Requirement and acceptance scenario structure aligns with principles — no changes required ✅
  - `.specify/templates/tasks-template.md`: Phase structure and task types are compatible with all 7 principles — no changes required ✅
  - `.specify/templates/commands/`: Directory not found — skipped ✅
  - `README.md`: Not present — skipped ✅
- **Deferred TODOs**: None
-->

# Sonnet Constitution

## Core Principles

### I. Modularity

Components MUST be designed as independently deployable and replaceable units with clearly defined
boundaries. Each module MUST expose a stable public interface and MUST NOT leak internal
implementation details. A module MUST be understandable, buildable, and testable in isolation from
the rest of the system without requiring knowledge of other modules' internals.

**Rationale**: Tight coupling amplifies the blast radius of changes and makes independent delivery
impossible. Modular boundaries enforce separation of concerns and enable parallel development.

### II. Usability

Every user-facing interface MUST be discoverable without consulting documentation for common tasks.
Error messages MUST identify what went wrong and MUST describe how to recover. APIs MUST follow
established conventions of their ecosystem and MUST be consistent in naming, behavior, and return
types across the codebase. Defaults MUST represent the most useful behavior for the majority of
callers.

**Rationale**: Systems that require deep expertise to use correctly produce errors in production,
slow onboarding, and reduce adoption. Usability is a first-class functional requirement, not a
polish pass.

### III. Stability

Public interfaces MUST NOT change in a breaking way without a version increment and a documented
migration path. Behavior changes MUST be detectable via automated tests before they reach users.
Releases MUST be reproducible: given the same inputs and dependency versions, builds MUST produce
functionally equivalent outputs.

**Rationale**: Unpredictable interfaces erode trust and force downstream consumers to treat every
upgrade as a potential breakage event. Stability enables confident adoption.

### IV. Predictability

System behavior MUST be deterministic given the same inputs and state. Side effects MUST be
explicit and documented at the API boundary. Operations with observable side effects (I/O, network,
state mutation) MUST be clearly separated from pure transformations. There MUST be no hidden global
state that can alter behavior without an explicit caller action.

**Rationale**: Predictable systems are easier to reason about, test, and debug. Hidden state and
implicit side effects are the primary causes of non-reproducible failures and production surprises.

### V. Testability

Every public interface MUST be testable without requiring external infrastructure (network,
filesystem, database) unless explicitly designated as an integration test. Unit tests MUST cover
all documented happy paths and error paths. Coverage gaps MUST be explicitly justified in code
review. Test suites MUST be runnable locally by any contributor without additional configuration
beyond project setup.

**Rationale**: Untestable code is unverifiable code. If a component cannot be tested in isolation,
it signals excessive coupling or hidden dependencies that violate the Modularity and Predictability
principles.

### VI. Portability

The system MUST run without modification on all officially supported platforms (documented in the
project README). Platform-specific code MUST be isolated in clearly named adapter layers and MUST
NOT bleed into business logic. Dependencies MUST be explicitly pinned and available on all target
platforms. Build and test scripts MUST function on macOS, Linux, and Windows unless a platform is
explicitly excluded with written justification.

**Rationale**: Portability constraints discovered late in development are expensive to fix.
Enforcing platform-neutral abstractions from the start prevents platform lock-in and widens the
contributor base.

### VII. Internationalization

All user-visible strings MUST be externalized into locale resource files and MUST NOT be hardcoded
in logic. Date, time, number, and currency formatting MUST use locale-aware libraries rather than
manual string construction. The system MUST handle multi-byte character encodings (UTF-8 minimum)
correctly at all I/O boundaries. Layout MUST be architecturally capable of supporting right-to-left
scripts even if not initially implemented.

**Rationale**: Retrofitting i18n into a mature codebase is disproportionately expensive. Encoding
i18n constraints from the first commit prevents structural decisions that make localization
impractical later.

## Quality Standards

All code changes MUST pass automated tests (unit + integration) before merging to the main branch.
Performance regressions greater than 10% on documented benchmarks MUST be approved by the project
lead before merging. Security vulnerabilities of severity MEDIUM or above MUST be resolved before
any release. User-facing interfaces MUST meet WCAG 2.1 AA accessibility standards.

## Development Workflow

Feature work MUST proceed through the Specify workflow: specification → clarification → planning →
task generation → implementation. Architectural decisions that affect two or more principles MUST
be documented as Architecture Decision Records (ADRs) in `docs/adr/`. Code review MUST include an
explicit Constitution Check confirming compliance with the Core Principles above. Non-compliant
code MUST NOT be merged without a documented and approved exception recorded in the Complexity
Tracking table of the relevant `plan.md`.

## Governance

This constitution supersedes all prior conventions and informal agreements. Amendments require:
(1) a written proposal documenting the change and rationale, (2) review by at least one
maintainer, and (3) an update to this file with a version increment following semantic versioning
rules (MAJOR: principle removal or redefinition; MINOR: new principle or material expansion;
PATCH: clarification or wording). MAJOR version bumps require a migration plan covering in-flight
features. All pull requests MUST verify compliance with the Core Principles via the Constitution
Check gate in `plan.md`. Any complexity justified as an exception MUST reference the specific
principle violated and explain why the simpler alternative was rejected.

**Version**: 1.0.0 | **Ratified**: 2026-05-01 | **Last Amended**: 2026-05-01
