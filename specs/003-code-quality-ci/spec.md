# Feature Specification: Code Quality Enforcement (Formatter + SAST)

**Feature Branch**: `003-code-quality-ci`  
**Created**: 2026-05-01  
**Status**: Draft  
**Input**: User description: "As a project maintainer I want to add a code formatting tool and a SAST tool in order to enforce good future-proof project standards. I want the CI to run the format checker and SAST before anything else, even the tests. Maintainers must be able to run those tools locally and independently. Pull requests that does not satisfy a minimum code quality threshold must be rejected immediately. Prefer free and open-source tools."

## Clarifications

### Session 2026-05-01

- Q: When the format check or SAST job fails on a PR, how should violations be surfaced to the contributor? → A: Inline check annotations on the diff (line-level comments in the PR Files view)
- Q: What is the minimum SAST finding severity that should block a PR? → A: Warnings and above block the PR; informational/note-level findings are surfaced as non-blocking pipeline warnings so maintainers retain full visibility
- Q: Should the CI format check and SAST scan cover the entire codebase or only changed files? → A: Entire codebase on every run
- Q: Should CI auto-fix formatting and commit back, or report only? → A: Report only; contributor fixes locally using the documented fix command
- Q: Should an infrastructure failure (runner crash, network timeout) block the PR? → A: No; infrastructure failures do not block the PR and must be re-triggered manually to obtain a valid result

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Automated Code Quality Gate on PR (Priority: P1)

A contributor opens a pull request. Before any tests run, the CI automatically checks that the code is properly formatted and passes static analysis. PRs that fail either check are blocked from merging immediately, with clear feedback identifying what needs to be fixed.

**Why this priority**: This is the primary enforcement mechanism. Without it, poorly formatted or statically unsafe code can enter the codebase undetected, eroding long-term maintainability.

**Independent Test**: Can be tested by opening a PR with deliberately misformatted code or a known static analysis violation and verifying the merge button is blocked with an actionable failure message.

**Acceptance Scenarios**:

1. **Given** a contributor marks a PR as ready for review, **When** the PR targets the main branch, **Then** the format check and SAST run automatically and complete before any test jobs start.
2. **Given** any formatting violation is detected, **When** the format check completes, **Then** the PR merge is blocked and violations appear as inline annotations on the affected diff lines in the PR Files view.
3. **Given** any static analysis finding above the minimum threshold is detected, **When** the SAST run completes, **Then** the PR merge is blocked and findings appear as inline annotations on the affected diff lines in the PR Files view.
4. **Given** both checks pass, **When** format check and SAST complete, **Then** downstream test jobs are allowed to proceed and the quality gate status is marked passing on the PR.

---

### User Story 2 - Local Format Check Before Committing (Priority: P2)

A maintainer wants to verify their code formatting locally before pushing to avoid a CI failure. They can run the format checker on demand for the whole project or a subset of files, and the tool reports all violations without modifying files.

**Why this priority**: Enabling local execution reduces the feedback loop — maintainers can fix issues before they ever reach CI, saving time and avoiding unnecessary CI cycles.

**Independent Test**: Can be tested by running the format check command locally on a file with known formatting violations and confirming a non-zero exit code and a human-readable report are produced.

**Acceptance Scenarios**:

1. **Given** a maintainer is working locally, **When** they run the format check command, **Then** the tool scans all relevant source files and reports any violations without modifying files.
2. **Given** the maintainer's code is correctly formatted, **When** they run the format check command, **Then** the tool exits with success and no violations are reported.
3. **Given** a maintainer wants to auto-fix formatting, **When** they run the format fix command, **Then** the tool rewrites all non-conforming files in-place to match the project's style rules.

---

### User Story 3 - Local SAST Run Before Committing (Priority: P3)

A maintainer wants to run static analysis locally on their changes before pushing to get early feedback on potential bugs or unsafe code patterns. They can invoke the SAST tool independently and review findings in the terminal.

**Why this priority**: Early local feedback on static analysis catches issues without consuming CI resources and helps maintainers learn project conventions proactively.

**Independent Test**: Can be tested by running the SAST command locally against a file with a known static analysis issue and confirming the tool reports the finding with location information.

