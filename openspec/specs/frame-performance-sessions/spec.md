
## Purpose

Profile one frame per ordinary replay process, with optional reference counters in the measured GPU window and independent outer repetitions instead of resource-restoring inner loops.

## Requirements

### Requirement: Repetition surrounds fresh replay processes

The shared profiling session SHALL execute one selected frame through normal replay once per process. Requested runs SHALL be outer controller repetitions. With shader overrides, baseline and modified processes SHALL alternate and retain separate identities, artifacts and individual samples. Loading SHALL remain outside the selected frame's GPU timestamp interval. The profiling path SHALL NOT snapshot/restore Vulkan resources, loop the selected frame inside the application, pace iterations, perform automatic warm-up or imply that repeated samples establish steady state.

#### Scenario: Three comparison runs
- **WHEN** three runs and an override are requested
- **THEN** the controller creates three independent baseline/modified process pairs and retains their individual evidence

### Requirement: Valid GPU timing is independent of optional counters

Frame GPU duration SHALL retain timestamp support, valid-bit/wrap, query availability, scope, completion and shader-application checks. Counter discovery, trace collection, clock-correlation, sparsity or individual metric failures SHALL NOT discard an otherwise valid GPU duration. Unsupported GPU timing or invalid endpoints SHALL still fail or invalidate the GPU measurement explicitly.

#### Scenario: Counter correlation fails
- **WHEN** a frame has valid GPU timestamp endpoints but no trustworthy counter clock window
- **THEN** GPU timing remains available and counters carry unavailable/null results with the correlation reason

### Requirement: Optional counters cover only the correlated frame window

Counter capture SHALL default off. Enabling it SHALL request all provider metrics supported together, without precision/bandwidth name allowlists or a required half-precision metric. Summaries and displayed series SHALL include only actual samples with timestamps inside the selected frame's correlated GPU interval. Complete raw traces and descriptors SHALL remain available separately. Empty windows SHALL have null means, valid zeros SHALL remain zero observations, and one-sample or low-coverage windows SHALL retain reference-quality samples and limitations without invented data or hard failure of valid GPU timing.

#### Scenario: Trace also contains loading and cleanup
- **WHEN** the raw trace contains samples outside the measured frame window
- **THEN** those samples remain in raw evidence but do not contribute to displayed frame-window means or lines

#### Scenario: Timing only
- **WHEN** counters are disabled
- **THEN** normal frame GPU timing runs without starting a counter provider

### Requirement: Comparable reports retain individual provenance

The versioned `gfxrecon-perf/1` report SHALL identify single-frame mode, selected frame, outer runs, variants, effective memory/swapchain policy, backend/device/driver/cache identities, confirmed overrides, raw samples, summaries and actual availability. Separate processes SHALL remain separate graph series rather than a fabricated continuous rendering sequence. GPU and counter comparison validity SHALL be assessed independently; missing or incompatible counter evidence SHALL not manufacture a counter delta or suppress an independently valid GPU result.

#### Scenario: Counter definitions differ across variants
- **WHEN** GPU timing is comparable but a counter's provider definition or usable window differs
- **THEN** the affected counter comparison is unavailable with its reason while compatible GPU evidence remains visible

### Requirement: Obsolete inner-loop controls are rejected

The experimental sustained/FPS/duration/warm-up controls SHALL be removed and rejected as unknown options. Documentation SHALL identify fresh outer runs as the replacement. Existing ordinary replay/inspection and non-counter CPU performance behavior SHALL remain compatible. No obsolete restoration/cadence requirement SHALL reject ordinary frames merely because they create or destroy resources.

#### Scenario: Resource lifetime calls occur in a selected frame
- **WHEN** normal replay handles buffer/image creation or destruction inside that frame
- **THEN** profiling executes that normal replay without a restoration-specific rejection

#### Scenario: Obsolete option supplied
- **WHEN** a caller supplies an old sustained, FPS, duration or warm-up option
- **THEN** validation rejects it before device replay and identifies the documented outer-run replacement

### Requirement: Failures preserve owned current-session evidence

The profiling controller SHALL preserve stage-aware current-session failures, original error classifications, cancellation and exclusive device ownership. Output reuse SHALL never overwrite a previous session or unrelated evidence. Error links SHALL name only existing artifacts from the failing owned session. Device validation SHALL retain honest output-correctness, timing, sample availability and comparison limitations without claiming a performance improvement from an unchanged-workload control.

#### Scenario: Selected frame fails before trace startup
- **WHEN** an owned fresh session fails during initialization or preflight
- **THEN** it retains the original failure stage/reason and no nonexistent measurement or stale artifact link is returned
