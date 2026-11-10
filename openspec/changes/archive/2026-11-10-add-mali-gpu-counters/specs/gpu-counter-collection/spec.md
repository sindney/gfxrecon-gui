## ADDED Requirements

### Requirement: Mali clock domains are normalized from trace evidence

The collector SHALL request periodic BOOTTIME clock snapshot evidence. The decoder SHALL convert absolute nanosecond MONOTONIC or MONOTONIC_RAW counter timestamps using direct snapshot pairs with BOOTTIME. Converted samples SHALL retain their source clock and timestamp. Reports SHALL retain conversion policy, snapshot evidence, observed offset variation, extrapolation distance, and the unverified uncertainty of drift and snapshot acquisition. Missing bridges, conflicting duplicate anchors, offset variation above 1 ms, extrapolation beyond the existing ten-minute trace lifetime, overflow, incremental clocks, and unsupported clock domains or multipliers SHALL fail counter decoding without invalidating independent GPU timestamps.

#### Scenario: Mali MONOTONIC_RAW trace
- **WHEN** sample packets use clock 5 and valid trace snapshots pair it with clock 6
- **THEN** samples are normalized to BOOTTIME for existing frame-window filtering and original clock evidence remains available

#### Scenario: Missing or inconsistent clock bridge
- **WHEN** a non-BOOTTIME sample has no supported direct snapshot bridge or the offset varies above the accepted limit
- **THEN** counter attribution is unavailable and the raw trace is retained

### Requirement: Mali OEM counters use the shared capture flow

The system SHALL support accessible Mali OEM Perfetto `gpu.counters` producers through the existing counter-enabled CLI and Perf panel. Discovery and selection SHALL use runtime descriptors rather than a vendor name, fixed counter IDs, or Adreno metric names. Counter values SHALL require successful trace decoding and existing clock/window validation. Unsupported devices SHALL receive vendor-neutral capability errors.

#### Scenario: Mali producer available but not registered
- **WHEN** a Mali device has an accessible OEM producer library and no registered counter source
- **THEN** the owned helper starts the producer and discovers its catalog for all-counter capture using the existing flow

#### Scenario: Mali catalog has no Adreno instruction metrics
- **WHEN** the catalog contains Mali utilization, byte counts, configuration constants, and ratios
- **THEN** all counters supported together remain eligible with their original definitions, and missing frame-window samples remain unavailable

### Requirement: Dimensionless unit factors have readable presentation

Counter presentation SHALL omit explicit `NONE` factors from unit products while retaining meaningful units and ratios. Fully dimensionless units SHALL display as `1`. Raw descriptor unit strings, unit arrays, sample numbers, and JSON/CSV provenance SHALL remain unchanged.

#### Scenario: Mali percentages and byte counts
- **WHEN** a descriptor supplies `PERCENT/NONE` or `BYTE/NONE`
- **THEN** presentation uses `%` or magnitude-scaled bytes respectively, without converting counts to rates

#### Scenario: Dimensionless numerator with a meaningful denominator
- **WHEN** a descriptor supplies `NONE/PIXEL`
- **THEN** presentation retains the reciprocal pixel unit as `1/PIXEL`

### Requirement: Registered producer and launch evidence are retained

Selected counter catalogs SHALL retain the matching registered producer ID, name, PID, and UID when supplied by Perfetto, with missing identity explicit. Owned helper console output SHALL be saved locally before remote cleanup when available. Launch/registration failures SHALL identify the OEM producer failure and available helper diagnostics without stopping unrelated producers or tracing sessions.

#### Scenario: Helper exits before registration
- **WHEN** the OEM producer cannot load or start and the helper exits
- **THEN** the error exposes available helper diagnostics and local evidence survives owned remote cleanup

#### Scenario: Existing producer reused
- **WHEN** exactly one registered counter source is reused
- **THEN** the selection retains that source's matching producer identity and marks reuse without launching a replacement
