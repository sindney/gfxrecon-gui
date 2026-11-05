## Context

The existing `perf --gpu-timing isolated-frame` measures one selected capture-relative frame interval at a time, preserves individual baseline/modified runs, and records actual replay metadata. It deliberately does not loop frames or collect production counters. A separate offscreen change exposes upstream swapchain policy before sustained work begins.

The Adreno 840 OEM Perfetto provider advertises 80 counters with a 1 ms minimum period and no instrumented sampling. A probe of the actual three-frame game capture successfully co-collected full/half fragment ALU rates and ALU percentages. This establishes collection feasibility, not precise rendering-only attribution or proof that a particular relaxed-precision edit executes FP16.

The Xcode reference shows multiple fine-grained vertex/fragment tracks and counter plots over a few milliseconds. Our first scope remains full-frame timing and sampled counters; the provider does not currently supply comparable render-stage events or sub-millisecond sampling guarantees.

## Goals / Non-Goals

**Goals:**

- CLI-first sustained sessions with explicitly configured pacing, warm-up and measured duration, required GPU counters, and valid per-iteration device timestamps.
- Precision-focused full/half fragment instruction-rate evidence with exact provider semantics, supported ALU activity metrics, raw data and honest attribution.
- Correct restoration of the selected frame, bounded cancellation/cleanup, offscreen operation, and a report shared by CLI and GUI.
- An Events toolbar Perf panel using ImPlot, with sortable tables, averages/spread and timing/counter lines on a shared time axis.

**Non-Goals:**

- Guaranteeing steady state from a one-second run, automatic proof of optimization significance, exact shader/pass attribution from sampled device-wide counters, or matching Xcode's sampling resolution.
- Treating fragment half-instruction counters as universal vertex/compute FP16 metrics, instruction rates as FP16 utilization, or relaxed precision annotations alone as proof of native FP16 execution.
- Root, forced clocks, driver changes, silent timing-only fallback, or a GUI-only measurement engine.

## Decisions

### 1. Shared session engine, delivered CLI before GUI

The UI-independent performance-session request/result/orchestrator under `src/replay` is shared by the CLI and GUI. It owns device locking, target/backend/counter discovery, provider lifecycle, replay initialization, warm-up, measured iterations, artifacts, progress, cancellation and cleanup. Both surfaces use the same comparison and statistics code.

Keep the existing single-run workflow. An explicit sustained request selects one capture-relative frame; a range is rejected in the first sustained implementation. CLI spelling:

```text
perf --capture C --frames N --sustained --warmup-ms 1000 --duration-ms 1000 --fps 30 --counter-profile precision --counter-period-us 1000 --swapchain offscreen --runs 1
```

`--runs` remains the outer baseline/modified session count. Inside each variant, the process loads state once and repeats the selected frame. Preserve A/B alternating session order. Do not alternate shaders every frame or restart a process per iteration. Snapshot override files/hashes before each paired session.

### 2. Explicit phases and pacing

Phases are discovery, capture loading/pipeline setup, restoration snapshot, warm-up, measurement, drain, trace retrieval and cleanup. Loading a mobile capture can take over a minute and is distinct from a one-second warm-up or measurement setting. Progress must expose that distinction.

Use device monotonic scheduled iteration-start deadlines. A requested 30 FPS means approximately 33.33 ms between starts; it does not mean 30 ms of GPU work or continuous load. Include rendering, required restoration and housekeeping in the pacing budget. Report target/achieved cadence, missed deadlines and actual durations. Never silently drop iterations, catch up by bursting, shorten GPU intervals, or count waits as GPU timestamp duration. Offer explicit unpaced mode later only with its own provenance/comparison policy.

Warm-up uses the same workload, state restoration, swapchain and pacing as measurement. Retain its samples with phase labels while excluding it from default measured summaries. Finishing a configured warm-up is not a steady-state claim. Clock/thermal drift and timing spread remain observations; any future stability-based termination requires separate documented criteria.

### 3. Restoration is a release gate

Upstream `FrameLoopInfo`, `PreloadFileProcessor` and `VulkanReplayFrameLoopConsumer` provide experimental one-frame repetition, decoder bookmarks, and buffer/image/fence/event/semaphore restoration. They do not supply duration, pacing, warm-up or a strict completeness contract. Some snapshot failures warn and skip work. `CanRestoreImage` excludes transient, external-format and AHB-backed images.

