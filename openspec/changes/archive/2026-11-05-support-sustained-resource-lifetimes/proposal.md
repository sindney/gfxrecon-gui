## Why

**Superseded:** The user selected `simplify-frame-performance-sessions` instead
of extending in-process state restoration. This document records the investigation
and does not describe the active implementation plan.

The available Deferred_Adreno650 capture cannot enter sustained counter collection: frame 30 creates buffers/images and binds memory, while frame 60 destroys images/views. The previous descriptor-only change did not cover those lifetimes, and preflight failures also advertise a failure.json file that is not written.

## What Changes

- Support ordinary buffer/image/view/device-memory lifetimes and bindings inside a repeated frame through validated baseline preservation and transient cleanup.
- Keep original resource handles needed by pre-existing command buffers and restore their contents/state before every repeat; recreate transient resources with fresh state per iteration.
- Write stage-aware failure artifacts for owned sessions, including preflight, and never return nonexistent report links.
- Validate on the real connected device and actual capture, including frames 30/60, repeated output checks, timestamps, delivered bandwidth samples and baseline/modified runs.
- Deploy matching native/APK/CLI/GUI artifacts locally while preserving user state. No commits, pushes or archive.

## Capabilities

### New Capabilities

- `sustained-resource-lifetimes`: Validated ordinary resource lifetimes during sustained frame replay.
- `performance-failure-artifacts`: Reliable session failure evidence for preflight and runtime failures.

### Modified Capabilities

None. This extends the existing unarchived sustained profiling implementation.

## Impact

Generated native replay overlays and tests, host planner/session/CLI diagnostics, backend capability metadata, documentation and local dist. Real-device validation is required for completion; simulated fixtures alone are insufficient.
