## ADDED Requirements

### Requirement: Explicit full-frame GPU elapsed measurement

The Vulkan replay backend SHALL provide an opt-in full-frame GPU elapsed measurement for its supported configurations. Each sample SHALL identify the logical replay frame, first and last included submissions, device and queue scope, timestamp stages, execution mode, and included/excluded waits. Isolation that changes adjacent-frame overlap MUST be explicitly selected and reported. The system SHALL distinguish this elapsed interval from pure GPU busy time and natural application frame latency. Pipeline, pass and draw timing SHALL NOT be necessary for this capability.

#### Scenario: Supported isolated frame
- **WHEN** the user selects isolated full-frame GPU timing on a supported frame using one active command queue
- **THEN** the report contains a GPU timestamp interval bracketing that frame's selected work, records the isolation and scope, and excludes query setup, host completion waiting and presentation work outside that scope

#### Scenario: Unsupported queue layout
- **WHEN** a requested full-frame scope includes work the implementation cannot correctly bracket, including an unsupported multi-queue configuration
- **THEN** the request fails with the unsupported scope and reason, without dropping work, adding queue durations into a frame duration, or substituting CPU elapsed time

### Requirement: Runtime timestamp support and validity

The backend SHALL query the actual replay device's queue timestamp support, record raw endpoints, timestamp period, valid bits, availability and wrap handling, and validate the clock-domain guarantee for the endpoint submissions. It SHALL publish a numeric duration only for valid, completed endpoints and a known scope. Any potentially ambiguous wrap interval SHALL be rejected.

#### Scenario: Timestamp conversion
- **WHEN** completed timestamps are available from a supported queue and clock domain
- **THEN** duration is converted from valid-bit-aware tick difference using the replay device's timestamp period, with the inputs and units retained

#### Scenario: Unsupported timestamp queue
- **WHEN** a required queue has zero valid timestamp bits or endpoint comparability cannot be established
- **THEN** GPU measurement fails explicitly and the report contains no fabricated GPU duration

#### Scenario: Incomplete or invalid query
- **WHEN** a query is unavailable, an endpoint is missing, the device is lost, completion times out, or wrap is ambiguous
- **THEN** the sample is invalid with a specific reason and cannot participate in a GPU comparison

### Requirement: Correct synchronization and query lifetime

The implementation SHALL establish execution ordering around measured work, reset queries before reuse, preserve supported captured submission synchronization, retain query and command resources until completion, and confirm completion before result consumption or reuse. Host waits SHALL be bounded and their placement reported. The end timestamp SHALL be associated with the final included GPU submission rather than a later CPU frame-end or presentation return.

#### Scenario: Presentation blocks the host
- **WHEN** the frame's presentation call blocks after its final measured GPU submission
- **THEN** that host presentation delay does not extend the GPU timestamp interval by delaying end-marker submission

#### Scenario: Multiple submissions in a supported frame
- **WHEN** a frame contains several supported submissions on the measured queue
- **THEN** the interval brackets all included work without inserting queue-idle waits between submissions and reports the boundary/completion strategy

### Requirement: Versioned GPU report provenance

Each GPU request SHALL require a compatible backend protocol and validate the returned request/capture/backend identity, selected frames and measurement mode. GPU reports SHALL retain individual frame samples and validity/errors independently of CPU replay metrics. Missing or mismatched reports SHALL fail the GPU request rather than reuse prior artifacts.

#### Scenario: Old Android APK
- **WHEN** GPU measurement is requested with an installed backend lacking the required protocol
- **THEN** the request fails with actionable backend capability information without claiming a GPU result

### Requirement: Optional thermal and clock observations

Thermal and GPU-clock observations SHALL be optional. Every observation SHALL identify source, access context, units, observation time/interval, freshness where known, and availability or failure reason. Collection SHALL occur outside measured GPU work. Missing data SHALL NOT invalidate an otherwise valid timestamp sample. Cached temperatures SHALL NOT be labelled current, and temperature snapshots SHALL NOT be represented as continuous monitoring.

#### Scenario: Thermal available and clocks denied
- **WHEN** ADB shell provides current-HAL GPU temperatures but clock access is denied
- **THEN** the result records labelled temperatures and clock status `permission_denied`, and GPU timing remains usable

#### Scenario: Only stale sensor readings exist
- **WHEN** a telemetry source supplies only cached readings without a fresh observation guarantee
- **THEN** the result labels that freshness limitation instead of reporting them as current GPU temperatures

### Requirement: Honest hardware-counter capability discovery

Counter discovery SHALL distinguish Vulkan extension support from vendor/Perfetto provider registration and successful access. An absent currently registered Perfetto producer SHALL NOT by itself establish hardware-counter impossibility. Counter availability SHALL NOT be required for GPU timestamp measurement. Reported counter data SHALL require a demonstrated provider, names, units, sampling scope/rate and attribution limits; capability discovery alone SHALL NOT produce counter values.

#### Scenario: OEM producer library exists but is not registered
- **WHEN** a device has an OEM GPU producer library but the current Perfetto query has no GPU-counter source
- **THEN** discovery reports that the provider is unverified and requires a launch/capability probe, without claiming either usable counters or definitive lack of support
