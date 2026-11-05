## Context

The sustained experiment mixed useful GPU timestamps/counter collection with Vulkan snapshot restoration, resource/descriptor lifetime tracking, pacing and warm-up. Device checks revealed portability/correctness problems and substantial implementation complexity. The user explicitly chose fresh replay sessions, GPU timing by default and optional approximate per-frame counters.

## Goals / Non-Goals

Goals: one ordinary selected-frame replay per process; an outer baseline/modified run loop; reliable GPU timestamp intervals; optional device-wide samples filtered to that interval; concise plots/averages/disclosure; durable failure evidence. No inner-loop frame playback, resource snapshots/restoration, FPS/duration/warm-up controls, clock locking or promise of statistically stable counters.

## Decisions

Counter UI follow-up: use one counters checkbox (default off), requesting `all`
provider metrics without precision/bandwidth allowlists. CLI `--counters` and
`--counter-profile all` enable it; legacy CLI precision/bandwidth names alias all.
Respect advertised simultaneous-counter block limits, retain uncollected metric
rows with reasons, and preserve raw report units. GUI statistics and plots share
readable SI scales per metric; this does not change sampled values or GPU timing.

- Keep `PerfSessionRequest` small: capture, target/tools/output/cache/session identity, overrides, frame, runs, counter profile/period, swapchain/memory policy, timeout/progress. GPU timing is always enabled for this API; profile `none` is the default. Each outer run/variant calls normal replay once, preserving alternating B/M order. Legacy CLI CPU-wall perf and existing manual GPU range behavior remain compatible.
- Optional counters use `ReplayMeasurement.counterWindow` and native JSON `counter_window` protocol 1. At the first selected submission, drain prefix work and prepare timestamp queries, publish ready, start host sampling and acknowledge a neutral `continue`. Reset the bounded measurement host deadline after the handshake. No frame-loop application/backend patches remain.
- Preserve strict GPU query support, result availability, wrap, scope, completion and override validation. After the target interval, correlate its GPU ticks to BOOTTIME using a bounded same-queue marker. Clock mapping is independent evidence: a failed or malformed mapping makes counters unavailable while valid GPU duration remains valid.
- Emit a binary-bound `frame_counter_window_protocol: 1` capability. Host normalizes counter-window metadata before filtering samples; legacy sustained requests/options are rejected rather than silently reinterpreted.
- Summaries and UI use only sample timestamps within the selected GPU interval. Preserve full raw trace/decoded evidence separately. Empty windows have unavailable/null means; one sample is sparse but visible; low coverage is disclosed without arbitrary hard failure. Counter discovery/start/decode failures retain valid GPU timing and an explicit counter reason. Cancellation, device contention and GPU failures retain their existing semantics.
- `gfxrecon-perf/1` remains the report envelope, with mode `single-frame`, one GPU sample per run and no pacing/restoration policy. Table/plots identify frame/run and show frame-relative milliseconds. Never connect separate replay sessions into one continuous series. Fit remains available; warm-up/pacing/restoration controls/bands are removed.
- Keep counter precision language in one concise tooltip and docs: device-wide periodic sampling, provider averaging and clock alignment blur boundaries; short frames may provide few samples. Device timestamp GPU duration remains the timing metric. No modal warnings/checklists.
- Retain owned session/failure receipts, source/device/driver/cache/memory/override provenance and actual-availability checks. Sparse counters must not create zeros or discard valid GPU rows. Comparison decisions retain actual identity/definition checks, without restoration/cadence gates.

## Risks / Trade-offs

- Short GPU intervals can contain no sample: show unavailable counts/means, keep GPU timestamps and raw trace, and let users repeat complete sessions.
- Counter-clock correlation can fail independently: normalize its status/reason without weakening GPU timing verification.
- Full process startup repeats are slower: reuse uploaded capture bytes and cached static inspection, while each replay itself starts normally. Loading remains outside the measured GPU interval.
- Removing experimental options changes scripts: document `--counter-profile bandwidth --frames N --runs N`; do not alias old duration/FPS semantics.
- Previous device tests exposed `remap` rendering corruption: validate the normal `rebind` path and compare clean reference images, without extending native memory-translation behavior.

## Migration Plan

Mark the resource-restoration change superseded, retain historical evidence, remove its runtime sources/tests/build registrations and introduce only the small counter-window clock helper. Build matching EXE/APK capabilities, validate real frames 30/60 and optional counters/outer B/M controls, then update local dist with backups/receipts and preserved user data. Leave work uncommitted, unpushed and unarchived.
