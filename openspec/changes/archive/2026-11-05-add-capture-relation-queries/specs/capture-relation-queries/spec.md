## ADDED Requirements

### Requirement: Shared generic query contract

The core SHALL expose one UI-independent query contract for supported entities, typed filters, projection, grouping, pagination and errors. CLI entity verbs and export adapters SHALL use the same validation/planning semantics. The public field catalog SHALL describe names, types, defaults, filterability, sections and availability without requiring a capture or database-specific knowledge.

#### Scenario: Discover available query fields
- **WHEN** an agent requests the field catalog for executions
- **THEN** it receives supported columns, sections, filters and types without opening a capture, contacting a device or inspecting SQL schema

### Requirement: Filtered relationship queries

The query API SHALL support execution/draw, pipeline, shader and pass lookups with applicable frame/pass, pipeline, shader-module and stage constraints. Filters across fields SHALL be conjunctive and repeated values of one filter disjunctive. Module and stage constraints SHALL match the same pipeline-stage edge. Pass selection SHALL require unambiguous frame identity.

#### Scenario: Vertex shader use in one pass
- **WHEN** an agent queries frame 1, pass 2, shader 1392 and vertex stage
- **THEN** results contain only submitted graphics draws in that pass whose captured vertex stage references module 1392

#### Scenario: Unrelated stage must not match
- **WHEN** a draw binds module X only as fragment shader and another module as vertex shader
- **THEN** a module-X/vertex predicate does not match that draw

### Requirement: Execution cardinality and relationship coverage

Execution results SHALL return one row per submitted execution, independently of matching join-edge multiplicity. The response SHALL disclose unresolved relationship coverage among the candidate scope. Successful empty results SHALL remain distinguishable from incomplete negative evidence.

#### Scenario: One module matches multiple stages
- **WHEN** a module filter matches multiple stage edges of one execution
- **THEN** that execution appears once in the default execution result

#### Scenario: No known shader matches with unresolved candidates
- **WHEN** no resolved stage matches the requested module but candidate executions include unresolved pipeline stages
- **THEN** the response has no matching rows and explicitly reports the unresolved population

### Requirement: Compact versioned projection

New/projected results SHALL use a versioned gfxrecon-query/1 result contract inside the existing CLI JSON envelope. Defaults SHALL contain bounded identity/count/use-summary rows and shared source/evidence metadata without full descriptor state, shader text, screenshots or repeated artifact trees. Requested columns and named sections SHALL be validated through the catalog; unknown or unsupported fields SHALL fail before expensive work.

#### Scenario: Compact draw list
- **WHEN** an agent requests execution, block, pipeline_id, index_count and instance_count
- **THEN** each returned row contains only those requested values, with provenance/page/coverage metadata at response level

### Requirement: Focused state and evidence sections

Explicit detail sections SHALL expose recorded state and evidence references without implicit replay or binary retrieval. Rasterization detail SHALL include relevant recorded dynamic overrides beside pipeline state and identify unresolved effective values. The response SHALL NOT present a static creation value as fully effective when applicable dynamic state remains unresolved.

#### Scenario: Rasterization has a dynamic override
- **WHEN** a draw's rasterization detail is requested and relevant dynamic state was recorded
- **THEN** the section includes that override and its source/resolution status without unrelated descriptors or vertex data

### Requirement: Bounded deterministic pagination

List queries SHALL default to 100 rows with a maximum requested page size of 1000 and deterministic ordering ending in a unique entity key. Cursors SHALL bind snapshot identity and normalized query/projection/order. Responses SHALL include returned count, has_more and next_cursor. Full matching counts SHALL be explicit rather than mandatory for each page.

#### Scenario: Resume a shader-use query
- **WHEN** an agent follows the cursor for the same snapshot and query
- **THEN** it receives the next ordered rows without duplicates or omissions

#### Scenario: Cursor reused for different filters
- **WHEN** a cursor is supplied with a different snapshot, shader filter or projection
- **THEN** the CLI returns a stable usage error rather than silently resuming another query

### Requirement: Explicit grouped execution counts

The API SHALL count submitted executions by frame/pass, pipeline or shader using the same filters and execution cardinality rules as list queries. Draws and dispatches SHALL be distinguishable. Shader groups SHALL count an execution once per matching shader and disclose overlapping group membership. Aggregates SHALL NOT infer triangles, actual indirect counts or unique vertices from unavailable bytes.

#### Scenario: Count draws in a pass
- **WHEN** an agent groups graphics executions by frame and pass
- **THEN** counts equal the matching submitted execution population, including repeated submissions but excluding dispatches

### Requirement: Explicit text export with stable stdout

Projected rows SHALL support explicit CSV/JSONL file export with deterministic columns and documented null/structured-value encoding. Stdout SHALL retain one JSON success/error document. Default export SHALL cover the selected page; export-only all-row requests SHALL stream with bounded memory and cancellation, without duplicating all rows on stdout.

#### Scenario: Export a small shader-use table
- **WHEN** an agent exports selected columns to CSV
- **THEN** the file contains that projected table and stdout contains compact export metadata and source/snapshot identity

### Requirement: Static-only source selection and compatible discovery

New list/stat queries SHALL accept exactly one explicit capture or index source. Capture-source queries SHALL ensure compatible derived relations; explicit-index queries SHALL read their snapshot directly without full capture-model loading. Commands, options and structured errors SHALL integrate with existing help/describe and exit-code conventions. Cancellation SHALL not publish a successful partial build/export.

#### Scenario: Inspect an index on a host without replay tools
- **WHEN** the CLI queries a supported explicit relation index with replay/conversion tools unavailable
- **THEN** it succeeds without probing those tools or contacting a device
