# cli-inspection-workflows Specification

## Purpose
TBD - created by archiving change cli-inspection-workflows. Update Purpose after archive.
## Requirements
### Requirement: Explicit output selection
Draw inspection SHALL accept an attachment or image ID and optional mip and layer. Selection SHALL consider only readable outputs, retain all returned resources, and report unavailable selections with available metadata. The default SHALL prefer COLOR 0.

#### Scenario: Select a depth output
- **WHEN** a draw with readable depth is replayed with `--attachment depth`
- **THEN** its primary image and selected metadata identify that depth output

#### Scenario: Missing subresource
- **WHEN** a requested mip or layer is not returned by the backend
- **THEN** inspection fails with `output_unavailable` and lists available outputs

### Requirement: Lightweight batch replay
`replay draws` SHALL accept explicit execution indexes and inclusive ranges, import once and read back bounded batches without generating a static dump. Original draws SHALL merge per frame where selectors are unambiguous. Diagnostic, subdraw and baseline requests SHALL use disclosed isolated replay strategy. Results SHALL retain requested order, per-execution status, artifact provenance and actual replay counts.

#### Scenario: Inspect several original draws
- **WHEN** two selectable original draws from one frame fit in one batch
- **THEN** a single resource-dump replay returns separate outputs for each execution without a static dump tree

#### Scenario: Diagnostic batch
- **WHEN** several executions are requested in outline mode
- **THEN** each draw receives its own outline sources and the result reports isolated strategy and all replay calls

### Requirement: Structured geometry evidence
Draw results SHALL report input-assembly triangle counts, distinct adjusted vertex IDs where known, and referenced buffer availability. Counts SHALL include active indirect records and instances, account for restart, and state their evidence source. Unsupported or missing evidence SHALL be unavailable, never an invented zero. Counts SHALL describe submitted geometry before shader amplification.

#### Scenario: Indexed cube
- **WHEN** a full readback contains 36 triangle-list indices referencing eight vertex IDs
- **THEN** the summary reports 12 submitted triangles, eight unique vertices and available referenced buffers

#### Scenario: Indirect arguments unavailable
- **WHEN** indirect argument readback is missing
- **THEN** geometry counts are unavailable with a reason

### Requirement: End to end inspection timing
Single and batch inspection SHALL distinguish total operation wall time, accumulated replay call wall time, remaining frontend processing time and actual replay call count. Auxiliary and baseline calls SHALL be included; backend timing fields SHALL remain available.

#### Scenario: Outline preparation
- **WHEN** an outline requires original, solid and outline replays
- **THEN** operation timing counts all three calls and total wall time covers preparation and image conversion

