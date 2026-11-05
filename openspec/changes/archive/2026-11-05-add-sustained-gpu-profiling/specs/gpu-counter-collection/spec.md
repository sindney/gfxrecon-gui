## ADDED Requirements

### Requirement: Required counter discovery and actual collection

A sustained counter session SHALL discover provider descriptors, supported selections, units, sample limits and access, then verify actual numeric samples for each required metric. The precision profile SHALL require its requested full/half fragment metrics rather than substituting shader-busy utilization. Valid zeros SHALL be preserved; missing values SHALL NOT become zero. A counter-required request SHALL fail explicitly when required discovery, selection or collection is unavailable and SHALL NOT silently succeed with timings only.

#### Scenario: Half metric is absent
- **WHEN** the requested precision profile cannot obtain a required half-precision metric
- **THEN** the result identifies that missing capability and cannot claim a successful precision-counter session

#### Scenario: Advertised counter returns no samples
- **WHEN** a required descriptor exists but the provider returns no valid samples during the measured window
- **THEN** collection is incomplete rather than reported as zero activity

### Requirement: Preserve precision metric meaning

Counter results SHALL retain provider IDs/names/descriptions, units, GPU/driver identity, descriptor identity, stage scope and verification status. Fragment full/half instruction rates SHALL NOT be labelled universal FP16 activity, FP16 utilization, FLOPs or exact per-frame totals. RelaxedPrecision SHALL NOT alone be interpreted as native FP16 execution. Strong precision conclusions SHALL require calibrated metric semantics and controlled unchanged-workload comparisons with verified shader application and output checks.

#### Scenario: Only fragment half-instruction data is available
- **WHEN** a user tests a vertex or compute precision edit with fragment-only half counters
- **THEN** results disclose that stage limitation and do not claim the edit's native FP16 execution was verified

#### Scenario: Valid sampled half activity
- **WHEN** the driver returns a half fragment instruction rate
- **THEN** the report identifies that exact sampled metric and its verification/attribution limits rather than relabelling it as percentage of FP16 execution time

### Requirement: Sampling limits and common-clock evidence

The system SHALL preserve requested and achieved sampling intervals, sample timestamps/counts, gaps/loss and provider window semantics where known. Requests below the supported floor SHALL be rejected or explicitly negotiated with recorded effective values. A detailed shared GPU/counter time axis SHALL require a validated clock bridge including BOOTTIME conversion where needed and uncertainty/deviation. Raw GPU ticks multiplied by their period SHALL NOT be treated as BOOTTIME timestamps.

#### Scenario: One-millisecond provider and sub-millisecond request
- **WHEN** a user asks for finer samples than a provider's supported one-millisecond minimum
- **THEN** the system explains the limit and does not pretend the requested fine resolution was collected

#### Scenario: No reliable clock bridge
- **WHEN** timestamp and counter domains cannot be correlated within the requested mode's policy
- **THEN** detailed alignment is unavailable and the required correlated session fails or remains explicitly incomplete without invented frame attribution

### Requirement: Restoration and system-wide scope remain visible

The report SHALL distinguish rendering, restoration, pacing and other session phases and identify device-wide versus process-scoped collection. Excluding restoration from GPU timestamps SHALL NOT imply its counter contribution was removed. Whole-loop counters SHALL be labelled accordingly; rendering-only estimates SHALL require justified sampling windows/overlap and uncertainty. System-wide samples SHALL NOT be claimed exclusive to the edited shader or replay process.

#### Scenario: Counter interval spans rendering and restoration
- **WHEN** a sampled interval overlaps both phases
- **THEN** it is marked mixed/approximate or included only in an explicitly whole-loop summary, without pretending it is an exact rendering-only sample

### Requirement: Metric-appropriate aggregation and raw artifacts

Counter aggregation SHALL respect metric units and documented rate/ratio/percentage semantics. It SHALL retain raw trace, descriptors, decoded timestamped samples, phase/clock mappings and calculation policies alongside JSON/CSV summaries. Summaries SHALL report actual coverage and SHALL NOT discard spikes, average unlike units or invent exact counts from unverified point/rate semantics.

#### Scenario: Rate samples arrive at irregular intervals
- **WHEN** a supported rate metric has unequal sampling intervals
- **THEN** summaries use a documented time-aware policy or explicitly labelled sampled statistic and retain the actual sample intervals

### Requirement: Owned provider lifecycle and device concurrency

The session SHALL coordinate target ownership across replay, provider startup, trace capture and cleanup. It SHALL refuse or wait for conflicting users without killing their processes or traces. Producer reuse/launch, helper identity, access failures and ownership SHALL be recorded. Cancellation/timeouts SHALL stop only owned processes/files, release the lease and preserve partial diagnostics, without root or global driver/clock/layer changes.

#### Scenario: Another agent uses the phone
- **WHEN** an incompatible replay or tracing session already owns the target
- **THEN** profiling reports busy or waits under a bounded policy without stopping that session
