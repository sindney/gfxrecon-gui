> Historical experiment: in-process sustained replay/restoration is superseded by
> `simplify-frame-performance-sessions`. Do not archive these inner-loop
> requirements as the current design. Counter profiles and durable failure
> evidence continue in the simpler fresh-session implementation.

## 1. Confirm feasibility and evidence semantics

- [x] 1.1 Capture the CLI-first/shared-core and later GUI scope, mandatory sustained counters, offscreen dependency, and explicit timing/attribution limits in proposal/design/specs.
- [x] 1.2 Probe full/half fragment ALU rates and ALU activity together on the connected Adreno using the matching game capture; preserve actual samples, descriptor/clock evidence, intervals and ownership cleanup. See `research.md`.
- [x] 1.3 Classify the target's AHB resources against active measured work; document inactive framework objects rather than rejecting their mere presence.
- [x] 1.4 Validate complete restoration of active resources/synchronization for the selected frame, including repeat-output/resource-state checks and strict failure handling. Stop and discuss unsupported required state before implementing a misleading loop.
- [x] 1.5 Validate precision-counter units/normalization with controlled FP32/FP16 workloads and recorded hardware/compiler support; distinguish fragment metrics from vertex/compute evidence. FullA/Half/FullB controls produced the expected full-to-half-to-full fragment instruction response with identical work, driver/features and matching FullA/B pixels; qualitative semantics only, not absolute FLOP calibration.
- [x] 1.6 Probe available calibrated host domains and establish a bounded GPU-to-BOOTTIME bridge with deviation/drift tests; resolve provider sampling-window uncertainty and acceptable attribution modes.
- [x] 1.7 Decide production producer-helper packaging/license provenance and target-level ownership across settings directories without root or global settings changes.

## 2. Implement the shared session and CLI contract

- [x] 2.1 Add versioned shared sustained request/result/progress/cancellation APIs with session, variant, phase and iteration identities; preserve existing manual mode.
- [x] 2.2 Add explicit sustained CLI options for one frame, warm-up, duration, pacing, required counter profile/period and output policy. Validate conflicting ranges/options before launching work.
- [x] 2.3 Integrate effective offscreen policy and cache/device/backend/override provenance from the focused offscreen change. Keep existing manual virtual default unchanged.
- [x] 2.4 Hold one coordinated device lease through discovery, producer/trace capture, replay phases and cleanup; prevent background preview or concurrent agent contamination.

## 3. Implement backend repeated measurements and counters

- [x] 3.1 Extend the validated loop path with strict restoration completeness, resource budget/error reports and explicit phase boundaries, without blindly resubmitting mutable state.
- [x] 3.2 Add monotonic start-deadline pacing, finite warm-up/measurement duration and achieved cadence/missed-deadline reporting, with no silent catch-up bursts or dropped iterations.
- [x] 3.3 Add append-only iteration GPU samples and safe per-iteration query ownership/reuse; preserve timestamp ordering, domain, availability, wrap and completion checks.
- [x] 3.4 Implement Adreno provider discovery/selection and owned launcher/Perfetto lifecycle, descriptor mapping and required actual sample validation. Preserve valid zeros, gaps and selection failures.
- [x] 3.5 Emit calibration/BOOTTIME mappings and render/restore/pacing phases with uncertainty, keeping whole-loop/system-wide scope explicit where rendering-only attribution is unproven.
- [x] 3.6 Implement bounded cancellation/timeout/device-loss cleanup for decoder loop, pending GPU objects, producer, trace and locks; retain partial diagnostic artifacts.

## 4. Implement summaries and CLI acceptance

- [x] 4.1 Produce per-iteration timing mean/median/p95/spread and provider-appropriate counter summaries, counts/coverage/intervals with explicit calculation policies.
- [x] 4.2 Validate baseline/modified compatibility for frame, work, effective modes, restoration/pacing, counters, clocks, caches and backend/device identity; suppress invalid/incomparable improvement claims.
- [x] 4.3 Retain raw trace/descriptors/decoded samples, phase/clock maps and per-iteration JSON, plus inspectable summary JSON/CSV exports.
- [x] 4.4 Test unsupported restoration/counters, missing/gapped data, wrong units/clock domains, pacing overload, repeated block IDs, stale identities, concurrent ownership and cancellation/failure cleanup.
- [x] 4.5 Build and validate matched same-byte control sessions and precision workloads on Adreno, including repeat-output/state equivalence, overhead and drift checks. Establish the CLI/core report contract before GUI implementation.

## 5. Add ImPlot and the GUI popup after CLI acceptance

