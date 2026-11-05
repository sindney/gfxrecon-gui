## Context

`src/cli/commands_replay.cpp::PerfRun` derives `frame_time_ms` from upstream replay elapsed time. `Perf` already preserves individual fresh runs and orders them baseline, modified, baseline, modified. The shared replay core bypasses result artifacts for measurements, manages target-generated pipeline caches, hashes inputs, and requires a replacement log line for each requested shader module.

The replay event plugin's timestamps come from the CPU. Its optional queue-idle completion tracking does not supply GPU timestamps. `Application::PlaySingleFrame` supplies logical replay frame boundaries, including trimmed-state loading considerations, but its end occurs after frame processing, which can include presentation and host waiting. Timestamp markers inserted only at those CPU boundaries would measure an inappropriate interval.

Implementation follows the user-approved scope below. Validation evidence and known limitations are recorded alongside the implementation; the initial exploration and counter probes are preserved in `research.md`.

## Goals / Non-Goals

**Goals:**

- Deliver trustworthy, explicitly scoped full-frame replay GPU elapsed measurements on the connected Adreno device.
- Preserve per-frame samples and distinguish GPU elapsed time from replay elapsed time.
- Let the user collect additional runs manually, with reproducible provenance and useful optional telemetry.
- Discover Adreno counter access honestly, with a separate integration decision after a real provider probe.

**Non-Goals:**

- Pipeline/pass/draw/submission profiling, automatic warm-up, new repetition controls, in-process loops, steady-state detection, automatic cooldown, or clock locking.
- Claiming that a short replay establishes a small optimization or represents the original game's natural frame latency.
- Root access, security-policy changes, driver replacement, deployment of a counter SDK, or Mali-specific integration in this stage.

## Decisions

### 1. Accepted first timing mode: isolated full frame on one active command queue

The user accepted this scope on 2026-11-02. Drain outstanding device work immediately before the selected frame's measured work, then bracket its first through last GPU submission using owned timestamp command buffers outside render passes. Use owned fence-backed drains of all previously active relevant queues with a shared deadline, or an explicit process watchdog; `vkDeviceWaitIdle` and `vkQueueWaitIdle` have no timeout and the existing `WaitDevicesIdle()` also ignores results. Check every operation and preserve application fences. Do not insert idle waits between submissions. Complete and read back the final timestamp after the measured work, with bounded completion handling.

The value is an isolated replay frame GPU elapsed interval. It can include GPU dependency waits and CPU submission gaps occurring between its timestamp endpoints, and contention from other processes. Captured waits that gate the start marker are excluded. It excludes state restoration, query reset/setup, host completion waits, and presentation/scanout work outside the measured submissions. Isolation changes overlap with adjacent frames and must be an explicit user-selected mode recorded in every sample; it is not natural game frame latency or pure GPU busy time.

A small read-only indexing/preparation pass should identify complete first/last submission boundaries and all active queues before measurement. Do not discover the last submission by waiting until after `vkQueuePresentKHR` or CPU `FrameEnd` to enqueue the end timestamp. Check markers bracket the intended work with Vulkan execution dependencies; merely placing command buffers in an array does not prove the necessary ordering. Preserve original waits, signals, fences, device masks, extension chains, and submit-call behavior for supported `vkQueueSubmit`, `vkQueueSubmit2`, and KHR aliases.

First-milestone support is limited to one active command queue for the selected frame. Extra logical devices created during state restoration do not by themselves violate that scope; bind provenance to the device owning the measured queue and reject actual measured work across devices. A separate presentation queue is acceptable only if no extra command work is omitted and all required dependencies remain accounted for. Protected submits, device groups, external work, sparse operations, suspended render-pass boundaries, missing frame boundaries, and other unhandled operations must be identified as unsupported rather than silently omitted. Do not make a universal-support claim for these cases.

Alternatives: pass timestamps risk changing tile/render-pass behavior; timestamp-only frame spans without isolation preserve more overlap but are harder to attribute; multi-queue spans require a common timestamp domain and carefully bounded endpoints. None are needed for the first milestone. If the user's target capture uses an unsupported layout, stop and discuss extending this design.

