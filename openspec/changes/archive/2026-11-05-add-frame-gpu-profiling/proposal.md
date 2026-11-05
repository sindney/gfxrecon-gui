## Why

The current `perf` command reports replay elapsed time divided by frame count and explicitly does not measure GPU execution. A three-frame capture needs genuine, clearly scoped GPU timestamps and enough provenance for users to compare manually collected baseline and modified samples.

## What Changes

- Add opt-in full-frame GPU timestamp measurement to Vulkan replay and the CLI, with explicit frame/queue scope, timestamp support, result validity, completion synchronization, and measurement perturbations.
- Prioritize reliable whole-frame measurements. Pipeline, pass, draw, and submission timing are deferred; they are not required to deliver the first milestone.
- Stop and discuss unsupported timing configurations or any need to change the agreed measurement semantics. Never fall back to CPU elapsed time while claiming GPU duration.
- Support the manual workflow with one fresh replay per variant (`--runs 1`), individual frame results, and existing alternating baseline/modified order. Preserve the existing `--runs` behavior for compatibility; add no warm-up, looping, repetition, or stability automation.
- Record the actual replay GPU/driver, capture/backend/override hashes, explicit cache policy and observed reuse, and structured confirmation for every requested shader replacement.
- Add optional, source-labelled thermal and GPU-clock observations where accessible. Missing or denied telemetry does not prevent timestamp profiling.
- Record Adreno hardware-counter discovery and provider feasibility separately. Integrate counters only after a supported provider is demonstrated on the connected phone; no root, driver replacement, proprietary SDK dependency, or system-wide tracing is assumed.

## Capabilities

### New Capabilities

- `frame-gpu-profiling`: Full-frame Vulkan GPU elapsed samples, support/validity reporting, synchronization scope, optional telemetry, and explicit unsupported behavior.

### Modified Capabilities

- `agent-replay`: Extend performance reporting with opt-in GPU samples and stronger measurement provenance while retaining the existing replay metrics and repeat-run contract.

## Impact

- CLI: `src/cli/commands_replay.cpp`, shared replay requests/results, Android artifact transfer, result schema, and usage documentation.
- Backend: bundled desktop replay and Android APK, frame-boundary and Vulkan submission hooks, query lifetime, shader replacement evidence, and capability negotiation.
- Build: repository-owned CMake integration for the pinned GFXReconstruct backend; avoid untracked changes inside the submodule.
- Validation: synchronization/validity checks, output equivalence on representative captures, manual phone measurements, and unsupported-case tests.
- Current device evidence: Adreno 840 advertises timestamps; thermal data is accessible and tested GPU-clock paths are denied. An official temporary AGI helper successfully registers the phone's OEM `gpu.counters` Perfetto producer without root or APK installation. Actual counter sampling and attribution remain separate feasibility gates, not dependencies of GPU timestamps.
- Implementation stays uncommitted and unpushed for user review; no archive or publication is requested. Mali-specific validation is deferred until that device is connected.
