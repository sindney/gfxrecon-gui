# capture-analysis Specification

## Purpose
TBD - created by archiving change gfxreconstruct-inspector. Update Purpose after archive.
## Requirements
### Requirement: Versioned streaming capture index
The application SHALL open Vulkan `.gfxr` files using a supported GFXReconstruct converter/info adapter, stream records into a versioned persistent index, preserve 64-bit block/object identities, and load detailed arguments on demand. The source capture SHALL remain unchanged.

#### Scenario: Reopen an unchanged capture
- **WHEN** capture content and processing/schema versions match a completed cached index
- **THEN** the application reuses that index without repeating full conversion

#### Scenario: Import is interrupted or input is truncated
- **WHEN** conversion or indexing fails before completion
- **THEN** the application reports the failure and does not publish the partial index as a completed cache

#### Scenario: Input or converter changes
- **WHEN** the capture fingerprint or processing/schema version differs from a cached index
- **THEN** derived data is rebuilt without reusing stale event or resource identities

### Requirement: Frame and event navigation
The application SHALL provide searchable raw API call order and a submitted execution hierarchy grouped by frame, submission, render scope, and available debug labels. It SHALL preserve API arguments, thread identity, and return values when present and SHALL identify capture-relative versus original frame numbering.

#### Scenario: Find a Vulkan command
- **WHEN** the user searches by API name, label, object name, or block index
- **THEN** matching calls can be selected and their captured arguments inspected

#### Scenario: Work was recorded before its presentation frame
- **WHEN** a command buffer recorded in an earlier frame is submitted for a later frame
- **THEN** its execution appears under the later submission with a link to the original recorded calls

### Requirement: Distinguish command recording generations and execution occurrences
The application SHALL track command-buffer resets/re-recordings, repeated submissions, and secondary invocation paths. Resource requests SHALL identify the selected execution occurrence and SHALL NOT treat a recorded draw block index alone as sufficient.

#### Scenario: A command buffer is submitted twice
- **WHEN** the same recorded draw executes in two submissions
- **THEN** the application exposes separate execution occurrences and preserves the selected submission in replay inspection requests

#### Scenario: A secondary buffer executes through two primary paths
- **WHEN** a secondary command buffer is invoked from different primary command buffers
- **THEN** each occurrence retains its primary invocation path and recording generation

### Requirement: Selected-event state and provenance
For supported core graphics and compute paths, the application SHALL resolve pipeline/stage identity, entry points, specialization constants, descriptor bindings, vertex/index bindings, push constants, render targets, and supported dynamic state for the selected occurrence. It SHALL preserve shader references after shader-module destruction and SHALL explicitly label unsupported or ambiguous state and unavailable resource contents.

#### Scenario: A shader module is destroyed after pipeline creation
- **WHEN** an existing pipeline is selected after its shader module was destroyed
- **THEN** its stages still link to the captured shader binary and correct original identity

#### Scenario: Command buffer is reset and rebound
- **WHEN** an event is selected from a later recording generation
- **THEN** its pipeline and bindings derive from that generation without leaking state from the prior recording

#### Scenario: Exact descriptor contents cannot be resolved offline
- **WHEN** update-after-bind or unsupported synchronization prevents an exact event-specific value
- **THEN** the view identifies the uncertainty and does not substitute a global/latest value as exact captured state
