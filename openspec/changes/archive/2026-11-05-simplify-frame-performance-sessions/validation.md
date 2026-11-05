# Validation — 2026-11-04

The active design profiles one normal selected frame per fresh replay process.
`--runs` repeats complete processes, alternating baseline/modified variants.
The prior resource-restoration investigation is superseded; its incomplete tasks
were not claimed as implemented requirements. No commits, pushes or archives were
performed.

## Implementation

- Removed native application/frame-loop/resource/descriptor patch generation,
  all five sustained restoration headers/CMake files, the host lifetime planner,
  restoration fixtures and the repeated-output test harness.
- Retained GPU timestamp scope, availability/completion, wrap/domain and override
  checks. Optional counter collection uses one ready/continue handshake after
  prefix drain and query setup. A bounded same-queue marker maps GPU ticks to
  BOOTTIME independently of GPU duration validity.
- Android artifacts advertise `frame_counter_window_protocol: 1`; no old
  sustained/restoration capabilities remain. Windows supports existing GPU timing
  without claiming an Android counter provider.
- Counter discovery/start/clock/decode failures retain a valid GPU result and an
  explicit unavailable reason. Zero samples have null means, one sample remains
  sparse reference data, and low coverage is disclosed rather than hard-failed.
  Only timestamps inside the GPU window contribute to points or either mean;
  full raw traces and decoded packets remain on disk.
- GUI settings are Frame/Profile/Runs/Swapchain, plus Counter us for requested
  counters. Graphs use per-frame milliseconds and separate run overlays. Removed
  FPS, duration, warm-up, pacing/restoration bands and pair controls. Fit remains.
  Tooltip/docs explain device-wide sampling, averaging/alignment limits and short
  windows. Rejected/null clock windows retain GPU rows/points without exceptions.
- The GUI/shared API default to GPU timing only. For CLI compatibility, plain
  legacy `perf` retains replay wall timing; explicit `--gpu-timing isolated-frame`
  or `--counter-profile none` selects GPU timing. Precision/bandwidth profiles
  automatically select GPU timing and require one explicit frame.
- Owned output/session nonce, stage-aware receipts and current-artifact links
  remain. GPU and counter comparison compatibility are independent.

## Actual device and capture

Tested the connected Adreno (TM) 840 (serial 5dc63c3e), Qualcomm driver version
2150932482, build `a5c340457d / I07bd6b6a94`, using the available
Deferred_Adreno650 capture (SHA256
`0086786ed556db6ac649bf8b2a7be6174f9f61b1c82da9ae4a1b3d7387302f91`).
All tests used ordinary `rebind` memory translation and offscreen swapchains.
The capture was originally made on Adreno 650; no resource-lifetime preflight or
in-process restoration was applied.

| Selected frame | Fresh bandwidth runs | GPU durations (ms) | Samples per metric |
| --- | ---: | --- | --- |
| 30 | 2 | 13.7522, 13.5994 | 13, 12 |
| 60 | 2 | 17.1993, 12.5961 | 16, 12 |

All five discovered metrics delivered data: total read/write byte rates and
texture, vertex and shader-processor read rates. Window verification checked nine
device runs (these four, four A/B control runs, and one precision run): each had
one valid GPU sample, an available correlated window, no loop launch arguments,
all displayed counter timestamps inside that window, recomputed means matching
the report and valid frame-relative milliseconds. Clock-bracket uncertainty was
74,142–268,777 ns; provider averaging-window extents remain unknown.

Additional checks:

- GPU-only profile `none`: two fresh device processes, valid GPU timestamps and
  no counter-run directory/provider start.
- Precision profile: one fresh frame 30 process, 12 samples for each of six
  selected ALU/clock/busy metrics.
- Baseline/same-byte modified controls for both frames confirmed module 306
  replacement with original SHA256
  `5d99525a9c8d6689b86a1d8d49c29e350e4f9d9eaa55ccb41982c0990f30f1bd`.
  After cache preparation, both pairs passed GPU/counter compatibility checks.
  Seed evidence retains the correctly rejected cold/warm cache mismatch.
- Separate ordinary image controls with the same-byte override were
  pixel-identical on frames 30 and 60. Their baseline outputs also exactly match
  the previously saved clean `rebind` reference images. This verifies normal
  rendering separately; counter sessions intentionally omit screenshot readback.