### 2. Timestamp support and completion are runtime facts

Query the actual replay physical device and queue-family properties, not capture-time properties or a separate system driver's probe. Record `timestampPeriod`, `timestampValidBits`, stages, raw ticks, and wrap-safe duration conversion. Reset queries before use, retain objects until completion, request 64-bit values and availability, and publish only completed valid pairs.

If the endpoints span separate Vulkan submissions, establish the required timestamp-domain guarantee. The connected phone advertises KHR/EXT calibrated timestamps, but supported does not mean enabled in the replay logical device. Enable a compatible extension where needed and supported, or reject an unproven cross-submission interval. A single-submission path does not require general cross-submission comparability. GPU timestamp ticks are independent of host timer units.

Timeouts, device loss, unavailable results, ambiguous wraparound, missing endpoints, unsupported scope, and malformed/mismatched reports invalidate the sample. Keep diagnostic artifacts, return a specific failure, and produce no GPU speedup claim. No CPU-duration fallback.

### 3. Add a versioned backend report and capability check

Extend `ReplayMeasurement`, local arguments, Android launch/transfer, and result parsing. Use a separate versioned GPU measurement report containing requested scope, observed scope, samples, queue/device identities, validity, synchronization and errors. Bind report identity to capture/backend/request hashes. Advertise support in the bundled backend and verify the installed APK's identity before relying on it. An older backend must fail clearly when GPU measurement is requested; existing CPU measurements remain usable.

Use repository-owned integration under `cmake/` with explicit pinned-source match checks, consistent with existing cache/readback hooks. Avoid modifying the GFXReconstruct submodule checkout. Hook submission execution and frame planning rather than repurposing the existing CPU replay-event timestamps.

### 4. Manual sampling retains existing CLI compatibility

Document `perf ... --runs 1` with the new explicit GPU timing mode. With overrides, retain baseline then modified and publish each frame sample. Users rerun that command with a new output directory to gather additional pairs. Do not add warm-up/repeat options, silently discard initial samples, or label a run steady-state.

Existing `--runs` remains supported with its current alternating order. GPU mode defaults to one run per variant, while CPU-only mode retains its default of three. CPU load/replay/FPS fields retain their original meanings. GPU samples and any comparison use separate fields and identical frame/scope/device/cache policy, capture and backend identities; mismatches suppress comparison. Multiple different frames are not interchangeable replicates.

### 5. Provenance comes from the effective replay

Record actual GPU/device/vendor IDs, Vulkan API, raw driver version, driver properties when available, OS/build and device serial, backend/APK hash, capture and effective capture hashes, optimization settings, requested frame scope, and instrumentation mode.

Distinguish result-cache bypass, captured cache-data exclusion, target pipeline cache loaded/saved, pipeline cache identity/hash, and uncontrolled driver/hardware caches. Do not label all caches cold because no pipeline-cache file was loaded. Baseline and modified caches remain variant-specific.

For GPU measurement requests, replace filename-only replacement evidence with structured per-module requested hash, selected replacement hash, successful API creation/application status, and backend evidence. File discovery is insufficient. Distinguish replacement creation from whether a dependent pipeline actually executes within the measured frame; report unknown execution coverage honestly. Any unconfirmed requested replacement fails the GPU comparison. Existing ordinary/CPU-only replay compatibility and its current replacement checks remain unchanged.

### 6. Optional telemetry is source-labelled and collected outside timed work

Collect bounded before/after host-side ADB observations, with host observation interval and device timestamps where available. Preserve source, access context, units, sensor identity, value, freshness and status (`available`, `permission_denied`, `unsupported`, or `error`). Never fabricate zero values, use battery temperature as GPU temperature, infer clocks from timestamp period, or call before/after snapshots a continuous history.

