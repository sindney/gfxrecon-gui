## Why

Agents can access durable capture evidence, but answering narrow relationship questions still requires loading large documents and reconstructing joins in custom scripts. Persisting the already-derived execution, pass, pipeline and shader relationships will make those questions cheap and precise without replaying the capture.

## What Changes

- Add a versioned SQLite relation index built from decoded capture metadata, reusing existing execution identities, pass grouping and shader-use logic. Existing evidence files remain authoritative and are referenced rather than replaced.
- Add a shared, UI-independent query service with common filters, field/section projection, pagination, grouping and typed errors. SQLite details stay behind the implementation boundary.
- Expose compact execution/draw, pipeline, shader and pass queries through thin CLI adapters, including shader/stage lookup and counts grouped by pass, pipeline or shader.
- Add explicit field/section selection to existing draw/pipeline queries and an execution selector for static draw inspection. Existing invocations retain their full response shapes and dump-equivalence guarantees.
- Provide bounded JSON responses and explicit CSV/JSONL file export, preserving the existing CLI envelope, error conventions and evidence identities.
- Reuse valid indexes and safely rebuild stale derived generations. Read-only queries against an explicit index snapshot do not require the original capture, full capture-model loading, shader extraction or device access.

## Capabilities

### New Capabilities

- `capture-relation-index`: Static relation materialization, source identity, integrity, safe publication, reuse and snapshot lifecycle.
- `capture-relation-queries`: Generic narrow queries, projection, relationship filters, grouping, pagination, evidence references and compatible CLI adapters.

### Modified Capabilities

- `agent-cli`: Explicitly distinguish the unchanged full static-query response from opt-in, versioned projected responses.

## Impact

- Shared analysis/cache/import/export services, CLI command registration and discovery, and focused tests/documentation.
- New embedded SQLite dependency with pinned provenance and distribution integration; no database server or SQLite command-line installation required.
- Windows and macOS core/CLI builds. The API is reusable by the GUI, but new GUI query screens are outside this change.
- No replay-backend changes, automatic readbacks, shader-text expansion, bone/influence analysis, geometry-count inference from missing bytes, report/doctor redesign, commits or pushes.
