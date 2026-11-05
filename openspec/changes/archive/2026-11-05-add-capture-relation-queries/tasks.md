## 1. Lock the shared API and compatibility contract

- [x] 1.1 Define UI-independent index/source handles and typed query request/result/page/grouping/error types, keeping SQL, SQLite handles and CLI argument maps out of public headers.
- [x] 1.2 Define one versioned entity/field/section/filter catalog, compact defaults, null/availability conventions and aliases; use it to validate requests and describe the API.
- [x] 1.3 Capture legacy unprojected draw/pipeline/object and dump-equivalence fixtures, including execution zero and repeated block/occurrence selection, before adding adapters.

## 2. Add embedded storage and static materialization

- [x] 2.1 Select and verify a pinned upstream SQLite source revision; integrate the private core dependency, provenance/license metadata and supported Windows/macOS build paths without requiring an external SQLite executable.
- [x] 2.2 Implement the internal relation schema, indexed common joins/filters, unsigned-ID codec and checked count arithmetic; cover IDs above INT64_MAX and UINT64 boundary serialization.
- [x] 2.3 Reuse existing decoded caches and provide metadata-only source resolution/conversion where necessary, preserving existing import defaults and avoiding shader extraction, dump creation and device/replay calls.
- [x] 2.4 Materialize execution/pass/pipeline/module/stage relationships using Capture, DumpIndex and existing shader-use derivation; preserve repeated/secondary invocations, entry points, specializations and unresolved bindings.
- [x] 2.5 Store compact arguments and shared state/evidence locators for opt-in detail sections; keep unavailable indirect arguments null and avoid duplicating descriptors or shader/buffer contents per execution.

## 3. Implement index identity, publication and snapshots

- [x] 3.1 Define source/parser/grouping/builder identity and owned manifests with completeness, integrity and coverage; implement valid-cache reuse and stale/incompatible/corrupt detection.
- [x] 3.2 Build private generations transactionally, validate before atomic publication and preserve active readers/old generations across rebuild, cancellation and failure, including Windows file-handle behavior.
- [x] 3.3 Implement read-only explicit snapshot opening without original files/tools, external-evidence availability and clear future-schema/unowned-database rejection.
- [x] 3.4 Expose index build/info/rebuild through the normal CLI registry, shared settings/cache conventions, progress, cancellation and structured errors.

## 4. Implement the common relation query engine

- [x] 4.1 Implement indexed execution/draw, pipeline, shader and pass queries with shared frame/pass/pipeline/module/stage/kind filters and deterministic entity ordering.
- [x] 4.2 Enforce same-stage-edge module/stage matching, one-row-per-execution cardinality, frame-scoped pass identity and candidate-scope unresolved coverage.
- [x] 4.3 Implement selected columns and named detail sections, including recorded rasterization plus relevant dynamic overrides and explicit unresolved-effective-state status; resolve evidence only when requested.
- [x] 4.4 Implement bounded pages and query/snapshot-bound cursors with clear errors for invalid or mismatched tokens; keep total-count scans explicit.
- [x] 4.5 Implement filtered execution counts grouped by frame/pass, pipeline or shader, separating draws/dispatches and documenting overlapping shader groups; do not infer geometry totals.

## 5. Add compatible CLI adapters and exports

- [x] 5.1 Add thin query entity/stats/field-catalog verbs with shared source/filter/projection/page options; ensure direct-index requests bypass full capture-model loading and source/device discovery.
- [x] 5.2 Add static draw --execution as an alternative selector and explicit draw/pipeline projection adapters; preserve full legacy defaults, schemas, dump equivalence, selectors and existing --full behavior.
- [x] 5.3 Implement deterministic CSV/JSONL projected-file exports with explicit page/all-row semantics, bounded streaming, cancellation and a compact JSON stdout receipt rather than duplicate full rows.
- [x] 5.4 Integrate help/describe, canonical field discovery, query schema identity and stable error/exit conventions; reject unknown fields, ambiguous passes and conflicting selectors before expensive operations.

## 6. Verify correctness, isolation and query cost

- [x] 6.1 Test query/dump parity for public sample captures and synthetic repeated primary/secondary submissions, stage aliases/multiple edges, entry points, shader-less/unresolved pipelines and frame/pass numbering.
- [x] 6.2 Test projection/section correctness, rasterization dynamic-state disclosure, empty/partial answers, null indirect counts, unsigned IDs, overflow handling and count-vs-join cardinality.
- [x] 6.3 Test cache reuse/invalidation, source mutation during build, interrupted publication, corrupt/future databases, absent external evidence and concurrent read/rebuild/pagination without touching authored data.
- [x] 6.4 Test legacy CLI defaults and opt-in adapters, cursor stability/errors, discovery/catalog consistency, JSON stdout/stderr isolation, export quoting/null/types and cancelled partial exports.
- [x] 6.5 Measure generated large-dataset cold builds and hot direct-index queries, recording time/memory/bytes for shader-use and grouped-count questions; verify narrow queries avoid full-model deserialization, dump generation, shader extraction and all device/replay calls. Distinguish capture-validation cost from snapshot-query cost.
- [x] 6.6 Validate core/CLI builds and relevant existing GUI consumers on Windows/macOS where available; verify dependency notices, offline packaged index queries and unchanged ordinary replay/dump workflows.

## 7. Document, review and deliver after implementation

- [x] 7.1 Document the generic API/catalog, field and filter semantics, frame/pass/stage identities, snapshot/live-source behavior, compatibility rules, explicit exports and representative narrow-query examples.
- [x] 7.2 Review specs/API/schema consistency and final diffs, record validation/limitations and prepare validated local dist artifacts while preserving user sessions/settings. Leave changes uncommitted/unpushed and do not archive or publish.
