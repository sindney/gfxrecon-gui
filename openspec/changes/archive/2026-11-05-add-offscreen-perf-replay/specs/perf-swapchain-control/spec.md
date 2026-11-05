## ADDED Requirements

### Requirement: Explicit performance swapchain selection

`gfxrecon-cli perf` SHALL accept `--swapchain virtual|captured|offscreen`, defaulting to `virtual`, and pass the requested mode through the shared local or Android replay path. Unsupported values SHALL fail before replay. Offscreen SHALL select the backend implementation that avoids real swapchain, surface and window creation; it SHALL NOT imply removal of all GPU waits or CPU submission gaps.

#### Scenario: Offscreen performance replay
- **WHEN** `perf --gpu-timing isolated-frame --swapchain offscreen` runs on a matching backend
- **THEN** every variant is replayed with the offscreen implementation and the original isolated-frame timing semantics remain explicit

#### Scenario: Invalid mode
- **WHEN** an unsupported swapchain value is provided
- **THEN** the CLI reports a usage error before importing or launching replay

### Requirement: Effective swapchain provenance

Performance reports SHALL distinguish requested mode from backend-observed effective mode. GPU mode SHALL require evidence from the selected swapchain implementation and reject missing, unsupported or mismatched evidence. Explicit non-default CPU measurements SHALL also reject missing or mismatched effective evidence. Older default CPU workflows SHALL report effective mode as unavailable when the backend supplies no evidence.

#### Scenario: Old GPU backend
- **WHEN** an older backend ignores the new mode-evidence request and supplies a GPU report without effective swapchain evidence
- **THEN** the GPU request fails clearly rather than claiming that offscreen was effective

#### Scenario: Backend mismatch
- **WHEN** offscreen is requested but the backend confirms a virtual swapchain
- **THEN** the measurement fails and no comparison delta is produced

### Requirement: Swapchain identity and comparable measurements

Requested mode SHALL participate in pipeline and result/artifact cache identities before their hashes are computed. GPU provenance SHALL retain requested/effective modes. Performance comparison SHALL reject or suppress deltas when swapchain modes differ or required effective evidence is unavailable.

#### Scenario: Cache segregation
- **WHEN** the same capture, backend, target and overrides are measured in virtual and offscreen modes
- **THEN** the requests have distinct pipeline and result identities

#### Scenario: Incompatible comparison
- **WHEN** baseline and modified measurements have differing effective swapchain modes
- **THEN** no numeric improvement delta is reported for that comparison
