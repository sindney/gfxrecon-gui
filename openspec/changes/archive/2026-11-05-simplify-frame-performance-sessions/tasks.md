## 1. Replace in-process repetition

- [x] 1.1 Retire the previous restoration tasks and remove inner-loop native patches, resource/descriptor planners, controls and obsolete fixtures/build registrations.
- [x] 1.2 Keep the shared request/report focused on one frame per fresh replay, with outer alternating baseline/modified runs and default GPU timing only.
- [x] 1.3 Preserve owned output/failure receipts, metadata/override evidence and ordinary replay compatibility.

## 2. Collect optional frame-window reference counters

- [x] 2.1 Implement a small counter readiness/continue protocol before the selected submission without snapshotting state or changing resource lifetimes.
- [x] 2.2 Preserve GPU timestamp validity independently and normalize bounded GPU-to-BOOTTIME correlation for optional counters.
- [x] 2.3 Filter samples/means to the GPU interval, retain raw traces, and expose empty/sparse/low-coverage data without fabricating zeros or failing valid GPU timing.
- [x] 2.4 Keep GPU and counter comparison validity separate, including explicit provider/definition/clock failures and cancellation/cleanup.

## 3. Simplify CLI and GUI

- [x] 3.1 Remove sustained/FPS/duration/warm-up options; retain outer runs and an optional all-counter capture toggle, with legacy CLI precision/bandwidth names accepted as aliases and compatible non-counter perf.
- [x] 3.2 Show a single-frame settings/table/plot flow with readable per-metric SI units, frame-relative lines, per-run overlays, Fit and concise counter-accuracy tooltip.
- [x] 3.3 Update docs and focused regression tests for optional counters, sparse samples, independent GPU timing, outer runs, diagnostics and removed controls.

## 4. Validate and deliver

- [x] 4.1 Build matching desktop/Android artifacts and binary-bound counter-window capabilities; run focused core/CLI/GUI/packaging checks.
- [x] 4.2 Validate actual frames 30/60 on the connected device with normal rebind, GPU timestamps, delivered optional counter samples, clean output references and outer baseline/modified controls.
- [x] 4.3 Record honest device/sample limitations, validate specs/diffs and deploy verified local dist artifacts with backups, preserved user data and no commits/push/archive.
