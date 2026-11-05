## Context

`Capture` already stores submitted executions, recording/invocation identities, pipeline stages and captured state. `DumpIndex` supplies frame/pass/execution locations, and dump export already constructs reverse shader-use mappings. `index.bin` persists the model, but existing queries deserialize the full model and serializers return full evidence documents. There is no SQLite dependency or relationship query service today.

The new facility materializes these existing relationships once and serves small answers directly. It shares capture identity, grouping and evidence semantics with the GUI and dump tools. It must not introduce another interpretation of what an execution or pass means.

## Goals / Non-Goals

**Goals:**

- Answer shader-use, pass membership and pipeline-use questions without custom joins, full-model loading on the query path, or replay.
- Provide a generic, typed query API whose filters, projections, pagination and errors are consistent across entities and CLI adapters.
- Preserve existing CLI defaults, envelopes, selectors and dump equivalence.
- Retain explicit evidence links, unresolved relationships and reproducible index identity.
- Support small and large captures with one storage implementation and explicit CSV/JSONL exports.

**Non-Goals:**

- Natural-language interpretation, public arbitrary SQL, a database server, or a new GUI query screen.
- Replay/backend changes, automatic resource readback, shader source export or binary loading.
- Triangle/unique-vertex inference, influence/bone analytics, general Vulkan effective-state evaluation, or report/doctor/perf redesign.
- Cross-capture global object IDs or inferring shader execution/visibility from a bound pipeline.

## Decisions

### 1. Separate source resolution, relation storage, query semantics and CLI formatting

Introduce UI-independent analysis services under `src/analysis`. Public headers expose ordinary C++ value types and opaque index handles, not SQLite types, SQL strings, ImGui objects or CLI argument maps. The conceptual API consists of:

- Source/index resolution: resolve a decoded source, ensure an index, or open an explicit snapshot.
- `RelationQuery`: entity, typed filters, requested fields/sections, grouping and page request.
- `RelationQueryResult`: query schema version, snapshot/source identity, field metadata, bounded rows, page information and coverage.
- A field/entity catalog shared by validation, planning, serialization and discovery.
- Cancellation and progress using the existing core conventions.

One validator/planner owns filter and relationship semantics. Entity-specific CLI verbs are thin adapters over it. Export writers consume the same projected rows. Do not embed separate shader joins in each command or let callers depend on database table names.

An alternative is to extend each existing serializer with independent filters. That would preserve the present duplication and still require full-model reads; reuse those serializers as evidence sources, not as the query engine.

### 2. Use SQLite as an immutable derived index, with text export

Use one embedded SQLite implementation for all capture sizes. JSON/JSONL/CSV are output formats rather than separate cache backends selected by a size threshold. This avoids divergent semantics and migrations while keeping small answers easy to inspect.

Pin the dependency and record its upstream provenance/license in the existing dependency and packaging system. Build it into the shared core on Windows and macOS; no external SQLite executable or service is needed. SQLite headers remain private to the storage adapter.

Internal tables cover metadata, executions, frame-scoped passes, pipelines, shader modules and pipeline-stage edges. Store call arguments and pipeline creation metadata once where needed for explicit detail requests. Reuse existing recorded dynamic-state data for focused sections; do not replicate large descriptor snapshots on every relation row. Related resource references can be obtained from the retained decoded evidence when explicitly requested. Default list queries never deserialize those documents.

Index common execution frame/pass/pipeline filters and reverse shader-stage/module lookups. Use prepared, parameter-bound queries and a canonical codec for captured unsigned 64-bit IDs. Opaque object IDs use fixed-width big-endian bytes internally, preserving equality and unsigned ordering without signed SQLite overflow. Public JSON retains existing unsigned numeric identities. Counts and totals use checked arithmetic; overflow is an error, not a wrapped result.

### 3. Materialize captured relationships, retaining execution identity

An execution key is capture-scoped and identifies one submitted execution, including repeated primary/secondary command-buffer invocations. Keep the existing global execution index, API block, occurrence, submission, recording generation and invocation path. Never deduplicate executions by API block or pipeline.

