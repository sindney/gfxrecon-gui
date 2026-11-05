## MODIFIED Requirements

### Requirement: Replay performance metrics

`gfxrecon-cli perf` SHALL run a configurable number of fresh replays of a frame range on a target and report state-load time, measured replay frame time and FPS per run with min, median and max, together with cache reuse and screen state. With overrides, it SHALL also measure the baseline and report deltas. Existing CPU-derived fields SHALL retain their replay-elapsed meaning. Opt-in GPU timing SHALL report individual full-frame GPU samples, support, scope and validity separately. Only valid GPU samples with matching frame scope, mode, device and explicit cache policy SHALL participate in GPU deltas. Baseline and modified variants SHALL retain alternating order. The documented manual sampling workflow SHALL use `--runs 1`; this change SHALL add no warm-up or repetition automation and SHALL NOT silently discard early samples or claim steady state.

#### Scenario: Override performance comparison
- **WHEN** `perf --capture C --frames 2 --runs 5 --override M=X.spv` runs
- **THEN** the result contains 5 baseline and 5 modified replay measurements with median replay frame-time delta, and result caches were not used

#### Scenario: Manual GPU comparison pair
- **WHEN** the user requests supported full-frame GPU timing with `--runs 1` and a shader override
- **THEN** one fresh baseline followed by one fresh modified replay runs, every selected frame GPU sample is retained, and any GPU delta is separate from replay elapsed metrics

#### Scenario: Manual follow-up sample
- **WHEN** the user reruns a GPU measurement command with a new output directory
- **THEN** it produces a fresh independently identified measurement pair without restoring result artifacts, performing hidden frame loops, or silently warming up

#### Scenario: Incomparable GPU samples
- **WHEN** baseline and modified GPU samples have invalid endpoints or differing measured scopes, devices, modes or explicit cache policies
- **THEN** the CLI reports the discrepancy and does not publish a numeric GPU improvement from those samples

### Requirement: Replay provenance

Every replay result SHALL include the replay manifest path with capture hash, backend hash, target and device identity, overrides with hashes, request options and artifact hashes, as produced by the shared replay core. GPU measurement provenance SHALL additionally identify the effective replay GPU, Vulkan API and driver properties, device OS/build, effective capture/optimization settings, instrumentation and completion mode, and controlled cache policy and observed pipeline-cache reuse/identity. Uncontrolled driver and hardware cache state SHALL be identified as unknown in GPU reports. For GPU measurement/comparison requests, every requested override SHALL have structured successful application evidence matching its effective bytes; replacement-file discovery alone SHALL NOT establish successful application. Whether a replacement executes within measured scope SHALL be distinguished from successful replacement creation. Ordinary and CPU-only replay SHALL retain their existing backend compatibility and replacement checks.

#### Scenario: Reproduce an agent's result
- **WHEN** a user inspects a replay result directory produced by the CLI
- **THEN** its manifest identifies the exact capture, backend, device and override files used, and the GUI can open the same capture and replay the same selection

#### Scenario: Actual replay driver differs from system probe
- **WHEN** Android selects a different driver for the replay process than a standalone system Vulkan probe
- **THEN** GPU measurement provenance identifies the actual replay driver and does not substitute the standalone probe's driver properties

#### Scenario: Requested replacement not successfully applied
- **WHEN** any shader replacement requested for a GPU comparison lacks successful backend application evidence matching its hash
- **THEN** the performance comparison fails and identifies the unconfirmed module rather than reporting an optimization result

#### Scenario: No explicit pipeline cache loaded
- **WHEN** the replay loads no target pipeline-cache file
- **THEN** provenance records that fact and does not imply that driver or hardware caches were cold
