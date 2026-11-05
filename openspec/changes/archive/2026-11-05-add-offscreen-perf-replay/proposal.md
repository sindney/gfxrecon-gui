## Why

Real swapchain acquisition can occur between a frame's GPU timestamp endpoints, allowing display pacing to influence an isolated replay measurement. Exposing the existing offscreen backend mode gives users an explicit way to remove that source of waiting while preserving reproducible comparison metadata.

## What Changes

- Add `perf --swapchain virtual|captured|offscreen`, retaining the backend's current `virtual` default.
- Pass the requested mode to local and Android replay, and record the effective backend swapchain implementation rather than inferring success solely from arguments.
- Include requested mode in pipeline/result cache identities, and requested/effective modes in provenance and comparison checks.
- Fail explicit offscreen measurement if the backend does not confirm the effective mode; never label a display-backed replay offscreen.
- Preserve manual runs, existing GPU timestamp semantics, and CPU-only replay compatibility. Add no counters, frame looping, or automatic warm-up.

## Capabilities

### New Capabilities

- `perf-swapchain-control`: Explicit swapchain mode selection, effective-mode evidence, cache segregation, and compatible performance comparisons.

### Modified Capabilities

None. This adds requirements alongside the existing replay and GPU profiling contracts.

## Impact

- CLI performance options/results, shared replay request and manifests, both backend launch paths, and GPU report validation.
- Repository-owned pinned-backend CMake hook to report the swapchain class actually selected.
- Focused tests and usage documentation. Desktop/Android runtime verification follows a rebuild of the bundled replay backend/APK.
- No commit, push, or archive. Sustained replay, counters, and GUI planning remain a separate change.
