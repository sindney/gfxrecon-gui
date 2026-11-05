# Adreno 650 to 840 replay validation

Evidence collected on 2026-11-03; captures and generated artifacts remain outside Git.

## Cause

The reported failed GUI session is `dist/gfxrecon-gui/sessions/17936942524956872/`. Its replay log ends with the expected device mismatch warning, but Android crash evidence identifies the actual fatal call: API index 5375, thread 3, vkAllocateMemory returning VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS instead of captured VK_SUCCESS. This happens during trim-state restoration, before resource readback.

Original source SHA-256 is `0086786ed556db6ac649bf8b2a7be6174f9f61b1c82da9ae4a1b3d7387302f91`; the optimized variant SHA-256 is `0ca237a2bfc450bc1d54abf4e5f6d23a3795557a99838929380833aa666592bb`. Both streams have the same early allocation: 4 MiB, captured memory type 2, DEVICE_ADDRESS flags. Captured type 2 has property flags 0xf. There is no recorded nonzero opaque capture address or address-get call in these streams, so the error must not be described as proof of an embedded old absolute GPU address. Memory type numbering/property compatibility is the supported diagnosis: upstream remap changes only the type index through compatibility mapping, retaining allocation sizes and flags.

## Controlled backend probes

Owned direct probes used the same optimized capture and virtual swapchain:

| Policy | Outcome |
| --- | --- |
| Rebind | Frame 1 completed, screenshot retained; approximately 3.78 s end to end |
| Remap | Frame 1 completed, screenshot retained; approximately 3.16 s end to end |
| Realign | Failed during its initial tracking pass, no screenshot |

Remap is the smallest demonstrated remedy. These probes held the common target lease, checked for an existing replay process, and stopped only the PID launched by the probe. Raw logs/screenshots are `.sessions/deferred650-replay-diagnostic/{rebind,remap,realign}-*/`.

## Updated shared CLI/core on the original source

- `cli-original-draw-remap-1793696424123448400`: original, unoptimized file; selected block 151214; replay succeeded and returned an image plus three outputs. Requested/effective argument policy both remap.
- `cli-original-frame60-remap-1793696575608759100`: original, unoptimized file; replay completed through frame 60 and produced the final screenshot.
- `cli-original-frame1-none-1793696584906450600`: deliberate no-translation control reproduced the allocation error. The CLI now surfaced the exact native API/result and the portability hint, with exit-info and bounded native crash logs retained.

All three are under `.sessions/deferred650-replay-diagnostic/`, with JSON/stdout/stderr and replay manifests. Backend APK SHA-256 stayed `65f3e3a5ee12529c4f58c20d967947cf5742f59ea48267f9909295bb3a84ed21`; no native backend changes or APK reinstall were needed.

## Regression checks

Release GUI/CLI/core builds succeeded; GUI and tests also built under SFML. The full core suite passed on retry, including policy parsing/resolution, cache partition, comparisons and native-crash attribution. The first run hit an existing Windows temporary journal rename conflict in the capture mock; its log is retained and no unrelated capture code was changed. Matching package/PID/time, stale/reused PID, foreign package/PID, non-native exit and timestamp-boundary cases are covered by the parser checks.

GUI Replay policy selection/persistence and restoration passed under SDL and SFML, with zero ImGui errors or ID-conflict frames. Logs are `.sessions/deferred650-memory-core-tests{,-retry}.log`, `deferred650-memory-sdl-ui-tests.log`, `deferred650-memory-sfml-ui-tests.log`, and their settings screenshots.

The frontend records requested/resolved mode as backend argument provenance, explicitly not native-confirmed allocator telemetry. Local macOS keeps its established rebind default; Windows/Android keep none unless explicitly selected. No automatic policy retry was added.

## Deployment and limits

GUI, CLI and help were deployed to `dist/gfxrecon-gui`, with its existing preferences explicitly set to memory_translation=remap. All other preference fields, captures, caches and sessions were preserved. Backups/receipt are `.sessions/deferred650-memory-deploy-20261103T090934Z/`. Deployed GUI smoke, CLI option discovery and all 102 inventory hashes passed.

Cross-device replay is demonstrated for this source, through its 60 frames and the reported draw readback. There is no original-device image reference, so pixel equivalence and cross-GPU performance equivalence are not claimed. Other captures may require different portability modes or unsupported features. The relation-query change remains preparation-only. Nothing was committed, pushed or archived.