Compatibility is not statistical confidence. The unchanged frame 60 control
varied from 12.8834 to 15.3972 ms, and its seed pair also varied. These controls
verify capture execution, sample plumbing and provenance; they do not demonstrate
an optimization or stable clocks. Counters remain reference estimates, and GPU
timestamps measure actual elapsed work under each run's conditions.

Raw evidence is under `.sessions/frame-perf-device-validation/`, including
`window-verification.json`, per-frame results, raw traces, CSVs, same-byte controls
and pixel-difference reports. The earlier `remap` rendering corruption and inner
loop failures remain historical evidence and are not counted as passing results.

## Local regression checks

- Windows Release core/CLI/SDL3 GUI and native replay/APK builds passed.
- SFML GUI/CLI build and Perf GUI suite also passed, including dense run
  overlays and unavailable/null counter windows, with zero ImGui errors/conflicts.
- Focused GPU measurement, counter-window, sparse/empty sample, session/receipt
  and CLI/replay suites passed. A new test initially warmed a shared comparison
  cache; it was isolated, retaining the production comparison checks unchanged.
- Perf GUI tests passed with zero ImGui errors/ID conflicts. Nullable/rejected
  window regressions cover unavailable endpoints, malformed available endpoints
  and stale numeric timestamps while preserving the GPU row and bounded axis.
- The full core suite passed after isolating test-owned device-lock files. An
  intermediate rerun encountered the existing mock display-setup race; logs are
  retained rather than treating it as a passing run.
- Ten Python packaging tests passed. Strict OpenSpec and source whitespace
  validation passed. Native macOS compilation was unavailable on this host.

## Delivery

The local dist update includes CLI/GUI, replay EXE/APK, matching capabilities and
updated docs. Deployment verifies the release inventory, CLI discovery/removed
options, offline relation queries and GUI opening/rendering with isolated
settings. Existing user preferences, captures, caches and sessions are preserved.
The validated memory policy for this capture is explicitly `rebind`; no global
preference is silently changed.

Deployment receipt and rollback copies:
`.sessions/frame-perf-deploy-20261104T034112Z/`. All 104 release inventory hashes
were verified, alongside packaged CLI help/retired-option checks, an offline
relation query and an isolated GUI local-triangle rendering smoke check.

```powershell
.\gfxrecon-cli.exe perf --capture C.gfxr --frames 30 --gpu-timing isolated-frame --runs 3 --memory-translation rebind --swapchain offscreen --out profiles/gpu-f30
.\gfxrecon-cli.exe perf --capture C.gfxr --frames 30 --counter-profile bandwidth --runs 3 --memory-translation rebind --swapchain offscreen --out profiles/bw-f30
```

Use fresh output directories. Timing-only repeated replay is the default shared
profiling workflow; counters are optional per-frame reference data.
# Counter capture and display follow-up (2026-11-04)

The later UI/CLI refinement replaces named acquisition profiles with a Counters checkbox and `perf --counters` (`--counter-profile all` equivalent). Legacy CLI precision/bandwidth values alias all. All provider metrics are exposed without a name allowlist, respecting advertised co-collection block capacity and retaining unavailable rows/reasons. Display uses shared SI units per metric while raw report/CSV values remain unchanged. This supersedes the profile-selection behavior described in the earlier validation below; no new OpenSpec change was created for this refinement.

Real Adreno device validation on `Deferred_Adreno650.gfxr`, frame 30, offscreen/rebind: all 80 advertised counters requested and reported; 79 delivered 10 or 13 samples inside the correlated GPU interval, including full/half fragment instructions and total read/write bandwidth together. GPU duration was 13.809895 ms. GPU % Utilization (ID 3) changed its description/normalization claim between discovery and trace and is explicitly unavailable; other counters remain valid reference samples, with the differing trace definition retained. Receipt: `.sessions/perf-all-device-20261104T092432Z/verification.json`.

Focused counter/timestamp/session/unit tests and CLI/replay CLI tests passed. Performance GUI suites passed on SDL3 and SFML, including scaled GB/s table/plot values, checkbox support invalidation, missing-clock behavior, cancellation, and the real 80-counter report. Logs: `.sessions/perf-all-gui-validation/` and `.sessions/perf-all-replay-cli-tests.log`. Updated desktop binaries/docs in dist, verified 104 inventory hashes and isolated-settings smoke checks, preserving settings/sessions and backend/APK. Deployment receipt: `.sessions/perf-all-deploy-20261104T092958Z/receipt.json`. No commit, push or archive.