Before timing, inventory the resources and synchronization state needed by the selected submitted work. Classify mutable state, reconstructible/discarded attachments, immutable inputs, and unrelated snapshot objects. Report restoration coverage and reject missing required state, failed snapshot allocations/binds, unsupported cross-boundary dependencies or ambiguous external work. Do not simply resubmit the same command buffers indefinitely. Validate repeated output and relevant resource state against fresh replay, across warm-up/measurement boundaries and timeline/binary synchronization cases. Memory overhead and unsupported resources must be explicit.

The target's mere AHB presence is not a blocker: offline evidence places AHB images 7507/7536 and their memories/views/framebuffers on inactive framework device 3, with no post-state use. Actual work uses queue 46 and UAGame device 45. Still validate restoration of the active resources; do not generalize that all AHB captures are safe.

### 4. Iteration-aware GPU query/report ownership

Current `SampleFor(block)` maps static captured API block IDs to one sample. Repetition reuses these IDs, so it must be replaced/extended by `(session, variant, phase, iteration, captured frame)` identities, with append-only samples and safe query reuse after completion. Never overwrite earlier samples or reuse a previous complete status.

Keep markers outside intact render passes, bracketing the first/last included real submission before presentation host delay. Frame-entry/loop-boundary synchronization, restoration and deadline waits are explicitly recorded. Do not insert idle waits between submissions. Preserve existing valid-bit, availability, wrap, clock-domain, timeout and shader-evidence checks. At least one fully completed iteration is required; the configured minimum sample/coverage policy can require more and must be reported rather than inferred from requested FPS.

### 5. Counter collection is mandatory in this mode

Implement a provider abstraction with discovery, selection validation, start/read/stop and descriptors. First production provider targets Adreno's OEM Perfetto `gpu.counters`, using the verified dynamic producer mechanism with safe ownership and artifact provenance. Do not depend on a user's preinstalled AGI GUI or silently download/run helpers. Build/package the small original launcher under this project's MIT license; no AGI source or proprietary producer binary is redistributed. Preserve launcher provenance, stale-PID safeguards and never stop a producer/process owned by another session.

The session owns a device lease across its entire lifetime, including the counter producer and trace. Current CLI locks are scoped to settings directories; production sustained collection must provide a common target-level ownership convention across GUI, CLI and direct provider helpers, or explicitly detect/refuse competing replay/tracing operations. Do not change global Perfetto, layer, clock or driver settings. Cancellation must clean only owned processes/files and retain partial diagnostics.

Discover provider names/IDs, descriptions, units, sampling limits, co-collection constraints, access and actual sample availability. IDs are not portable across drivers/vendors. The precision profile maps semantic requirements to discovered descriptors and records that mapping/hash. On this phone the promising metrics are:

| Observed ID | Provider metric | Interpretation |
|---|---|---|
| 26 | Fragment ALU Instructions / Sec (Full) | Full-precision fragment scalar instruction rate |
| 132 | Fragment ALU Instructions / Sec (Half) | Half-precision fragment scalar instruction rate |
| 143 | % Shader ALU Capacity Utilized | Provider-defined shader ALU capacity percentage |
| 145 | % Time ALUs Working | Provider-defined ALU activity while shaders are busy |
| 1 | Clocks / Second | Sampled rate; instantaneous DVFS semantics not established |
| 21 | % Shaders Busy | Overall shader activity, not a precision measurement |

Required counter discovery/selection or collection failure fails a sustained precision session. Valid zeros are retained; missing samples are not replaced with zeros. Generic ALU activity does not satisfy a missing requested half-precision metric. A vertex/compute-only edit cannot be claimed verified by fragment-only half counters.

### 6. Validate precision-counter semantics

Provider descriptors include legacy normalization language and derived full-equivalent instruction metrics. Preserve the exact names/units and validate the selected full/half metrics on this GPU/driver with controlled workloads before attaching a strong interpretation. Prefer known full/half arithmetic controls that resist compiler elimination, with identical output/work counts and hardware feature checks. If a calibration workload cannot be built, label semantics unverified and do not claim shader-level precision proof.

RelaxedPrecision permits a compiler to use reduced precision; it does not force native FP16 arithmetic. A comparison of unchanged/edited shaders needs identical frame/work/restoration/pacing, verified override application, independent output checks and counter/timing repeatability. Full/half rates can support a precision-conversion interpretation; they alone do not localize the change to one shader or prove a speedup. Do not convert them to FP16 utilization/FLOPs or combine full-equivalent totals without verified equations.

### 7. Align clocks and expose attribution limits

