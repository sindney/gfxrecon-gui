# Implementation validation

Validated on Windows x64, 2026-11-03. The change remains uncommitted,
unarchived and unpublished.

## Delivered behavior

- A shared core request/catalog/query API in `src/analysis/relations.h`; SQL and
  SQLite handles remain private. Public projected results use `gfxrecon-query/1`.
- Embedded, checksum-pinned SQLite 3.54.1, with upstream provenance and a
  distribution notice. No SQLite executable or service is required.
- Owned immutable index generations, source/decoded digests, build validation,
  atomic publication, cancellation and concurrent readers of previous snapshots.
- CLI `index build/info`, `query executions/draws/pipelines/shaders/passes/stats/fields`,
  projection, named details, same-edge shader/stage filters, bounded cursor pages
  and explicit CSV/JSONL exports. Existing full draw/pipeline/object documents are
  preserved; `draw --execution 0` is accepted.
- Guide: `docs/RELATIONS.md`; CLI examples: `docs/CLI.md`.

## Checks and evidence

- Release core, CLI, GUI and GUI-test targets built with the SDL3 configuration;
  SFML CLI/GUI/GUI-test targets also built successfully. Both final build processes
  returned zero, recorded in `.sessions/relations-release-build.log` and
  `.sessions/relations-sfml-build-confirm.log`.
- The four focused relation suites pass (`--relation-tests`), including unsigned
  IDs through UINT64_MAX, repeated primary/secondary submissions, same-stage-edge
  matching, null indirect counts, pipeline-library/missing-metadata partial status,
  grouped cardinality, projection, dynamic rasterization disclosure, cursors,
  export encoding, source changes, cancelled builds, corrupt/future/unowned
  snapshots and concurrent read/rebuild publication.
- The full existing core suite passes, including existing CLI, replay, dump,
  capture/cache and image tests. Its first run encountered the existing Windows
  mock Android journal rename race; a complete rerun passed without source changes
  (`.sessions/relations-all-core-rerun.log`). These Android tests use mocks.
- SDL3 shell and inspection GUI suites pass; SFML inspection also passes, with
  zero ImGui errors/ID conflicts. No connected-phone operations were needed.
- All ten Python packaging tests pass. Both packagers require the SQLite notice,
  dependency manifest and relation guide. OpenSpec strict validation and diff
  whitespace checks pass.
- Three public bundled captures (triangle, indexed cube and indirect triangles)
  match their complete static dumps for execution/block/frame/pass/pipeline,
  recorded direct counts, unavailable indirect counts and reverse shader uses.
  Grouped counts equal their submitted populations. Evidence is retained under
  `.sessions/relations-real-validation/`.
- The available larger capture materialized 19,150 executions over 60 frames,
  275 passes, 223 pipelines, 224 modules and 441 pipeline-stage edges. Its database
  is 92,573,696 bytes. Five 100-row projected shader-use CLI calls had a median
  process wall time of 60.1 ms and returned 19,109 bytes each. Capture-source reuse
  with current identity validation took 502 ms; this is deliberately separate
  from explicit-snapshot query cost.

## Generated workload measurements

`gfxrecon_core_tests --relation-benchmark 100000` generated 100,000 submitted
executions of two recorded draws across 100 frames. This measures repeated
submission relationships, rather than 100,000 unique API records. The original
and decoded files were removed and the Capture model destroyed before direct
snapshot queries. No converter, extraction tool, replay tool or device was
configured for this benchmark. File-system caches were not flushed.

| Operation | Time |
| --- | ---: |
| New immutable database build | 3,427 ms |
| Explicit snapshot open | 0.70 ms |
| First 100-row shader page | 0.98 ms |
| Median of 20 subsequent shader pages | 0.83 ms |
| Median frame/pass grouping | 111 ms |
| Median pipeline grouping | 158 ms |
| Median shader grouping | 334 ms |

The database occupies 75,354,112 bytes. The compact page is 8,259 serialized
bytes. Process peak working set, including fixture generation/build, was
56,094,720 bytes; after queries it was 14,417,920 bytes. These are local workload
measurements, not cross-machine performance guarantees. Counts and scoped
coverage can scan the indexed candidate population; requesting a bounded page
does not promise constant-time aggregation. Full results and per-sample timings
are in `.sessions/relations-benchmark-100k.json` and its named temporary artifact
directory.

## Local deployment

Updated `dist/gfxrecon-gui` in place with GUI/CLI executables, CLI/relation docs,
dependency manifest and SQLite notice. All 104 inventoried release files were
hash-verified. Existing preferences, captures, caches and sessions were preserved;
the prior memory-translation preference was not changed.

The deployed CLI queried a copied 100,000-execution snapshot after source removal,
with conversion/extraction/replay/ADB paths disabled and a clean PATH. It returned
the exact 50,000 matching submissions and preserved maximum unsigned IDs. The
deployed GUI opened/rendered the public local triangle using isolated settings.
The receipt and rollback files are in
`.sessions/relations-deploy-20261103T095430Z/`.

## Limits

Native macOS compilation/signing was unavailable on this Windows host. Portable
code paths and packaging contracts were reviewed, and macOS required-file checks
were tested in Python. Static relationships describe captured bindings, not proof
of shader instruction execution or visibility. Effective rasterization state
remains explicitly unresolved. Explicit snapshot opens validate ownership/schema/
completion metadata without a full database integrity scan on every query; build
publication performs integrity/FK/count validation, and capture-source reuse also
checks database/decoded digests. Source availability is queried only for requested
evidence sections, and presence does not mean its contents were revalidated.