**Acceptance Scenarios**:

1. **Given** a maintainer is working locally, **When** they run the static analysis command, **Then** the tool analyses all relevant source files and prints findings with file, line, and a human-readable description.
2. **Given** no static analysis findings are present, **When** the maintainer runs the SAST command, **Then** the tool exits successfully with no findings reported.

---

### Edge Cases

- What happens when the formatter or SAST tool is not installed locally? The tool should fail fast with a clear message instructing the maintainer how to install it.
- What happens when a PR modifies only non-source files (e.g., documentation, CI configs)? The quality gate still runs but may report no relevant files to check, exiting successfully.
- What happens if the SAST tool produces warnings for code in third-party vendor directories? Vendor and generated code directories are excluded from analysis scope.
- What happens when a new static analysis rule causes previously-passing PRs to fail? The minimum threshold is set at project configuration time; rule additions are a deliberate maintainer action.
- What happens if a CI quality gate job fails due to infrastructure (runner crash, network timeout)? The failure does not block the PR; the job must be re-triggered manually to obtain a valid result.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The CI pipeline MUST run the format check and SAST jobs as the first stage, before any build or test jobs begin, scanning the entire codebase on every run.
- **FR-002**: The CI pipeline MUST block PR merging if the format check job fails; the CI job MUST NOT modify or commit back to the branch (report only).
- **FR-003**: The CI pipeline MUST block PR merging if the SAST job reports any finding at warning severity or above.
- **FR-003a**: The CI pipeline MUST surface informational/note-level SAST findings as non-blocking pipeline warnings so maintainers retain full visibility without being blocked.
- **FR-004**: Maintainers MUST be able to run the format checker locally with a single documented command without CI infrastructure.
- **FR-005**: Maintainers MUST be able to run the SAST tool locally with a single documented command without CI infrastructure.
- **FR-006**: The format checker MUST support an auto-fix mode that rewrites source files in-place to conform to the project style.
- **FR-007**: Both the format checker and SAST tool MUST be free and open-source software.
- **FR-008**: The SAST tool MUST exclude third-party vendor and generated code directories from its analysis scope.
- **FR-009**: Failure reports from both tools MUST be surfaced as inline annotations on the affected diff lines in the PR Files view, including at minimum: file path, line number, and a human-readable description of the violation.
- **FR-010**: The CI quality gate jobs MUST complete within a time consistent with the overall CI time budget (30-minute ceiling established in spec 002).

### Key Entities

- **Format Rule Set**: The project-level style configuration that defines what "correctly formatted" means; versioned alongside source code.
- **SAST Configuration**: The project-level static analysis rule set and severity threshold; versioned alongside source code.
- **Quality Gate**: The CI check that aggregates format and SAST results and enforces pass/fail status on PRs.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of pull requests targeting the main branch are checked for formatting and static analysis violations before any test job runs.
- **SC-002**: A PR with at least one formatting violation or one SAST finding at or above the threshold cannot be merged; the merge button remains blocked until the issues are resolved.
- **SC-003**: A maintainer can execute the format check and SAST independently on their local machine using a single command each, with no additional setup beyond documented tool installation.
- **SC-004**: The combined format check and SAST CI stage adds no more than 5 minutes to the total CI wall-clock time for a typical PR.
- **SC-005**: Failure output from both tools is actionable: a maintainer reading the CI log can identify and locate every violation without running the tool locally.

## Assumptions

- The project is a compiled, systems-level codebase; formatting conventions and static analysis rules appropriate for that domain will be applied.
- Vendor and generated code directories (e.g., `vendor/`, generated header directories) are excluded from both formatting and SAST scope.
- The project already has a CI pipeline (spec 002); this feature adds new jobs to that pipeline rather than replacing it.
- The format rule set and SAST configuration will be committed to the repository so all contributors use identical settings.
- Local tool installation is the maintainer's responsibility; the spec does not require automatic developer environment provisioning (e.g., no mandatory pre-commit hooks or container setup).
- The minimum SAST severity threshold is "warning" level: findings at warning or above block the PR; informational/note-level findings are shown as non-blocking warnings.
- The same tools used in CI are the tools used locally — there is no separate "local-only" vs "CI-only" configuration.