Perfetto GPU counter samples use BOOTTIME nanoseconds. Raw GPU ticks multiplied by timestamp period are not BOOTTIME timestamps. Independent probes found the current driver's KHR and EXT calibrated DEVICE timestamps map about 28.99 seconds outside actual timestamp-query submit/fence brackets, with either MONOTONIC or RAW and with no counter producer. The implementation preserves those raw diagnostics and does not trust their advertised sub-microsecond deviation.

The validated bridge instead executes owned GPU timestamp markers on the same measured queue, outside the render interval. BOOTTIME readings before submit and after fence completion bound each actual marker. Three probes must have consistent offset bounds; the best bracket plus one timestamp tick defines the reported uncertainty, which must not exceed the requested counter period. Raw GPU endpoints are mapped relative to the owned query marker, with actual brackets, queue/domain/period, availability and uncertainty retained and independently verified. This is measured bounded calibration, not a guessed epoch shift or a CPU iteration-start masquerading as GPU time. The actual game run achieved approximately 153 microseconds uncertainty for a 1 ms counter period. Failure to establish valid bounds rejects correlated results.

Record render, restoration, pacing-idle and warm-up phases on a common axis. Restoration uses GPU copies and idle operations and may affect caches/clocks; excluding it from timestamp duration does not remove it from device-wide counter intervals. First release can report **whole-loop counter scope** across the measured window, clearly labelled. Rendering-only estimates require known sample-window semantics and validated overlap/uncertainty. Samples crossing restoration/phase boundaries are not exact frame measurements. Do not subtract GPU restoration activity from a global metric using host timestamps alone.

### 8. Statistics and artifacts retain evidence

GPU mean/median/min/max/p95 and variability come from individual valid measured iteration durations, not wall time divided by frames. Keep phases and variants separate. Record requested/achieved sampling intervals, actual sample counts, window coverage, gaps/loss, clock uncertainty and counter completeness. Rates use documented interval/time-aware aggregation; ratios/percentages require provider-specific weighting or retain a clearly labelled sampled mean when their denominator is unavailable. A rate integral divided by completed renders is only a per-render estimate when its units, windows and attribution justify that operation.

Retain raw backend iteration reports, raw Perfetto trace, decoded counters/descriptors, phase/clock mapping, request/provenance and a versioned result JSON. Export CSV for inspection without discarding raw data. Summaries and plot reductions never replace raw artifacts. Compare matching frame/variant policies, device/driver/backend, effective offscreen mode, caches, restoration, pacing and counter definitions; suppress numerical improvement claims for mismatches, invalid samples or insufficient required data.

### 9. GUI follows the stable CLI/core contract

In `Workspace::DrawEvents`, place a clock icon and visible `Perf` action immediately right of the filter arrow; reserve space for both buttons when sizing the search field. Existing fonts do not reliably cover supplementary clock emoji, so use a small vector clock with readable text/tooltip and stable test ID.

Click opens a resizable dockable panel and begins non-destructive capability discovery when the target is free. Default its frame from the selected execution, then selected event/current frame; show an explicit selector. Configuration includes target, mode, frame, overrides, effective swapchain policy, warm-up, measured duration, FPS, counter profile and requested/available sampling period. Start is enabled only for a supported requested mode. Offer existing single-frame timing as an explicit separate choice if counters are unavailable; never silently downgrade sustained precision.

Use Workspace background jobs and a shared result payload, not blocking UI calls or a child CLI process. Send capture-loading and phase/progress diagnostics to Console, show the existing panel status dot, and provide Cancel; pause automatic preview scheduling during measurement. Snapshot selection/capture generation and retain finished artifacts if the user changes capture, without applying stale results to the new selection.

The panel includes a sortable table (metric, unit, baseline/modified mean, spread, delta, sample count, coverage, scope/status), plus linked-time plots. GPU elapsed ms is a separate track from counter instruction rates and percentages. Full and half instruction rates can share a unit-consistent plot; ALU percentages use their own track. Show phase shading, average lines, actual sample positions/tooltips, gaps and provenance. A requested 1 ms period does not justify interpolating missing data or fabricating within-frame detail. Single-frame zoom with too few samples says so explicitly.

### 10. Integrate pinned ImPlot without renderer assumptions

ImPlot v1.1 is hash-pinned at commit `819f200639455a58b6b3d4156b70e4271aeb2d00` as a minimal vendored source subset, compatible with the repository's ImGui 1.92.9b. Record the dependency and MIT license, compile separate normal/tested libraries from `implot.cpp`/`implot_items.cpp`, create context after OS backend initialization and destroy it before backend shutdown (SFML may own the ImGui context).

