## Context

The shared counter path already loads `libgpudataproducer.so`, calls its `start` export, discovers `gpu.counters`, and captures Perfetto packets. Discovery on the connected V2339A (MediaTek mt6989) succeeded with 363 Mali counters at a 1 ms minimum period. Many descriptors explicitly contain `NONE` denominator factors, unlike the Adreno descriptors previously tested.

Reference: AGI commit `d08ae4f58566327881a8faf51758b8a2cc9ebfbe`, [launcher](https://github.com/google/agi/blob/d08ae4f58566327881a8faf51758b8a2cc9ebfbe/cmd/launch_producer/main.cpp) and [Mali validation](https://github.com/google/agi/blob/d08ae4f58566327881a8faf51758b8a2cc9ebfbe/gapis/trace/android/mali/validate.go). The launcher uses the same library/export; its global PID-file replacement behavior is unnecessary for our session-owned helper.

## Goals / Non-Goals

**Goals:** Capture, save, and display Mali counters through the existing CLI/Perf panel; preserve all advertised metrics and raw definitions; retain actual producer identity and failure evidence; verify on the attached phone.

**Non-Goals:** Direct `/dev/mali0` ioctl access, shipping a counter database, AGI render-stage reconstruction, root/property changes, universal support for every Mali driver, or inventing Adreno precision/bandwidth equivalents for Mali metrics.

## Decisions

1. Reuse the OEM Perfetto adapter for both vendors. Capability is established by registration and emitted samples rather than GPU-name heuristics. A separate Mali backend would duplicate working device functionality and require new driver/version dependencies.
2. Keep descriptor `unit`, numerator/denominator arrays, hashes, and exported numbers unchanged. Simplify dimensionless `NONE` factors only in shared presentation scaling, so Mali `PERCENT/NONE` displays as `%`, `BYTE/NONE` as scaled bytes, `NONE/NONE` as `1`, and ratios retain meaningful denominators. Older saved reports gain the same readable presentation.
3. Attach the matching registered producer record (ID/name/PID/UID) to the selected catalog. Missing identity remains explicit rather than inferred from names or counter IDs. Save helper stdout/stderr before deleting remote files and include launch failures in vendor-neutral errors. Preserve existing owned-PID cleanup and watchdog behavior.
4. Use bounded synthetic Mali protobuf fixtures to cover high IDs, explicit NONE factors, integer/double samples, generic selection, BOOTTIME aggregation, and display. Real acceptance exercises a fresh frame replay, raw trace decoding, report/CSV generation, and existing GUI plotting where available.
5. Device capture demonstrated clock ID 5 (`MONOTONIC_RAW`) in Mali sample packets. Request service clock snapshots every 100 ms and explicitly use BOOTTIME as the primary trace clock. Normalize MONOTONIC (3) and MONOTONIC_RAW (5) only from direct, absolute, nanosecond snapshot pairs with BOOTTIME (6). Use the nearest snapshot offset, retain original timestamps/clock IDs and bridge evidence, reject contradictory snapshots, offset variation above 1 ms, and extrapolation beyond the existing ten-minute trace lifetime. Perfetto retains periodic clock snapshots only when significant drift is observed, so stable traces can have startup anchors only. Offset spread is observed evidence, not a proven uncertainty bound; drift/read latency between snapshots remains unverified. Reject unknown, incremental, and unsupported-unit clock domains. This extends the decoder; it does not treat GPU ticks as host time. Reference: [Perfetto clock snapshots](https://github.com/google/perfetto/blob/main/protos/perfetto/trace/clock_snapshot.proto) and [built-in trace configuration](https://github.com/google/perfetto/blob/main/protos/perfetto/config/trace_config.proto).

## Risks / Trade-offs

- OEM library/access/driver differences across Mali phones → report actual launch/registration errors; do not advertise all Mali devices as supported.
- Single frames may be shorter than the sampling period → retain unavailable/sparse rows and independently valid GPU duration; also validate sustained sample emission with a bounded test capture when needed.
- Presentation normalization could alter raw provenance → restrict normalization to `PerformanceScale`, with tests asserting unchanged source descriptors and values.
- The connected phone requires the compatible replay APK → install the configured development APK as part of authorized device testing; retain test evidence under ignored reports.
- Android 16 package visibility prevents the sample's Vulkan loader from seeing the replay layer APK → add an explicit replay package query to the generated sample manifest in the existing Gradle init script, leaving pinned backend sources unchanged.
