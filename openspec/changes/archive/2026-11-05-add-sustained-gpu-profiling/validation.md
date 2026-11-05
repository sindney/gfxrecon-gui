# Sustained profiling validation

Evidence collected on 2026-11-02. Generated traces, screenshots and private-capture data remain outside Git. No commit, push, archive, root access, driver/clock changes or keyguard dismissal was performed for these diagnostics.

## Production counter lifecycle and native controls

The original ARM64 producer launcher, identity sidecar and production `AndroidGpuCounters` API were exercised independently before capture looping. Discovery, binary Perfetto configuration/start acknowledgement, actual six-metric collection, decoding and cleanup succeeded. A roughly 1.25-second idle window returned 1,226 samples per selected metric, approximately 1.083 ms median intervals and about 100 percent adjacent-interval coverage estimate. Valid zero ALU values were retained. Cleanup verified that the owned helper, PID files and trace files were removed, the producer was unregistered, no tracing session remained, and the device lease was released.

Live tests exposed and fixed three adapter details: Windows ADB shell stdin truncates binary data at Ctrl-Z, so configuration is transported as ASCII base64 and decoded into binary on the device; this device acknowledges `--background-wait` without printing a PID, so ownership uses the client's unique trace-output argument; and Perfetto marks a sequence's first packet with `previous_packet_dropped`, which is not itself data loss. Later loss/overwrite indicators remain failures. Discovery and emitted descriptor wrappers differ, so selected metric meaning is compared strictly while both raw descriptor hashes are retained.

Opt-in original FP32/explicit-FP16 controls then ran Full A, Half, Full B on the same Adreno 840 / driver 2150932482. Every variant completed 30 frames paced at 30 FPS, 512 x 512 resolution and 256 runtime arithmetic iterations per fragment, with identical enabled Float16 feature support and zero missed deadlines. Full SPIR-V has Float32 arithmetic without Float16 types or RelaxedPrecision decorations; Half explicitly declares Float16. The promoted reviewable shader binaries match the executed controls byte-for-byte.

| Control | Full fragment instruction rate/s | Half fragment instruction rate/s | ALU capacity percent | ALUs working percent |
|---|---:|---:|---:|---:|
| FP32 A | 164.756 billion | 0 | 69.72 | 68.33 |
| Explicit FP16 | 0.426 billion | 174.323 billion | 37.81 | 43.65 |
| FP32 B | 166.248 billion | 0 | 69.25 | 68.19 |

Full A/B pixel hashes match. Half pixels differ as expected from precision changes; this is not an assertion of FP32/FP16 output equivalence. This establishes qualitative full/half **fragment** counter response on this driver, not exact scalar-op/FLOP normalization or causality for a separate RelaxedPrecision edit. Host submit/fence intervals in these controls are not GPU timestamp durations and were not used for a GPU-speedup claim.

Reproducible nonprivate source/build/run instructions are in `tests/precision_calibration.md`, `src/tests/precision_calibration.cpp`, `src/tests/precision_counter_driver.cpp`, `tests/shaders/precision*` and `scripts/validate_precision_counters.py`. Raw evidence is `.sessions/counter-production-smoke/run/` and `.sessions/precision-calibration/run-17935985443968462/`.

## Successful bounded-clock sustained CLI session

The actual three-frame game capture has SHA-256 `3c77f6fa01433cb46595109da7ccff5230634bc38c171cb72b52f78b4e49b1c4`. A public sustained CLI run selected capture-relative frame 3, used offscreen replay, requested 200 ms warm-up and 600 ms measurement at 5 FPS, and required the precision counter profile with a 1 ms period. It completed one warm-up and three measured iterations with zero missed deadlines. Achieved cadence was approximately 4.9994 FPS, with an actual measurement window of 600.069 ms.

The three valid measured GPU intervals were approximately 5.034, 5.149 and 5.479 ms (mean 5.221 ms). The separately retained warm-up interval was approximately 7.727 ms. These are device-timestamp render intervals, not wall time divided by frames or a steady-state claim. The recorded device is Adreno 840 with 48 timestamp bits and a 52.083332 ns tick period.

