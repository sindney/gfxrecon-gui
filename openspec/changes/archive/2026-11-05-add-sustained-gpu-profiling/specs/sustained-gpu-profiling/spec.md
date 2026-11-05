## ADDED Requirements

### Requirement: Explicit sustained session phases

The system SHALL provide an opt-in sustained session for one selected capture-relative frame, with separate discovery/loading, warm-up, measurement, drain and cleanup phases. It SHALL load replay state once per variant and restore/replay the selected frame in process. Requested duration, warm-up and FPS SHALL be explicit in the request/provenance. Loading SHALL NOT count as warm-up or measurement, and completed warm-up SHALL NOT by itself establish steady state.

#### Scenario: Thirty FPS measurement window
- **WHEN** a user requests one second of measurement at 30 FPS after a configured warm-up
- **THEN** the session attempts that pacing after initialization, records actual iterations/durations/deadlines and preserves warm-up samples separately from measured summaries

### Requirement: Correct and complete frame restoration

The backend SHALL validate restoration of required active mutable resources and synchronization state before declaring sustained profiling supported. Snapshot/restore failures, unhandled dependencies and ambiguous required external state SHALL fail explicitly. Unused snapshot objects SHALL NOT alone invalidate an otherwise supported selected frame. Repetition SHALL NOT silently reuse mutated buffers/images or consume semaphores incorrectly.

#### Scenario: Required image cannot be restored
- **WHEN** a required frame input/output cannot be snapshotted or restored correctly
- **THEN** sustained measurement fails with the resource/scope reason and preserves diagnostics without publishing a successful precision comparison

#### Scenario: Unused AHB image in another device snapshot
- **WHEN** an AHB image belongs to an inactive device and is proven unrelated to selected submitted work
- **THEN** its presence alone does not reject the selected frame, and the restoration report records that exclusion

### Requirement: Iteration-aware device timing

Every GPU sample SHALL identify session, variant, phase, iteration, captured frame and submission scope. Repeated captured block IDs SHALL NOT overwrite prior results or reuse completed queries without synchronization. Validity, availability, timestamp conversion, completion, clock domain and errors SHALL remain explicit. GPU durations SHALL come from device timestamp endpoints and SHALL NOT be derived from wall time divided by iteration count.

#### Scenario: Same captured submissions replayed repeatedly
- **WHEN** multiple iterations reuse the same captured API block IDs
- **THEN** each completed iteration has a distinct retained GPU sample and query ownership is safe across repetitions

### Requirement: Pacing and actual work reporting

The backend SHALL schedule iteration starts from a monotonic pacing policy and report requested/achieved cadence, missed deadlines, actual measured duration, restoration cost and completed iteration count. It SHALL NOT silently drop iterations or burst to catch up. Effective swapchain mode, frame restoration and pacing SHALL be included in comparison compatibility.

#### Scenario: Restoration and rendering exceed the pacing budget
- **WHEN** one iteration cannot finish within its requested frame period
- **THEN** the session reports the missed deadline and achieved cadence without shortening the GPU interval or claiming the requested FPS was achieved

### Requirement: Valid summaries preserve individual evidence

Measured GPU summary statistics SHALL derive from valid individual measured samples with a documented percentile/variability policy. Warm-up and incompatible variants SHALL remain separate. Raw per-iteration samples, phases, provenance and partial failures SHALL be retained. The session SHALL apply explicit minimum sample/coverage policies and SHALL NOT equate requested sample count with valid measured data or statistical significance.

#### Scenario: Measured window has insufficient valid data
- **WHEN** the measured window fails its required sample/coverage policy
- **THEN** the result reports insufficient data and does not publish a successful GPU/counter improvement summary
