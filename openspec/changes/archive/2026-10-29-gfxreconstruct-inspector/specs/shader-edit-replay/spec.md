## ADDED Requirements

### Requirement: Compile and validate editable shader representations
The application SHALL allow editing supported Vulkan GLSL and SPIR-V assembly for captured vertex, fragment, and compute modules using ImGuiColorTextEdit with custom callback-based highlighting, undo/redo, and read-only original views. It SHALL compile or assemble using explicit stage, entry point, target environment, and pinned/configured tools, then validate the resulting binary and display actionable diagnostics.

#### Scenario: Compile a fragment color change
- **WHEN** the user edits a supported fragment shader without changing its interface
- **THEN** compilation and SPIR-V validation produce a candidate revision associated with that captured shader identity

#### Scenario: Edit contains an error
- **WHEN** compilation, assembly, or validation fails
- **THEN** diagnostics identify the failed operation and available source location while the active working revision remains unchanged

### Requirement: Check interface and replacement support
The application SHALL check replacement entry point/stage, descriptor requirements, push constant ranges, stage interfaces, capabilities, and execution modes against the original and captured pipeline layout. It SHALL preserve or explicitly reject unresolved specialization/interface requirements and SHALL enable replacement only for tested upstream identity paths.

#### Scenario: Edit requires an unbound descriptor
- **WHEN** an edit adds an incompatible descriptor binding or push constant requirement
- **THEN** Apply is rejected with the incompatible requirement identified

#### Scenario: Inline shader replacement mapping is unverified
- **WHEN** a shader uses an inline pipeline/library or shader-object path without a validated backend mapping
- **THEN** inspection remains available where supported and replay editing explains its unsupported path

### Requirement: Reproducible replay override revisions
The application SHALL store authored text, compiled binaries, and active override sets outside the original capture in versioned revisions. Apply SHALL start fresh replay with the selected immutable override set using upstream replacement filenames. It SHALL show the replacement scope and all known affected uses of shared modules and SHALL account for conflicting pipeline caches/binaries.

#### Scenario: Apply an edit to a shared module
- **WHEN** a supported captured module is referenced by multiple pipelines or entry points
- **THEN** the application identifies those affected uses and replaces that module according to upstream module-wide semantics

#### Scenario: Replay cannot create the edited pipeline
- **WHEN** a validated candidate fails pipeline creation or replay
- **THEN** the failed revision and diagnostics remain inspectable and the last successful result is not replaced by a claimed successful edit

### Requirement: Baseline comparison and revert
The application SHALL collect baseline and modified outputs with matching target, selection, and replay options, provide side-by-side and absolute image difference views, and restore baseline behavior through Revert. A modified result SHALL be marked successful only after actual replacement and replay output are verified.

#### Scenario: Verify a known shader fix
- **WHEN** a known fragment-color edit is applied to the upstream triangle workload
- **THEN** the modified replay displays the expected image change against its same-target baseline and records the replacement and artifact identities

#### Scenario: Revert an applied edit
- **WHEN** the user reverts the active overrides
- **THEN** fresh baseline replay reproduces the expected original output within the fixture's declared tolerance and the original capture hash is unchanged

#### Scenario: Baseline belongs to another device
- **WHEN** the selected baseline and modified artifacts have different replay targets or options
- **THEN** they are labeled incomparable for the shader-change verification until a matching baseline is generated

#### Scenario: Render with an active edit
- **WHEN** the user renders a frame or dumps a selected event after applying a shader override
- **THEN** active overrides remain in effect on the chosen replay target until explicitly reverted

### Requirement: Preserve captured entry-point identity
Reconstructed GLSL main() SHALL compile to the original captured SPIR-V entry name for supported single-entry vertex, fragment and compute modules. Validation and entry/stage/interface checks SHALL remain mandatory. An original OpTypeImage Depth operand of unknown may refine to a definite depth/non-depth declaration without changing descriptor layout; known incompatible depth declarations SHALL remain rejected. Incompatible resource diagnostics SHALL identify the affected binding/location.

#### Scenario: Edit a named game fragment shader
- **WHEN** the user edits reconstructed GLSL for a module whose captured entry is not main
- **THEN** the candidate exports the captured name, applies through the existing module identity and changes replay output
- **AND** revert restores the baseline without changing the capture or discarding authored text