All six selected counters returned 529 measured samples each. Estimated adjacent-interval coverage was approximately 96.14 percent; the median interval was 1.079 ms and the largest observed gap was 9.550 ms. Full/half fragment instruction samples and both ALU activity metrics include nonzero measurements. Raw samples, descriptors, trace, clock mapping, per-iteration JSON, CSVs and timing/coverage summaries were retained. This successful CLI evidence is a **baseline-only** session; it is not a measured optimization or a paired private-shader improvement result.

Evidence: `.sessions/sustained-validation/bounded-clock-frame3/result.json` and its per-run/counter artifacts.

## Restoration cost and scope

The successful public CLI report records 699 buffers, 4,269 images and 392,592,768 bytes of shadow state: approximately **392.6 MB decimal / 374.4 MiB**. The initial implementation conservatively restores all bound resources on the measured device. Individual restoration phases in that run took approximately 120-152 ms. A 33.33 ms budget for 30 FPS is therefore not achievable for this capture under this policy; 5 FPS was validated. Requested cadence is not automatically achieved or silently substituted, and missed deadlines suppress comparison deltas.

The snapshot excludes 15 unrelated-device objects. Inactive framework AHB images are not treated as active restoration blockers. One dedicated transient depth/stencil image (12174) has an offline first-full-clear proof, and native replay verifies its attachment/layout/clear identity on every iteration. Its contents are explicitly discarded rather than pretending they were copied; layouts are restored. Required unproven state, allocation/bind failures, unsupported resource lifetimes, descriptor/query/event/addressing cases fail explicitly.

Restoration and pacing are excluded from render GPU elapsed intervals but remain inside **device-wide whole-loop counters**. The latter cannot be presented as rendering-only activity, exact per-frame totals or exclusive edited-shader measurements. The conservative restoration overhead is a material limitation for GPU frequency/load representativeness and small precision experiments.

## GPU/counter clock correlation

A standalone empty timestamp-query/fence probe reproduced an Adreno calibrated-timestamp inconsistency without an active counter producer or trace. Both KHR/EXT aliases and both MONOTONIC/RAW mappings put actual GPU query endpoints roughly 28.99 seconds outside their real host submit/fence bracket despite advertised sub-microsecond deviation. Selecting RAW did not fix it; no unexplained constant offset was applied.

The production sustained run instead uses actual owned timestamp markers on the measured queue, bracketed by device BOOTTIME submission/completion reads. Three probes establish overlapping offset bounds. Every retained render sample has its own bounded mapping, and uncertainty must fit within the requested counter period. The successful run's final bridge uncertainty was approximately 153 microseconds; all samples passed the 1 ms policy. The raw inconsistent vendor calibration is retained as diagnostic evidence, not trusted for alignment.

This is bounded correlation at the provider's granularity, not an assertion of exact event ordering inside a millisecond counter sample. Hardware averaging-window extents remain unverified. Counter rates/percentages retain their provider names, units and sampled-mean policy; interval-weighted estimates are labelled estimates. A 1 ms advertised sampling floor and observed larger gaps do not reproduce the Xcode reference's fine-grained vertex/fragment tracks.

Evidence: `.sessions/calibrated-clock-diagnostic/`, the successful CLI GPU report's `sustained.clock_bridge` and per-sample `clock_calibration` records.

## Repeat pixels and independent Vulkan diagnostics

A separate diagnostic compared a fresh offscreen frame 3 to four repeated iterations of that same captured frame. All images are byte-identical at 1080 x 2344, with BMP SHA-256 `0defe899a30304cc7842f25e33878c338aa3f45462dfc45aa436f740f239ab77`. This diagnostic used screenshot/validation switches independently of performance collection and explicitly bypassed the public counter requirement only for diagnostic execution; the successful public CLI session above actually collected its required counters.

