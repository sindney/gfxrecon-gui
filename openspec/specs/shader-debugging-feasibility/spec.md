# shader-debugging-feasibility Specification

## Purpose
TBD - created by archiving change gfxreconstruct-inspector. Update Purpose after archive.
## Requirements
### Requirement: Bounded invocation-debugging investigation
After shader edit/replay works, the project SHALL complete an investigation bounded to two engineering days for invocation-level shader debugging. It SHALL evaluate a concrete known fragment or compute invocation, source/debug metadata, input/resource reconstruction, backend hooks, instrumentation or interpretation, and unsupported semantics.

#### Scenario: Candidate approach is feasible within the bound
- **WHEN** an experimental path can report a known invocation value with available replay hooks
- **THEN** a minimal prototype is run against a known shader and its value is checked against expected output with method, device, tool versions, and limits recorded

#### Scenario: Required backend work exceeds the bound
- **WHEN** faithful invocation state or execution requires substantial new backend work
- **THEN** the investigation documents the missing capability and a concrete follow-up scope without marking shader stepping implemented

### Requirement: Evidence-based debugging scope decision
The project SHALL produce a go/no-go report and a separate implementation proposal if production debugging is viable. The report SHALL address missing source mappings, derivatives/subgroups, memory/atomics, and unsupported instructions. The application SHALL expose operational breakpoint/step/watch controls only when a validated backend supports them.

#### Scenario: No production debugger exists
- **WHEN** the investigation completes without a validated production debugging backend
- **THEN** shader compilation diagnostics, resource inspection, edit/replay, and comparison remain available while invocation stepping is identified as unavailable

#### Scenario: Prototype has limited instruction support
- **WHEN** a prototype succeeds only for a restricted shader subset
- **THEN** its report names that subset and limitations and does not claim arbitrary shader source-level debugging
