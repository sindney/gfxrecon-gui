## 1. Resolve measurement scope and feasibility

- [x] 1.1 Confirm the user accepts isolated full-frame elapsed timing and one active command queue for the first milestone; record its overlap/attribution limits and stop-discuss cases. Accepted with "Good lets do it" on 2026-11-02.
- [x] 1.2 Inspect the user's selected short capture for first/last submissions, trimmed-state boundaries, active queues, present behavior and unsupported external/protected/device-group/sparse operations. Stop and discuss if it does not fit the agreed scope. The three-frame capture hash `3c77f6fa01433cb46595109da7ccff5230634bc38c171cb72b52f78b4e49b1c4` has two null-pNext submissions/frame on queue 46. Extra logical devices are created in the state snapshot but do not submit measured work.
- [x] 1.3 Verify actual replay-driver timestamp properties and endpoint clock-domain support, independently of the system-driver probe. Reject unsupported configurations explicitly. Actual Adreno 840 samples use 48 valid bits, 52.083332 ns/tick and enabled calibrated-device timestamps.

## 2. Implement backend GPU samples

- [x] 2.1 Define versioned request/report/capability schemas with individual frame samples, raw timestamp inputs, scope, synchronization, validity and request/capture/backend identities.
- [x] 2.2 Add repository-owned pinned-backend integration for frame submission planning and query resource lifetime on desktop and Android.
- [x] 2.3 Implement bounded frame-entry isolation using owned drains/shared deadlines or an explicit process watchdog, plus correctly ordered first/last GPU timestamp markers for supported submit variants, preserving application fences and submission metadata and excluding trimmed-state/presentation host delay.
- [x] 2.4 Implement bounded completion, availability checks, valid-bit/wrap conversion, error propagation and safe cleanup without per-submission idle waits.
- [x] 2.5 Add backend capability negotiation and installed-APK identity checks so old backends fail GPU requests clearly. An old bundled desktop backend was explicitly rejected with `cpu_fallback: false`.

## 3. Expose manual CLI measurements and provenance

- [x] 3.1 Extend shared measurement requests, local arguments, Android transfer and report validation for explicit GPU mode.
- [x] 3.2 Add separate per-frame GPU sample/comparison fields; retain CPU metrics, existing `--runs`, alternating variants and fresh result-cache bypass. Add no warm-up, loop or repetition automation.
- [x] 3.3 Query actual replay device/GPU/driver properties and record capture/backend hashes, effective optimization settings, cache identities/policy and uncontrolled cache limitations.
- [x] 3.4 Emit and validate structured per-module successful shader replacement evidence for GPU requests matching effective hashes; distinguish measured execution coverage from successful creation and retain ordinary/CPU-only backend compatibility. Real module 5168 control override confirmed selected, actual SHA-256 matched and creation returned success.
- [x] 3.5 Collect optional bounded host-side thermal/clock snapshots with source, context, units, freshness, time interval and explicit unavailable reasons; prefer current-HAL sensors over cached readings.

## 4. Complete the separate Adreno counter feasibility gate

- [x] 4.1 Preserve a concise capability record for the exact phone/driver: standard Vulkan counter extension, Perfetto producers, vendor libraries and accessible telemetry. See `research.md`; actual replay-process driver identity remains an implementation check.
- [x] 4.2 Probe the OEM GPU producer registration using the official AGI helper when available, inspect advertised counters/units/rates, and clean up only the helper/process/files created for the probe. Do not collect traces or change system settings during capability discovery. Registered 80 counters with 1 ms minimum period; capability-probe cleanup verified. A separate implementation-stage sampling smoke test is recorded in `research.md`.
- [x] 4.3 Evaluate Qualcomm PIL's exact-device support, access requirements and redistribution/license terms if the OEM producer path is unusable or insufficient. The OEM path produced actual sampled values, so the conditional PIL fallback is unnecessary for this change; licensing/support uncertainty remains documented for any future SDK integration.
- [x] 4.4 Discuss provider results and attribution/multipass constraints with the user before adding a counter-integration implementation scope. Timestamp delivery must not depend on counters. The user accepted the timestamp-first proposal; no production counter integration was added. A separate bounded provider smoke probe produced system-wide numeric samples without establishing successful-frame attribution.

## 5. Validate and document

- [x] 5.1 Test meaningful failure cases: unsupported queues/domain, unavailable or wrapped timestamps, missing endpoint/report, timeout/device loss, old backend, identity mismatch and unconfirmed overrides. Focused fixtures pass; real invalid-queue and 1 ns timeout tests fail explicitly and terminate cleanly.
- [x] 5.2 Validate synchronization and output equivalence separately from timed runs on representative supported captures; ensure CPU present delay cannot move the last GPU marker. Desktop and real Android screenshots are byte-identical. Final instrumented Android validation adds zero diagnostics relative to the CPU-only baseline (47 errors/23 warnings); existing capture/replay errors remain.
- [x] 5.3 Build the desktop backend and APK, then manually collect fresh baseline/modified frame samples on the connected Adreno phone and verify raw conversion, provenance and optional telemetry behavior. Final three-frame control pair contains six valid samples and structured same-byte override evidence.
- [x] 5.4 Document exact scope/overhead, `--runs 1` manual collection, short-capture limitations and unsupported reasons. Preserve all individual samples without claiming steady state or statistical significance.
- [x] 5.5 Review the final diff and OpenSpec consistency for user code review. Do not commit, push, publish or archive. Evidence, scope and limitations are recorded in `validation.md`.