On this phone, detailed `dumpsys thermalservice` works as ADB shell but is denied under replay `run-as`. The cached GPU sensor block differs greatly from the explicit current-HAL block; use current-HAL readings when obtainable and label cached data explicitly otherwise. Public thermal status is a possible later app-side fallback. Tested GPU clock sysfs paths are inaccessible in both contexts; optional discovery must not elevate privileges.

### 7. Hardware counters remain a separate provider gate

Standard `VK_KHR_performance_query`, AGI's Perfetto OEM producer path, and Qualcomm's Profiler Interface Library must be evaluated separately from Vulkan timestamps. A system-driver extension probe does not establish the effective replay driver's support.

The initial Perfetto query had no `gpu.counters`/`gpu.renderstages` registration, but `/vendor/lib64/libgpudataproducer.so` exists. A capability-only probe with the official AGI v3.3.3 ARM64 helper successfully loaded `libgpudataproducer.so` by name and registered `gpu.counters`, without root, settings changes or APK installation. Its resolved absolute path was not independently verified. It advertises 80 counters, a minimum period of 1 ms and no instrumented sampling. The initial probe collected no samples; a later separate implementation-stage smoke test confirmed numeric system-wide collection. `gpu.renderstages` remained absent. Helpers and newly created PID files were cleaned up, and provider unregistration was verified. See `research.md` for evidence and descriptors.

Registration establishes a promising provider path, not successful sampling or frame attribution. Names/units/rates are advertised metadata requiring validation; permissions, achieved sampling rate, process attribution and per-frame coverage remain unverified. The `Clocks / Second` descriptor may help with drift context but is not a validated instantaneous frequency API. A utilization description hard-codes 585 MHz, so normalization must be checked on Adreno 840 before interpretation. System-sampled counters must not be presented as exact per-frame shader counters. Some counter sets require multiple measurement passes; do not introduce hidden replay automation to support them.

## Risks / Trade-offs

- Isolation and timestamp dependencies perturb workload overlap -> require explicit mode selection, disclose synchronization, and use the same mode for both variants.
- Last-marker placement after presentation adds artificial idle time -> plan submission boundaries before replay and bracket before host presentation waits.
- Short capture and DVFS yield noisy manual samples -> preserve every sample and telemetry; make no stability or significance claim.
- Backend optimization/readback changes work -> record effective capture/settings, avoid diagnostic passes during measurement, and validate output separately.
- Telemetry snapshots can be stale or misleading -> record source/freshness and current-HAL versus cached distinction.
- Vendor counter availability, permissions, version support and licensing vary -> capability probe first; keep timestamps independent of counter integration.

## Migration Plan

Add opt-in schema/CLI behavior, then update the bundled desktop backend and APK with identity-bound capability information. Validate CPU-only compatibility and manual Android GPU samples before packaging. Rollback is disabling GPU measurement and retaining existing CPU mode; existing result files keep their old schema/meaning. No commit, push, or external publication is authorized.

## Open Questions

1. Resolved: the user accepts isolated full-frame timing and one active command queue for the first milestone, with unsupported target captures triggering discussion.
2. Does the user's short target capture fit that queue/boundary scope? Do not substitute a sample capture as proof of target-capture support.
3. Partially resolved: AGI's OEM producer collected numeric system-wide samples in a bounded trace. Instantaneous clock interpretation, normalization and attribution to successful replay frames remain unverified. No production counter-integration scope is added by this change.

## References

- [Vulkan timestamp queries and result availability](https://docs.vulkan.org/spec/latest/chapters/queries.html#queries-timestamps)
- [Vulkan device properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceProperties.html)
- [Vulkan driver properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDriverProperties.html)
- [Android thermal access](https://source.android.com/docs/core/power/thermal-mitigation)
- [AGI source](https://github.com/google/agi)
- [AGI OEM producer launcher path](https://github.com/google/agi/blob/main/gapidapk/gapidapk.go)
- [Official helper used for the phone probe](https://github.com/google/agi/blob/v3.3.3/cmd/launch_producer/main.cpp)