Address indexing consistently across SDL/OpenGL and SFML. SDL advertises vertex offsets; SFML does not. The implementation keeps 16-bit indices with a tested draw budget: GPU timing plus at most four selected counter metrics, two runs of one selected pair, and at most 512 extrema/gap-preserving display points per series. Dense discontinuities use bounded scatter rather than inventing connections. Zoomed views retrieve detail from raw data; plot reductions are cached and never replace raw artifacts. Test both renderer configurations and cancellation/reopening/stale-result cases.

## Risks / Trade-offs

- Experimental restoration is incomplete for some active resources -> strict inventory/report, repeat-output/state checks, and explicit unsupported failures before measurement.
- Frame restoration and other GPU clients contaminate counters -> whole-loop/system-wide labels, synchronized phase data, conservative precision conclusions and optional finer attribution only after validation.
- One second at 30 FPS may not warm clocks or supply stable statistics -> report actual samples/spread/drift; configurable durations, no automatic steady-state claim.
- Counter names or normalized percentages can be legacy -> calibrate semantics, retain descriptors and limit evidence to verified stage/units.
- Driver/sample clock domains differ -> explicit calibration/BOOTTIME bridge and uncertainty; refuse unsupported detailed correlation.
- Large traces and many plotted metrics hurt UI responsiveness/index limits -> bounded buffers, streamed artifacts, background decoding, selectable tracks and safe render budgets.
- Concurrent agents use different settings directories -> global target lease/concurrency design is required for production tracing; do not interrupt another session.

## Migration Plan

1. Land and validate the focused offscreen perf change without waiting for counters or warm-up.
2. Complete feasibility/calibration gates and the CLI/core sustained implementation with versioned backend/counter reports. Existing manual perf remains available.
3. Validate repeated state, output, clocks/counter coverage, failure cleanup and paired controls on Adreno.
4. Add pinned ImPlot and the Events performance panel using the already-tested core contract.
5. Validate a later Mali provider independently; retain explicit availability on unsupported devices. No commit, push or archive is part of this implementation request.

## Validation decisions and remaining limits

- The selected game frame restores 699 buffers and 4,269 images using about 393 MB of shadows. Repeated output matches a fresh replay. This conservative restoration supports 5 FPS on this capture; it does not meet a 30 FPS budget.
- Controlled FP32/explicit-FP16 workloads validate a qualitative full/half fragment instruction-rate response. Hardware averaging-window boundaries remain undocumented, so counter summaries retain system-wide whole-loop scope and do not claim exact frame or shader attribution.
- The driver's calibrated DEVICE epoch is inconsistent with real query timestamps. Owned, fence-completed GPU marker brackets instead provide a verified BOOTTIME bridge with approximately 153 microseconds uncertainty in the validated game run.
- The existing 16-bit index configuration is retained with bounded, extrema/gap-preserving plot reduction. Dense-data popup tests pass under both SDL and SFML without ImGui errors or draw-index overflow.

See [validation.md](validation.md) for retained evidence and the precise limits of these checks.

## Primary References

- [ImPlot integration and features](https://github.com/epezent/implot)
- [Perfetto GPU data sources](https://perfetto.dev/docs/data-sources/gpu)
- [Vulkan SPIR-V environment and precision](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html)

## Review follow-up: dockable performance workspace

The reviewed interface replaces the modal performance popup with a normal dockable GPU Performance window. Events Perf focuses/reopens it, and View provides access independently. Closing a panel preserves its active job and retained results; active performance jobs rather than panel visibility control preview exclusion. Existing title/dock-tab status dots indicate inactive, running, success or failure. Progress, error reasons and artifact diagnostics use Console, leaving the panel for controls and quantitative results.

ImPlot previously registered average references as separate legend entries, producing a dark secondary swatch clipped below baseline 1 in short graphs. Mean and measurement-boundary items now use NoLegend; mean visibility follows the corresponding baseline/modified series. Raw measurements are unchanged.

The image comparison workflow also needs the existing replay core swapchain policy exposed on replay frame, so an offscreen timing run can be paired with an offscreen image replay using explicit matching provenance and cache identity.

## Plot context menu follow-up

The metric filter and input state are removed. The table always shows all metrics, retaining sorting and plot selection. Warm-up visibility and Fit move to a shared graph right-click menu. The refit flag is consumed before menu actions, so a requested fit is applied on the next frame rather than cleared at the end of the current drawing pass.
