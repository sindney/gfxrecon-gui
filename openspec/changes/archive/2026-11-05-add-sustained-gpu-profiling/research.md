# Sustained profiling feasibility evidence

Probe date: 2026-11-02. This records planning/research, not a shipped sustained replay or production counter collector. The independent offscreen CLI change is implemented separately.

## Actual precision-counter co-collection

The existing official AGI ARM64 launcher temporarily registered the phone's OEM `gpu.counters` provider. Immediately before testing, no replay PID, producer or tracing session was active. The probe held known built/dist/prior-validation CLI lock contexts and launched a unique-context, single fresh replay of the original matching Adreno 840 capture. No root, forced clocks, driver/layer settings or APK installation were used by the counter probe.

The selected counters co-collected successfully during a successful game replay:

| ID | Provider metric | Samples in the approximately 47.68 ms CPU replay window | Nonzero samples |
|---|---|---:|---:|
| 26 | Fragment ALU Instructions / Sec (Full) | 43 | 32 |
| 132 | Fragment ALU Instructions / Sec (Half) | 43 | 30 |
| 143 | % Shader ALU Capacity Utilized | 43 | 33 |
| 145 | % Time ALUs Working | 43 | 33 |

Clock rate (1) and shader activity (21) were collected simultaneously. The bounded trace contained 90,577 actual samples per selected metric overall. Requested period was 1 ms; the observed median was 1.083177 ms and maximum gap 11.197761 ms. Raw clock snapshots identify BOOTTIME (clock ID 6), nanoseconds, with no overriding packet/sequence default. Embedded descriptor SHA-256 is `cb678ad51adbc52ec7f6eecfa562c01c011845a022e9e3f9f123a5966196ebb9`.

This removes the earlier mismatched-sample limitation: the replay used the actual matching three-frame game capture, SHA-256 `3c77f6fa01433cb46595109da7ccff5230634bc38c171cb72b52f78b4e49b1c4`, and exited successfully. However, its initial sample filter uses upstream CPU BOOTTIME boundaries. These are system-wide sampled values, not exact GPU-only frame attribution. Native GPU/counter clock correlation and render/restoration window semantics remain implementation gates.

The provider exposes full/half instruction rates for fragment work, not a universal per-stage FP16 metric. Some combined descriptors normalize two mediump instructions to one full-equivalent instruction. Percentages 143/145 have conditional provider-defined denominators. Keep exact labels and do not rename these metrics as FLOPs, FP16 utilization, or fraction of time executing FP16. Actual collection is established; causal precision-edit semantics are not.

The trace schema describes timestamped counter values and a requested interval, not guaranteed timing or explicit hardware averaging-window bounds. A one-millisecond floor cannot reproduce the reference Xcode detail over a few-millisecond frame. Sustained windows can still plot useful real series with gaps/uncertainty.

Helper/PID files were removed, provider unregistered and locks released after the probe. Raw evidence remains ignored:

- `.sessions/gpu-profiling-probe/counter-precision-1793590273146318200/sampling-smoke-result.json`
- `measurement-window-counter-samples.json`, raw trace and descriptors in the same folder.

## Selected-frame restoration assessment

Upstream loops include buffer/image copies, fence/event state, binary semaphore fixups and timeline semaphore reconstruction. They also contain warn-and-skip paths for snapshot failures. Their resource completeness and overhead must be validated before producing sustained results.

The target's two AHB images, 7507/7536, were created at blocks 990/991 on inactive framework device 3. Imported memories 7508/7537, views 7509/7510/7538/7539 and framebuffers 7512/7541 belong to that device. No descriptor writes reference those views and no post-state commands reference those objects/derived framebuffers. All six actual submits use queue 46, with command buffers 58/60/620 allocated on UAGame device 45/pool 57. Therefore AHB presence alone is not evidence of an active restoration blocker. Active resource restoration still needs runtime proof.

Current GPU profiling maps captured block IDs to one static sample. A loop repeats those IDs, so production work needs iteration/phase identities and append-only queries/reports, not a wrapper around the existing one-frame result.

## GUI integration findings

- Events filter/arrow are in `src/gui/events_window.cpp`; the Perf action belongs immediately right of the arrow with updated search-field width.
- Workspace already has background Start/Poll, cancellation and selection generation. A shared PerfSession core should serve both CLI and GUI; the GUI should not shell out to CLI or run preview replays during profiling.
- Existing fonts do not reliably cover supplementary clock emoji. A vector clock plus readable Perf text is suitable.
- ImGui is 1.92.9b with default 16-bit indices. SDL/OpenGL supports vertex offsets; SFML does not currently advertise/honor them. Plot budgets or consistently configured 32-bit indices require verification.
- ImPlot master is WIP; pin a tested compatible revision, compile its core/items files, preserve MIT licensing, and create/destroy its context within the backend-owned ImGui lifetime.

## Primary references

- [ImPlot integration](https://github.com/epezent/implot#integration)
- [Perfetto GPU sources](https://perfetto.dev/docs/data-sources/gpu)
- [GPU counter configuration schema](https://github.com/google/perfetto/blob/main/protos/perfetto/config/gpu/gpu_counter_config.proto)
- [GPU counter event schema](https://github.com/google/perfetto/blob/main/protos/perfetto/trace/gpu/gpu_counter_event.proto)
- [SPIR-V RelaxedPrecision rules](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_relaxed_precision)
- [Android reduced-precision guidance](https://developer.android.com/games/optimize/vulkan-reduced-precision)
