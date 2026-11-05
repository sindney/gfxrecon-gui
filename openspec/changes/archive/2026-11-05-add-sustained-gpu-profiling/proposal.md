> Historical experiment: in-process sustained replay/restoration is superseded by
> `simplify-frame-performance-sessions`. Do not archive these inner-loop
> requirements as the current design. Counter profiles and durable failure
> evidence continue in the simpler fresh-session implementation.

## Why

A short capture's individual GPU timings cannot reliably align with millisecond-sampled hardware counters or establish a small relaxed-precision improvement. A controlled sustained replay window needs valid frame restoration, real ALU/half-precision counter samples, and a common time axis before its averages or plots are meaningful.

## What Changes

- Deliver CLI support first: an explicit sustained profiling mode for one selected frame, with configurable warm-up, measurement duration and target FPS, per-iteration GPU timestamps, and required sampled GPU counters. Preserve the current manual single-run workflow.
- Require an actual counter provider and usable requested metrics; report unavailable counters explicitly and fail a counter-required session instead of silently producing timing-only results.
- Provide a precision-focused counter selection containing full/half fragment ALU instruction rates and available ALU activity/capacity metrics, preserving the provider's exact names and units. Advertised names alone do not establish accuracy or prove an edited shader executed FP16 arithmetic.
- Use offscreen replay by default for the sustained mode to avoid real swapchain acquisition/display pacing inside timed work. Depend on the separately implemented `add-offscreen-perf-replay` change for explicit swapchain mode, effective provenance, cache identity and comparison checks.
- Correlate GPU timestamp intervals with the counter clock domain, retain raw samples, expose actual sampling intervals, warm-up/measurement/restoration/pacing phases, gaps and uncertainty, and compute metric-appropriate summaries.
- Treat correct resource/synchronization restoration and its counter contamination as an implementation gate. Never blindly loop the file or treat an experimental backend loop as validated for the selected capture.
- Follow CLI validation with a GUI Perf button immediately right of the Events filter arrow, opening a dockable performance panel with configuration and Console progress, a sortable metrics table, average/variability summaries, and aligned timing/counter line plots using pinned ImPlot.
- Support a useful sustained counter graph at the provider's real granularity. Detailed single-frame plots require adequate actual samples and clock correlation; the Xcode screenshot is a UI reference, not a promised Adreno sampling capability.

## Capabilities

### New Capabilities

- `sustained-gpu-profiling`: Controlled in-process frame repetition, state validation, pacing, warm-up and measured windows, per-iteration timing and comparable result summaries.
- `gpu-counter-collection`: Required provider/metric discovery, sampled ALU/precision metrics, common-clock correlation, raw trace preservation, completeness and attribution limits.
- `performance-dialog`: Events toolbar Perf action, asynchronous dockable panel workflow, metrics table and ImPlot timing/counter visualizations.

### Modified Capabilities

- `agent-replay`: Add CLI sustained measurement and counter-selection/report contracts while retaining manual `perf` behavior and alternating baseline/modified variants.

## Impact

- Shared replay/performance-session core, backend iteration-aware query/report lifetime, frame-loop restoration, Android producer/Perfetto lifecycle and cancellation/device locking.
- CLI options, capability discovery, JSON/CSV/trace artifacts and comparisons. Production counter integration is new work; previous probes are not a shipped collector.
- GUI Workspace jobs/results, Events toolbar and dockable panel, selection/capture lifetime, progress/cancel behavior and retained result loading.
- Build and packaging: pinned ImPlot dependency, MIT license, context lifecycle, renderer/index limits and bounded plot rendering.
- Main initial hardware is the connected Adreno 840. Mali and other providers need capability-specific validation later. No root, driver/settings changes, commit or push is authorized or required.
- This change is a plan. Implementation starts with CLI after its resource/counter feasibility gates, followed by GUI after the CLI contract is validated. The independent offscreen CLI request is implemented now for the other agent's next measurement pass.
