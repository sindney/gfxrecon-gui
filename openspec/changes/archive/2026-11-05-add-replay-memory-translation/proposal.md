## Why

A capture from Adreno 650 fails on Adreno 840 because the replay frontend forwards the default allocation policy without exposing upstream memory translation. Android's native allocation error is also hidden behind a generic early-exit message, making a recoverable portability problem look like an unexplained crash.

## What Changes

- Expose explicit default/none/remap/realign/rebind policies through shared replay requests, CLI replay/perf/dynamic dump options, and Settings > Replay.
- Preserve established platform defaults and avoid automatic retries that silently change the replay policy.
- Record requested/effective backend-argument policy and segregate caches/comparisons by memory mode.
- Preserve and surface a native abort reason only when it matches the latest exit's package, PID and timestamp.
- Validate the reported capture's frame and selected-draw readback using remap; deploy reviewable local artifacts with an explicit working preference for this portable installation.

## Capabilities

### New Capabilities

- `replay-memory-translation`: Memory portability selection, propagation, provenance, cache isolation, GUI persistence and attributable Android crash diagnostics.

### Modified Capabilities

None. Added fields/options preserve existing defaults, error envelopes and ordinary replay behavior.

## Impact

- Shared replay/perf/dynamic-dump services, CLI options, GUI replay settings and request snapshots, targeted tests and docs.
- Uses the existing bundled replay backend/APK; no pinned submodule changes or new dependencies are needed.
- The prepared relation-query change remains unimplemented and separate.
