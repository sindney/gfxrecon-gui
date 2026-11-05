## Context

The failing GUI session is `dist/gfxrecon-gui/sessions/17936942524956872`. Android exit-info records native SIGABRT; the crash buffer identifies `vkAllocateMemory` at API index 5375 returning `VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS`. Original and optimized streams both allocate 4 MiB using memory type 2 and DEVICE_ADDRESS flags. The captured type has flags 0xf; a numeric memory-type index is not a portable property description. The original stream contains no nonzero opaque-address metadata, so this is not evidence of a recorded nonzero address that must be patched.

Owned direct backend probes on the connected Adreno 840 completed frame 1 with both remap and rebind. Remap changes only the type index through upstream compatibility mapping, making it the smallest verified remedy. Realign failed during its initial tracking pass and is not a remedy for this capture. Frame success alone does not prove pixel equivalence to the original device or that every capture is portable.

## Goals / Non-Goals

- Expose a working, explicit portability policy across the existing shared replay paths and make the real native failure diagnosable.
- Preserve defaults, asynchronous GUI behavior, ownership/cancellation, durable evidence and strict policy comparison.
- Do not patch captured allocations, disable required features, modify the source trace, change pinned backend code, retry modes silently or promise universal cross-device replay.

## Decisions

### Shared policy resolution and compatibility

Requests use `memoryTranslation` with default `default`. A single resolver accepts default/none/remap/realign/rebind. Default preserves the existing local macOS rebind behavior and resolves to none for Windows/Android. Explicit none is honored everywhere. Each native launch receives exactly one resolved memory-translation option.

GUI, CLI and dynamic dumps share this contract. GUI settings persist the requested policy and snapshot it into frame/draw/mesh/diagnostic/shader comparison/reset and perf jobs. A policy change invalidates replay-derived views, pending previews and profiling capability state; it does not launch another session automatically.

### Provenance and cache separation

Record requested/effective mode with source `backend_arguments` and `backend_confirmed: false`. Here effective denotes the resolved option passed to native replay; the backend does not emit a confirming allocator-policy event. Do not invent native confirmation.

Pipeline identities include resolved mode; result identities additionally retain requested policy so metadata is not reused from a different request. Profiling policies and comparison guards include memory translation. Static dumps remain static; dynamic dump resume identities distinguish replay policies.

### Android native failures

When replay exits unexpectedly, save a bounded crash-logcat snapshot alongside exit-info and replay.log. Select an abort reason only when the latest exit is native and package/PID/timestamp match a crash report. A stale crash, a reused PID at a different time, another package, or a non-native latest exit must not be attributed to this session. Otherwise retain the existing generic/low-memory diagnosis. Diagnostic access failure must not replace an otherwise available exit reason.

For invalid opaque capture-address errors, include the native API/result and a memory-portability hint. Preserve raw logs as evidence. Do not clear the system log buffer or stop an unrelated process.

### Delivery and validation

Keep the library/platform defaults unchanged. For the user's current portable installation, select remap explicitly in its preference file with a backup, preserving all other settings and sessions. Validate actual frame and requested draw readback on the connected phone, then deploy matching frontend artifacts and documentation. No APK rebuild is needed because the existing backend already accepts the option.

## Risks / Trade-offs

- Remapping can still fail for unsupported features, external resources or address-dependent captures → expose explicit choices and report actual failures; do not claim general portability from this one successful capture.
- A policy change alters allocation behavior → separate caches, request snapshots and performance comparisons.
- Shared logcat contains unrelated historical failures → require package/PID/time attribution and keep an unknown fallback.
- New global GUI preference could invalidate displayed artifacts → clear derived views on change and preserve static capture/shader documents.
- Original-device image reference is unavailable → validate completed replay/readbacks and inspect results, without claiming original-device pixel equivalence.
