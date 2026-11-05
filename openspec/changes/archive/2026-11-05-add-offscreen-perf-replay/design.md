## Context

The pinned backend supports `virtual`, `captured`, and `offscreen` swapchains and defaults to `virtual`. The first two use a real presentation swapchain; the offscreen implementation avoids real swapchain, surface, and window creation. GPU timestamps currently bracket capture submissions, so real acquisition between submissions can enter the interval.

## Goals / Non-Goals

**Goals:** explicit CLI selection on desktop/Android, effective implementation evidence, segregated caches, and comparisons with identical requested/effective modes.

**Non-Goals:** counters, warm-up/repetition, new timing scope, automatic offscreen selection, GUI changes, or claiming offscreen timing represents display latency.

## Decisions

1. Add `ReplayRequest::swapchainMode`, default `virtual`, and `perf --swapchain virtual|captured|offscreen`. Validate before import; pass the exact supported value through both launch paths.
2. The repository-owned backend hook logs the implementation immediately after the corresponding `VulkanCapturedSwapchain`, `VulkanOffscreenSwapchain`, or `VulkanVirtualSwapchain` object is created. GPU reports contain a `swapchain` object with requested/effective modes and evidence source. This observes the selected implementation, not just parsed arguments.
3. New GPU requests include `swapchain_mode` and require matching requested/effective evidence. Older GPU protocol-1 backends without that added evidence fail explicitly, especially for offscreen. The base protocol remains 1 because the new request field requests an additive evidence feature; older requests remain accepted by the new backend.
4. CPU-only default replay remains usable with older backends: effective mode is `null` when no evidence log exists. Non-default modes require backend evidence and cannot silently proceed under an unverified implementation.
5. Include requested mode before computing the pipeline identity, automatically segregating artifact/result identities too. GPU provenance includes verified effective mode. Comparisons require identical swapchain evidence in addition to existing device, scope, backend, capture and cache checks.

## Risks / Trade-offs

- Offscreen changes presentation behavior and potentially image allocation -> retain explicit opt-in, unchanged virtual default, and mode provenance.
- A user invokes a stale APK -> reject missing mode evidence, preserve diagnostics, and explain the required backend update.
- Offscreen eliminates display acquisition but not GPU dependency waits or CPU gaps -> preserve existing timestamp scope wording and make no stronger timing claim.

## Migration Plan

Build desktop replay and the APK with the new hook, regenerate identity-bound capability files, and install the matching APK for Android validation. Existing default CPU workflows remain compatible; explicit modes and new GPU requests require evidence from the updated backend. No commit, push, or archive.