- [x] 5.1 Pin a tested ImPlot revision compatible with the current ImGui, register build/license/dependency metadata and correct context lifetime for SDL/OpenGL and SFML. ImPlot v1.1 commit 819f2006 is vendored/hash-pinned with MIT license; normal and test-instrumented libraries compile.
- [x] 5.2 Validate consistent index configuration or strict draw budgets across renderers; add extrema-preserving plot reduction while retaining raw data.
- [x] 5.3 Add the clock/Perf action immediately right of the Events arrow, resize the filter field, and open a resizable capability-aware popup with explicit target/frame/settings.
- [x] 5.4 Integrate the shared session into Workspace jobs/progress/cancel and generation-safe result ownership; suppress automatic preview work while profiling.
- [x] 5.5 Add sortable/filterable metric tables with averages/spread/deltas, units, coverage and scope, plus linked-time GPU/counter plots with phase shading, sample tooltips and visible gaps.
- [x] 5.6 Test placement, no-capture/unsupported/busy states, counter-required failure, cancellation, stale selections, reopening retained results, dense data and inadequate single-frame sampling.

## 6. Document and review

- [x] 6.1 Document exact sustained scope, manual-mode compatibility, warm-up/cadence policies, counter semantics/limits, raw artifacts and no automatic precision/significance claim.
- [x] 6.2 Review schema/spec consistency and final diffs, preserve user changes, and leave everything uncommitted/unpushed for user review. Do not archive or publish.

## 7. Review feedback: dockable performance panel and matched-mode images

- [x] 7.1 Replace the modal popup with a closable dockable panel, add View access, preserve results and active jobs when closed, and limit preview suppression to active performance work.
- [x] 7.2 Route progress/errors/artifact diagnostics to Console and use the existing panel status dot, including unsupported/invalid settings and retained measurement warnings.
- [x] 7.3 Remove redundant mean/boundary legend swatches while preserving useful mean lines and series visibility behavior.
- [x] 7.4 Expose replay frame swapchain selection for matched-mode image comparisons, preserving the existing default and provenance/cache identity.
- [x] 7.5 Validate panel docking/status/logging/legend behavior and the replay CLI option; update docs and deploy current artifacts to dist without committing or pushing.

## 8. Review feedback: compact Perf controls and honest sample/status presentation

- [x] 8.1 Rename the dockable panel to Perf, retain the saved dock placement, shorten controls to Probe / Start-Cancel / Open, and move device/GPU/driver into a tooltip.
- [x] 8.2 Remove persistent explanatory rows and expanded diagnostics; retain data/table controls and context tooltips, with new runs displayed automatically and Open reserved for saved reports.
- [x] 8.3 Keep valid counter samples connected, preserve real zeros and missing intervals, and improve line/zero-baseline visibility without smoothing away the real short bursts.
- [x] 8.4 Distinguish completed-with-warning cadence/comparison results from failed measurements using amber versus red, with precise reasons in Console and the status tooltip.
- [x] 8.5 Validate compact UI, saved-report loading/warning states and both renderers; update docs and deploy to dist without device tests, commits or pushes.

## 9. Review feedback: graph context actions

- [x] 9.1 Remove the metric filter and its input/state, showing all metric rows.
- [x] 9.2 Move Warm-up and Fit to a shared right-click menu on the plotted graphs; preserve refitting and synchronized time axes.
- [x] 9.3 Validate the context actions and clean toolbar under both renderers, update docs, and deploy the GUI to dist without device tests, commits or pushes.

## 10. Review feedback: timing edits and compact image toolbar

- [x] 10.1 Keep successful probe support after valid FPS/warm-up/duration edits; retain discovery invalidation for mode/frame/swapchain/counter changes and explain disabled Start in a tooltip.
- [x] 10.2 Move draw visualization before RGB image controls and place scale immediately before Flip V, preserving all image/view operations and keeping the toolbar compact.
- [x] 10.3 Validate timing-edit readiness, image control placement/behavior and both renderers; update docs and deploy to dist without device tests, commits or pushes.

## 11. Review feedback: image toolbar spacing

- [x] 11.1 Narrow the view selector and add breathing room between toolbar groups.
- [x] 11.2 Place Fit/scale after Flip V with a vertical separator when inline, preserving responsive wrapping.
- [x] 11.3 Validate image control geometry/behavior on both renderers and deploy the GUI/help to dist, without device tests, commits or pushes.

## 12. Review feedback: Settings groups

- [x] 12.1 Separate Editor, Replay and Logging controls in Settings, placing Draw replay in Replay; verify the existing Settings workflow and deploy to dist without commits or pushes.