The Khronos layer was enabled for both CPU-only and sustained diagnostics. CPU baseline emitted 73 diagnostic lines and looping emitted 85 because captured commands repeat. There were **zero new unique VUIDs** and identical normalized diagnostic-message sets after normalizing object handles and iteration frame numbers. Both report the same pre-existing 2,072 leaked-object count. Existing extension dependencies, memory binding, recorded barriers/stage masks, render-pass, shader and teardown errors remain; neither run is globally validation-clean. This result must not be described as equal raw error counts or a clean capture.

Evidence: `.sessions/gpu-profiling-probe/sustained-diagnostic-1793601976713911000/result.json`, screenshot metadata, raw logs and CPU/loop validation artifacts.

## Remaining interpretation limits

- Precision metrics describe fragment instruction rates, not universal vertex/compute FP16 usage. RelaxedPrecision permits reduced precision; it does not force native FP16 lowering.
- Counter data remains system-wide and whole-loop, with restoration/pacing/background activity and unknown exact hardware averaging windows.
- Warm-up completion and a short mean do not establish steady state, statistical significance or absence of DVFS/thermal drift.
- The current game capture's conservative restoration cost prevents 30 FPS; avoiding immutable-resource copies is future work that needs a new correctness proof.
- Calibration/controller controls support this Adreno driver only. Mali/provider availability and semantics require independent validation.

Final application/GUI build, renderer, focused/full test and review results are recorded by the primary implementation agent alongside this evidence; this document makes no claim that unobserved targets or unchecked variants passed.

## Same-byte paired control and final review guards

The public CLI also completed `.sessions/sustained-validation/same-byte-pair-initial/result.json` with baseline followed by an override containing the original bytes of module 5168 (SHA-256 `da8b0f354f761cc28d3580473d6b625ad2c4c70ff3f081fbb8a3a9bd7c9dfd43`). Native shader creation confirms replacement selection, successful creation and the exact hash. Both variants loaded no target pipeline-cache file, saved identical pipeline-cache bytes, used the same restoration facts, and reported no missed deadlines. Every measured GPU iteration and all six counter series were retained.

Baseline and modified completed six measured iterations each, GPU means 4.920 and 4.689 ms, with approximately 8.0 and 8.8 percent coefficient of variation. Counter windows retained 1,005 and 1,022 samples per metric, estimated coverage 97.3 and 96.4 percent. Actual windows/throughput (approximately 5.31 and 5.14 completed iterations per second) remain distinct from the requested 5 FPS cadence. A final in-flight iteration can extend the requested window; loading is excluded.

The apparent -4.69 percent GPU delta despite identical shader bytes is a control observation of run variability, not an optimization or significance claim. A one-second window is not guaranteed steady state; longer warm-up/windows and repeated outer pairs remain configurable. This paired control preceded the final zero-property-memory rejection guard; that guard closes an unsupported resource case and does not change the validated Adreno resources or rendering workload.

Final review added conservative rejection for memory binding inside the selected loop (including core/KHR v2 forms), so an object first bound after snapshot cannot silently escape restoration. Successful binds to zero-property memory are tracked explicitly and rejected when upstream snapshot metadata cannot restore them safely. Tiny FPS values with unrepresentable periods are rejected; measured-phase waiting is capped at the window end. Warm-up is a minimum duration under the requested cadence, not an exact wall-time cutoff.

Cancellation uses the host watchdog and bounded, cancellation-independent ADB cleanup. Native upstream device-idle/restoration waits are not individually bounded. A disconnected transport can prevent confirmation; `replay-cleanup.json` records `unknown` rather than claiming an attempted force-stop succeeded.

## Build and desktop verification

