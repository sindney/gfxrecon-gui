
## Purpose

Collect broad optional OEM GPU reference counters with explicit provider identity, sample semantics, frame-window correlation and per-metric availability, independently of device timestamp validity.

## Requirements

### Requirement: Collect provider metrics without named profiles

Counter discovery SHALL enumerate provider IDs, names, descriptions, units, sampling bounds and simultaneous-collection constraints. All advertised metrics supported together SHALL be eligible without a name allowlist, a required FP16/full-half metric or substitution of one named metric for another. Advertised block capacities SHALL constrain collection deterministically; metrics excluded by those capacities SHALL remain exposed with an unavailable reason rather than disappear. Hidden multipass collection SHALL NOT occur. CLI legacy precision/bandwidth names SHALL alias the all-counter request.

#### Scenario: Unfamiliar vendor metric
- **WHEN** a provider advertises a valid metric not recognized by a predefined semantic name
- **THEN** it remains eligible and retains its original name and units

#### Scenario: Simultaneous capacity is smaller than the catalog
- **WHEN** a provider advertises more eligible counters in a block than can be sampled together
- **THEN** a supported deterministic subset is sampled and every remaining row identifies the capacity limit without fabricated values

### Requirement: Availability is per metric where possible

Actual samples SHALL be distinguished from advertisement. Missing samples SHALL remain unavailable/null, genuine zeros and signed values valid for their provider units SHALL be retained, and sparse data SHALL be labelled reference quality. Under all-counter collection, an individual discovery/trace descriptor mismatch SHALL make that metric unavailable with both definitions/reason retained, rather than discard valid unrelated metrics. Malformed trace, packet loss or unavailable global clock correlation MAY invalidate the counter collection but SHALL NOT invalidate independent valid GPU timestamps.

#### Scenario: One provider description changes
- **WHEN** one counter changes its normalization description between discovery and trace
- **THEN** that row is unavailable with the actual trace definition retained while other validated counters remain usable

#### Scenario: Only one observed sample
- **WHEN** exactly one valid sample falls in the GPU window
- **THEN** its value remains visible as a sparse single observation, not a fabricated average of multiple samples or a zero-valued absence

### Requirement: Preserve metric meaning and raw units

Counter evidence SHALL retain original IDs, names, descriptions, units, provider/device/driver identity, descriptor hashes, selected definitions, stage/scope labels and verification status. Provider-labelled full/half fragment instruction rates SHALL NOT be relabelled universal FP16 utilization, FLOPs, exact bytes per frame or proof of one shader's native precision lowering. Generic vendor metrics SHALL not receive invented stage or normalization claims. Display scaling SHALL not mutate raw JSON/CSV values or provider unit definitions.

#### Scenario: Relaxed precision investigation
- **WHEN** a relaxed-precision shader edit is compared using fragment half-instruction rates
- **THEN** the report preserves those exact metric semantics and does not claim native FP16 execution or shader-local causality from the counter alone

### Requirement: Sampling limits and clock evidence are explicit

Requested/observed sample intervals, timestamps, counts, gaps, losses and known provider averaging semantics SHALL be retained. Unsupported requested intervals SHALL be rejected or explicitly negotiated with recorded effective values. GPU-relative counter display SHALL require a validated GPU-to-BOOTTIME bridge with its uncertainty and status. Raw GPU ticks multiplied by timestamp period SHALL NOT be treated directly as BOOTTIME. Failed correlation SHALL retain raw evidence and make frame attribution unavailable.

#### Scenario: Requested period is below provider minimum
- **WHEN** the provider supports a one-millisecond floor and finer sampling is requested
- **THEN** the counter result explains the unsupported interval without claiming the finer resolution was sampled

### Requirement: Frame-window aggregation is an approximate reference

Only samples timestamped inside the correlated GPU interval SHALL contribute to displayed frame statistics/series. The report SHALL identify device-wide sampling, hardware averaging-window uncertainty, clock-alignment uncertainty, actual estimated coverage and any missing intervals. Sampled means SHALL be labelled as such; time-weighted estimates SHALL retain their calculation/coverage policy. Other GPU clients and averaging-window overlap SHALL not be claimed excluded, and a short window SHALL not be represented as a detailed pass/draw profile.

#### Scenario: A sample's hardware average crosses a frame boundary
- **WHEN** its timestamp falls inside the GPU interval but the provider averaging extent is unverified
- **THEN** it remains an explicitly approximate device-wide reference observation rather than an exact rendering-only measurement

### Requirement: Provider ownership and device concurrency are bounded

The session SHALL coordinate exclusive target ownership across replay, provider launch/reuse, tracing and cleanup. Conflicting replay or tracing sessions SHALL fail busy or wait under a bounded policy without stopping another user's processes or traces. Helper identity, provider reuse, access errors, raw traces and partial diagnostics SHALL remain recorded. Cancellation/timeouts SHALL stop only owned processes/files and release the lease without root, global driver, layer or clock changes.

#### Scenario: Another client is tracing
- **WHEN** an incompatible tracing or replay session already owns the target
- **THEN** profiling reports contention without interrupting that session
