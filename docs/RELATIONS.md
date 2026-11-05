# Static capture relationships

The shared relation API answers questions about submitted executions, pipelines,
shader modules and frame-scoped passes. It reads recorded metadata without replay,
a connected device, resource readback, shader extraction or disassembly. CLI
commands use the same API; a future GUI consumer can use it directly.

The public interface is [relations.h](../src/analysis/relations.h). Callers use
typed requests and the field catalog; storage handles and SQL are private.
[CLI examples](CLI.md#static-relationship-queries) describe the command interface.

## Discover fields before querying

The execution catalog lists typed `aggregate_fields` for grouped counts.
Other entity catalogs expose no grouping operations.

```powershell
.\gfxrecon-cli.exe query fields --entity executions
.\gfxrecon-cli.exe query fields --entity pipelines
```

In C++, `RelationFields(entity)` returns typed field descriptors and
`RelationFieldCatalog(entity)` returns their JSON representation. Discovery needs
no source. Descriptors identify names, types, defaults, nullable values, filterable
fields and structured sections. The catalog also describes filters, grouping and
page bounds.

`columns` replaces the default projection; `sections` appends explicit detail.
Unknown names fail validation before conversion or index construction. Execution
defaults include compact shader edges (`shader_id`, `stage`); requesting the
`shaders` section adds recorded entry points and specialization. Pipeline shader
edges are an opt-in section. Detailed sections never retrieve resource bytes or
shader text.

## Use the core API

An explicit snapshot is the shortest path for repeated queries:

```cpp
#include "analysis/relations.h"

auto index = gfx::OpenRelationIndex("relations.sqlite");
gfx::RelationQuery query;
query.entity = gfx::RelationEntity::Executions;
query.kind = gfx::RelationKind::Draw;
query.filters.frames = {1};
query.filters.passes = {2};
query.filters.shaders = {1392};
query.filters.stages = {"vertex"};
query.columns = {"execution", "block", "pipeline_id", "index_count"};
query.limit = 50;
gfx::ValidateRelationQuery(query);
auto page = gfx::QueryRelations(index, query);
auto document = page.ToJson();

while (page.hasMore) {
    query.cursor = page.nextCursor;
    page = gfx::QueryRelations(index, query);
    // Consume this page before requesting another.
}
```

To resolve a current capture instead, validate the request first and call
`EnsureRelationIndex(capturePath, cacheRoot, tools, buildOptions, cancel)`.
`tools` is the application's tool map; a fresh binary capture can require the
configured `convert` tool. A decoded `.jsonl` source needs no converter. Compatible
decoded metadata and complete indexes are reused. Normal application import
behavior is unchanged.

`RelationQueryResult::ToJson()` produces `gfxrecon-query/1` with `entity`, `fields`,
`rows`, `source`, `coverage` and `page`. The CLI places this document inside its
existing success envelope. `fields` describes the selected columns in projection
order; each row is an object keyed by those names. `source` appears once per
response. `page` contains `returned`, `has_more`, nullable `next_cursor`, and an
optional `total` when `includeCount` is requested.

An optional `std::atomic_bool` cancellation flag is accepted by index, query and
export operations. `RelationError` exposes a category (`Usage`, `Data`,
`Cancelled`, or `MissingTool`), a stable `code`, a message and optional detail.
Examples include `unknown_field`, `ambiguous_pass`, `invalid_cursor`,
`cursor_mismatch`, `snapshot_changed`, `relation_index_version` and `cancelled`.
Use the category/code for control flow rather than matching message text.

## Identity, scope and missing values

| Value | Meaning |
| --- | --- |
| `execution` | Zero-based capture-scoped submitted execution ID. Zero is valid. |
| `block` | Recorded API block; multiple submitted executions can share it. |
| `occurrence` | Zero-based submitted occurrence of that block. |
| `frame` | Capture-relative one-based frame. |
| `pass` | Zero-based ordinal within one frame, using existing `DumpIndex` grouping. |
| `pipeline_id`, `shader_id` | Captured object/module IDs, including unsigned 64-bit values. |

A pass is identified by **frame and pass together**. A pass filter requires one
explicit frame. Repeated submissions and secondary command-buffer invocations
remain separate executions; counting API blocks would lose that distinction.
Provenance retains submission, recording and invocation identities.

All IDs and recorded integer counts retain their exact unsigned values, including
values above `INT64_MAX`. JSON consumers must use an integer representation that
can preserve uint64; converting these values to an IEEE-754 double can lose
precision. CSV exports preserve their decimal text.

Different filter fields combine with AND. Values within one filter combine with
OR. Shader and stage constraints match the **same stage edge**. A shader used only
by a fragment stage does not match a vertex-stage predicate because another
module supplies that vertex stage. Stage aliases normalize through
`NormalizeRelationStage`.

Execution rows appear once per submitted execution even if several matching
stage edges reference the same module. Pipeline and shader summaries count
distinct matching executions. Execution-scoped filters restrict their use counts;
without such filters, metadata lists can include unused captured pipelines or
shader modules.

A captured pipeline-stage reference establishes binding, not proof that shader
instructions executed, a branch was taken or pixels became visible.
`relation_status` distinguishes `resolved`, `partial` and `unresolved` references.
Unsupported composition or absent module metadata does not become complete
negative evidence.

`coverage` measures candidate executions **before shader/stage filtering**, while
retaining the kind and other scope filters. It reports known relationship
candidates and unresolved candidate executions. For pipeline/shader relationship
queries, unresolved candidate pipeline metadata is also considered, including
unused candidates. An empty `rows` array together with
`negative_answer_complete: false` means no known match was found in incomplete
relationship evidence. `complete` index metadata means construction completed;
it does not mean every captured relationship was resolvable.

Direct recorded counts retain valid zero values. Inapplicable or unavailable
counts are null. Indirect draw counts, index counts and instance counts remain
null with `unavailable` reasons; a recorded maximum is not substituted for the
actual GPU argument value. Queries do not trigger a replay to fill those values.

## Grouped counts and pages

Set `groupBy` to `frame`, `frame,pass`, `pipeline_id` or `shader_id` on an execution
request. Grouping by pass automatically includes frame. Groups report distinct
`execution_count`, `draw_count`, `dispatch_count` and `unknown_count` and accept the
same scope filters. `query stats` defaults to grouping by frame and pass.

A shader group counts an execution once per matching module. An execution that
binds multiple modules can belong to several groups, so shader-group totals must
not be added as a global execution total; `coverage.shader_groups_overlap`
discloses this. Statistics do not infer triangles, unique vertices or indirect
counts from unavailable bytes.

Pages default to 100 rows, with a maximum requested size of 1000. Ordering is
ascending by the unique entity identity (frame/pass for passes, group identity
for statistics); IDs follow unsigned ordering. Cursors bind the snapshot and
normalized filters, projection, sections, grouping and order. Keep the same
`RelationIndexRef` for all pages. Changing the page size or requesting a total does
not change query identity; changing filters or projection invalidates the cursor.
A new published generation has a different snapshot and cannot resume an old
cursor. The old immutable generation remains queryable by its own index path.

`includeCount` explicitly requests a full matching row/group count. Without it,
pagination fetches a bounded page plus one row to determine `has_more`. Coverage
can still require scoped aggregation; omitting totals is not a guarantee of
constant-time work for arbitrary filters.

## Snapshot lifecycle and query cost

`EnsureRelationIndex` is the capture-validation path. It hashes the current
source, checks compatible parser/grouping/builder and converter identity, and
validates cached index identity and available decoded evidence before reuse.
These checks can read large files even when the resulting query returns a small
page. Fresh construction can convert metadata and load the capture model.

`OpenRelationIndex` plus `QueryRelations` is the portable snapshot path. It opens a
complete supported index read-only and validates schema and summary metadata.
It does not load the capture model, rehash the original capture or require the
converter, decoded file or a device. It reports **as-indexed** source identity;
it does not claim that a current external file was validated. Query pages do not
rerun a full database integrity scan; full integrity and referential checks run
before a new snapshot is marked complete and published.

Construction uses the existing submitted-execution and pass grouping logic. It
records source/decoded digests and parser/grouping/builder versions, validates
integrity and source stability, then publishes a new immutable generation through
the owned cache manifest. Rebuilds preserve old generations and readers. Failure
or cancellation does not publish a replacement as complete. Unowned databases
are not adopted, newer schemas are rejected, and explicit snapshots are not
silently migrated.

`BuildRelationSnapshot` is a lower-level entry point for an already loaded
`Capture`; it requires a new destination and refuses an existing file. Applications
normally use `EnsureRelationIndex` for ownership, reuse and publication.

## Detail and evidence references

Execution sections include `arguments`, `shaders`, `rasterization`,
`resource_refs` and `provenance`. Pipeline sections include `create_info`,
`shaders`, `rasterization` and `provenance`. Shader/pass provenance is available
as a section. Discover exact availability through the field catalog.

Rasterization detail presents recorded static creation components, declared
dynamic state and relevant recorded dynamic calls beside each other. Its
`effective_status` remains `unresolved`; a static creation value is not presented
as the final Vulkan state. Resource references provide compact object identities
and availability, without full descriptors, resource bytes or readback.

Provenance can include JSONL offsets, API blocks, submission/recording/invocation
identities and logical dump-relative paths. A logical path labelled
`not_generated` is an intended evidence location, not an existing dump artifact.
Snapshot queries remain usable after original files are removed. Explicit
`provenance` or `resource_refs` requests also report whether source/decoded files
are present through `source.evidence_availability`; presence alone does not
validate their content against the stored digest.

## CLI and export

```powershell
.\gfxrecon-cli.exe index build --capture C.gfxr
# Use the returned database path as relations.sqlite below.
.\gfxrecon-cli.exe query draws --index relations.sqlite --frame 1 --pass 2 --shader 1392 --stage vertex --columns execution,block,pipeline_id,index_count
.\gfxrecon-cli.exe query executions --index relations.sqlite --kind dispatch --section arguments
.\gfxrecon-cli.exe query stats --index relations.sqlite --kind draw --group-by shader_id
.\gfxrecon-cli.exe query draws --index relations.sqlite --columns execution,block,pipeline_id --export draws.csv --export-format csv
```

Each new list/stat command accepts exactly one `--capture` or `--index`. Field
discovery accepts neither. Existing unprojected `draw`, `pipeline` and `object`
commands preserve their full dump-equivalent documents; explicit columns or
sections on `draw`/`pipeline` opt into the shared result contract.

`ExportRelations` and CLI `--export` write CSV or JSONL. The default export covers
the selected page; export-only `--all` follows pages with bounded memory, starting
at any supplied cursor. Stdout contains a compact receipt, not a duplicate table.
CSV follows projection/catalog column order, quotes every non-null cell and
doubles embedded quotes. Null is an unquoted empty cell; an empty string is
`""`; arrays/objects use compact JSON inside quoted cells. JSONL preserves JSON
types. Output is staged and published only after success, preserving an existing
destination on cancellation or failure. Export cannot replace the index or its
recorded source/decoded evidence.

See [CLI.md](CLI.md#static-relationship-queries) for legacy selectors, additional
examples and export options.
