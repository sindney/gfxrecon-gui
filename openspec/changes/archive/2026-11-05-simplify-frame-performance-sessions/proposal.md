## Why

In-process repeated-frame replay requires complex and fragile Vulkan state restoration, while the useful workflow is profiling an ordinary selected frame with reliable device timestamps and approximate counter samples. The user chose an outer loop of fresh replay sessions and explicit counter limitations instead of sustaining rendering inside one process.

## What Changes

- Replace the experimental sustained path with normal replay-to-frame once per process; retain `--runs` as an outer baseline/modified session loop.
- Preserve GPU timestamp measurements, device/driver/cache/memory/override provenance and owned failure receipts.
- Collect optional precision/bandwidth counters around one selected GPU interval using a small readiness handshake and bounded GPU-to-BOOTTIME correlation.
- Show filtered frame-relative counter lines and means, including sparse/unavailable samples, with concise tooltip/docs disclosure.
- **BREAKING** Remove experimental sustained/FPS/duration/warm-up controls, restoration hooks, resource/descriptor snapshot planners and their test harnesses. Ordinary inspection/replay and legacy non-counter perf behavior remain compatible.
- Validate actual frames 30/60 on the connected Adreno device using the clean ordinary `rebind` path and deploy matching artifacts locally without committing/pushing.

## Capabilities

### New Capabilities

- `frame-performance-sessions`: Fresh outer-loop profiling with GPU timestamp intervals and approximate optional counters.

### Modified Capabilities

None. This supersedes the unarchived sustained profiling/resource-lifetime direction; historical investigation remains documented.

## Impact

Shared perf/counter core, native GPU timing overlay, CLI/GUI adapters, tests, OpenSpec and documentation, replay EXE/APK capability metadata and local dist. No new external dependencies or native frame-loop mutation.
