## ADDED Requirements

### Requirement: Explicit shared memory policy

Replay requests SHALL support default, none, remap, realign and rebind. Default SHALL preserve local macOS rebind and Windows/Android none. Explicit policies SHALL be validated before launch, forwarded exactly once and never silently retried as another mode.

#### Scenario: Cross-device remap
- **WHEN** an Android frame or draw replay requests remap
- **THEN** native replay receives `--memory-translation remap` through the shared core

#### Scenario: Existing platform behavior
- **WHEN** callers omit the new option
- **THEN** the established platform policy remains unchanged and explicit none still overrides it

### Requirement: Consistent frontend and job propagation

CLI replay/perf/dynamic dump requests and persisted GUI Replay settings SHALL use the shared policy. Frame, resource, diagnostic, shader comparison/reset and perf jobs SHALL snapshot the selected value. Changing it SHALL invalidate derived GUI views and cached profiling support without automatically replaying.

#### Scenario: Shader baseline comparison
- **WHEN** the GUI applies a shader while remap is selected
- **THEN** baseline, modified and auxiliary replay requests use the same memory policy

### Requirement: Honest provenance and compatibility

Results SHALL record requested/resolved backend-argument policy without claiming unavailable native confirmation. Replay/pipeline caches, dynamic-dump resume identities and performance compatibility SHALL distinguish memory policies.

#### Scenario: Existing none-mode artifact
- **WHEN** a matching frame is requested with remap after a none-mode replay
- **THEN** the none artifact is not reused as a remap result

### Requirement: Attributable native crash diagnosis

Unexpected Android exits SHALL retain bounded native crash logs and surface a matching abort reason when package, PID and timestamp correspond to the latest native exit. Unrelated or stale crashes SHALL not replace the current exit diagnosis. Missing crash evidence SHALL retain a safe fallback.

#### Scenario: Allocation fails during restored state
- **WHEN** the owned replay's native abort identifies vkAllocateMemory and invalid opaque capture address
- **THEN** the frontend reports that API/result and a portability hint, preserving exit-info and crash evidence

#### Scenario: Previous crash belongs to an old session
- **WHEN** the shared crash buffer contains another package, PID or mismatched timestamp
- **THEN** it is not attributed to the current replay
