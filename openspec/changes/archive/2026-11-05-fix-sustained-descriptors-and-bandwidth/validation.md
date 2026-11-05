# Validation — 2026-11-03

All ten implementation tasks are complete. Changes remain uncommitted, unpushed
and unarchived. No connected-phone operations were performed; the user's other
agent owns device measurement validation.

## Descriptor behavior

The host planner admits supported `vkAllocateDescriptorSets`,
`vkFreeDescriptorSets` and `vkResetDescriptorPool` calls and records the required
lifecycle policy/call count. The native overlay preserves baseline physical sets
and uses owned pools for transient loop allocations, grouping frees by actual
physical pool and retaining original pool flags during overflow. Restoring an
iteration clears transient mappings, resets owned pools, releases spill pools,
restores baseline metadata and restores mutated baseline slots. Unsupported forms
and cross-device loop operations fail before unsupported mutation.

The fake Vulkan driver fixture directly includes the production descriptor
helper. It verifies five repeated mixed-free/reset/reallocation sequences,
within-binding arrays, physical and logical handle reuse, unchanged baseline
values and handle metadata, bounded owned pool count, actual injected allocation
failure with mapping rollback, simulated overflow spill allocation/free/cleanup,
cross-device rejection and preservation of original pools during cleanup. This is
native C++ state/reference validation; it does not claim rendered pixel equality
or successful sustained replay of the user's capture on Android.

Fresh-state pre-loop layout metadata is explicitly enabled for sustained replay.
Ordinary replay remains on the existing path. The pinned GFXReconstruct submodule
was not modified. Its status is clean.

## Bandwidth behavior

`precision` remains the default. `bandwidth` discovers counter IDs and requires
provider-labelled Read Total and Write Total with BYTE/SECOND units. Optional
texture, vertex and shader-processor rates have explicit missing/unavailable
metadata. Missing optional definitions, fewer than two measured samples or less
than 80% coverage do not fabricate means or fail otherwise valid required rates.
Raw trace/decoded data and optional samples remain diagnostic evidence.

Fixtures cover an ALU-free provider, arbitrary discovered IDs, valid numeric
zeros, aliases, missing required/optional metrics, ambiguous names/IDs/providers,
invalid units/periods/capacity, optional delivery/coverage failure and required
failure. Session tests verify profile provenance, original sample types and
suppressed deltas for mismatched profiles, definitions or per-iteration descriptor
work. Accumulated iteration totals do not change comparison compatibility.

CLI/GUI share the request/profile. GUI profile changes invalidate a successful
probe. Bandwidth remains a sampled whole-device whole-loop byte rate, including
restoration and pacing, rather than exact bytes attributed to one frame/shader.

## Executed checks

- Release SDL3 desktop core/CLI/GUI/GUI-test build: passed.
- Updated desktop replay backend build: passed.
- Updated ARM64 Android replay APK build: passed; no installation on the phone.
- `gfxrecon_core_tests --perf-session-tests`: passed, including timestamp,
  telemetry, counter, session, series and descriptor lifecycle fixtures.
- `gfxrecon_core_tests --replay-cli-tests`: passed, including existing full CLI
  and replay CLI regressions and new counter-profile options.
- Perf GUI suite and focused timing/profile invalidation check: passed with zero
  ImGui errors/ID conflicts.
- Ordinary local offscreen GPU timing replay of the public Windows triangle:
  passed, with completed GPU timestamps and backend-confirmed offscreen mode.
- Ten Python packaging tests: passed.
- Synthetic old/new EXE/APK capability-marker checks: passed. Both actual native
  artifacts advertise descriptor lifecycle protocol 1 and exact binary hashes.
- OpenSpec strict validation and whitespace diff checks: passed.

The complete core suite was attempted twice and hit the existing Windows
mock-capture atomic journal/session rename race (`Access is denied`) in different
mock Android capture phases. The focused suites listed above passed; a complete
core pass is not claimed. Logs are retained in `.sessions/descriptor-bandwidth-*`.

## Local delivery

Updated `dist/gfxrecon-gui` in place: CLI, GUI, desktop replay EXE, ARM64 replay APK,
both capability sidecars and documentation. All 104 inventoried files were
hash-verified. Existing preferences, captures, caches and sessions were preserved.
The deployed CLI exposes the bandwidth profile, offline relation queries still
work with source/tools absent, and the deployed GUI opened/rendered the public
local triangle using isolated settings.

Receipt and rollback files:
`.sessions/descriptor-bandwidth-deploy-20261103T110120Z/`.

Actual Android counter delivery, sustained restoration of the reported capture,
and resulting bandwidth measurements remain for the user's device-testing agent.

```powershell
.\gfxrecon-cli.exe perf --capture C.gfxr --frames 3 --sustained --counter-profile bandwidth --swapchain offscreen --warmup-ms 1000 --duration-ms 1000 --fps 5 --out profiles/bandwidth-retest
```

Use the intended capture/frame and retain any previously required memory
translation option. Use a fresh output directory per session.