Frame numbers remain capture-relative and one-based. Pass ordinals remain zero-based within each frame, matching `DumpIndex` and dump directories. A pass identity is the pair `(frame, pass)`; CLI `--pass` requires a single explicit `--frame`. Grouping by pass must include frame identity.

Shader use means the captured pipeline stage references a module for that submitted execution. Preserve stage, entry point and specialization identity. It does not prove shader instructions ran or contributed visible pixels. A shader-module/stage predicate must match the same stage edge. For example, a fragment use of module X plus an unrelated vertex stage must not satisfy `--shader X --stage vertex`.

One execution appears once in an execution/draw result even if several matching stage edges exist. Grouping by shader counts each execution once per shader group; different shader groups can overlap and are not additive totals. Pipeline libraries, shader-object cases or missing bindings remain explicit partial/unresolved relationships. Shader filtering matches known relationships and reports the unresolved candidate population, so an empty match is not falsely presented as complete negative evidence.

Arguments such as index count, vertex count and instance count are recorded values. Indirect values absent from static evidence are null with an unavailable reason. Do not turn a maximum draw count into an actual count or trigger readback. The first aggregation operation is count of submitted executions, split by draw/dispatch kind; geometry totals are deferred.

### 4. Build once, publish safely, and distinguish live source from snapshot queries

`index build --capture FILE` reuses an existing valid decoded cache and builds relations without creating a dump tree. Fresh metadata indexing may run conversion, but must not require replay, device discovery, shader extraction or shader disassembly. If necessary, factor a metadata-only import option behind the existing importer; its default behavior stays unchanged for current GUI/CLI consumers.

Index identity includes capture digest, decoded-data digest, parser/index schema, pass-grouping semantics and relation-builder version. Optional shader-extraction tool changes must not be treated as relation changes when the actual relation source is identical. Store build completeness, integrity information, counts and coverage.

Build into an owned private generation using transactions and bounded insertion batches. Validate schema, referential integrity and source stability before publishing. Publish a complete immutable database generation and atomically update its small manifest. Readers retain the old generation while a replacement is built, including on Windows. Cancelled/failed builds never replace a valid index or become successful query sources. Do not delete authored files, existing dumps, readbacks or shader revisions.

New query commands accept exactly one of `--capture FILE` and `--index FILE`:

- `--capture` resolves the current source identity, lazily ensures a matching relation index and queries it. Validation can include streaming source hashing, but must not deserialize `index.bin` or reconvert a valid cache on each query.
- `--index` opens a complete read-only snapshot directly. The result identifies the as-indexed source digest; it does not claim the original path still exists or is unchanged. It works without the original capture, decoded files, converter or connected device. Missing external evidence affects link availability, not the stored relation rows.

`index info` reports identity, schema, completeness and coverage. `index build --rebuild` rebuilds owned derived data; it never blindly overwrites an unrelated SQLite database. Unsupported future schemas fail clearly. Explicit snapshots are not silently migrated. Disable extension loading and do not accept user SQL.

### 5. Share one compact, versioned response contract

The outer `gfxrecon-cli/1` success/error convention stays unchanged. New and explicitly projected queries use `result.schema = gfxrecon-query/1`. Results contain entity, snapshot/source identity, requested field metadata, rows, page information and relationship coverage. Shared evidence roots appear once; full state and repeated artifact trees are absent by default.

Execution defaults are execution, block, frame, pass, pipeline_id, command kind/name, recorded vertex/index/instance counts, and resolved stage/module references. Pipeline, shader and pass entities have similarly bounded identity/use-summary defaults. Canonical field names and types come from the catalog, with aliases normalized at the CLI boundary. Additive optional fields are compatible within v1; changes to existing meanings/types require a new query schema version. Internal database versions evolve independently.

Use `--columns` for scalar/compact list projection and `--section` for named structured detail. Unknown fields, unsupported operators, conflicting selectors or ambiguous pass references fail before expensive work. Filters across fields are AND; repeated values of one filter are OR. Module/stage association follows the same-edge rule above. Do not introduce a general JSONPath/expression language in v1.

Default page size is 100, maximum 1000. Default order is deterministic, ending with the entity's unique key. Opaque keyset cursors bind snapshot identity, normalized filters/projection/order and last key; stale or mismatched cursors are rejected. Page metadata includes returned count, has_more and next_cursor. Total matching count is explicit (`--count` or stats), avoiding a full count scan for every small page.

