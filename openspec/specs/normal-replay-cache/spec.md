# normal-replay-cache Specification

## Purpose
Keep normal GUI draw selection responsive through bounded validated process-local snapshots and hash-checked preparation/PSO caches, without trusting rendered output across frontend sessions.

## Requirements
### Requirement: Reuse bounded validated file identities
The shared core SHALL validate current file bytes for every replay/artifact content identity. A timestamp-keyed memoized digest SHALL NOT substitute for content validation: same-size rewrites can preserve Windows file identity, modification time and NTFS change time. Cancellation SHALL be honored, and changed files SHALL not publish successful preparation results. Static metadata caches MAY reuse completed results only after validating these content identities.

#### Scenario: Same-size mutation with restored modification time
- **WHEN** file contents change without changing size and the modification time is restored
- **THEN** hashing the current bytes produces a new content identity rather than reusing a stale digest

### Requirement: Retain completed draw inspections within one GUI session
The GUI SHALL retain completed compatible graphics-draw inspections in an LRU bounded to 16 selections and 128 MiB of resource-file working set. Snapshots SHALL preserve geometry, shaders and fetched display modes. Restoration SHALL validate resource identity and current capture/target/override/replay-policy/subdraw compatibility and bind the result to the current selection generation. These snapshots SHALL be process-local and SHALL NOT become persistent rendered-result caches.

#### Scenario: Revisit a completed draw
- **WHEN** a retained draw is selected again with compatible settings in the same GUI session
- **THEN** its data is restored locally without ADB or GPU replay and geometry remains consistent with that draw

#### Scenario: Reselect the currently displayed draw
- **WHEN** an idle successful draw remains displayed with compatible identity and requested view
- **THEN** selecting it again starts no work

#### Scenario: Changed or missing resource
- **WHEN** a snapshot resource no longer matches its validated identity
- **THEN** the snapshot is rejected and the regular replay path remains available

### Requirement: Never reuse rendered results across frontend sessions
A new GUI or CLI session SHALL execute frame/draw replay on the selected local or Android target rather than restoring rendered results from a previous session. Completed session artifacts MAY remain as evidence, but SHALL NOT be read as a replay-result cache or duplicated into a persistent result-cache directory. Static capture indexes, converted metadata, shader inputs and hash-checked target-generated PSO caches are not rendered results and MAY persist across sessions.

#### Scenario: New session with a matching old result
- **WHEN** a new frontend session requests the same capture, draw and settings as an earlier session
- **THEN** it performs a real replay and reports no result-cache hit, even when old cache files are present

#### Scenario: Fresh, validation or performance replay
- **WHEN** a fresh, validation or performance request runs
- **THEN** it always performs a real replay; compatible target-generated PSO caches MAY be reused to keep PSO creation warm, but old images/readbacks never satisfy the request

#### Scenario: Repeated draw within one GUI session
- **WHEN** the user revisits a compatible completed draw in the same GUI process
- **THEN** the bounded process-local inspection cache can avoid redundant device work without creating a persistent rendered-result cache
