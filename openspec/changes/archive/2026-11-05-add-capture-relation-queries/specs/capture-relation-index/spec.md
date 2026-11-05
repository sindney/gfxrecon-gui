## ADDED Requirements

### Requirement: Static relationship materialization

The system SHALL build a persistent relation index from decoded capture metadata, covering submitted executions, frame-scoped passes, pipelines, shader modules and pipeline-stage relationships. It SHALL reuse existing execution/pass semantics and shader-use derivation. Building SHALL NOT require replay, a connected device, full dump generation, shader extraction/disassembly or resource readback. Existing import consumers SHALL retain their default behavior.

#### Scenario: Build from a decoded capture
- **WHEN** an agent builds an index for an already decoded capture
- **THEN** the index contains its static relationships without replaying or generating a dump tree or shader-text artifacts

#### Scenario: Fresh capture without shader tools
- **WHEN** a supported capture needs metadata conversion and conversion is configured but shader extraction tools are absent
- **THEN** relation indexing can complete without requiring shader binaries or changing normal GUI import defaults

### Requirement: Submitted execution and stage identity

Each indexed execution SHALL retain its capture-scoped execution ID, block, occurrence, frame, pass and submission/invocation identity. Repeated submissions SHALL remain distinct. Shader-use edges SHALL preserve stage, module, entry point and specialization where recorded. A shader-use relationship SHALL describe captured binding, not proven instruction execution or visible contribution.

#### Scenario: Repeated secondary command buffer
- **WHEN** the same recorded draw block is submitted through two secondary-command-buffer invocations
- **THEN** both submitted executions appear separately with their original execution/occurrence identities

### Requirement: Existing frame and pass semantics

Frame numbers SHALL remain capture-relative and one-based; pass ordinals SHALL remain zero-based within each frame and agree with the existing DumpIndex. Index construction SHALL NOT introduce a second pass-grouping algorithm or globally unique pass ordinals without frame identity.

#### Scenario: Same ordinal in different frames
- **WHEN** frame 1 and frame 2 both contain pass 2
- **THEN** they are distinct pass records with separate execution memberships matching the structured dump

### Requirement: Provenance and unresolved data

An index SHALL record capture/decoded digests, parser/grouping/builder versions, completeness and relationship coverage. Rows SHALL retain resolvable references to original evidence. Missing stages, unsupported pipeline composition and unavailable indirect values SHALL remain explicitly unresolved/null with reasons, without fabricated zero counts or automatic replay. References to ungenerated dump files SHALL be labelled ungenerated rather than existing evidence.

#### Scenario: Indirect argument bytes are unavailable
- **WHEN** an indirect execution has no static evidence of its actual argument values
- **THEN** unavailable counts remain null with an evidence reason and indexing does not trigger readback

### Requirement: Reuse and safe rebuilding

The system SHALL reuse a complete compatible index for identical relation-source identity and SHALL rebuild stale owned derived data into a new generation. It SHALL detect corrupt, incomplete or incompatible indexes. An explicit rebuild SHALL NOT overwrite an unrelated database or delete existing evidence, captures or authored shader revisions.

#### Scenario: Parser or grouping semantics change
- **WHEN** the relation builder's required parser/grouping identity differs from a cached index
- **THEN** the cache is rebuilt safely rather than silently reused

### Requirement: Atomic publication and reader isolation

Construction SHALL publish only after data, schema, referential integrity and source stability are validated. Failure or cancellation SHALL leave the previously published index usable. Readers SHALL retain a consistent immutable generation while another process publishes a replacement, including on Windows.

#### Scenario: Cancelled replacement build
- **WHEN** a replacement build is cancelled during insertion
- **THEN** it is not published as complete and the old generation remains queryable

#### Scenario: Rebuild during pagination
- **WHEN** a reader holds a snapshot while a new generation is published
- **THEN** the reader's query pages remain bound to its original snapshot

### Requirement: Read-only portable snapshots

Queries using an explicit index SHALL open a complete snapshot read-only, identify its as-indexed source, and work without the original capture, converter, decoded files or device. Missing external evidence SHALL be reported through availability metadata. Snapshot queries SHALL NOT claim to have validated a missing/live source or silently migrate an unsupported schema.

#### Scenario: Original capture is offline
- **WHEN** an agent opens a valid explicit relation database after its source files become unavailable
- **THEN** indexed relationship queries succeed with snapshot provenance and unavailable external-link status, without conversion or device discovery

### Requirement: Embedded storage behind a public API boundary

SQLite SHALL be an embedded, pinned core dependency with recorded provenance and distribution notices on supported Windows/macOS builds. Public query/index headers SHALL NOT expose SQLite handles, SQL strings or database table names. Captured unsigned 64-bit identities SHALL round-trip without signed overflow or precision loss inside the implementation.

#### Scenario: Large captured ID
- **WHEN** an indexed shader or pipeline ID exceeds INT64_MAX
- **THEN** equality filters, deterministic ordering and JSON/CSV export preserve its exact unsigned identity