- Release CLI, GUI and GUI tests build under SDL; GUI and GUI tests also build under SFML. The patched Windows replay backend and ARM64 Android replay APK build successfully, with identity-matched GPU/sustained capability sidecars.
- Full `gfxrecon_core_tests` passes, including CLI contracts and real local replay/GPU/offscreen integration checks. The focused measurement/telemetry/session/counter/series suite and Windows child-service lifetime/cancellation fixtures pass.
- The three performance GUI tests pass under SDL and SFML: toolbar placement/dense plots/reopening, cancellation/stale selection, and a real local single-frame timing session through the shared core. Both renderers report zero ImGui errors and zero frames with ID conflicts; dense plots remain below the 16-bit draw-index budget.
- Nine release-package tests pass, including acceptance of the extensionless Android helper only at its exact bundle path. Modified Python build/package/validation scripts compile. OpenSpec strict validation and `git diff --check` pass.
- Windows process ownership explicitly lets the shared ADB server outlive individual ADB clients, while clients remain cancellable and ordinary tools retain whole-tree cleanup. Discovery in the final phone popup took approximately six seconds instead of repeatedly restarting/killing the daemon.

Logs are `.sessions/sustained-final-core.log`, `sustained-sdl-final-tests.log`, `sustained-sfml-final-tests.log`, `sustained-package-tests.log` and the final host/native/Android build logs. Desktop popup screenshots are `.sessions/sustained-sdl-final-review/perf-popup.png` and `.sessions/sustained-sfml-final-review/perf-popup.png`; those dense-data screenshots are explicitly synthetic UI fixtures, not phone measurements.

## Final connected-phone popup and cancellation acceptance

The final replay APK (including the zero-property-memory guard) and final shared preflight passed the real popup's device test. The popup discovered counters, started the shared sustained session, retained three measured GPU iterations plus six counter series, and displayed actual results without ImGui errors or ID conflicts. It achieved approximately 5.00 FPS over a 600.1 ms measured window with zero missed deadlines. Each counter retained 541 measured samples, with approximately 98.9 percent estimated coverage. The real-data screenshot is `.sessions/sustained-real-ui-review/perf-popup-device.png`; report data is `build-agent-replay/Release/test-state/sessions/perf/17936042496228354/result.json`, and test log is `.sessions/sustained-real-ui-final.log`.

The opt-in `gfxrecon_core_tests --perf-device-cancel` then confirmed measurement-phase replay and an acknowledged, identity-owned counter trace were simultaneously active before setting cancellation. The operation returned in approximately **3.874 seconds**, retained `failure.json` plus a nonempty partial trace, and did not publish `result.json`. Cancellation-independent observations verified that the owned replay, helper and trace processes were gone, all owned temporary helper/trace/PID files were removed, the named Perfetto session ended, and a separate process reacquired the device lease. Verification did not signal or delete foreign resources. Evidence: `.sessions/sustained-validation/active-cancel-final/cancel-verification.json` and `.sessions/sustained-active-cancel-final.log`.

All OpenSpec tasks are complete. Changes remain uncommitted and unpushed; the change has not been archived. Generated private captures, reports, screenshots and device traces were not added to Git.

## Dockable-panel and matched-mode image review follow-up

The modal popup is now a normal closable/dockable GPU Performance window, reachable through Events Perf, View, and the command palette. Progress, unsupported capabilities, invalid settings, failures and artifact paths use Console. The existing panel status dot and tooltip report running, inactive, success and failed/incomparable states. Quantitative cadence/coverage warnings remain visible with the data. Merely opening the panel no longer blocks automatic previews; active profiling jobs retain exclusive ownership. Closing a running panel preserves its job and results.

The dark secondary rectangle beneath baseline 1 was an average-reference legend swatch clipped in a short plot. Mean and boundary references now omit legend entries. Faint means remain drawn and follow the corresponding data series visibility. The dense-data tests assert exactly the two actual baseline/modified legend entries.

Five performance tests passed under both SDL and SFML: docking/titlebar close/reopen/retained layout, cancellation and stale selection, successful job completion/logging while closed, unsupported-probe failure and local timing success, and offline rendering of the user's retained phone report. Both runs report zero ImGui errors and zero frames with ID conflicts. The retained-report screenshots under `.sessions/perf-panel-sdl-review/` and `perf-panel-sfml-review/` are offline UI re-renders, not new phone measurements. Test logs: `.sessions/perf-panel-sdl-tests.log`, `.sessions/perf-panel-sfml-tests.log`.

