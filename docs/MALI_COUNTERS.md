# Mali GPU counters

Mali devices with an accessible OEM Perfetto `gpu.counters` producer use the same
**Perf > Counters > Start** and CLI `perf --counters` flow as Adreno. The helper
loads the phone's `libgpudataproducer.so` and calls `start`. No counter database,
root access, vendor-name selection, or GPU clock changes are required.

This follows AGI's [producer launcher](https://github.com/google/agi/blob/d08ae4f58566327881a8faf51758b8a2cc9ebfbe/cmd/launch_producer/main.cpp).
AGI's [Mali validation](https://github.com/google/agi/blob/d08ae4f58566327881a8faf51758b8a2cc9ebfbe/gapis/trace/android/mali/validate.go)
was also inspected. We discover the entire device catalog rather than requiring
its example utilization counter or copying its global producer PID replacement.

## Device acceptance

Validated on 2026-11-09 using the connected vivo V2339A, Android 16, MediaTek
mt6989, **Mali-G720-Immortalis MC12**, driver
`v1.r44p1-01eac0.7c759d1daf93e2baa476e2d4d07e761b`:

- Runtime discovery selected **363 counters**, with a 1 ms minimum interval.
- A fresh three-frame triangle capture was recorded on this phone and replayed
  offscreen. Its capture settings journal confirmed restoration.
- Three CLI frame-3 runs produced valid GPU durations of approximately 0.535,
  0.656, and 0.557 ms. Runs 1 and 3 each retained one real observation for all
  363 counters. Run 2 reported Perfetto trace-writer packet loss; its counters
  remained unavailable while its independent GPU duration remained valid.
- The resulting report retained 364 metric rows and exported 726 counter sample
  rows, plus raw traces, descriptors, producer identity, and helper logs.
- The real Perf panel's Probe/Start flow also completed a fresh frame replay and
  displayed all 363 counters with sparse observations. Screenshot review checked
  the metric table, percentage/dimensionless units, and frame-relative plots.
- A deliberately failing test helper demonstrated that launch diagnostics are
  saved and surfaced while GPU timestamp measurement still completes.
- Final cleanup left zero Perfetto sessions and no session-owned helper/trace
  files on the phone. No previously running producer was killed.

Ignored local evidence is under `reports/mali-investigation/`: `accepted/result.json`,
`accepted/counter-samples.csv`, `accepted/counter-runs/`, `gui-device.log`,
`gui-review/perf-window-device.png`, and `diagnostics-test/`. The GUI's fresh result
was saved under `build/Release/test-state/sessions/perf/17942074096175929/`.
Generated device captures and binaries are not checked into the repository.

## Clock and metric meaning

This Mali producer emits counter packets in **MONOTONIC_RAW** (clock ID 5),
whereas the measured frame window uses **BOOTTIME** (clock ID 6). The decoder
uses direct nanosecond clock snapshot pairs to normalize timestamps, retaining
source clock IDs/timestamps in `gpu-counters-decoded.json` and conversion evidence
in the report's `counter_clock_bridges`.

Trace configuration requests clock snapshots every 100 ms. Perfetto can retain
only startup snapshots when it observes no significant clock drift; see its
[snapshot implementation](https://github.com/google/perfetto/blob/v49.0/src/tracing/service/tracing_service_impl.cc).
The decoder uses the nearest recorded offset, records observed offset spread and
extrapolation, and rejects missing/contradictory bridges, offset variation above
1 ms, or extrapolation beyond the collector's ten-minute lifetime. Incremental,
unknown, and unsupported-unit clocks fail explicitly. Snapshot acquisition latency
and drift between snapshots have no proven uncertainty bound; these remain
approximate reference observations. GPU-to-host alignment has its own separately
recorded uncertainty.

Mali's `PERCENT/NONE`, `BYTE/NONE`, and `NONE/NONE` labels display as `%`, scaled
bytes, and `1`. Original descriptor units, arrays, hashes, and JSON/CSV numbers
remain unchanged. Byte counts are not relabelled rates, and vendor expressions
do not establish exact bytes per frame or shader-local FP16 execution.

Single frames shorter than the sampling period can contain zero or one sample.
Packet loss, inaccessible OEM libraries, or unsupported driver versions remain
explicit failures of counter collection. Testing this phone does not establish
support for every Mali device or revalidate Adreno hardware; existing Adreno
protocol regression fixtures remain covered by the core tests.

## Reproduce

Configure `adb`, compatible `replay_apk`, `test_apk`, and `gpu_counter_producer`
in `local-tools.json`; build/install the APKs and helper as described in
[BUILD.md](BUILD.md). Unlock and authorize the connected phone, then run:

```powershell
.\build\Release\gfxrecon-cli.exe --tools local-tools.json --serial DEVICE capture android --package com.lunarg.gfxreconstruct.test.test_apps.test_launcher --frames 1-3 --args triangle --out reports/mali-capture
.\build\Release\gfxrecon-cli.exe --tools local-tools.json --serial DEVICE --target android perf --capture reports/mali-capture/c_frames_1_through_3.gfxr --frames 3 --counters --runs 3 --out reports/mali-perf
```

Inspect each run's counter status and quality alongside the GPU duration; an
overall completed report can contain unavailable optional counters. In the GUI,
open the fresh capture, select frame 3 and the Android replay device, open Perf,
enable Counters, and click Start. Saved `result.json` files can also be opened
with Perf's Open button.
