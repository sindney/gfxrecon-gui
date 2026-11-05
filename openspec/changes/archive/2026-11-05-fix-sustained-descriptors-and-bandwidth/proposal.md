> Historical experiment: in-process sustained replay/restoration is superseded by
> `simplify-frame-performance-sessions`. Do not archive these inner-loop
> requirements as the current design. Counter profiles and durable failure
> evidence continue in the simpler fresh-session implementation.

## Why

Sustained replay currently rejects descriptor allocation and pool-reset calls in otherwise useful captured frames. The counter selector also exposes only the precision profile despite the existing Qualcomm provider advertising memory-bandwidth counters, blocking bandwidth investigations.

## What Changes

- Restore supported descriptor allocation/free/reset lifetimes across sustained iterations, with explicit rejection of unproven descriptor forms.
- Add a bandwidth counter profile using runtime-discovered read/write rates and optional texture/vertex/shader memory rates, without requiring precision metrics.
- Share profile selection across CLI and GUI and include it in provenance/comparison checks.
- Validate locally and update dist artifacts, including the replay backend/APK, without using the connected phone.

## Capabilities

### New Capabilities

- `sustained-descriptor-lifetimes`: Correct descriptor state and lifetime handling across repeated frame execution.
- `bandwidth-counter-profile`: Discover, collect and report provider bandwidth rates through the shared perf session.

### Modified Capabilities

None. These extend the existing unarchived sustained profiling implementation while preserving its precision default.

## Impact

Generated native replay patches, shared perf/counter core, CLI/GUI adapters, tests, documentation and locally deployed Windows tools/Android APK. No new external dependency, commits, pushes or publication.