`replay frame --swapchain virtual|captured|offscreen` now reaches the existing replay core, retains virtual as the default, and exposes requested/effective mode in results and existing manifest/cache identity. Full core/CLI tests passed, including an offscreen baseline/override image comparison. The image fixture has an isolated settings/cache directory so it does not prime the later GPU timing comparison's baseline cache. Log: `.sessions/perf-panel-core-tests.log`.

The current GUI, CLI and docs were deployed in place to `dist/gfxrecon-gui`, preserving captures, settings and caches. All 102 inventory file hashes were verified. Deployed GUI smoke and `replay frame --swapchain offscreen` image replay passed, the latter reporting effective offscreen from the backend swapchain class. Receipt and backups: `.sessions/perf-panel-deploy-20261102T081145Z/`. No device probing/replay, commit, push or archive was performed for this follow-up.

## Compact Perf controls, connected samples and warning semantics

The panel is now named **Perf**, with Probe, Start/Cancel and Open controls, a compact Settings section, and device/GPU/driver information only in the info tooltip. The prior GPU Performance dock settings are migrated on rename. Explanatory rows, expanded diagnostic dumps and repeated provenance/cadence labels were removed from the data area. New measurements still display automatically; Open uses the shared asynchronous saved-report loader and is not required after a run.

The user's retained frame-1 report `.sessions`/dist artifact `dist/gfxrecon-gui/sessions/perf/17936072512290566/result.json` contains seven GPU-duration samples (mean 5.336 ms) and 902 measured samples per counter over 1,013.203 ms. Full/half fragment rates are zero for approximately 89.4/95.0 percent of samples. The 45 nonzero half-counter values form seven narrow bursts aligned with rendering. Render intervals total approximately 37.351 ms, while restoration phases occupy approximately 905.805 ms (89.4 percent of the window). Counter graphs already used connected lines; the scatter fallback was not active. Six gaps exceed the 4 ms gap threshold and remain actual missing-data gaps.

Both sample types retain connected valid values. No zero samples were dropped, missing data bridged, or sustained ALU activity invented. A 1.5-pixel line and ten-percent vertical fit padding keep zero connections visible above the horizontal plot edge. Axis/table units use compact equivalents (instr/s, percent symbol, 1/s), with raw provider units retained in tooltips and reports.

The prior red dot classified missed cadence as failure. Completed valid reports with missed cadence or incompatible comparisons now use a distinct amber Warning state. The real saved report loads through the production asynchronous path with an amber warning and a Console/status-tooltip reason: approximately 6.91 achieved versus 30 requested FPS, 14 missed deadlines. True unsupported/invalid operations still use Failed/red. Raw data and comparison guards are unchanged.

All five performance GUI tests passed under SDL and SFML, including tooltip-only fixture GPU/driver text, concise/absent legacy controls, Start/Cancel toggling, docking/reopening, local GPU timing and unsupported failures, and loading the actual retained report into Warning with a warning log. Both runs report zero ImGui errors or ID-conflict frames. Logs and offline real-data screenshots are under `.sessions/perf-compact-sdl-tests.log`, `perf-compact-sfml-tests.log`, `perf-compact-sdl-review/` and `perf-compact-sfml-review/`.

The GUI and updated help were deployed in place to `dist/gfxrecon-gui`. Deployed GUI smoke and all 102 inventory hashes passed; backup/receipt are `.sessions/perf-compact-deploy-20261102T083758Z/`. Captures, settings and caches were preserved. No phone tests, commits, pushes or archiving were performed for this follow-up.

## Graph context actions follow-up

The Perf metric filter/input and backing state are removed; the sortable table shows all metrics. Warm-up visibility and Fit now live in a shared right-click menu over the graphs. The pending refit is consumed before menu handling, preserving next-frame refitting when either action is selected.

