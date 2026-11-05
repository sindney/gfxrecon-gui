# Connected Adreno feasibility evidence

Probe date: 2026-11-02. The initial probe was capability discovery only. A later implementation-stage sampled probe is recorded separately below. Neither counter probe used root, settings changes or APK installation.

## Device and timestamps

- Connected model `2512BPNDAC`, Android 16/API 36, SoC `SM8850`, GPU Adreno 840 (`Adreno840v2` in sysfs).
- System Vulkan probe: 48 timestamp valid bits on all reported queue families, `timestampPeriod` approximately 52.08333 ns, graphics/compute timestamp support true, EXT/KHR calibrated timestamps advertised.
- `VK_KHR_performance_query` is absent from the system-driver extension list.
- A selectable Qualcomm pre-release GPU driver package is installed. These system probe results are preliminary; actual replay-process properties must be captured before measurements.

## Thermal and clocks

- ADB shell can query thermal status and current HAL GPU0-GPU10 temperatures. During the probe, current HAL GPU temperatures were about 30.2-30.6 C and thermal status was 0. These are observations at probe time, not a persistent device condition.
- The cached thermal block simultaneously reported roughly 48-56 C. It must not be treated as a fresh reading.
- Replay `run-as` cannot dump thermalservice because it lacks `android.permission.DUMP`; use host-side ADB for detailed readings.
- Tested `gpuclk`, `clock_mhz`, devfreq/frequency and power-level paths were denied or absent. No instantaneous GPU frequency reading was validated.
- `gpubusy` is readable in shell and replay contexts, but its kernel-specific units, interval/reset semantics and attribution were not validated. Do not derive per-frame utilization from it yet.

## AGI OEM counter provider probe

The initial Perfetto v49 service query contained `android.gpu.memory`, but no `gpu.counters` or `gpu.renderstages`. The device contains `/vendor/lib64/libgpudataproducer.so` and `libq3dtools_adreno.so`.

AGI's launcher loads `libgpudataproducer.so` by name and calls its exported `start()` function. The resolved absolute library path was not independently verified through process mappings. It has a fixed PID-file path and can signal a prior recorded PID, so the probe first verified that file was absent. The official AGI v3.3.3 ARM64 helper was extracted from the official release's nested APK, pushed temporarily, and executed as shell. It successfully registered `gpu.counters`.

| Advertised property | Result |
|---|---|
| Counter count | 80 |
| Minimum sampling period | 1,000,000 ns (1 ms) |
| Maximum sampling period | Not specified |
| Instrumented sampling | False |
| `gpu.renderstages` | Not registered |
| Actual counter samples | Not collected |

Representative descriptors:

| ID | Name | Advertised units or limitation |
|---|---|---|
| 1 | Clocks / Second | 1/second; sampled clock rate, not validated instantaneous DVFS frequency |
| 3 | GPU % Utilization | Percent; description refers to a fixed peak clock of 585 MHz |
| 21 | % Shaders Busy | Percent |
| 108 | L1 Texture Cache Miss Per Pixel | Derived count per pixel; attribution still unverified |
| 160 | Texture Memory Read BW | Bytes/second |
| 161 | Vertex Memory Read | Bytes/second |
| 162 | SP Memory Read | Bytes/second |

No thermal/temperature descriptor names were found. Registration and descriptors do not prove successful sample collection or correct units/normalization on this GPU. In particular, the 585 MHz description requires checking before interpreting utilization. Missing instrumented sampling and render-stage data also prevent claims of exact pass/frame attribution. A 1 ms advertised period is a sampling limit, not timestamp precision or a guaranteed achieved rate.

Cleanup verified the started helper terminated, its temporary binary and newly created PID file were removed, `gpu.counters` unregistered, and no tracing session was started. No unrelated process was signalled.

Local evidence retained outside Git:

- `.sessions/gpu-profiling-probe/adreno-counters-20261102/download-provenance.json`
- `.sessions/gpu-profiling-probe/adreno-counters-20261102/perfetto-query-with-producer.txt`
- `.sessions/gpu-profiling-probe/adreno-counters-20261102/perfetto-service-state.pb`
- `.sessions/gpu-profiling-probe/adreno-counters-20261102/gpu-counter-descriptors.json`
- `.sessions/gpu-profiling-probe/adreno-counters-20261102/capability-probe-result.json`

Helper SHA-256: `01dda37d4386d3e58ce47d77fee431e2bdc94889120171fda0e445a19c934473`.

## Recommendation and remaining probe

Deliver full-frame timestamps and provenance independently. For a separate counter milestone, test a small selected counter set during a manually launched replay, correlate its device/host time domain, check background-process contamination and overhead, and validate units and normalization. Do not request all counters by default or assume registration makes all combinations concurrently collectable. Discuss those results before integrating a provider into normal profiling.

The OEM producer is currently the most concrete Adreno route because its registration was demonstrated on the connected phone. Qualcomm's official QHWI integration documents an Android ARM64 Profiler Interface Library obtained through QPM, requiring Snapdragon Profiler at runtime. Its wrapper demonstrates counter enumeration, selection validation, monitoring and value retrieval through `QProfilerInterface.h`. Exact Adreno 840 support, package access and redistribution terms have not been established; treat this as a secondary route. Mali will require a fresh device/provider probe when connected.

## Implementation-stage counter sampling smoke test

After the user approved implementation, one bounded ten-second Perfetto trace requested only counter IDs 1, 21 and 160 at a 1 ms period, with no instrumented-sampling or clock-fixing flag. The producer returned 9,040 finite numeric samples for each selected counter. The median interval was 1.081823 ms and the maximum was 15.785938 ms. Clock snapshots identify BOOTTIME (clock ID 6) and nanosecond timestamps.

Observed system-wide ranges were 0-726.48 million clocks/second, 0-100 percent shader activity, and 0-7.09 GB/s texture read bandwidth. These establish successful sampled collection only. They do not validate the instantaneous-frequency interpretation, normalized utilization semantics, or isolation from background GPU users.

The independently launched sample replay did not complete: that sample was captured on Adreno 850 and the connected GPU is 840, with unsupported feature warnings. No samples are attributed to a successful replayed frame. The matching three-frame game capture is used separately for timestamp validation. The trace/helper and newly created PID file were cleaned from the phone, the producer unregistered, and no active tracing session remained. No production counter integration was added.

Evidence retained outside Git: `.sessions/gpu-profiling-probe/counter-sampling-1793586845080702300/sampling-smoke-result.json`, `counter-smoke.perfetto-trace`, and `decoded-gpu-counter-samples.json` in the same directory.

## Primary references

- [Official AGI v3.3.3 release](https://github.com/google/agi/releases/tag/v3.3.3)
- [Official release ZIP used](https://github.com/google/agi/releases/download/v3.3.3/agi-3.3.3-linux.zip)
- [AGI launcher source](https://github.com/google/agi/blob/v3.3.3/cmd/launch_producer/main.cpp)
- [AGI producer launch orchestration](https://github.com/google/agi/blob/main/gapidapk/gapidapk.go)
- [Vulkan timestamps](https://docs.vulkan.org/spec/latest/chapters/queries.html#queries-timestamps)
- [Android thermal access](https://source.android.com/docs/core/power/thermal-mitigation)
- [Perfetto GPU data sources](https://perfetto.dev/docs/data-sources/gpu)
- [Qualcomm QHWI debugging integration](https://github.com/qualcomm/qhwi#debugging)
- [Qualcomm metric wrapper](https://github.com/qualcomm/qhwi/blob/main/util/qhwi_metrics.cpp)
