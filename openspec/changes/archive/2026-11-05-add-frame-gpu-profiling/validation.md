# Implementation validation

Validated on 2026-11-02. No commit, push, publication or archive was performed.

## Builds and tests

- Application/CLI and native core tests: Release build under `build-agent-replay` succeeded.
- Desktop replay backend: Release build under `build-tools` succeeded; identity-bound capability reports GPU protocol 1.
- Android replay: ARM64 debug APK built with the repository init script, capability generated, and installed on the connected phone. Final APK SHA-256 starts `86aadd1a`; the complete identity is retained in its capability file and measurement reports.
- Focused native GPU/telemetry tests passed, including original-capture planning, unsupported queues/boundaries, failed presentation, stale identities, invalid domains, availability, wrap arithmetic, inconsistent reports, override evidence, SHA-256 vectors, current-HAL parsing, denied sources, deadlines and cancellation.
- Full core/CLI suite passed on retry, including desktop GPU baseline/override integration. An initial run stalled in an indirect-outline pipeline-cache diagnostic and was stopped. The exact cold/warm diagnostic was then tested with both old and new backends: all runs passed with identical output hashes. No speculative fix was made; the subsequent full suite completed with exit 0.
- A final desktop CLI same-byte override control passed on the final application build: four valid frame samples, matching comparison scopes, and successful structured replacement evidence. An old bundled backend was rejected explicitly with no CPU fallback.
- Direct backend invalid-queue and one-nanosecond deadline requests returned explicit failures. The deadline case terminated in approximately 0.61 seconds with no valid samples and no teardown crash/hang.

## Actual Android measurements

Target source is `dist/gfxrecon-gui/sessions/17933263383023460/captures/capture-1.gfxr`, SHA-256 `3c77f6fa01433cb46595109da7ccff5230634bc38c171cb72b52f78b4e49b1c4`.

The final manual command produced one baseline followed by one modified replay, without automatic warm-up or looping. Both replayed the original capture and bypassed result artifacts. Each contains three valid GPU frame samples from queue 46. The actual replay reports Adreno 840, 48 timestamp bits, period 52.08333206176758 ns, enabled calibrated timestamps, availability true and owned-fence completion.

| Frame | Baseline GPU ms | Same-byte override GPU ms |
|---|---:|---:|
| 1 | 10.316458 | 10.925677 |
| 2 | 9.077448 | 9.797396 |
| 3 | 11.095781 | 10.914635 |

This is a functional control, not an optimization result: module 5168 was replaced with its unchanged original SPIR-V. The backend confirms the replacement file was selected, actual SHA-256 matches the requested `da8b0f354f761cc28d3580473d6b625ad2c4c70ff3f081fbb8a3a9bd7c9dfd43`, and creation returned success. Execution coverage remains explicitly unknown. Variation in this control demonstrates why a single short-capture pair does not establish a small improvement.

Both variants report no pipeline-cache file loaded and successful target-cache saves, with separate cache identities/hashes. Driver/hardware cache state remains unknown. Current-HAL GPU temperatures are available; direct clock sources are explicitly denied. Telemetry snapshots and all individual samples are retained.

Evidence: `.sessions/gpu-profiling-validation/game-control-final/result.json` and per-run reports/manifests. Final desktop evidence is `.sessions/gpu-profiling-validation/desktop-control-final/result.json`. These private generated artifacts remain outside Git.

## Output equivalence and Vulkan validation

- Desktop indexed cube: CPU and GPU-instrumented BMP files are byte-identical, SHA-256 `c5cc2c05b17692e8864cd022267b4a75cc9d062938f1fb3f015d19162c15623c`.
- Actual Android game frame 3: normal and instrumented screenshots are byte-identical at 1080 x 2344, with matching decoded pixels. Screenshot work is outside the measured endpoints and this diagnostic is separate from performance samples.
- Desktop Khronos validation is unavailable in the elevated environment: the loader ignores explicit layer paths and no linked medium token exists. It is not recorded as a clean pass.
- Android Khronos validation is explicitly enabled. CPU-only baseline has 47 errors and 23 warnings. The final instrumented replay has exactly the same VUID counts and byte-for-byte diagnostic message multiset: zero additions and zero omissions. No new timestamp, query-readback or submit VUID appears.
- The initial instrumented version added a Properties2 API-version diagnostic on an inactive Vulkan 1.0 instance. Dispatch now uses the actual instance version/KHR support; final validation confirms that diagnostic is removed. Existing extension-dependency, memory-binding, Features2 and recorded stage-mask errors remain baseline issues. This capture is not globally validation-clean.

Evidence: `.sessions/gpu-backend-validation/final-backend-summary.json`, `.sessions/gpu-profiling-probe/output-equivalence-1793587426996400300/output-equivalence-result.json`, and `.sessions/gpu-profiling-probe/validation-final-baseline-comparison.json` with accompanying raw logs and reports.

## Scope and remaining limits

Supported timing is isolated full-frame elapsed time on one active command queue. It may include waits and CPU submission gaps between endpoints; it excludes waits gating the first marker. Isolation and execution barriers change adjacent-frame overlap. Pipeline/pass timing, automatic repetition, steady-state claims, clock controls and production hardware-counter integration are not included.

The Adreno provider separately returned real system-wide sampled counters, as documented in `research.md`. Counter normalization, instantaneous-frequency semantics and successful-frame attribution remain future work. Mali support requires validation when that device is connected.