All five performance tests pass under SDL and SFML, with zero ImGui errors or ID-conflict frames. The dense-plot test checks the absence of persistent filter/Warm-up/Fit controls, opens the graph context menu, toggles Warm-up, reopens it to verify the checked state, and selects Fit. Logs and offline screenshots are `.sessions/perf-context-sdl-tests.log`, `perf-context-sfml-tests.log`, `perf-context-sdl-review/` and `perf-context-sfml-review/`.

The current GUI and help were deployed in place to dist; deployed GUI smoke and all 102 inventory hashes passed. Sessions/settings/caches were preserved. Receipt/backups are in the latest `.sessions/perf-context-deploy-*/` directory. No device tests, commits, pushes or archiving were performed.

## Timing edits and image toolbar follow-up

Previously every Perf settings edit cleared discovered support, disabling Start even after a valid FPS-only change. FPS, warm-up and measured-duration edits now retain successful discovery. Mode/frame/swapchain/counter-interval edits still invalidate it; invalid numeric values still disable Start. Disabled Start exposes the reason in a tooltip. A regression test uses a complete bundled sample capture plus an explicit support fixture (no device calls) to check 5-to-6 FPS, warm-up/duration changes, invalid-zero FPS, recovery and counter-interval invalidation.

The image draw-view selector and indirect-subdraw selector now render inside the active image viewer toolbar before RGB. Fit/scale/Flip V form a compact group with scale directly before Flip V; toolbar groups wrap and scale shrinks on narrower panes. Selection controls remain usable before the first texture arrives. Frame/resource/difference/comparison viewers keep the existing behavior; RGB, sRGB, rotation, range and source pixels are unchanged.

Six Perf tests and the image-toolbar test passed under SDL and SFML with zero ImGui errors or ID-conflict frames. The image test uses an actual local draw readback to verify selector-before-R placement, scale-before-Flip-V placement, orientation changes with unchanged source bytes, and Solid coverage/Original mode switching. The test driver resolves the real child window before accessing hidden combo IDs. Logs and screenshots are `.sessions/perf-image-toolbar-sdl-performance.log`, `perf-image-toolbar-sdl-image-toolbar.log` and their SFML equivalents, plus `perf-image-toolbar-sdl-review/` and `perf-image-toolbar-sfml-review/`.

The updated GUI and help were deployed in place to dist. Deployed GUI smoke and all 102 inventory hashes passed, preserving sessions, caches and settings. Receipt/backups are in the latest `.sessions/perf-image-toolbar-deploy-*/` directory. No phone tests, commits, pushes or archiving were performed.

## Image toolbar spacing follow-up

The draw-view selector now has an eight-font-unit preferred width (previously fourteen), with additional spacing before RGB and orientation groups. The inline control order is Flip H, Flip V, a vertical bar, Fit and scale. On narrower rows, the zoom group wraps and the redundant leading separator is omitted. Slider limits and image operations are unchanged.

SDL and SFML image tests pass, checking selector width/order, Flip V-before-Fit-before-scale geometry and spacing, flip behavior with unchanged source pixels, and Solid coverage/Original switching. Both report zero ImGui errors and ID-conflict frames. Evidence is `.sessions/image-toolbar-spacing-sdl-tests.log`, `image-toolbar-spacing-sfml-tests.log` and their review screenshot directories. The current GUI/help were deployed to dist with backups under `.sessions/image-toolbar-spacing-deploy-*/`; GUI smoke and all 102 inventory hashes passed. User settings and sessions were preserved. No device tests, commits, pushes or archiving were performed.

## Settings group follow-up

Settings now uses separate Editor, Replay and Logging section headers. Draw replay is in Replay; its widget identity, values and persistence are unchanged. Existing Settings open/close/palette UI checks pass under SDL and SFML with zero ImGui errors or ID-conflict frames. Screenshots/logs are under `.sessions/settings-replay-group-sdl-review/`, `settings-replay-group-sfml-review/` and their test logs. GUI/help were deployed in place to dist, preserving user state; deployed smoke and all 102 inventory hashes passed. No device tests, commits, pushes or archiving were performed.
