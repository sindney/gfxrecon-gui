## ADDED Requirements

### Requirement: Independent bandwidth counter selection

The shared performance API and CLI/GUI SHALL accept precision and bandwidth counter profiles. Precision SHALL remain the default. Bandwidth SHALL require provider-labelled total GPU read/write rates in BYTE/SECOND and include available texture, vertex and shader-processor read rates without requiring full/half ALU metrics. Counter IDs SHALL be discovered at runtime.

#### Scenario: Device offers bandwidth without precision metrics
- **WHEN** its provider advertises valid read/write rates but no full/half fragment counters
- **THEN** bandwidth discovery succeeds independently and reports absent optional bandwidth metrics

#### Scenario: Invalid units or unsupported simultaneous collection
- **WHEN** required metrics have ambiguous names, incompatible units or exceed the provider's simultaneous capacity
- **THEN** the session fails explicitly without fabricated counter values or hidden multipass collection

### Requirement: Profile provenance and reporting

The requested/effective profile and selected provider definitions SHALL be retained in provenance and comparison checks. Bandwidth rates SHALL use the existing sample table, plots and exports, with explicit system-wide whole-loop scope and no inferred exact bytes per frame.

#### Scenario: Bandwidth session results are inspected
- **WHEN** bandwidth sampling completes
- **THEN** results contain individual rate samples and summaries with their original units, selected profile and whole-loop scope

#### Scenario: Counter profiles differ
- **WHEN** a comparison combines different counter profiles or definitions
- **THEN** numeric comparison deltas are suppressed with a clear reason