CSV/JSONL export is an explicit file operation. Stdout remains a single JSON document describing the export and its identity. Export the selected page by default; an explicit export-only `--all` streams all matching rows with bounded memory and cancellation. Do not emit both an entire exported table and the same rows again on stdout. Structured columns use documented compact JSON encoding in text exports; JSONL preserves JSON types and nulls.

### 6. CLI surface and compatibility adapters

The planned surface is:

```text
index build --capture C.gfxr [--rebuild]
index info --index relations.sqlite
query executions --index relations.sqlite --kind draw|dispatch|all ...
query draws --index relations.sqlite ...
query pipelines|shaders|passes --index relations.sqlite ...
query stats --index relations.sqlite --kind draw --group-by frame,pass ...
query fields --entity executions|pipelines|shaders|passes
```

All list/stats verbs share source selection and applicable frame/pass/pipeline/shader/stage filters. `query draws` is a convenience adapter for executions filtered to graphics draws. Field discovery explains supported filters, sections, types, defaults and availability without opening a capture. Register commands/options through the existing help/describe mechanism rather than maintaining separate documentation-only lists.

Examples:

```text
query draws --capture C.gfxr --frame 1 --pass 2 --shader 1392 --stage vertex
  --columns execution,block,pipeline_id,index_count,instance_count --limit 50
query shaders --index relations.sqlite --frame 1 --pass 2
draw --capture C.gfxr --execution 42 --section rasterization
pipeline 12 --capture C.gfxr --section rasterization
```

Existing `draw --block B [--occurrence K]`, `pipeline ID`, events, dump, replay and perf defaults remain unchanged. Add `--execution` as an alternative to the static draw block/occurrence selector; conflicting selector forms fail. Without projection, the new selector still returns the legacy full draw document. Explicit `--columns`/`--section` opt into the shared query response. No silent compact-default change and no change to existing `--full` readback semantics.

The rasterization section returns the recorded pipeline rasterization components, relevant recorded dynamic overrides, source references and resolution/availability status. Where a fully effective value is not resolved, return recorded components and name the unresolved state rather than guess or silently report only the static creation value. Resource-reference sections contain metadata/references, never bytes or all bound shader text.

Keep stable errors under the existing exit categories: usage for bad filters/projection/selectors/cursors, operation failure for stale/corrupt/incompatible indexes, cancellation for cancelled work, and existing missing-tool behavior only when fresh conversion is actually required. Successful empty results are not errors.

## Risks / Trade-offs

- Persistent SQLite adds a dependency → pin provenance, keep the adapter private, include packaging notices and validate Windows/macOS builds.
- Wrong joins can silently multiply draws or conflate recorded and submitted work → same-edge filters, execution-key fixtures, repeated/secondary submission parity with `DumpIndex` and dump shader-use mappings.
- A derived index can drift from the capture → versioned identity, atomic immutable generations, explicit snapshot semantics and no in-place changes to a queried generation.
- Large detailed state can defeat narrow queries → compact rows/defaults, selective detail fetches, query-page limits, direct-index benchmarks and streaming exports.
- Missing bindings/indirect bytes can look like zero results → explicit coverage and null/unavailable values; no inferred geometry or hidden replay.
- Source validation can dominate latency → distinguish end-to-end `--capture` validation cost from direct `--index` query cost in measurements.

## Migration Plan

1. Add the catalog/API and characterize legacy query/dump contracts with public fixtures.
2. Integrate SQLite and static relation materialization alongside the existing cache.
3. Add shared queries, narrow CLI adapters, opt-in projections and bounded exports.
4. Validate parity, failure/concurrency behavior, large-data query paths and packaged discovery.
5. Deploy only when implementation is requested; retain existing file evidence and derived generations for rollback. No commit, push or archive is part of preparation.

## Open Questions

No product-scope decision blocks implementation. Resolve the concrete SQLite revision and build integration against primary upstream sources when implementing, and record them before vendoring. Use public/generated fixtures plus available private captures for optional local parity/performance checks; private inputs and generated databases stay outside Git.
